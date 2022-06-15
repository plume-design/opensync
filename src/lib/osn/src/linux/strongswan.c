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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "osn_ipsec.h"
#include "strongswan.h"
#include "daemon.h"
#include "ds_tree.h"
#include "memutil.h"
#include "execsh.h"
#include "const.h"
#include "util.h"
#include "log.h"
#include "ev.h"
#include "evx.h"

#define   SS_UPDOWN_SCRIPT                        CONFIG_INSTALL_PREFIX "/scripts/osn_ipsec_updown.sh"
#define   SS_IPSEC_STATUS_CMD                     CONFIG_OSN_IPSEC_BIN_PATH " status"
#define   SS_IPSEC_RELOAD_CMD                     CONFIG_OSN_IPSEC_BIN_PATH " reload"

#define   SS_POLL_AFTER                           5
#define   SS_POLL_REPEAT                          5
#define   SS_TIME_DOWN_CONN_RETRY_THRESH          30
#define   SS_DAEMON_DEBOUNCE_TIME                 0.4  /* 400ms */

static bool strongswan_apply_all(void);
static bool util_cfg_subnets_write(FILE *f, const char *cfg_key, osn_ipany_addr_t *subnets, int len, bool sourceip);
static void strongswan_poll_status_cb(struct ev_loop *loop, ev_timer *watcher, int revents);
static void strongswan_poll_status_start(void);
static void strongswan_poll_status_stop(void);
static void strongswan_monitor_status_start(void);
static void strongswan_monitor_status_stop(void);

static ds_tree_t strongswan_tunnel_list =
        DS_TREE_INIT(ds_str_cmp, struct strongswan, ss_tnode);

static daemon_t strongswan_daemon;
static ev_debounce strongswan_daemon_debounce;  /* config and daemon restart debounce */
static ev_timer poll_status_timer;              /* polling for ipsec status from time to time */
static ev_stat monitor_status_stat;             /* monitor for status changes */
static ev_debounce monitor_status_debounce;

bool strongswan_init(strongswan_t *self, const char *tunnel_name)
{
    if (tunnel_name == NULL)
        return false;

    if (ds_tree_find(&strongswan_tunnel_list, tunnel_name) != NULL)
    {
        LOG(ERR, "strongswan: %s: A tunnel with the same name already exists", tunnel_name);
        return false;
    }

    memset(self, 0, sizeof(*self));

    self->ss_tunnel_name = strdup(tunnel_name);

    self->ss_ike_lifetime = OSN_IPSEC_IKELIFETIME_DEFAULT;
    self->ss_lifetime = OSN_IPSEC_LIFETIME_DEFAULT;

    self->ss_dpd_delay = OSN_IPSEC_DPD_DELAY_DEFAULT;
    self->ss_dpd_timeout = OSN_IPSEC_DPD_TIMEOUT_DEFAULT;
    self->ss_dpd_action = OSN_IPSEC_DPD_ACTION_DEFAULT;

    ds_tree_insert(&strongswan_tunnel_list, self, self->ss_tunnel_name);

    return true;
}

bool strongswan_fini(strongswan_t *self)
{
    ds_tree_remove(&strongswan_tunnel_list, self);

    FREE(self->ss_tunnel_name);

    FREE(self->ss_left);
    FREE(self->ss_right);
    FREE(self->ss_leftid);
    FREE(self->ss_rightid);

    FREE(self->ss_psk);
    FREE(self->ss_xauth_user);
    FREE(self->ss_xauth_pass);
    FREE(self->ss_eap_identity);
    FREE(self->ss_eap_id);
    FREE(self->ss_eap_secret);

    if (!strongswan_apply_all())
    {
        LOG(ERROR, "strongswan: Error applying strongswan configuration.");
        return false;
    }

    return true;
}

bool strongswan_apply(strongswan_t *self)
{
    (void)self;

    return strongswan_apply_all();
}

/*
 * write a strongswan key=value config for key 'cfg_key' where value is a
 * list of subnets 'subnets' (a subnet may be an IP as well with or without
 * a prefix)
 */
static bool util_cfg_subnets_write(FILE *f, const char *cfg_key, osn_ipany_addr_t *subnets, int len, bool sourceip)
{
    osn_ipany_addr_t *ip;
    int i;

    for (i = 0; i < len; i++)
    {
        ip = &subnets[i];

        if (i == 0)
        {
            fprintf(f, "    %s=", cfg_key);
        }
        else
        {
            fprintf(f, ",");
        }
        if (ip->addr_type == AF_INET)
        {
            if (sourceip && ip->addr.ip4.ia_addr.s_addr == 0)
                fprintf(f, "%%config");
            else
                fprintf(f, PRI_osn_ip_addr, FMT_osn_ip_addr(ip->addr.ip4));
        }
        else if (ip->addr_type == AF_INET6)
        {
            // TODO: handle %config6 (IPv6 virtual IP request)
            fprintf(f, PRI_osn_ip6_addr, FMT_osn_ip6_addr(ip->addr.ip6));
        }
        else
        {
            fprintf(f, "<error: unknown addr family>");
            return false;
        }
        if (i+1 == len)
        {
            fprintf(f, "\n");
        }
    }

    return true;
}

static const char *util_auth_mode_to_str(enum osn_ipsec_auth_mode auth_mode)
{
    char *auth_str[] = {
            [OSN_IPSEC_AUTHMODE_NOT_SET] = NULL,
            [OSN_IPSEC_AUTHMODE_PSK]="psk", [OSN_IPSEC_AUTHMODE_PUBKEY]="pubkey",
            [OSN_IPSEC_AUTHMODE_XAUTH]="xauth", [OSN_IPSEC_AUTHMODE_EAP_MSCHAPv2]="eap-mschapv2"  };

    if (auth_mode >= OSN_IPSEC_AUTHMODE_MAX) return NULL;

    return auth_str[auth_mode];
}

static const char *util_enc_to_str(enum osn_ipsec_enc enc)
{
    char *enc_str[] = {
            [OSN_IPSEC_ENC_NOT_SET] = NULL,
            [OSN_IPSEC_ENC_3DES]="3des", [OSN_IPSEC_ENC_AES128]="aes128",
            [OSN_IPSEC_ENC_AES192]="aes192", [OSN_IPSEC_ENC_AES256]="aes256"  };

    if (enc >= OSN_IPSEC_ENC_MAX) return NULL;

    return enc_str[enc];
}

