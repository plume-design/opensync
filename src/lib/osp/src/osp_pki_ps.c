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

#define _GNU_SOURCE /* For strptime() */
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "arena.h"
#include "arena_util.h"
#include "execssl.h"
#include "kconfig.h"
#include "log.h"
#include "memutil.h"
#include "osp_pki.h"
#include "osp_ps.h"
#include "util.h"

/* OpenSync persistent store name */
#define PKI_PS_STORE      "certs"
#define PKI_PS_STORE_KEY  "key"
#define PKI_PS_STORE_PUB  "pub"
#define PKI_PS_STORE_CRT  "crt"
#define PKI_PS_STORE_CA   "ca"
#define PKI_PS_EXT_NEW    ".new"
#define PKI_PS_EXT_RENAME ".default"

#define PKI_LABEL_STR(x) ((x) == NULL ? "<default>" : (x))

static void defer_osp_ps_fn(void *data);
static osp_ps_t *pki_ps_open(arena_t *a, int flags);
static bool pki_ps_get_string(arena_t *a, const char *label, const char *key, char **dest);
static bool pki_ps_set_string(const char *label, const char *key, const char *val);

static char *pki_key_gen(arena_t *arena);
static char *pki_pub_gen(arena_t *arena, const char *key);
static bool pki_cert_verify(const char *crt);
static bool pki_cert_info(const char *crt, time_t *expire_date, char *sub, size_t sub_sz);
static bool pki_ps_write(int cert_dir, const char *path, const char *label, const char *key);
static bool pki_safe_rename(int dir, const char *path);
static void pdefer_memset_fn(void *data);
static bool arena_defer_memset(arena_t *arena, void *data, size_t sz);
static bool arena_defer_sigblock(arena_t *arena);

/*
 * ======================================================================
 * Main API
 * ======================================================================
 */
bool osp_pki_cert_info(const char *label, time_t *expire_date, char *sub, size_t sub_sz)
{
    ARENA_SCRATCH(scratch);

    char *crt;
    if (!pki_ps_get_string(scratch, label, PKI_PS_STORE_CRT, &crt) || crt == NULL)
    {
        LOG(INFO, "osp_pki: %s: No certificate present.", PKI_LABEL_STR(label));
        return false;
    }

    /* Verify the crt */
    if (!pki_cert_verify(crt))
    {
        return false;
    }

    /* Verify the date and time */
    time_t enddate;
    if (!pki_cert_info(crt, &enddate, sub, sub_sz))
    {
        LOG(ERR, "osp_pki: %s: Unable to verify certificate expire date.", PKI_LABEL_STR(label));
        return false;
    }

    time_t cvalid = enddate - time(NULL);
    if (cvalid < 0)
    {
        time_t etime = -cvalid;
        LOG(NOTICE,
            "osp_pki: %s: Certificate expired %" PRId64 " days %" PRId64 " hours and %" PRId64 " minutes ago.",
            PKI_LABEL_STR(label),
            (int64_t)etime / (24 * 60 * 60),
            ((int64_t)etime / 60 / 60) % 24,
            ((int64_t)etime / 60) % 60);
    }
    else
    {
        LOG(NOTICE,
            "osp_pki: %s: Certificate expires in %" PRId64 " days %" PRId64 " hours and %" PRId64 " minutes.",
            PKI_LABEL_STR(label),
            (int64_t)cvalid / (24 * 60 * 60),
            ((int64_t)cvalid / 60 / 60) % 24,
            ((int64_t)cvalid / 60) % 60);
    }

    if (expire_date != NULL) *expire_date = enddate;

    return true;
}

