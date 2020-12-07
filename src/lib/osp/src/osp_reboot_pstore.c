/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
 * ===========================================================================
 *  Reboot Counter/Reason implementation using PSTORE (/dev/pmsg0) as backend
 * ===========================================================================
 */
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>

#include "log.h"
#include "osp_reboot.h"
#include "const.h"
#include "execsh.h"
#include "kconfig.h"
#include "os_time.h"
#include "os_regex.h"
#include "util.h"

/** Pattern for matching the dmesg file */
#define PSTORE_DMESG        "dmesg-ramoops-*"
/** Pattern for matching the pmsg file */
#define PSTORE_PMSG         "pmsg-ramoops-*"
/**
 * The reboot reason may not be correctly retrieved more than once per boot.
 * In order to support multiple calls to osp_unit_reboot_get() we need to cache
 * it somewhere and tmpfs seems perfect for this. To avoid accidentally
 * tampering with this file, it will be set to immutable.
 */
#define PSTORE_REBOOT_TMP   "/var/run/osp_reboot_reason"

static const char * const pstore_reboot_type_map[] =
{
    [OSP_REBOOT_CANCEL]         = "CANCEL",
    [OSP_REBOOT_UNKNOWN]        = "UNKNOWN",
    [OSP_REBOOT_COLD_BOOT]      = "COLD_BOOT",
    [OSP_REBOOT_POWER_CYCLE]    = "POWER_CYCLE",
    [OSP_REBOOT_WATCHDOG]       = "WATCHDOG",
    [OSP_REBOOT_CRASH]          = "CRASH",
    [OSP_REBOOT_USER]           = "USER",
    [OSP_REBOOT_DEVICE]         = "DEVICE",
    [OSP_REBOOT_HEALTH_CHECK]   = "HEALTH_CHECK",
    [OSP_REBOOT_UPGRADE]        = "UPGRADE",
    [OSP_REBOOT_THERMAL]        = "THERMAL",
    [OSP_REBOOT_CLOUD]          = "CLOUD",
};

static const char* pstore_reboot_type_str(enum osp_reboot_type type);
static bool pstore_save(enum osp_reboot_type type, const char *reason);
/* Parse the PSTORE dmesg file */
static void pstore_parse_dmesg(const char *path, enum osp_reboot_type *type, char *reason, ssize_t rsz);
/* Parse a file containing REBOOT commands as they were stored in /dev/pmsg0 */
static bool pstore_parse_reboot(const char *path, enum osp_reboot_type *type, char *reason, ssize_t rsz);

bool osp_unit_reboot_ex(enum osp_reboot_type type, const char *reason, int ms_delay)
{
    /*
     * Just record the reboot reason if ms_delay is negative or if the reason is
     * OSP_REBOOT_CANCEL
     */
    if (ms_delay < 0 || type == OSP_REBOOT_CANCEL)
    {
        LOG(DEBUG, "osp_reboot: ms_delay is negative or reboot cancel called, only storing reboot reason.");
        return pstore_save(type, reason);
    }

    /*
     * If we're going for a reboot, do not bail out if we fail to save the
     * reboot reason. We still want to attempt the reboot.
     */
    (void)pstore_save(type, reason);

    LOG(NOTICE, "osp_reboot: Reboot in %0.2f seconds. Type: %s Reason: %s.",
            (double)ms_delay / 1000.0,
            pstore_reboot_type_str(type),
            reason);

    clock_sleep((double)ms_delay / 1000.0);

    if (execsh_log(LOG_SEVERITY_INFO, CONFIG_OSP_REBOOT_COMMAND) != 0)
    {
        LOG(ERR, "osp_reboot: Reboot command failed.");
        /* Notify that the reboot was cancelled */
        pstore_save(OSP_REBOOT_CANCEL, NULL);
        return false;
    }

    return true;
}

/**
 * Retrieve the boot reason from pstore
 */
