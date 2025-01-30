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

#include "execssl.h"
#include "kconfig.h"
#include "log.h"
#include "memutil.h"
#include "osp_pki.h"
#include "osp_ps.h"
#include "util.h"

/* OpenSync persistent store name */
#define PKI_PS_STORE_NAME       "certs"
#define PKI_PS_STORE_KEY        "key"
#define PKI_PS_STORE_PUB        "pub"
#define PKI_PS_STORE_CRT        "crt"
#define PKI_PS_STORE_NEW_EXT    ".new"
#define PKI_PS_STORE_RENAME_EXT ".default"

static bool pki_ps_get_string(const char *key, char **dest);
static bool pki_ps_set_string(const char *key, const char *val);

static char *pki_key_gen(void);
static char *pki_pub_gen(const char *key);
static bool pki_cert_verify(const char *crt);
static bool pki_cert_info(const char *crt, time_t *expire_date, char *sub, size_t sub_sz);
static bool pki_ps_write(int cert_dir, const char *path, const char *key);
static bool pki_safe_rename(int dir, const char *path);

static void strcfree(char *buf);

/*
 * ======================================================================
 * Main API
 * ======================================================================
 */
bool osp_pki_cert_info(time_t *expire_date, char *sub, size_t sub_sz)
{
    char *crt = NULL;
    char *key = NULL;
    bool retval = false;
    time_t enddate;

    time_t cvalid;

    if (!pki_ps_get_string(PKI_PS_STORE_CRT, &crt) || crt == NULL)
    {
        /* No certificate, return false */
        goto exit;
    }

    /* Verify the crt */
    if (!pki_cert_verify(crt))
    {
        goto exit;
    }

    /* Verify the date and time */
    if (!pki_cert_info(crt, &enddate, sub, sub_sz))
    {
        LOG(ERR, "Unable to verify certificate expire date.");
        goto exit;
    }

    cvalid = enddate - time(NULL);

    if (cvalid < 0)
    {
        time_t etime = -cvalid;
        LOG(NOTICE,
            "Certificate expired %" PRId64 " days %" PRId64 " hours and %" PRId64 " minutes ago.",
            (int64_t)etime / (24 * 60 * 60),
            ((int64_t)etime / 60 / 60) % 24,
            ((int64_t)etime / 60) % 60);
    }
    else
    {
        LOG(NOTICE,
            "Certificate expires in %" PRId64 " days %" PRId64 " hours and %" PRId64 " minutes.",
            (int64_t)cvalid / (24 * 60 * 60),
            ((int64_t)cvalid / 60 / 60) % 24,
            ((int64_t)cvalid / 60) % 60);
    }

    if (expire_date != NULL) *expire_date = enddate;

    retval = true;

exit:
    FREE(crt);
    if (key != NULL) strcfree(key);

    return retval;
}

char *osp_pki_cert_request(const char *subject)
{
    LOG(INFO, "Generating CSR: %s", subject);

    char *key = NULL;
    char *csr = NULL;
    char *bder = NULL;

    if (!pki_ps_get_string(PKI_PS_STORE_KEY, &key) || key == NULL)
    {
        LOG(NOTICE, "Generating new private key...");
        key = pki_key_gen();
        if (key == NULL)
        {
            LOG(ERR, "Private key generation failed.");
            goto exit;
        }
    }

    /*
     * Genereate the CSR -- in newer version of openssl (3.x) it is impossible to read the private key from stdin
     * as `-key -` does not work. The workaround is to use `-key /dev/stdin`, however, `/dev/stdin` is not present
     * on many systems. Since `/dev/stdin` is usually just a symlink to `/proc/self/fd/0`, just use that directly.
     */
    bder =
            execssl(key,
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
        LOG(ERR, "Error generating CSR.");
        goto exit;
    }

    /* Convert to base64 */
    csr = execssl(NULL, "base64", "-in", "/tmp/csr.tmp");
    if (csr == NULL)
    {
        LOG(ERR, "Error converting to base64");
    }

    /* Strip \r and \n */
    strstrip(csr, "\r\n");

exit:
    if (key != NULL) strcfree(key);
    if (bder != NULL) FREE(bder);
    (void)unlink("/tmp/csr.tmp");
    return csr;
}