char *osp_pki_cert_request(const char *label, const char *subject)
{
    ARENA_SCRATCH(scratch);

    LOG(INFO, "osp_pki: %s: Generating CSR: %s", PKI_LABEL_STR(label), subject);

    char *key;
    /*
     * It is intentional that NULL is used here for the label as we want just
     * a single private key.
     */
    if (!pki_ps_get_string(scratch, NULL, PKI_PS_STORE_KEY, &key) || key == NULL)
    {
        LOG(NOTICE, "osp_pki: Generating new private key...");
        key = pki_key_gen(scratch);
        if (key == NULL)
        {
            LOG(ERR, "osp_pki: Private key generation failed.");
            return NULL;
        }
    }

    if (!arena_defer_memset(scratch, key, strlen(key)))
    {
        LOG(ERR, "osp_pki: %s: Error deferring cleanup function (memset).", PKI_LABEL_STR(label));
        return NULL;
    }

    /*
     * Generate the CSR -- in newer version of openssl (3.x) it is impossible to read the private key from stdin
     * as `-key -` does not work. The workaround is to use `-key /dev/stdin`, however, `/dev/stdin` is not present
     * on many systems. Since `/dev/stdin` is usually just a symlink to `/proc/self/fd/0`, just use that directly.
     */
    char *bder = execssl_arena(
            scratch,
            key,
            "req",
            "-subj",
            subject,
            "-new",
            "-key",
            "/proc/self/fd/0",
            "-outform",
            "DER",
            "-out",
            "/tmp/csr.tmp");
    if (bder == NULL)
    {
        LOG(ERR, "osp_pki: %s: Error generating CSR.", PKI_LABEL_STR(label));
        return NULL;
    }

    /* Convert to base64 */
    char *csr = execssl_arena(scratch, NULL, "base64", "-in", "/tmp/csr.tmp");
    if (csr == NULL)
    {
        LOG(ERR, "osp_pki: %s: Error converting CSR to base64.", PKI_LABEL_STR(label));
        return NULL;
    }

    /* Strip \r and \n */
    strstrip(csr, "\r\n");

    (void)unlink("/tmp/csr.tmp");
    return STRDUP(csr);
}

bool osp_pki_cert_update(const char *label, const char *crt)
{
    ARENA_SCRATCH(scratch);

    if (!pki_cert_verify(crt))
    {
        LOG(ERR, "osp_pki: %s: Certificate verification failed.", PKI_LABEL_STR(label));
        return false;
    }

    /* Certificate seems ok, write it to persistent storage */
    if (!pki_ps_set_string(label, PKI_PS_STORE_CRT, crt))
    {
        LOG(EMERG, "osp_pki: %s: Unable to write certificate to store.", PKI_LABEL_STR(label));
        return false;
    }
    LOG(INFO, "osp_pki: %s: Certificate successfully updated.", PKI_LABEL_STR(label));

    return true;
}

bool osp_pki_cert_remove(const char *label)
{
    ARENA_SCRATCH(scratch);

    if (!arena_defer_sigblock(scratch))
    {
        LOG(WARN, "osp_pki: %s: Error blocking signals during certificate removal.", PKI_LABEL_STR(label));
    }

    if (label == NULL)
    {
        LOG(WARN, "osp_pki: The default certificate cannot be removed. Ignoring request.");
        return false;
    }

    osp_ps_t *ps = pki_ps_open(scratch, OSP_PS_WRITE | OSP_PS_PRESERVE | OSP_PS_ENCRYPTION);
    if (ps == NULL)
    {
        LOG(ERR, "osp_pki: %s: Error deleting certificate: Unable to open store.", PKI_LABEL_STR(label));
        return false;
    }

    char *key = arena_sprintf(scratch, "%s:%s", label, PKI_PS_STORE_CRT);
    if (key == NULL)
    {
        LOG(ERR, "osp_pki: %s: Error deleting certificate: Unable to create key.", PKI_LABEL_STR(label));
        return false;
    }

    ssize_t rc = osp_ps_set(ps, key, NULL, 0);
    if (rc != 0)
    {
        LOG(ERR, "osp_pki: %s: Error deleting certificate key: %s (%zd)", PKI_LABEL_STR(label), key, rc);
        return false;
    }

    /* Delete file structure -- this might cause soft errors, can be safely ignored */
    char *cert_path = arena_sprintf(scratch, "%s/%s", CONFIG_TARGET_PATH_CERT, label);
    if (cert_path == NULL)
    {
        LOG(ERR, "osp_pki: %s: Error deleting certificate: Unable to construct folder path.", PKI_LABEL_STR(label));
        return false;
    }

    int cert_dir = open(cert_path, O_RDONLY);
    if (cert_dir < 0 && errno == ENOENT)
    {
        LOG(DEBUG, "osp_pki: %s: Folder %s doesn't seem to exist, skipping.", PKI_LABEL_STR(label), cert_path);
        return true;
    }
    else if (cert_dir < 0)
    {
        LOG(ERR,
            "osp_pki: %s: Error deleting certificate: Unable to open folder '%s': %s",
            PKI_LABEL_STR(label),
            cert_path,
            strerror(errno));
        return false;
    }

    if (!arena_defer_close(scratch, cert_dir))
    {
        LOG(ERR, "osp_pki: %s: Error deleting certificate: Unable to defer close().", PKI_LABEL_STR(label));
        return false;
    }

    char *unlinks[] = {
        CONFIG_TARGET_PATH_PRIV_KEY,
        CONFIG_TARGET_PATH_PRIV_CERT,
        CONFIG_TARGET_PATH_PRIV_KEY "" PKI_PS_EXT_NEW,
        CONFIG_TARGET_PATH_PRIV_CERT "" PKI_PS_EXT_NEW,
        CONFIG_TARGET_PATH_PRIV_KEY "" PKI_PS_EXT_RENAME,
        CONFIG_TARGET_PATH_PRIV_CERT "" PKI_PS_EXT_RENAME,
    };

    for (int ii = 0; ii < ARRAY_LEN(unlinks); ii++)
    {
        if (unlinkat(cert_dir, unlinks[ii], 0) != 0)
        {
            LOG(DEBUG,
                "osp_pki: %s: Error deleting certificate: Unable to unlink '%s': %s",
                PKI_LABEL_STR(label),
                unlinks[ii],
                CONFIG_TARGET_PATH_PRIV_KEY);
        }
    }

    if (rmdir(cert_path) != 0)
    {
        LOG(WARN, "osp_pki: %s: Unable to remove folder '%s': %s", PKI_LABEL_STR(label), cert_path, strerror(errno));
    }

    return true;
}