bool osp_unit_reboot_get(enum osp_reboot_type *type, char *reason, ssize_t reason_sz)
{
    struct dirent *pde;
    char fpath[C_MAXPATH_LEN];

    bool retval = false;
    DIR *psd = NULL;
    char *pmsg = NULL;

    *type = OSP_REBOOT_UNKNOWN;
    if (reason_sz == 0) reason = NULL;
    if (reason != NULL) reason[0] = '\0';

    /*
     * Use the reboot cache if it exists.
     */
    if (access(PSTORE_REBOOT_TMP, R_OK) == 0)
    {
        LOG(DEBUG, "osp_reboot: Using cached reboot reason: %s", PSTORE_REBOOT_TMP);
        if (!pstore_parse_reboot(PSTORE_REBOOT_TMP, type, reason, reason_sz))
        {
            LOG(ERR, "osp_reboot: Error reading reboot reason cache.");
            goto exit;
        }
        return true;
    }

    /* Scan the PSTORE folder and find known file types */
    psd = opendir(CONFIG_OSP_REBOOT_PSTORE_FS);
    if (psd == NULL)
    {
        LOG(ERR, "osp_reboot: Error opening PSTORE folder.");
        /* Failed to retrieve the reboot reason */
        return false;
    }

    pmsg = NULL;
    while ((pde = readdir(psd)) != NULL)
    {
        if (fnmatch(PSTORE_DMESG, pde->d_name, FNM_PATHNAME) == 0)
        {
            /*
             * This file is typically created when a kernel crash happens.
             * If the file is there, read the PC of the crash and return it as
             * the reason string.
             *
             * Delete all occurrences of this file since they persist across
             * reboots.
             */
            LOG(DEBUG, "osp_reboot: Found DMESG file: %s", pde->d_name);
            if (*type == OSP_REBOOT_UNKNOWN)
            {
                int rc;

                rc = snprintf(fpath, sizeof(fpath), "%s/%s", CONFIG_OSP_REBOOT_PSTORE_FS, pde->d_name);
                if (rc >= (int)sizeof(fpath))
                {
                    LOG(ERR, "osp_reboot: pstore filesystem path too long: %s/%s",
                             CONFIG_OSP_REBOOT_PSTORE_FS, pde->d_name);
                    continue;
                }

                pstore_parse_dmesg(fpath, type, reason, reason_sz);
            }

            /* Unlink the file and rescan the folder */
            if (unlinkat(dirfd(psd), pde->d_name, 0) == 0)
            {
                rewinddir(psd);
            }
            else
            {
                LOG(WARN, "osp_reboot: Error unlinking dmesg file: %s", pde->d_name);
            }
            continue;
        }

        if (fnmatch(PSTORE_PMSG, pde->d_name, FNM_PATHNAME) == 0)
        {
            /* Save the pmsg for later processing */
            if (pmsg == NULL)
            {
                pmsg = strdup(pde->d_name);
            }
            continue;
        }
    }

    if (*type != OSP_REBOOT_UNKNOWN)
    {
        retval = true;
        goto exit;
    }

    if (pmsg != NULL)
    {
        snprintf(fpath, sizeof(fpath), "%s/%s", CONFIG_OSP_REBOOT_PSTORE_FS, pmsg);
        LOG(DEBUG, "osp_reboot: Found PMSG file: %s", fpath);

        if (!pstore_parse_reboot(fpath, type, reason, reason_sz))
        {
            /* By default assume a type POWER_CYCLE */
            *type = OSP_REBOOT_POWER_CYCLE;
            if (reason != NULL)
            {
                strscpy(reason, "Power cycle.", reason_sz);
            }
        }

        retval = true;
        goto exit;
    }

    /*
     * If the pstore file system is empty it may indicate with some certainty
     * that it's a cold boot
     */
    *type = OSP_REBOOT_COLD_BOOT;
    strscpy(reason, "Power up.", reason_sz);

    /* Write the cache file */
    retval = true;

exit:
    if (retval)
    {
        FILE *fc;
        /* Write cache file */
        fc = fopen(PSTORE_REBOOT_TMP, "w+");
        if (fc != NULL)
        {
            fprintf(fc, "REBOOT %s %s\n", pstore_reboot_type_str(*type), reason);

            /* Set the immutable flag */
            ioctl(fileno(fc), FS_IOC_SETFLAGS, (int[]){ FS_IMMUTABLE_FL });
            fclose(fc);
        }
        else
        {
            LOG(ERR, "osp_reboot: Unable to create cache file: %s", PSTORE_REBOOT_TMP);
        }

        LOG(INFO, "osp_reboot: Last reboot reason: [%s] %s",
                pstore_reboot_type_str(*type),
                reason == NULL ? "" : reason);
    }

    free(pmsg);

    if (psd != NULL && closedir(psd) != 0)
    {
        LOG(WARN, "osp_reboot: Error closing PSTORE folder.");
    }

    return retval;
}