static const char *util_auth_to_str(enum osn_ipsec_auth auth)
{
    char *auth_str[] = {
            [OSN_IPSEC_AUTH_NOT_SET] = NULL,
            [OSN_IPSEC_AUTH_SHA1]="sha1", [OSN_IPSEC_AUTH_MD5]="md5",
            [OSN_IPSEC_AUTH_SHA256]="sha256" };

    if (auth >= OSN_IPSEC_AUTH_MAX) return NULL;

    return auth_str[auth];
}

static const char *util_dh_to_str(enum osn_ipsec_dh_group dh)
{
    char *dh_str[] = {
            [OSN_IPSEC_DH_NOT_SET] = NULL,
            [OSN_IPSEC_DH_1]="modp768", [OSN_IPSEC_DH_2]="modp1024",
            [OSN_IPSEC_DH_5]="modp1536", [OSN_IPSEC_DH_14]="modp2048"  };

    if (dh >= OSN_IPSEC_DH_MAX) return NULL;

    return dh_str[dh];
}

static const char *util_dpdaction_to_str(enum osn_ipsec_dpd_action dpd_action)
{
    char *dpdaction_str[] = {
            [OSN_IPSEC_DPD_NONE] = "none",
            [OSN_IPSEC_DPD_CLEAR]="clear", [OSN_IPSEC_DPD_HOLD]="hold",
            [OSN_IPSEC_DPD_RESTART]="restart" };

    if (dpd_action >= OSN_IPSEC_DPD_MAX) return NULL;

    return dpdaction_str[dpd_action];
}

static const char *util_mode_to_str(enum osn_ipsec_mode mode)
{
    char *mode_str[] = {
            [OSN_IPSEC_MODE_TUNNEL] = "tunnel",
            [OSN_IPSEC_MODE_TRANSPORT]="transport" };

    if (mode >= OSN_IPSEC_MODE_MAX) return NULL;

    return mode_str[mode];
}

static bool util_cfg_write_cipher_suite(
        FILE *f,
        const char *cfg_key,
        enum osn_ipsec_enc *enc_set, int enc_set_len,
        enum osn_ipsec_auth *auth_set, int auth_set_len,
        enum osn_ipsec_dh_group *dh_group_set, int dh_group_set_len)
{
    int i;
    int j;

    if (enc_set_len == 0 && auth_set_len == 0 && dh_group_set_len == 0)
        return true;

    fprintf(f, "    %s=", cfg_key);

    j = 0;
    for (i = 0; i < enc_set_len; i++, j++)
    {
        if (j != 0)
            fprintf(f, "-");
        fprintf(f, "%s", util_enc_to_str(enc_set[i]));
    }
    for (i = 0; i < auth_set_len; i++, j++)
    {
        if (j != 0)
            fprintf(f, "-");
        fprintf(f, "%s", util_auth_to_str(auth_set[i]));
    }
    for (i = 0; i < dh_group_set_len; i++, j++)
    {
        if (j != 0)
            fprintf(f, "-");
        fprintf(f, "%s", util_dh_to_str(dh_group_set[i]));
    }
    fprintf(f, "!");
    fprintf(f, "\n");

    return true;
}

/*
 * Write strongSwan config file according to configured tunnels.
 *
 * true is returned if at least one tunnel config successfully written
 */