/*
 * Install certificates and keys to tmpfs. This function will rename existing
 * certficates/keys by adding the ".default" extension. Certificates from the
 * persistent store will be copied to the destination folder using the ".new"
 * extension. Finally it will symlink the certificate/private keys filenames
 * to the ".new" files.
 */
bool osp_pki_cert_install(const char *label)
{
    ARENA_SCRATCH(scratch);
    char *cert_path;
    int cert_dir;

    /* First, check if we have a valid certificate and private key */
    if (!osp_pki_cert_info(label, NULL, NULL, 0))
    {
        /* No valid certificate yet, nothing to do */
        return false;
    }

    LOG(INFO, "osp_pki: %s: Certificate check passed, installing...", PKI_LABEL_STR(label));

    if (!arena_defer_sigblock(scratch))
    {
        LOG(WARN, "osp_pki: %s: Error blocking signals during certificate installation.", PKI_LABEL_STR(label));
    }

    /* Generate the destination folder name */
    if (label == NULL)
    {
        cert_path = CONFIG_TARGET_PATH_CERT;
    }
    else
    {
        cert_path = arena_sprintf(scratch, CONFIG_TARGET_PATH_CERT "/%s", label);
        /* Create the target folder, if it does not exist */
        if (access(cert_path, R_OK) != 0 && mkdir(cert_path, 0755) != 0)
        {
            LOG(ERR, "osp_pki: %s: Error creating certificate: %s", PKI_LABEL_STR(label), cert_path);
            return false;
        }
    }

    cert_dir = open(cert_path, O_RDONLY);
    if (cert_dir < 0)
    {
        LOG(ERR, "osp_pki: %s: Unable to open certificate folder: %s", PKI_LABEL_STR(label), strerror(errno));
        return false;
    }

    if (!arena_defer_close(scratch, cert_dir))
    {
        LOG(ERR, "osp_pki: %s: Unable to defer cert dir close.", PKI_LABEL_STR(label));
        return false;
    }

    /*
     * Write out the certificates and keys to filesystem
     */
    if (!pki_ps_write(cert_dir, CONFIG_TARGET_PATH_PRIV_KEY "" PKI_PS_EXT_NEW, NULL, PKI_PS_STORE_KEY))
    {
        LOG(ERR, "osp_pki: %s: Error writing private key.", PKI_LABEL_STR(label));
        return false;
    }

    if (!pki_ps_write(cert_dir, CONFIG_TARGET_PATH_PRIV_CERT "" PKI_PS_EXT_NEW, label, PKI_PS_STORE_CRT))
    {
        LOG(ERR, "osp_pki: %s: Error writing certificate.", PKI_LABEL_STR(label));
        return false;
    }

    /*
     * Check the current key/certificate files. If it is not a symlink,
     * rename.
     */
    if (!pki_safe_rename(cert_dir, CONFIG_TARGET_PATH_PRIV_KEY))
    {
        LOG(ERR, "osp_pki: %s: Error renaming private key.", PKI_LABEL_STR(label));
        return false;
    }

    if (!pki_safe_rename(cert_dir, CONFIG_TARGET_PATH_PRIV_CERT))
    {
        LOG(ERR, "osp_pki: %s: Error renaming certificate.", PKI_LABEL_STR(label));
        return false;
    }

    /*
     * Create the symbolic links
     */
    if (symlinkat(CONFIG_TARGET_PATH_PRIV_KEY "" PKI_PS_EXT_NEW, cert_dir, CONFIG_TARGET_PATH_PRIV_KEY) != 0)
    {
        LOG(WARN, "osp_pki: %s: Error creating symbolic link to private key.", PKI_LABEL_STR(label));
    }

    if (symlinkat(CONFIG_TARGET_PATH_PRIV_CERT "" PKI_PS_EXT_NEW, cert_dir, CONFIG_TARGET_PATH_PRIV_CERT) != 0)
    {
        LOG(WARN, "osp_pki: %s: Error creating symbolic link to certificate.", PKI_LABEL_STR(label));
    }

    LOG(INFO, "osp_pki: %s: Certificate successfully installed.", PKI_LABEL_STR(label));

    return true;
}