/* define signatures. keep 0 as last id for 'no-match' */
static os_reg_list_t dmesg_sig_pattern[] =
{
    /* ARM __show_regs */
    OS_REG_LIST_ENTRY(1, "PC is at"),
    OS_REG_LIST_ENTRY(2, "LR is at"),

    /* MIPS __show_regs */
    OS_REG_LIST_ENTRY(3, "epc"RE_SPACE": [[:xdigit:]]+"),
    OS_REG_LIST_ENTRY(4, "ra"RE_SPACE": [[:xdigit:]]+"),

    /* QCOM Wifi firmware assert */
    OS_REG_LIST_ENTRY(5, "\\[wifi"RE_NUM"\\]: XXX TARGET ASSERTED XXX"),
    OS_REG_LIST_ENTRY(6, "\\[02\\]"RE_SPACE":"RE_SPACE RE_XNUM),

    /* BUG crash recovery */
    OS_REG_LIST_ENTRY(7, "detected wmi/htc/ce stall"),
    OS_REG_LIST_ENTRY(8, "Temperature over thermal shutdown limit"),

    OS_REG_LIST_END(0)
};

/**
 * Open the dmesg file and search for signatures
 */
void pstore_parse_dmesg(const char *path, enum osp_reboot_type *type, char *reason, ssize_t rsz)
{
    FILE *fd;
    char dbuf[1024];
    int match;
    ssize_t cpysz;

    if ((NULL == reason) || (0 == rsz))
    {
        LOG(ERR, "osp_reboot: reason buffer is not valid");
        return;
    }

    fd = fopen(path, "r");
    if (fd == NULL)
    {
        LOG(ERR, "osp_reboot: Error opening DMESG file: %s", path);
        return;
    }

    /*
     * Just the mere fact the that the dmesg file exists it means that the kernel
     * crashed
     */
    *type = OSP_REBOOT_CRASH;

    /* quick way of cleaning up string buffer */
    reason[0] = '\0';

    /*
     * Scan the file and try to find the "PC is at" and "LR is at" lines,
     * which should give some indication on where the kernel actually crashed.
     */
    while (fgets(dbuf, sizeof(dbuf), fd) != NULL)
    {
        match = os_reg_list_match(dmesg_sig_pattern, dbuf, NULL, 0);
        if (match)
        {
            cpysz = strscat(reason, dbuf, rsz);
            if (cpysz < 0)
            {
                LOG(WARN, "%s: not enough memory to save dmesg sig reason",
                          __func__);
                break;
            }
        }
    }

    /* Unknown if no-match */
    if (0 == strnlen(reason, rsz))
        strscpy(reason, "(Unknown)", rsz);

    fclose(fd);
}

/**
 * Parse a single file containing the reboot reason:
 *
 * REBOOT [TYPE] Reason reason reason...
 *
 * For example:
 *
 * REBOOT USER Shell command.
 *
 * Lines not starting with REBOOT are skipped; if multiple REBOOT entries are
 * present only the last one will be returned.
 */