bool strongswan_write_config(void)
{
    strongswan_t *tunnel;
    bool rv = false;
    FILE *f;

    remove(CONFIG_OSN_IPSEC_TMPFS_CONF_FILE_PATH);

    if (ds_tree_is_empty(&strongswan_tunnel_list))
    {
        LOGD("strongswan: tunnel list empty: nothing to write to ipsec config");
        return false;
    }

    f = fopen(CONFIG_OSN_IPSEC_TMPFS_CONF_FILE_PATH, "w");
    if (f == NULL)
    {
        LOG(ERR, "Error opening ipsec config file %s", CONFIG_OSN_IPSEC_TMPFS_CONF_FILE_PATH);
        return false;
    }

    // write config header:
    fprintf(f, "config setup\n");
    fprintf(f, "    charondebug=\"cfg 2, dmn 2, ike 2, net 2\"\n");
    fprintf(f, "\n");

    ds_tree_foreach(&strongswan_tunnel_list, tunnel)
    {
        if (!tunnel->ss_enable)
        {
            LOGD("strongswan: config: tunnel %s not enabled: skipping", tunnel->ss_tunnel_name);
            continue;
        }

        fprintf(f, "conn \"%s\"\n", tunnel->ss_tunnel_name);

        if (tunnel->ss_role == OSN_IPSEC_ROLE_INITIATOR)
            fprintf(f, "    auto=start\n");
        else if (tunnel->ss_role == OSN_IPSEC_ROLE_RESPONDER)
            fprintf(f, "    auto=add\n");

        if (tunnel->ss_left != NULL)
        {
            fprintf(f, "    left=%s\n", tunnel->ss_left);
        }
        else
        {
            fprintf(f, "    left=%%defaultroute\n");
        }

        if (tunnel->ss_leftid != NULL)
            fprintf(f, "    leftid=%s\n", tunnel->ss_leftid);
        if (tunnel->ss_right != NULL)
            fprintf(f, "    right=%s\n", tunnel->ss_right);
        if (tunnel->ss_rightid != NULL)
            fprintf(f, "    rightid=%s\n", tunnel->ss_rightid);

        util_cfg_subnets_write(f, "leftsubnet", tunnel->ss_leftsubnet, tunnel->ss_leftsubnet_len, false);
        util_cfg_subnets_write(f, "rightsubnet", tunnel->ss_rightsubnet, tunnel->ss_rightsubnet_len, false);
        util_cfg_subnets_write(f, "leftsourceip", tunnel->ss_leftsourceip, tunnel->ss_leftsourceip_len, true);
        util_cfg_subnets_write(f, "rightsourceip", tunnel->ss_rightsourceip, tunnel->ss_rightsourceip_len, true);

        if (tunnel->ss_leftauth != OSN_IPSEC_AUTHMODE_NOT_SET)
            fprintf(f, "    leftauth=%s\n", util_auth_mode_to_str(tunnel->ss_leftauth));
        if (tunnel->ss_rightauth != OSN_IPSEC_AUTHMODE_NOT_SET)
            fprintf(f, "    rightauth=%s\n", util_auth_mode_to_str(tunnel->ss_rightauth));
        if (tunnel->ss_leftauth2 != OSN_IPSEC_AUTHMODE_NOT_SET)
            fprintf(f, "    leftauth2=%s\n", util_auth_mode_to_str(tunnel->ss_leftauth2));
        if (tunnel->ss_rightauth2 != OSN_IPSEC_AUTHMODE_NOT_SET)
            fprintf(f, "    rightauth2=%s\n", util_auth_mode_to_str(tunnel->ss_rightauth2));

        if (tunnel->ss_eap_identity != NULL)
            fprintf(f, "    eap_identity=%s\n", tunnel->ss_eap_identity);

        if (tunnel->ss_neg_mode == OSN_IPSEC_NEG_AGGRESSIVE)
            fprintf(f, "    aggressive=yes\n");
        else
            fprintf(f, "    aggressive=no\n");

        if (tunnel->ss_key_exchange == OSN_IPSEC_KEY_EXCHANGE_IKEv1)
            fprintf(f, "    keyexchange=ikev1\n");
        else if (tunnel->ss_key_exchange == OSN_IPSEC_KEY_EXCHANGE_IKEv2)
            fprintf(f, "    keyexchange=ikev2\n");  // default: ike (ikev1 | ikev2)

        if (tunnel->ss_ike_lifetime != 0)
            fprintf(f, "    ikelifetime=%d\n", tunnel->ss_ike_lifetime);
        if (tunnel->ss_lifetime != 0)
            fprintf(f, "    lifetime=%d\n", tunnel->ss_lifetime);

        fprintf(f, "    type=%s\n", util_mode_to_str(tunnel->ss_mode));

        util_cfg_write_cipher_suite(f, "ike",
                tunnel->ss_ike_enc_set, tunnel->ss_ike_enc_set_len,
                tunnel->ss_ike_auth_set, tunnel->ss_ike_auth_set_len,
                tunnel->ss_ike_dh_set, tunnel->ss_ike_dh_set_len);

        util_cfg_write_cipher_suite(f, "esp",
                tunnel->ss_esp_enc_set, tunnel->ss_esp_enc_set_len,
                tunnel->ss_esp_auth_set, tunnel->ss_esp_auth_set_len,
                tunnel->ss_esp_dh_set, tunnel->ss_esp_dh_set_len);

        fprintf(f, "    dpddelay=%d\n", tunnel->ss_dpd_delay);
        fprintf(f, "    dpdtimeout=%d\n", tunnel->ss_dpd_timeout);
        fprintf(f, "    dpdaction=%s\n", util_dpdaction_to_str(tunnel->ss_dpd_action));

        if (tunnel->ss_mark != 0)
            fprintf(f, "    mark=%d\n", tunnel->ss_mark);

        /* Use the updown plugin to implement event-driven tunnel status
         * change monitoring and as a more reliable method to determine
         * the virtual IPs assigned (through PLUTO_ vars) then parsing
         * 'ipsec status all' or swanctl output */
        fprintf(f, "    leftupdown=\"%s\"\n", SS_UPDOWN_SCRIPT);

        fprintf(f, "\n");

        LOG(DEBUG, "strongswan: %s: Wrote config to: %s",
                tunnel->ss_tunnel_name, CONFIG_OSN_IPSEC_TMPFS_CONF_FILE_PATH);
        rv = true;
    }

    fclose(f);
    return rv;
}

/*
 * Write strongSwan secrets file according to configured tunnels.
 *
 * true is returned if secrets for at least one tunnel successfully written
 */
bool strongswan_write_secrets(void)
{
    strongswan_t *tunnel;
    bool rv = false;
    FILE *f;

    remove(CONFIG_OSN_IPSEC_TMPFS_SECRETS_FILE_PATH);

    if (ds_tree_is_empty(&strongswan_tunnel_list))
    {
        LOGD("strongswan: tunnel list empty: nothing to write to ipsec secrets config");
        return false;
    }

    f = fopen(CONFIG_OSN_IPSEC_TMPFS_SECRETS_FILE_PATH, "w");
    if (f == NULL)
    {
        LOG(ERR, "Error opening ipsec secrets file %s", CONFIG_OSN_IPSEC_TMPFS_SECRETS_FILE_PATH);
        return false;
    }

    fprintf(f, "# strongSwan IPsec secrets file\n");
    fprintf(f, "\n");

    ds_tree_foreach(&strongswan_tunnel_list, tunnel)
    {
        if (!tunnel->ss_enable)
        {
            LOGD("strongswan: secrets: tunnel %s not enabled: skipping", tunnel->ss_tunnel_name);
            continue;
        }

        fprintf(f, "# %s\n", tunnel->ss_tunnel_name);
        if (tunnel->ss_psk != NULL)
        {
            if (tunnel->ss_leftid != NULL)
                fprintf(f, "%s ", tunnel->ss_leftid);
            if (tunnel->ss_rightid != NULL)
                fprintf(f, "%s ", tunnel->ss_rightid);

            fprintf(f, ": PSK \"%s\"\n", tunnel->ss_psk);
        }
        if (tunnel->ss_xauth_user != NULL && tunnel->ss_xauth_pass != NULL)
        {
            fprintf(f, "%s : XAUTH \"%s\"\n", tunnel->ss_xauth_user, tunnel->ss_xauth_pass);
        }
        if (tunnel->ss_eap_id != NULL && tunnel->ss_eap_secret != NULL)
        {
            fprintf(f, "%s : EAP \"%s\"\n", tunnel->ss_eap_id, tunnel->ss_eap_secret);
        }

        LOG(DEBUG, "strongswan: %s: Wrote secrets to: %s", tunnel->ss_tunnel_name, CONFIG_OSN_IPSEC_TMPFS_SECRETS_FILE_PATH);
        rv = true;
    }

    fclose(f);
    return rv;
}

static void strongswan_init_tunnels(void)
{
    strongswan_t *tunnel;

    ds_tree_foreach(&strongswan_tunnel_list, tunnel)
    {
        if (!tunnel->ss_enable)
        {
            continue;
        }
        tunnel->ss_time_last_up = ev_now(EV_DEFAULT);
    }
}