bool osp_pki_setup(void)
{
    ARENA_SCRATCH(scratch);

    bool retval = true;

    osp_ps_t *ps = pki_ps_open(scratch, OSP_PS_READ | OSP_PS_PRESERVE | OSP_PS_ENCRYPTION);
    if (ps == NULL) return false;

    const char *key;
    while ((key = osp_ps_next(ps)) != NULL)
    {
        /* Reset arena frame each iteration */
        arena_frame_auto_t af = arena_save(scratch);

        char *label;
        char *type;

        char *pkey = arena_strdup(scratch, key);

        /* Look for keys that match the "crt" or "LABEL:crt" pattern */
        if (strchr(pkey, ':') == NULL)
        {
            label = NULL;
            type = pkey;
        }
        else
        {
            label = strsep(&pkey, ":");
            type = strsep(&pkey, ":");
        }

        if (strcmp(type, PKI_PS_STORE_CRT) != 0) continue;

        if (!osp_pki_cert_install(label))
        {
            LOG(WARN, "osp_pki: %s: Error installing certificate.", PKI_LABEL_STR(label));
            retval = false;
        }
    }

    return retval;
}

/*
 * ======================================================================
 * Persistent store function
 * ======================================================================
 */
void defer_osp_ps_fn(void *data)
{
    osp_ps_t *ps = data;

    if (!osp_ps_close(ps))
    {
        LOG(WARN, "osp_pki: Error closing persistent store in defer function.");
    }
}

osp_ps_t *pki_ps_open(arena_t *a, int flags)
{
    osp_ps_t *ps = osp_ps_open(PKI_PS_STORE, flags);
    if (ps == NULL || !arena_defer(a, defer_osp_ps_fn, ps))
    {
        LOG(DEBUG, "osp_pki: Error opening persistent storage for reading: %s", PKI_PS_STORE);
        return NULL;
    }
    return ps;
}

bool pki_ps_set_string(const char *label, const char *key, const char *val)
{
    ARENA_SCRATCH(scratch);
    osp_ps_t *ps;

    if (label != NULL)
    {
        key = arena_sprintf(scratch, "%s:%s", label, key);
        if (key == NULL) return false;
    }

    ps = pki_ps_open(scratch, OSP_PS_WRITE | OSP_PS_PRESERVE | OSP_PS_ENCRYPTION);
    if (ps == NULL)
    {
        LOG(EMERG, "osp_pki: %s: Error opening OpenSync persistent storage.", PKI_LABEL_STR(label));
        return false;
    }

    if (osp_ps_set(ps, key, (char *)val, strlen(val)) <= 0)
    {
        LOG(EMERG, "osp_pki: %s: Error storing key to private store.", PKI_LABEL_STR(label));
        return false;
    }
    return true;
}