bool pstore_parse_reboot(
        const char *path,
        enum osp_reboot_type *type,
        char *reason,
        ssize_t rsz)
{
    FILE *fp;
    char pbuf[1024];
    char last_reboot[1024];
    int ii;

    bool retval = false;

    fp = fopen(path, "r");
    if (fp == NULL)
    {
        LOG(ERR, "osp_reboot: Error opening PMSG file: %s", path);
        return false;
    }

    last_reboot[0] = '\0';

    /* Scan the file and find the last line that starts with "REBOOT" */
    while (fgets(pbuf, sizeof(pbuf), fp) != NULL)
    {
        if (strncmp(pbuf, "REBOOT", strlen("REBOOT")) != 0) continue;

        strscpy(last_reboot, pbuf, sizeof(last_reboot));
    }

    /*
     * No line starting with "REBOOT" found, the reboot reason is probably
     * a power cycle or spontaneous reset
     */
    if (last_reboot[0] == '\0') goto exit;

    LOG(DEBUG, "osp_reboot: Parsing pmsg reboot line: %s", last_reboot);

    /* Extract the reboot type and reason from last_reboot */
    char *saveptr = NULL;
    char *sreboot = strtok_r(last_reboot, " \n", &saveptr);
    char *stype = strtok_r(NULL, " \n", &saveptr);
    char *sreason = strtok_r(NULL, "\n", &saveptr);

    if (sreboot == NULL || stype == NULL)
    {
        LOG(ERR, "osp_reboot: Error parsing last pmsg reboot line.");
        goto exit;
    }

    if (sreason == NULL) sreason = "";

    /* Lookup the reboot type */
    for (ii = 0; ii < ARRAY_LEN(pstore_reboot_type_map); ii++)
    {
        if (pstore_reboot_type_map[ii] == NULL) continue;
        if (strcmp(stype, pstore_reboot_type_map[ii]) == 0) break;
    }

    if (ii < ARRAY_LEN(pstore_reboot_type_map))
    {
        *type = ii;
    }
    else
    {
        LOG(WARN, "osp_reboot: Unknown reboot type: %s", stype);
    }

    strscpy(reason, sreason, rsz);

    retval = true;

exit:
    if (fp != NULL) fclose(fp);

    return retval;
}

/**
 * Return the reboot type as string
 */
static const char* pstore_reboot_type_str(enum osp_reboot_type type)
{
    const char *ret = pstore_reboot_type_map[OSP_REBOOT_UNKNOWN];

    if (type < ARRAY_LEN(pstore_reboot_type_map) && pstore_reboot_type_map[type] != NULL)
    {
        ret = pstore_reboot_type_map[type];
    }

    return ret;
}

/**
 * Save the reboot reason to PSTORE
 */
static bool pstore_save(enum osp_reboot_type type, const char *reason)
{
    char pbuf[1024];
    int pfd;

    bool retval = false;

    snprintf(pbuf, sizeof(pbuf), "REBOOT %s %s\n",
            pstore_reboot_type_str(type),
            reason == NULL ? "" : reason);

    pfd = open(CONFIG_OSP_REBOOT_PSTORE_DEV, O_WRONLY | O_APPEND);
    if (pfd < 0)
    {
        LOG(ERR, "osp_reboot: Error opening %s for writing. Error: %s",
                CONFIG_OSP_REBOOT_PSTORE_DEV,
                strerror(errno));
        goto exit;
    }

    /*
     * Write the reason as a single atomic write operation to prevent data
     * interleaving with other services writing to /dev/pmsg0 (logs)
     */
    if (write(pfd, pbuf, strlen(pbuf)) < 0)
    {
        LOG(ERR, "osp_reboot: Error writing to %s. Error: %s",
                CONFIG_OSP_REBOOT_PSTORE_DEV,
                strerror(errno));
    }

    retval = true;

exit:
    if (pfd >= 0) close(pfd);

    return retval;
}