static bool util_unlink_file(const char *file_path)
{
    if (unlink(file_path) == -1 && errno != ENOENT)
    {
        LOG(ERR, "strongswan: Error removing file: %s: %s", file_path, strerror(errno));
        return false;
    }
    return true;
}

static bool util_symlink(const char *target, const char *linkpath)
{
    if (symlink(target, linkpath) == -1)
    {
        LOG(ERR, "strongswan: Error creating symlink: %s <-- %s: %s",
                target, linkpath, strerror(errno));
        return false;
    }
    return true;
}

/* Create all parent directories of the given absolute path
 *
 * The last component is considered a directory if it ends with a slash /,
 * otherwise it is considered to be a file.
 *
 * e.g.:
 * /var/etc/strongswan.d/charon.conf would create the following directories:
 *  - /var/
 *  - /var/etc
 *  - /var/etc/strongswan.d/
 *
 * /var/etc/ would create:
 *  - /var/
 *  - /etc/
 */
static bool util_create_parent_dirs(const char *file_path)
{
    char *buf;
    const char *first;
    char *c;
    bool rv = true;

    buf = strdup(file_path);

    if ((first = strchr(buf, '/')) == NULL)
    {
        return false;
    }
    c = strrchr(buf, '/');
    if (c == NULL)
    {
        return false;
    }
    if (c != first)
    {
        *c = '\0';
        rv = util_create_parent_dirs(buf);
        if (!rv) goto out;
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST)
    {
        LOG(ERR, "strongswan: Error creating dir: %s: %s\n", buf, strerror(errno));
        rv = false;
    }

out:
    FREE(buf);
    return rv;
}

static bool strongswan_init_charon_conf(void)
{
    const char *charon_config =
        "# Options for the charon IKE daemon.\n"
        "charon {\n"
        "    install_routes = no\n"
        "    install_virtual_ip = no\n"
        "}\n";
    FILE *f;

    f = fopen(CONFIG_OSN_IPSEC_TMPFS_CHARON_CONF_FILE_PATH, "w");
    if (f == NULL)
    {
        LOG(ERR, "strongswan: Error opening file %s: %s", CONFIG_OSN_IPSEC_TMPFS_CHARON_CONF_FILE_PATH, strerror(errno));
        return false;
    }

    if (fwrite(charon_config, strlen(charon_config), 1, f) != 1)
    {
        LOG(ERR, "strongswan: Error writing to file %s", CONFIG_OSN_IPSEC_TMPFS_CHARON_CONF_FILE_PATH);
    }
    fclose(f);
    return true;
}

static bool strongswan_config_files_init(void)
{
    bool rv = true;

    /* First, remove any previous or original config files: */
    rv &= util_unlink_file(CONFIG_OSN_IPSEC_CONF_FILE_PATH);
    rv &= util_unlink_file(CONFIG_OSN_IPSEC_SECRETS_FILE_PATH);
    rv &= util_unlink_file(CONFIG_OSN_IPSEC_CHARON_CONF_FILE_PATH);
    if (!rv)
    {
        return false;
    }

    /* Then, create symlinks in place of the original config files pointing to
     * tmpfs locations:
     */
    rv &= util_symlink(CONFIG_OSN_IPSEC_TMPFS_CONF_FILE_PATH, CONFIG_OSN_IPSEC_CONF_FILE_PATH);
    rv &= util_symlink(CONFIG_OSN_IPSEC_TMPFS_SECRETS_FILE_PATH, CONFIG_OSN_IPSEC_SECRETS_FILE_PATH);
    rv &= util_symlink(CONFIG_OSN_IPSEC_TMPFS_CHARON_CONF_FILE_PATH, CONFIG_OSN_IPSEC_CHARON_CONF_FILE_PATH);
    if (!rv)
    {
        return false;
    }

    /* Finally, create all parent directories for the tmpfs locations of config
     * files, if needed:
     */
    rv &= util_create_parent_dirs(CONFIG_OSN_IPSEC_TMPFS_CONF_FILE_PATH);
    rv &= util_create_parent_dirs(CONFIG_OSN_IPSEC_TMPFS_SECRETS_FILE_PATH);
    rv &= util_create_parent_dirs(CONFIG_OSN_IPSEC_TMPFS_CHARON_CONF_FILE_PATH);

    /* Create OSN ipsec status dir (where updown script will create status files): */
    rv &= util_create_parent_dirs(CONFIG_OSN_IPSEC_TMPFS_STATUS_DIR"/");

    /* Finally, create charon.conf file with the configuration we need: */
    rv &= strongswan_init_charon_conf();

    return rv;
}

static bool strongswan_daemon_init(void)
{
    if (!strongswan_config_files_init())
    {
        LOG(ERR, "strongswan: Error creating and initializing config files infrastructure");
        return false;
    }

    if (!daemon_init(&strongswan_daemon, CONFIG_OSN_IPSEC_STARTER_PATH, 0))
    {
        LOG(ERR, "strongswan: Unable to initialize global daemon object.");
        return false;
    }

    /* Set the PID file location -- necessary to kill stale instances */
    if (!daemon_pidfile_set(&strongswan_daemon, CONFIG_OSN_IPSEC_DAEMON_PID_PATH, false))
    {
        LOG(WARN, "strongswan: Error setting the PID file path.");
    }

    if (!daemon_restart_set(&strongswan_daemon, true, 3.0, 5))
    {
        LOG(WARN, "strongswan: Error enabling daemon auto-restart on global instance.");
    }

    daemon_arg_add(&strongswan_daemon, "--daemon", "charon");
    daemon_arg_add(&strongswan_daemon, "--nofork");

    LOG(DEBUG, "strongswan: Daemon inited");
    return true;
}