bool pki_ps_get_string(arena_t *arena, const char *label, const char *key, char **val)
{
    ARENA_SCRATCH(scratch, arena);

    osp_ps_t *ps;
    ssize_t rc;

    if (label != NULL)
    {
        key = arena_sprintf(scratch, "%s:%s", label, key);
        if (key == NULL) return false;
    }

    ps = pki_ps_open(scratch, OSP_PS_READ | OSP_PS_PRESERVE | OSP_PS_ENCRYPTION);
    if (ps == NULL) return false;

    rc = osp_ps_get(ps, key, NULL, 0);
    if (rc < 0) return false;

    if (rc == 0)
    {
        *val = NULL;
        return true;
    }

    *val = arena_push(arena, rc + 1);
    if (osp_ps_get(ps, key, *val, rc) != rc)
    {
        LOG(NOTICE, "osp_pki: %s: PS Key size changed. Data may be truncated.", PKI_LABEL_STR(label));
    }

    /* Pad the string with \0 */
    (*val)[rc] = '\0';

    return true;
}

/*
 * Write the data associated with @p key to the file represented by @p path
 */
bool pki_ps_write(int cert_dir, const char *path, const char *label, const char *key)
{
    ARENA_SCRATCH(scratch);

    char *data;
    if (!pki_ps_get_string(scratch, label, key, &data) || data == NULL)
    {
        LOG(DEBUG, "osp_pki: %s: Error retrieving data or data does not exist: %s", PKI_LABEL_STR(label), path);
        return false;
    }

    int fd = openat(cert_dir, path, O_WRONLY | O_CREAT | O_TRUNC, 0660);
    if (fd < 0)
    {
        LOG(DEBUG, "osp_pki: %s: Error opening file: %s", PKI_LABEL_STR(label), path);
        return false;
    }
    ssize_t rc = write(fd, data, strlen(data));
    if (rc < 0)
    {
        LOG(DEBUG, "osp_pki: %s: Error writing data: %s", PKI_LABEL_STR(label), path);
    }
    else if ((size_t)rc != strlen(data))
    {
        LOG(DEBUG, "osp_pki: %s: Short write: %s", PKI_LABEL_STR(label), path);
    }
    close(fd);

    return true;
}

/*
 * Try to safely rename the current file:
 *
 *  - If the target already exists and is a regular file, assume a rename
 *    operation already occurred and return success
 *  - If the current file is not a regular file or does not exist, return an error
 */