bool osp_pki_cert_update(const char *crt)
{
    if (!pki_cert_verify(crt))
    {
        LOG(ERR, "Certificate verification failed.");
        return false;
    }

    /* Certificate seems ok, write it to persistent storage */
    if (!pki_ps_set_string(PKI_PS_STORE_CRT, crt))
    {
        LOG(EMERG, "Unable to write certificate to store.");
        return false;
    }

    LOG(NOTICE, "Certificate successfully updated.");

    return true;
}

/*
 * Install certificates and keys to tmpfs. This function will rename existing
 * certficates/keys by adding the ".defualt" extension. Certificates from the
 * persistent store will be copied to the destination folder using the ".new"
 * extension. Finally it will symlink the certificate/private keys filenames
 * to the ".new" files.
 */
bool osp_pki_setup(void)
{
    int cert_dir = -1;
    bool retval = false;

    /* First, check if we have a valid certificate and private key */
    if (!osp_pki_cert_info(NULL, NULL, 0))
    {
        /* No valid certificate yet, nothing to do */
        return true;
    }

    LOG(INFO, "Certificate check passed, installing...");

    cert_dir = open(CONFIG_TARGET_PATH_CERT, O_RDONLY);
    if (cert_dir < 0)
    {
        LOG(ERR, "Unable to open certificate folder: %s\n", strerror(errno));
        goto exit;
    }

    /*
     * Write out the certificates and keys to filesystem
     */
    if (!pki_ps_write(cert_dir, CONFIG_TARGET_PATH_PRIV_KEY "" PKI_PS_STORE_NEW_EXT, PKI_PS_STORE_KEY))
    {
        LOG(ERR, "Error writing private key.");
        goto exit;
    }

    if (!pki_ps_write(cert_dir, CONFIG_TARGET_PATH_PRIV_CERT "" PKI_PS_STORE_NEW_EXT, PKI_PS_STORE_CRT))
    {
        LOG(ERR, "Error writing certificate.");
        goto exit;
    }

    /*
     * Check the current key/certificate files. If it is not a symlink,
     * rename.
     */
    if (!pki_safe_rename(cert_dir, CONFIG_TARGET_PATH_PRIV_KEY))
    {
        LOG(ERR, "Error renaming private key.");
        goto exit;
    }

    if (!pki_safe_rename(cert_dir, CONFIG_TARGET_PATH_PRIV_CERT))
    {
        LOG(ERR, "Error renaming certificate.");
        goto exit;
    }

    /*
     * Create the symbolic links
     */
    if (symlinkat(CONFIG_TARGET_PATH_PRIV_KEY "" PKI_PS_STORE_NEW_EXT, cert_dir, CONFIG_TARGET_PATH_PRIV_KEY) != 0)
    {
        LOG(WARN, "Error creating symbolic link to private key.");
    }

    if (symlinkat(CONFIG_TARGET_PATH_PRIV_CERT "" PKI_PS_STORE_NEW_EXT, cert_dir, CONFIG_TARGET_PATH_PRIV_CERT) != 0)
    {
        LOG(WARN, "Error creating symbolic link to certificate.");
    }

    LOG(INFO, "Certificate successfully updated.");

    retval = true;

exit:
    if (cert_dir >= 0) close(cert_dir);

    return retval;
}

/*
 * ======================================================================
 * Persistent store function
 * ======================================================================
 */
bool pki_ps_set_string(const char *key, const char *val)
{
    osp_ps_t *ps = NULL;
    bool retval = false;

    ps = osp_ps_open(PKI_PS_STORE_NAME, OSP_PS_WRITE | OSP_PS_PRESERVE | OSP_PS_ENCRYPTION);
    if (ps == NULL)
    {
        LOG(EMERG, "Error opening OpenSync persistent storage.");
        goto exit;
    }

    if (osp_ps_set(ps, key, (char *)val, strlen(val)) <= 0)
    {
        LOG(EMERG, "Error storing key to private store.");
        goto exit;
    }

    retval = true;

exit:
    if (ps != NULL) osp_ps_close(ps);
    return retval;
}