void strongswan_apply_all_ev(struct ev_loop *loop, struct ev_debounce *ev, int revent)
{
    bool rv = true;

    /*
     * Always stop and restart the daemon when reconfiguring.
     *
     * Seems to be a strongswan limitation when using the ipsec.conf interface:
     * if configuring an additional tunnel config or changing a tunnel config,
     * one would expect that "ipsec reload" would do the job, but it turns out
     * it does not.
     *
     * With daemon restart the existing established tunnels might jump down for
     * short instance and then quickly up again. However, this turns out to be
     * really quick and does not break nor affect much the established streams,
     * for instance. So this approach seems to work and be okay.
     */
    LOG(NOTICE, "strongswan: Stopping daemon...");
    daemon_stop(&strongswan_daemon);

    rv &= strongswan_write_config();
    rv &= strongswan_write_secrets();
    if (rv)
    {
        /* At least one strongswan tunnel/conn config/secrets written */
        strongswan_init_tunnels();

        if (!daemon_start(&strongswan_daemon))
        {
            LOG(ERROR, "strongswan: Error starting strongswan daemon");
            return;
        }
        LOG(NOTICE, "strongswan: Daemon started");

        /* At least one tunnel/conn configured && IPSec daemon started: */
        strongswan_monitor_status_start();
        strongswan_poll_status_start();
    }
}

static bool strongswan_apply_all(void)
{
    static bool inited = false;

    if (!inited)
    {
        if (!strongswan_daemon_init())
        {
            LOG(ERROR, "strongswan: Error initializing strongswan daemon");
            return false;
        }

        ev_debounce_init(
                &strongswan_daemon_debounce,
                strongswan_apply_all_ev,
                SS_DAEMON_DEBOUNCE_TIME);

        inited = true;
    }

    /* Do the actual reconfiguration and daemon restart
     * with a debounce timer: */
    ev_debounce_start(EV_DEFAULT, &strongswan_daemon_debounce);

    return true;
}

bool strongswan_leftright_set(strongswan_t *self, const char *left, const char *right)
{
    LOG(TRACE, "%s", __func__);

    FREE(self->ss_left);
    FREE(self->ss_right);

    self->ss_left = left && *left ? strdup(left) : NULL;
    self->ss_right = right && *right ? strdup(right) : NULL;
    return true;
}

bool strongswan_leftrightid_set(strongswan_t *self, const char *leftid, const char *rightid)
{
    LOG(TRACE, "%s", __func__);

    FREE(self->ss_leftid);
    FREE(self->ss_rightid);

    self->ss_leftid = leftid && *leftid ? strdup(leftid) : NULL;
    self->ss_rightid = rightid && *rightid ? strdup(rightid) : NULL;
    return true;
}

bool strongswan_leftsubnet_set(strongswan_t *self, osn_ipany_addr_t *subnets, int subnets_len)
{
    int i;

    LOG(TRACE, "%s", __func__);

    for (i = 0; i < OSN_SUBNETS_MAX && i < subnets_len; i++)
    {
        self->ss_leftsubnet[i] = subnets[i];
    }
    self->ss_leftsubnet_len = subnets_len;
    return true;
}

bool strongswan_rightsubnet_set(strongswan_t *self, osn_ipany_addr_t *subnets, int subnets_len)
{
    int i;

    LOG(TRACE, "%s", __func__);

    for (i = 0; i < OSN_SUBNETS_MAX && i < subnets_len; i++)
    {
        self->ss_rightsubnet[i] = subnets[i];
    }
    self->ss_rightsubnet_len = subnets_len;
    return true;
}

bool strongswan_leftsourceip_set(strongswan_t *self, osn_ipany_addr_t *sourceip, int sourceip_len)
{
    int i;

    LOG(TRACE, "%s", __func__);

    for (i = 0; i < OSN_SUBNETS_MAX && i < sourceip_len; i++)
    {
        self->ss_leftsourceip[i] = sourceip[i];
    }
    self->ss_leftsourceip_len = sourceip_len;
    return true;
}

bool strongswan_rightsourceip_set(strongswan_t *self, osn_ipany_addr_t *sourceip, int sourceip_len)
{
    int i;

    LOG(TRACE, "%s", __func__);

    for (i = 0; i < OSN_SUBNETS_MAX && i < sourceip_len; i++)
    {
        self->ss_rightsourceip[i] = sourceip[i];
    }
    self->ss_rightsourceip_len = sourceip_len;
    return true;
}

bool strongswan_leftrightauth_set(
        strongswan_t *self,
        enum osn_ipsec_auth_mode leftauth,
        enum osn_ipsec_auth_mode rightauth)
{
    LOG(TRACE, "%s", __func__);

    self->ss_leftauth = leftauth;
    self->ss_rightauth = rightauth;
    return true;
}

bool strongswan_leftrightauth2_set(
        strongswan_t *self,
        enum osn_ipsec_auth_mode leftauth2,
        enum osn_ipsec_auth_mode rightauth2)
{
    LOG(TRACE, "%s", __func__);

    self->ss_leftauth2 = leftauth2;
    self->ss_rightauth2 = rightauth2;
    return true;
}

bool strongswan_psk_set(strongswan_t *self, const char *psk)
{
    LOG(TRACE, "%s", __func__);

    FREE(self->ss_psk);

    self->ss_psk = psk && *psk ? strdup(psk) : NULL;
    return true;
}

bool strongswan_xauth_credentials_set(
        strongswan_t *self,
        const char *xauth_user,
        const char *xauth_pass)
{
    LOG(TRACE, "%s", __func__);

    FREE(self->ss_xauth_user);
    FREE(self->ss_xauth_pass);

    self->ss_xauth_user = xauth_user && *xauth_user ? strdup(xauth_user) : NULL;
    self->ss_xauth_pass = xauth_pass && *xauth_pass ? strdup(xauth_pass) : NULL;
    return true;
}

bool strongswan_eap_identity_set(strongswan_t *self, const char *eap_identity)
{
    LOG(TRACE, "%s", __func__);

    FREE(self->ss_eap_identity);

    self->ss_eap_identity = eap_identity && *eap_identity ? strdup(eap_identity) : NULL;
    return true;
}

bool strongswan_eap_credentials_set(strongswan_t *self, const char *eap_id, const char *eap_secret)
{
    LOG(TRACE, "%s", __func__);

    FREE(self->ss_eap_id);
    FREE(self->ss_eap_secret);

    self->ss_eap_id = eap_id && *eap_id ? strdup(eap_id) : NULL;
    self->ss_eap_secret = eap_secret && *eap_secret ? strdup(eap_secret) : NULL;
    return true;
}

bool strongswan_neg_mode_set(strongswan_t *self, enum osn_ipsec_neg_mode neg_mode)
{
    LOG(TRACE, "%s", __func__);

    self->ss_neg_mode = neg_mode;
    return true;
}