bool pki_safe_rename(int dir, const char *path)
{
    struct stat st;
    char target_path[strlen(path) + strlen(PKI_PS_EXT_RENAME) + sizeof(char)];

    /* Construct the target_path by append PKI_STORE_RENAME_EXT to the original filename */
    snprintf(target_path, sizeof(target_path), "%s%s", path, PKI_PS_EXT_RENAME);

    /* Check if the current file exists and/or is a regular file */
    if (fstatat(dir, path, &st, AT_SYMLINK_NOFOLLOW) != 0)
    {
        /* File does not exist -- nothing to do */
        if (errno == ENOENT) return true;

        LOG(ERR, "osp_pki: %s: Error stating source file: %s\n", path, strerror(errno));
        return false;
    }
    else if (S_ISLNK(st.st_mode))
    {
        /* Source file is a symbolic link, probably from a previous rename. Remove it */
        unlinkat(dir, path, 0);
        return true;
    }
    else if (!S_ISREG(st.st_mode))
    {
        /*
         * At this point we know the file exists and is not a symbolic link,
         * so it should be a regular file. However, if it is not, bail out.
         */
        LOG(ERR, "osp_pki: %s: Not a regular file.", path);
        return false;
    }

    /* Check if the target_path exists and is a regular file */
    if (fstatat(dir, target_path, &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(st.st_mode))
    {
        return true;
    }

    /*
     * Target path does not exist and the source is a regular file. Rename
     * the files
     */
    LOG(DEBUG, "osp_pki: Renaming %s -> %s\n", path, target_path);
    if (renameat(dir, path, dir, target_path) != 0)
    {
        LOG(ERR, "osp_pki: %s: Error renaming file: %s\n", path, strerror(errno));
        return false;
    }

    return true;
}

/*
 * ======================================================================
 * Key management functions
 * ======================================================================
 */

/*
 * Generate the private key and store it to persistent storage
 */
char *pki_key_gen(arena_t *arena)
{
    ARENA_SCRATCH(scratch, arena);

    char *key;
    if (kconfig_enabled(CONFIG_OSP_PKI_PS_ALGO_RSA4096))
    {
        key = execssl_arena(
                arena,
                NULL,
                "genpkey",
                "-algorithm",
                "RSA",
                "-pkeyopt",
                "rsa_keygen_bits:4096",
                "-out",
                "-");
    }
    else if (kconfig_enabled(CONFIG_OSP_PKI_PS_ALGO_RSA3072))
    {
        key = execssl_arena(
                arena,
                NULL,
                "genpkey",
                "-algorithm",
                "RSA",
                "-pkeyopt",
                "rsa_keygen_bits:3072",
                "-out",
                "-");
    }
    else if (kconfig_enabled(CONFIG_OSP_PKI_PS_ALGO_ED25519))
    {
        key = execssl_arena(arena, NULL, "genpkey", "-algorithm", "ed25519", "-out", "-");
    }
    else if (kconfig_enabled(CONFIG_OSP_PKI_PS_ALGO_P256))
    {
        key = execssl_arena(
                arena,
                NULL,
                "genpkey",
                "-algorithm",
                "EC",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-pkeyopt",
                "ec_param_enc:named_curve",
                "-out",
                "-");
    }
    else
    {
        if (!kconfig_enabled(CONFIG_OSP_PKI_PS_ALGO_P384))
        {
            LOG(WARN, "osp_pki: Invalid or no PK algorithm selected, using EC:P384 as default.");
        }

        key = execssl_arena(
                arena,
                NULL,
                "genpkey",
                "-algorithm",
                "EC",
                "-pkeyopt",
                "ec_paramgen_curve:P-384",
                "-pkeyopt",
                "ec_param_enc:named_curve",
                "-out",
                "-");
    }
    if (key == NULL)
    {
        LOG(EMERG, "osp_pki: Unable to generate private key. Aborting.");
        return NULL;
    }

    char *pub = pki_pub_gen(scratch, key);
    if (pub == NULL)
    {
        LOG(EMERG, "osp_pki: Unable to verify private key. Aborting.");
        return NULL;
    }

    if (!pki_ps_set_string(NULL, PKI_PS_STORE_PUB, pub))
    {
        LOG(EMERG, "osp_pki: Error writing public key to persistent storage.");
        return NULL;
    }

    if (!pki_ps_set_string(NULL, PKI_PS_STORE_KEY, key))
    {
        LOG(EMERG, "osp_pki: Error writing private key to persistent storage.");
        return NULL;
    }

    return key;
}

/*
 * Derive the public key from the private key
 */
char *pki_pub_gen(arena_t *arena, const char *key)
{
    return execssl_arena(arena, key, "pkey", "-pubout");
}

/*
 * Compare the public keys derived from the public key and certificate. If they
 * do n ot match, fail the check.
 */
bool pki_cert_verify(const char *crt)
{
    ARENA_SCRATCH(scratch);

    /* Extract the public key from the certificate */
    char *pub_crt = execssl_arena(scratch, crt, "x509", "-pubkey", "-noout");
    if (pub_crt == NULL)
    {
        LOG(INFO, "osp_pki: Unable to verify certificate: Error extracting public key from certificate.");
        return false;
    }

    /* Verify the certificate using the private key */
    char *key;
    if (!pki_ps_get_string(scratch, NULL, PKI_PS_STORE_KEY, &key) || key == NULL)
    {
        LOG(ERR, "osp_pki: Unable to verify certificate: Private key missing.");
        return false;
    }

    if (!arena_defer_memset(scratch, key, strlen(key)))
    {
        LOG(ERR, "osp_pki: Error deferring memset (pki_cert_verify).");
        return false;
    }

    char *pub_key = pki_pub_gen(scratch, key);
    if (pub_key == NULL)
    {
        LOG(ERR, "osp_pki: Unable to verify certificate: Error deriving public key from private key.");
        return false;
    }

    if (strcmp(pub_key, pub_crt) != 0)
    {
        LOG(ERR, "osp_pki: Certificate verification failed: Public keys mismatch.");
        return false;
    }

    return true;
}

/*
 * Get the expiration date of the certificate
 */
bool pki_cert_info(const char *crt, time_t *expire_date, char *sub, size_t sub_sz)
{
    ARENA_SCRATCH(scratch);
    struct tm tm;
    time_t edate;

    char *crtinfo = execssl_arena(scratch, crt, "x509", "-enddate", "-subject", "-nameopt", "compat", "-noout");
    if (crtinfo == NULL)
    {
        LOG(ERR, "osp_pki: Error retrieving certificate information.");
        return false;
    }

    /* Extract the end date and subject from the openssl output */
    char *p = crtinfo;
    char *dateinfo = strsep(&p, "\n");
    char *subinfo = strsep(&p, "\n");
    if (dateinfo == NULL || subinfo == NULL)
    {
        LOG(ERR, "osp_pki: Error parsing certificate information.");
        return false;
    }

    if (strptime(dateinfo, "notAfter=%b %d %H:%M:%S %Y", &tm) == NULL)
    {
        LOG(ERR, "osp_pki: Error parsing certificate expiration date: %s", dateinfo);
        return false;
    }

    /*
     * The certificate time is always assumed to be in UTC, however the tm
     * structure doesn't have any notion of timezones. Therefore we need to
     * convert it to unix time by using timegm(), which is a non-portable
     * function (not POSIX). Fortunately it is available in both uClibc and
     * libmusl.
     */
    edate = timegm(&tm);
    if (edate == -1)
    {
        LOG(ERR, "osp_pki: Error converting certificate expire date to Unix time.");
        return false;
    }

    if (strncmp(subinfo, "subject=", strlen("subject=")) != 0)
    {
        LOG(ERR, "osp_pki: Error parsing subject line: %s\n", subinfo);
        return false;
    }

    subinfo += strlen("subject=");

    if (expire_date != NULL) *expire_date = edate;
    if (sub != NULL && sub_sz != 0) strscpy(sub, subinfo, sub_sz);

    return true;
}

/*
 * ======================================================================
 * Miscellaneous
 * ======================================================================
 */
struct defer_memset_ctx
{
    void *data;
    size_t datasz;
};

void pdefer_memset_fn(void *data)
{
    struct defer_memset_ctx *ctx = data;
    memset(ctx->data, 0, ctx->datasz);
}

bool arena_defer_memset(arena_t *arena, void *data, size_t datasz)
{
    struct defer_memset_ctx ctx = {
        .data = data,
        .datasz = datasz,
    };

    return arena_defer_copy(arena, pdefer_memset_fn, &ctx, sizeof(ctx));
}

struct defer_sigblock
{
    sigset_t ds_oldmask;
};

void defer_sigblock_fn(void *data)
{
    struct defer_sigblock *ctx = data;

    if (sigprocmask(SIG_SETMASK, &ctx->ds_oldmask, NULL) != 0)
    {
        LOG(WARN, "osp_pki: sigprocmask() failed to restore signals.");
    }
}

/*
 * Block signals until the arena defer function is called.
 *
 * This function blocks several signals that may interrupt the execution of the
 * process, but the most important signal to block is SIGTERM. Blocking signals
 * prevents from writing a partial certificate state to the filesystem (for
 * example, the process writes the private key, but is killed before it can
 * write the public key).
 */
bool arena_defer_sigblock(arena_t *arena)
{
    sigset_t block_mask;

    struct defer_sigblock ds = {0};

    /*
     * SIGTERM - Normal manager shutdown
     * SIGINT - CTRL-C
     * SIGQUIT - CTRL-\
     * SIGHUP - Usually config reload, block it just in case
     */
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGTERM);
    sigaddset(&block_mask, SIGINT);
    sigaddset(&block_mask, SIGQUIT);
    sigaddset(&block_mask, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &block_mask, &ds.ds_oldmask) != 0)
    {
        LOG(WARN, "osp_pki: Unable to block signals, sigprocmask() failed: %s", strerror(errno));
        return false;
    }

    return arena_defer_copy(arena, defer_sigblock_fn, &ds, sizeof(ds));
}