bool pki_ps_get_string(const char *key, char **val)
{
    ssize_t rc;

    osp_ps_t *ps = NULL;
    bool retval = false;

    *val = NULL;

    ps = osp_ps_open(PKI_PS_STORE_NAME, OSP_PS_READ | OSP_PS_PRESERVE | OSP_PS_ENCRYPTION);
    if (ps == NULL)
    {
        LOG(DEBUG, "Error opening persistent storage for reading.");
        goto exit;
    }

    rc = osp_ps_get(ps, key, NULL, 0);
    if (rc < 0)
    {
        goto exit;
    }
    else if (rc == 0)
    {
        retval = true;
        goto exit;
    }

    *val = MALLOC(rc + 1);
    if (osp_ps_get(ps, key, *val, rc) != rc)
    {
        LOG(NOTICE, "PS Key size changed. Data may be truncated.");
    }

    /* Pad the string with */
    (*val)[rc] = '\0';

    retval = true;

exit:
    if (ps != NULL) osp_ps_close(ps);
    return retval;
}

/*
 * Write the data associated with @p key to the file represented by @p path
 */
bool pki_ps_write(int cert_dir, const char *path, const char *key)
{
    bool retval = false;
    char *data = NULL;
    int fd = -1;
    ssize_t rc;

    if (!pki_ps_get_string(key, &data) || data == NULL)
    {
        LOG(DEBUG, "%s: Error retrieving data or data does not exist.", path);
        goto exit;
    }

    fd = openat(cert_dir, path, O_WRONLY | O_CREAT | O_TRUNC, 0660);
    if (fd < 0)
    {
        LOG(DEBUG, "%s: Error opening file.", path);
        goto exit;
    }

    rc = write(fd, data, strlen(data));
    if (rc < 0)
    {
        LOG(DEBUG, "%s: Error writing data.", path);
    }
    else if ((size_t)rc != strlen(data))
    {
        LOG(DEBUG, "%s: Short write.", path);
    }

    retval = true;

exit:
    if (fd >= 0) close(fd);
    if (data != NULL) strcfree(data);

    return retval;
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
    char target_path[strlen(path) + strlen(PKI_PS_STORE_RENAME_EXT) + sizeof(char)];

    /* Construct the target_path by append PKI_STORE_RENAME_EXT to the original filename */
    snprintf(target_path, sizeof(target_path), "%s%s", path, PKI_PS_STORE_RENAME_EXT);

    /* Check if the current file exists and/or is a regular file */
    if (fstatat(dir, path, &st, AT_SYMLINK_NOFOLLOW) != 0)
    {
        /* File does not exist -- nothing to do */
        if (errno == ENOENT) return true;

        LOG(ERR, "%s: Error stating source file: %s\n", path, strerror(errno));
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
        LOG(ERR, "%s: Not a regular file.", path);
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
    LOG(DEBUG, "Renaming %s -> %s\n", path, target_path);
    if (renameat(dir, path, dir, target_path) != 0)
    {
        LOG(ERR, "%s: Error renaming file: %s\n", path, strerror(errno));
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
char *pki_key_gen(void)
{
    char *key = NULL;
    char *pub = NULL;
    char *retval = NULL;

    if (kconfig_enabled(CONFIG_OSP_PKI_PS_ALGO_RSA4096))
    {
        key = execssl(NULL, "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:4096", "-out", "-");
    }
    else if (kconfig_enabled(CONFIG_OSP_PKI_PS_ALGO_RSA3072))
    {
        key = execssl(NULL, "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072", "-out", "-");
    }
    else if (kconfig_enabled(CONFIG_OSP_PKI_PS_ALGO_ED25519))
    {
        key = execssl(NULL, "genpkey", "-algorithm", "ed25519", "-out", "-");
    }
    else if (kconfig_enabled(CONFIG_OSP_PKI_PS_ALGO_P256))
    {
        key =
                execssl(NULL,
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
            LOG(WARN, "Invalid or no PK algorithm selected, using EC:P384 as default.");
        }

        key =
                execssl(NULL,
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
        LOG(EMERG, "Unable to generate private key. Aborting.");
        goto exit;
    }

    pub = pki_pub_gen(key);
    if (pub == NULL)
    {
        LOG(EMERG, "Unable to verify private key. Aborting.");
        goto exit;
    }

    if (!pki_ps_set_string(PKI_PS_STORE_PUB, pub))
    {
        LOG(EMERG, "Error writing public key to persistent storage.");
        goto exit;
    }

    if (!pki_ps_set_string(PKI_PS_STORE_KEY, key))
    {
        LOG(EMERG, "Error writing private key to persistent storage.");
        goto exit;
    }

    retval = key;
    key = NULL;

exit:
    if (pub != NULL) FREE(pub);
    if (key != NULL) strcfree(key);

    return retval;
}

/*
 * Derive the public key from the private key
 */
char *pki_pub_gen(const char *key)
{
    return execssl(key, "pkey", "-pubout");
}

/*
 * Compare the public keys derived from the public key and certificate. If they
 * do n ot match, fail the check.
 */
bool pki_cert_verify(const char *crt)
{
    char *key = NULL;
    char *pub_key = NULL;
    char *pub_crt = NULL;
    bool retval = false;

    /* Extract the public key from the certificate */
    pub_crt = execssl(crt, "x509", "-pubkey", "-noout");
    if (pub_crt == NULL)
    {
        LOG(INFO, "Unable to verify certificate: Error extracting public key from certificate.");
        goto exit;
    }

    /* Verify the certificate using the private key */
    if (!pki_ps_get_string(PKI_PS_STORE_KEY, &key) || key == NULL)
    {
        LOG(ERR, "Unable to verify certificate: Private key missing.");
        goto exit;
    }

    pub_key = pki_pub_gen(key);
    if (pub_key == NULL)
    {
        LOG(ERR, "Unable to verify certificate: Error deriving public key from private key.");
        goto exit;
    }

    if (strcmp(pub_key, pub_crt) != 0)
    {
        LOG(ERR, "Certificate verification failed: Public keys mismatch.");
        goto exit;
    }

    retval = true;

exit:
    if (key != NULL) strcfree(key);
    if (pub_key != NULL) FREE(pub_key);
    if (pub_crt != NULL) FREE(pub_crt);

    return retval;
}

/*
 * Get the expiration date of the certificate
 */
bool pki_cert_info(const char *crt, time_t *expire_date, char *sub, size_t sub_sz)
{
    struct tm tm;
    time_t edate;
    char *p;

    char *crtinfo = NULL;
    char *dateinfo = NULL;
    char *subinfo = NULL;
    bool retval = false;

    crtinfo = execssl(crt, "x509", "-enddate", "-subject", "-nameopt", "compat", "-noout");
    if (crtinfo == NULL)
    {
        LOG(ERR, "Error retriving certificate information.");
        goto exit;
    }

    /* Extract the end date and subject from the openssl output */
    p = crtinfo;
    dateinfo = strsep(&p, "\n");
    subinfo = strsep(&p, "\n");
    if (dateinfo == NULL || subinfo == NULL)
    {
        LOG(ERR, "Error parsing certificate information.");
        goto exit;
    }

    if (strptime(dateinfo, "notAfter=%b %d %H:%M:%S %Y", &tm) == NULL)
    {
        LOG(ERR, "Error parsing certificate expiration date: %s", dateinfo);
        goto exit;
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
        LOG(ERR, "Error converting certificate expire date to Unix time.");
        goto exit;
    }

    if (strncmp(subinfo, "subject=", strlen("subject=")) != 0)
    {
        LOG(ERR, "Error parsing subject line: %s\n", subinfo);
        goto exit;
    }

    subinfo += strlen("subject=");

    if (expire_date != NULL) *expire_date = edate;
    if (sub != NULL && sub_sz != 0) strscpy(sub, subinfo, sub_sz);

    retval = true;

exit:
    FREE(crtinfo);
    return retval;
}

/*
 * ======================================================================
 * Miscellaneous
 * ======================================================================
 */

/* Clear and free a string */
void strcfree(char *buf)
{
    memset(buf, 0xCC, strlen(buf));
    FREE(buf);
}