bool strongswan_key_exchange_set(strongswan_t *self, enum osn_ipsec_key_exchange key_exchange)
{
    LOG(TRACE, "%s", __func__);

    self->ss_key_exchange = key_exchange;
    return true;
}

bool strongswan_ike_lifetime_set(strongswan_t *self, int ike_lifetime)
{
    LOG(TRACE, "%s", __func__);

    self->ss_ike_lifetime = ike_lifetime;
    return true;
}

bool strongswan_lifetime_set(strongswan_t *self, int lifetime)
{
    LOG(TRACE, "%s", __func__);

    self->ss_lifetime = lifetime;
    return true;
}

bool strongswan_role_set(strongswan_t *self, enum osn_ipsec_role role)
{
    LOG(TRACE, "%s", __func__);

    self->ss_role = role;
    return true;
}

bool strongswan_mode_set(strongswan_t *self, enum osn_ipsec_mode mode)
{
    LOG(TRACE, "%s", __func__);

    self->ss_mode = mode;
    return true;
}

bool strongswan_ike_cipher_suite_set(
        strongswan_t *self,
        enum osn_ipsec_enc *enc_set, int enc_set_len,
        enum osn_ipsec_auth *auth_set, int auth_set_len,
        enum osn_ipsec_dh_group *dh_set, int dh_set_len)
{
    LOG(TRACE, "%s", __func__);

    memcpy(self->ss_ike_enc_set, enc_set, MIN(enc_set_len, OSN_CIPHER_SUITE_MAX) * sizeof(*enc_set));
    self->ss_ike_enc_set_len = enc_set_len;

    memcpy(self->ss_ike_auth_set, auth_set, MIN(auth_set_len, OSN_CIPHER_SUITE_MAX) * sizeof(*auth_set));
    self->ss_ike_auth_set_len = auth_set_len;

    memcpy(self->ss_ike_dh_set, dh_set, MIN(dh_set_len, OSN_CIPHER_SUITE_MAX) * sizeof(*dh_set));
    self->ss_ike_dh_set_len = dh_set_len;

    return true;
}

bool strongswan_esp_cipher_suite_set(
        strongswan_t *self,
        enum osn_ipsec_enc *enc_set, int enc_set_len,
        enum osn_ipsec_auth *auth_set, int auth_set_len,
        enum osn_ipsec_dh_group *dh_set, int dh_set_len)
{
    LOG(TRACE, "%s", __func__);

    memcpy(self->ss_esp_enc_set, enc_set, MIN(enc_set_len, OSN_CIPHER_SUITE_MAX) * sizeof(*enc_set));
    self->ss_esp_enc_set_len = enc_set_len;

    memcpy(self->ss_esp_auth_set, auth_set, MIN(auth_set_len, OSN_CIPHER_SUITE_MAX) * sizeof(*auth_set));
    self->ss_esp_auth_set_len = auth_set_len;

    memcpy(self->ss_esp_dh_set, dh_set, MIN(dh_set_len, OSN_CIPHER_SUITE_MAX) * sizeof(*dh_set));
    self->ss_esp_dh_set_len = dh_set_len;

    return true;
}

bool strongswan_dpd_set(
        strongswan_t *self,
        int dpd_delay,
        int dpd_timeout,
        enum osn_ipsec_dpd_action dpd_action)
{
    LOG(TRACE, "%s", __func__);

    self->ss_dpd_delay = dpd_delay;
    self->ss_dpd_timeout = dpd_timeout;
    self->ss_dpd_action = dpd_action;
    return true;
}

/* reserved value: mark==0: unset */
bool strongswan_mark_set(strongswan_t *self, int mark)
{
    LOG(TRACE, "%s", __func__);

    self->ss_mark = mark;
    return true;
}

bool strongswan_enable_set(strongswan_t *self, bool enable)
{
    LOG(TRACE, "%s", __func__);

    self->ss_enable = enable;
    return true;
}

bool strongswan_notify_status_set(strongswan_t *self, osn_ipsec_status_fn_t *status_fn_cb)
{
    self->ss_status_cb = status_fn_cb;
    return true;
}

/* compare two osn_ipsec_statuses: 0 - equal, nonequal otherwise */
static int util_ipsec_status_cmp(struct osn_ipsec_status *a, struct osn_ipsec_status *b)
{
    int i;

    if (strcmp(a->is_tunnel_name, b->is_tunnel_name) != 0)
    {
        return -1;
    }
    if (a->is_conn_state != b->is_conn_state)
    {
        return -1;
    }

    if (a->is_local_ts_len != b->is_local_ts_len)
    {
        return -1;
    }
    if (a->is_remote_ts_len != b->is_remote_ts_len)
    {
        return -1;
    }

    for (i = 0; i < a->is_local_ts_len; i++)
    {
        if (osn_ipany_addr_cmp(&a->is_local_ts[i], &b->is_local_ts[i]) != 0)
        {
            return -1;
        }
    }
    for (i = 0; i < a->is_remote_ts_len; i++)
    {
        if (osn_ipany_addr_cmp(&a->is_remote_ts[i], &b->is_remote_ts[i]) != 0)
        {
            return -1;
        }
    }

    return 0;
}

/* Parse white-space separated list of subnets (or IPs) in string 'subnets_str'
 * to an array of osn_ip_any_addr_t.
 *
 * Return number of subnets/IPs parsed. */
static int util_parse_subnets(osn_ipany_addr_t *subnets_array, int subnets_array_len, const char *subnets_str)
{
    char *ts = strdup(subnets_str);
    char *tok;
    int i;

    i = 0;
    tok = strtok(ts, " \n");
    while (tok != NULL && i < subnets_array_len)
    {
        if (!osn_ipany_addr_from_str(&subnets_array[i], tok))
        {
            LOG(ERR, "Error parsing an IP/subnet: '%s'", tok);
            break;
        }
        i++;
        tok = strtok(NULL, " ");
    }
    FREE(ts);
    return i;
}

/* For tunnel 'tunnel_name' obtain virtual IPs assigned, if any. Currently, only
 * IPv4 virtual IPs are supported.
 */
static bool strongswan_get_status_updown(struct osn_ipsec_status *tunnel_status, const char *tunnel_name)
{
    const char *VIRT_IP4_KEYWORD = "VIRT_IP4";
    char status_file_path[C_MAXPATH_LEN];
    char buffer[512];
    char *line;
    FILE *f;

    snprintf(status_file_path, sizeof(status_file_path), "%s/%s", CONFIG_OSN_IPSEC_TMPFS_STATUS_DIR, tunnel_name);

    if (access(status_file_path, F_OK) != 0)
    {
        /* When the tunnel is down our updown script will delete this status file.
         *
         * In this function we're not interested in the tunnel status but
         * in the extended tunnel info (such as virtual IPs assigned) for the
         * tunnels that are up, so simply skip if the tunnel is down. */
        return true;
    }

    f = fopen(status_file_path, "r");
    if (f == NULL)
    {
        if (errno == ENOENT)
        {
            return true;
        }
        LOG(ERR, "strongswan: %s: Error opening file %s", tunnel_name, strerror(errno));
        return false;
    }

    while ((line = fgets(buffer, sizeof(buffer), f)) != NULL)
    {
        LOG(TRACE, "strongswan: %s: Parsing osn updown status line: '%s'", tunnel_name, line);

        /* IPv4 virtual IPs assigned:
         *   lines such as: 'VIRT_IP4 10.10.10.1 10.10.10.2' */
        if (strncmp(line, VIRT_IP4_KEYWORD, strlen(VIRT_IP4_KEYWORD)) == 0)
        {
            line += strlen(VIRT_IP4_KEYWORD);
            if (*line == '\0')
            {
                break;
            }
            tunnel_status->is_local_virt_ip_len =
                    util_parse_subnets(tunnel_status->is_local_virt_ip, OSN_SUBNETS_MAX, line);
        }
    }
    fclose(f);

    return true;
}

/* For tunnel 'tunnel_name' obtain its current connection status and the
 * established traffic selectors status from the "ipsec status" output. */
static bool strongswan_get_status_ipsec(struct osn_ipsec_status *tunnel_status, const char *tunnel_name)
{
    char cmd[C_MAXPATH_LEN];
    char buffer[512];
    char *tok;
    char *line;
    int n;
    FILE *f = NULL;
    bool phase1_up, phase2_up, phase1_connecting;
    bool status_obtained;
    bool rv = false;

    LOG(TRACE, "strongswan: %s called", __func__);

    /* Run and parse "ipsec status <tunnel_name>" command: */
    snprintf(cmd, sizeof(cmd), "%s \"%s\" 2>/dev/null", SS_IPSEC_STATUS_CMD, tunnel_name);
    LOG(TRACE, "strongswan: Running cmd: %s", cmd);
    f = popen(cmd, "r");
    if (f == NULL)
    {
        LOG(ERR, "strongswan: Error running cmd %s: %s", SS_IPSEC_STATUS_CMD, strerror(errno));
        goto out;
    }

    n = 0;
    phase1_up = false;
    phase1_connecting = false;
    phase2_up = false;
    memset(tunnel_status, 0, sizeof(*tunnel_status));  // the goal is to fill this var with status info
    STRSCPY(tunnel_status->is_tunnel_name, tunnel_name);

    /* Initially, assume status obtained -- because if no status info in
     * "ipsec status <name>" for this tunnel, then that means we know the
     * tunnel is down */
    status_obtained = true;

    while ((line = fgets(buffer, sizeof(buffer), f)) != NULL)  // "ipsec status <name>" lines
    {
        LOG(TRACE, "strongswan: %s: parsing ipsec status line: '%s'", tunnel_name, line);

        tok = strtok(buffer, " ");
        if (tok == NULL)
        {
            break;
        }
        if (strncmp(tok, tunnel_name, strlen(tunnel_name)) != 0)
        {
            /*
             * We're skipping all the leading lines that don't begin with
             * a tunnel name (plus maybe leading whitespace).
             */
            continue;
        }
        n++;

        /* At least one line beginning with tunnel name present, tunnel may be
         * up or down or somewhere inbetween. status will be obtained only
         * if we finish parsing everything successfully */
        status_obtained = false;

        if (n == 1)  // line 1: IKE phase 1 status
        {
            if ((tok = strtok(NULL, ", ")) == NULL)
            {
                break;
            }
            if (strcmp(tok, "ESTABLISHED") == 0)
            {
                phase1_up = true;  // phase 1 (IKE) up

                status_obtained = true;
            }
            else if (strcmp(tok, "CONNECTING") == 0)
            {
                phase1_connecting = true;  // phase 1 stuck in connecting

                status_obtained = true;
                break;
            }
        }
        else if (n == 2)  // line 2: IKE phase 2 status
        {
            if ((tok = strtok(NULL, ", ")) == NULL)
            {
                break;
            }
            if (strcmp(tok, "INSTALLED") == 0)
            {
                phase2_up = true;  // phase 2 up
            }
        }
        else if (n == 3)  // line 3: phase 2 traffic selectors status
        {
            const char *ts_left;
            const char *ts_right;

            if ((tok = strtok(NULL, "\n")) == NULL)
            {
                break;
            }
            ts_left = tok;
            tok = strstr(tok, " === ");
            if (tok == NULL)
            {
                break;
            }
            *tok = '\0';
            ts_right = tok + 5;
            if (ts_right == NULL)
            {
                break;
            }

            /* parse traffic selectors strings to our internal structs: */
            tunnel_status->is_local_ts_len = util_parse_subnets(tunnel_status->is_local_ts, OSN_SUBNETS_MAX, ts_left);
            tunnel_status->is_remote_ts_len = util_parse_subnets(tunnel_status->is_remote_ts, OSN_SUBNETS_MAX, ts_right);

            status_obtained = true;
            break;
        }
    } /* while (we're reading lines from the "ipsec status <tunnel_name>" output */
    fclose(f);
    f = NULL;

    if (!status_obtained)
    {
        goto out;
    }

    LOG(DEBUG, "strongswan: %s: phase1_connecting=%d, phase1_up=%d, phase2_up=%d'",
               tunnel_status->is_tunnel_name, phase1_connecting, phase1_up, phase2_up);

    /* Determine the tunnel overall connection status based on the
     * obtained phase1 and phase2 statuses: */
    if (phase1_connecting || (phase1_up && !phase2_up))
    {
        tunnel_status->is_conn_state = OSN_VPN_CONN_STATE_CONNECTING;
    }
    else if (phase1_up && phase2_up)
    {
        tunnel_status->is_conn_state = OSN_VPN_CONN_STATE_UP;
    }
    else
    {
        tunnel_status->is_conn_state = OSN_VPN_CONN_STATE_DOWN;
    }
    rv = true;
out:
    if (f != NULL)
    {
        fclose(f);
    }
    return rv;
}

static void strongswan_check_status(void)
{
    struct osn_ipsec_status tunnel_status;
    strongswan_t *tunnel;

    LOG(TRACE, "strongswan: %s called", __func__);

    if (ds_tree_is_empty(&strongswan_tunnel_list))
    {
        /* No tunnel configured, stop checking the status */
        strongswan_monitor_status_stop();
        strongswan_poll_status_stop();
        return;
    }

    /* For each configured tunnel: */
    ds_tree_foreach(&strongswan_tunnel_list, tunnel)
    {
        memset(&tunnel_status, 0, sizeof(tunnel_status));

        /* Get:
         * - connection status,
         * - established traffic selectors:
         * (by parsing "ipsec status <tunnel_name>")
         */
        if (!strongswan_get_status_ipsec(&tunnel_status, tunnel->ss_tunnel_name))
        {
            LOG(ERR, "strongswan: %s: Error obtaining tunnel status from ipsec status", tunnel->ss_tunnel_name);
            continue;
        }

        /* Also get:
         * - info about any virtual IPs assigned:
         * (by parsing info our updown script hook extracts from PLUTO_ vars)
         */
        if (!strongswan_get_status_updown(&tunnel_status, tunnel->ss_tunnel_name))
        {
            LOG(ERR, "strongswan: %s: Error obtaining tunnel status from the updown script ", tunnel->ss_tunnel_name);
            continue;
        }

        /* Finally, determine if tunnel/connection status changed.
         * If it changed, report the new status.
         */
        if (util_ipsec_status_cmp(&tunnel->ss_status, &tunnel_status) != 0)
        {
            tunnel->ss_status = tunnel_status;
            LOG(NOTICE, "strongswan: tunnel %s connection status changed. status=%s",
                    tunnel->ss_tunnel_name,
                    osn_vpn_conn_state_to_str(tunnel->ss_status.is_conn_state));

            /* Notify via registered callback: */
            if (tunnel->ss_status_cb != NULL)
            {
                tunnel->ss_status_cb(&tunnel->ss_status);
            }
        }

        /* Handle any tunnels that have been down: try to re-establish the connection: */
        if (tunnel->ss_enable && tunnel->ss_status.is_conn_state == OSN_VPN_CONN_STATE_DOWN)
        {
            ev_tstamp time_down = ev_now(EV_DEFAULT) - tunnel->ss_time_last_up;

            if (time_down >= SS_TIME_DOWN_CONN_RETRY_THRESH)
            {
                LOG(INFO, "strongswan: %s: tunnel enabled, but down for %.1f s: retry",
                        tunnel->ss_tunnel_name, time_down);

                /* Tunnel enabled, but down for more then the threshold.
                 * If a tunnel is down, strongSwan (even if initiator) will not
                 * automatically re-attempt to establish the connection. That's
                 * why we issue "ipsec reload" here which does the job. */
                execsh_log(LOG_SEVERITY_DEBUG, SS_IPSEC_RELOAD_CMD);

                tunnel->ss_time_last_up = ev_now(EV_DEFAULT);  // assume up after retry
            }
            LOG(DEBUG, "strongswan: %s: tunnel enabled, but DOWN: time down=%.1f",
                    tunnel->ss_tunnel_name, time_down);
        }
        else
        {
            tunnel->ss_time_last_up = ev_now(EV_DEFAULT);
        }
    } /* all the configured tunnels iterated */
}

static void strongswan_poll_status_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    strongswan_check_status();
}

static void strongswan_monitor_status_cb_debounced(struct ev_loop *loop, struct ev_debounce *ev, int revent)
{
    strongswan_check_status();
}

static void strongswan_monitor_status_cb_timer(struct ev_loop *loop, ev_stat *w, int revents)
{
    static bool inited = false;

    if (!inited)
    {
        ev_debounce_init2(
                &monitor_status_debounce,
                strongswan_monitor_status_cb_debounced,
                1.04, 5.0);

        inited = true;
    }

    ev_debounce_start(EV_DEFAULT, &monitor_status_debounce);
}

/* Monitor for status in an event-driven way (implemented via strongswan
 * updown script writing files to a dedicated directory in tmpfs and an
 * ev_stat watcher watching that directory.
 *
 * On linux libev will probably use the inotify backend, so it should be
 * efficient.
 */
static void strongswan_monitor_status_start(void)
{
    static bool inited = false;

    if (!inited)
    {
        /* Monitor a dedicated directory for status changes. strongswan
         * updown script will create/delete files there. */
        ev_stat_init(&monitor_status_stat, strongswan_monitor_status_cb_timer, CONFIG_OSN_IPSEC_TMPFS_STATUS_DIR, 0.);
        inited = true;
    }

    if (!ev_is_active(&monitor_status_stat))
    {
        ev_stat_start(EV_DEFAULT, &monitor_status_stat);
    }
}

static void strongswan_monitor_status_stop(void)
{
    ev_stat_stop(EV_DEFAULT, &monitor_status_stat);
}

/* In addition to event-driven status monitoring poll for status changes from
 * time to time as well. This is needed because strongSwan invokes the updown
 * script only when an IKEv2 CHILD SA (or an IKEv1 Quick Mode) gets established
 * or deleted. However if we want to catch the stuck in "connecting" state too
 * we need to poll as well. Additionally, in the status poll handler we
 * check for tunnels that are enabled but have been down for longer then the
 * threshold -- those tunnels need to be restarted explicitly to potentially
 * bring them up again if remote becomes available.
 */
static void strongswan_poll_status_start(void)
{
    static bool inited = false;

    if (!inited)
    {
        ev_timer_init(&poll_status_timer, strongswan_poll_status_cb, SS_POLL_AFTER, SS_POLL_REPEAT);
        inited = true;
    }

    if (!ev_is_active(&poll_status_timer))
    {
        ev_timer_start(EV_DEFAULT, &poll_status_timer);
    }
}

static void strongswan_poll_status_stop(void)
{
    ev_timer_stop(EV_DEFAULT, &poll_status_timer);
}
