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

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "mupnp_server.h"
#include "const.h"
#include "util.h"
#include "log.h"
#include "daemon.h"
#include "execsh.h"

// Enable support of multiple listener interfaces when necessary
#ifndef MUPNP_LISTENER_MAX
#define MUPNP_LISTENER_MAX 1
#endif

// Constatnt empty interface string
static const char * const EMPTY_IFC = "";

// mupnp server object structure
struct upnp_server
{
    daemon_t server_process;
    char *path_cfg;
    char *path_lease;
    const mupnp_config_t *config;
    const char *ext_ifc4;
    const char *ext_ifc6;
    const char *int_ifc[MUPNP_LISTENER_MAX]; // array to support multiple listening interfaces
};

/* Dynamic allocation of string for file path with extension. Caller is 
 * responsible to free() thins string array when no longer used */
static char *file_path(const char *dir, const char *name, const char *extension)
{
    char buf[C_MAXPATH_LEN];
    if (WARN_ON(snprintf(buf, sizeof(buf), "%s/%s%s", dir, name, extension) >= (int)sizeof(buf)))
        return NULL;
    return strdup(buf);
}

static bool upnp_process_on_exit(daemon_t *d)
{
    mupnp_server_t *self = CONTAINER_OF(d, mupnp_server_t, server_process);
    LOG(ERR, "miniupnp: %s server process exited", self->config->name);
    (void)daemon_stop(&self->server_process);
    return true;
}

static bool service_init(mupnp_server_t *self, const mupnp_config_t *cfg)
{
    size_t n;
    char *path_cfg = NULL;
    char *path_pid = NULL;
    char *path_lease = NULL;

    if (mkdir(cfg->config_dir_path, 0700) != 0 && errno != EEXIST)
    {
        LOG(ERR, "miniupnp: Error creating MiniUPnPD config dir: %s", cfg->config_dir_path);
        goto init_err;
    }

    if (!daemon_init(&self->server_process, CONFIG_OSN_MINIUPNPD_PATH, 0))
    {
        LOG(ERR, "miniupnp: Error initializing UPnP process object.");
        goto init_err;
    }

    path_cfg = file_path(cfg->config_dir_path, cfg->name, ".conf");
    if(NULL == path_cfg) goto oom_err;

    // Path to the PID file
    path_pid = file_path("/var/run", cfg->name, ".pid");
    if(NULL == path_pid) goto oom_err;

    daemon_atexit(&self->server_process, &upnp_process_on_exit);
    daemon_arg_add(&self->server_process, "-d"); // Run in foreground
    daemon_arg_add(&self->server_process, "-f", path_cfg); // add config file path
    daemon_arg_add(&self->server_process, "-P", path_pid); // add PID file path

    if (!daemon_pidfile_set(&self->server_process, path_pid, false))
    {
        LOG(ERR, "miniupnp: Error initializing UPnP process PID file.");
        goto init_err;
    }

    path_lease = file_path(cfg->config_dir_path, cfg->name, ".leases");
    if (NULL == path_lease) goto oom_err;

    free(path_pid);

    self->config = cfg;
    self->ext_ifc4 = EMPTY_IFC;
    self->ext_ifc6 = EMPTY_IFC;
    for (n = 0; n < ARRAY_SIZE(self->int_ifc); self->int_ifc[n++] = EMPTY_IFC);
    self->path_cfg = path_cfg;
    self->path_lease = path_lease;
    return true;

oom_err:
    LOG(ERR, "miniupnp: Error allocating memory for object.");

init_err:
    free(path_pid);
    free(path_cfg);
    free(path_lease);
    return false;
}

mupnp_server_t *mupnp_server_new(const mupnp_config_t *cfg)
{
    if (cfg == NULL) return NULL;
    if (cfg->name == NULL || strlen(cfg->name) == 0) return NULL;

    mupnp_server_t *self = calloc(1, sizeof(*self));
    if (self == NULL) return NULL;

    if (!service_init(self, cfg))
    {
        free(self);
        self = NULL;
    }
    return self;
}

void mupnp_server_del(mupnp_server_t *self)
{
    if (self == NULL) return;
    (void)mupnp_server_stop(self);
    daemon_fini(&self->server_process);
    free(self->path_cfg);
    free(self->path_lease);
    free(self);
}

static bool can_attach_detach(const mupnp_server_t *self, const char *ifname)
{
    if (ifname == NULL || strlen(ifname) == 0) return false;
    if (mupnp_server_started(self)) return false;
    return true;
}

bool mupnp_server_attach_external(mupnp_server_t *self, const char *ifname)
{
    if (!can_attach_detach(self, ifname)) return false;
    if (self->ext_ifc4 != EMPTY_IFC) return false;

    self->ext_ifc4 = ifname;
    return true;
}

bool mupnp_server_attach_external6(mupnp_server_t *self, const char *ifname)
{
    if (!can_attach_detach(self, ifname)) return false;
    if (self->ext_ifc6 != EMPTY_IFC) return false;
    
    self->ext_ifc6 = ifname;
    return true;
}

bool mupnp_server_attach_internal(mupnp_server_t *self, const char *ifname)
{
    if (!can_attach_detach(self, ifname)) return false;

    size_t n;
    for (n = 0; n < ARRAY_SIZE(self->int_ifc); n++)
    {
        if (self->int_ifc[n] == EMPTY_IFC)
        {
            self->int_ifc[n] = ifname;
            return true;
        }
    }
    return false;
}

bool mupnp_server_detach(mupnp_server_t *self, const char *ifname)
{
    if (!can_attach_detach(self, ifname)) return false;

    if (0 == strcmp(self->ext_ifc4, ifname))
    {
        self->ext_ifc4 = EMPTY_IFC;
        return true;
    }

    if (0 == strcmp(self->ext_ifc6, ifname))
    {
        self->ext_ifc6 = EMPTY_IFC;
        return true;
    }

    size_t n;
    for (n = 0; n < ARRAY_SIZE(self->int_ifc); ++n)
    {
        if (0 == strcmp(self->int_ifc[n], ifname))
        {
            self->int_ifc[n] = EMPTY_IFC;
            return true;
        }
    }

    return false;
}

enum config_file_err
{
    CFERR_NONE = 0,
    CFERR_NO_IFC = 1, //< not all upnp interfaces attached
    CFERR_NO_MEM = 2, //< provided buffer for config file too small
    CFERR_FOPEN  = 3, //< cannot open config file for writing
    CFERR_FWRITE = 4 //< error while writing to config file
};

static enum config_file_err create_config_file(const mupnp_server_t *self)
{
    char fbuf[4096];
    const mupnp_config_t *cfg = self->config;
    bool is_ext = false;
    int n = 0;

    n += snprintf(fbuf + n, sizeof(fbuf) - n, "#\n# Auto-generated by OpenSync\n#\n\n");
    if ((size_t)n >= sizeof(fbuf)) return CFERR_NO_MEM;
    
    if(self->ext_ifc4 != EMPTY_IFC)
    {
        n += snprintf(fbuf + n, sizeof(fbuf) - n, "ext_ifname=%s\n", self->ext_ifc4);
        if ((size_t)n >= sizeof(fbuf)) return CFERR_NO_MEM;
        is_ext = true;
    }

    if(self->ext_ifc6 != EMPTY_IFC)
    {
        n += snprintf(fbuf + n, sizeof(fbuf) - n, "ext_ifname6=%s\n", self->ext_ifc6);
        if ((size_t)n >= sizeof(fbuf)) return CFERR_NO_MEM;
        is_ext = true;
    }

    if (!is_ext) return CFERR_NO_IFC;

    size_t i;
    for (i = 0; i < ARRAY_SIZE(self->int_ifc); ++i)
    {
        if (self->int_ifc[i] == EMPTY_IFC) return CFERR_NO_IFC;
        n += snprintf(fbuf + n, sizeof(fbuf) - n, "listening_ip=%s\n", self->int_ifc[i]);
        if ((size_t)n >= sizeof(fbuf)) return CFERR_NO_MEM;
    }

    n += snprintf(fbuf + n, sizeof(fbuf) - n, 
        "\nlease_file=%s\n"
        "enable_natpmp=%s\n"
        "secure_mode=%s\n"
        "system_uptime=%s\n"
        "http_port=%u\n", 
        self->path_lease,
        cfg->enable_natpmp ? "yes" : "no",
        cfg->secure_mode ? "yes" : "no",
        cfg->system_uptime ? "yes" : "no",
        cfg->http_port);
    
    if ((size_t)n >= sizeof(fbuf)) return CFERR_NO_MEM;

    if (cfg->upnp_forward_chain != NULL)
    {
        n += snprintf(fbuf + n, sizeof(fbuf) - n, "upnp_forward_chain=%s\n", cfg->upnp_forward_chain);
        if ((size_t)n >= sizeof(fbuf)) return CFERR_NO_MEM;
    }
    if (cfg->upnp_nat_chain != NULL)
    {
        n += snprintf(fbuf + n, sizeof(fbuf) - n, "upnp_nat_chain=%s\n", cfg->upnp_nat_chain);
        if ((size_t)n >= sizeof(fbuf)) return CFERR_NO_MEM;
    }
    const char *prule;
    for (i = 0, prule = cfg->perm_rules[i]; prule != NULL; prule = cfg->perm_rules[++i])
    {
        n += snprintf(fbuf + n, sizeof(fbuf) - n, "%s\n", prule);
        if ((size_t)n >= sizeof(fbuf)) return CFERR_NO_MEM;
    }

    FILE *fconf = fopen(self->path_cfg, "w");
    if (NULL == fconf) return CFERR_FOPEN;

    enum config_file_err rv = (1 == fwrite(fbuf, (size_t)n, 1, fconf)) ? CFERR_NONE : CFERR_FWRITE;
    fclose(fconf);
    return rv;
}

bool mupnp_server_start(mupnp_server_t *self)
{
    if (mupnp_server_started(self)) return true;

    int rv = create_config_file(self);
    if (rv != CFERR_NONE)
    {
        // suppress incomplete interfaces error log: expected case in interface driven algorithm
        if (rv != CFERR_NO_IFC)
        {
            LOG(ERR, "miniupnp: Error=%d while creating MiniUPnPD config file: %s", rv, self->path_cfg);
        }
        return false;
    }

    LOG(INFO, "miniupnp: %s server start", self->config->name);
    if (!daemon_start(&self->server_process))
    {
        LOG(ERR, "miniupnp: Error starting %s server process", self->config->name);
        return false;
    }
    return true;
}

bool mupnp_server_stop(mupnp_server_t *self)
{
    int rc;

    if (!mupnp_server_started(self)) return true;

    LOG(INFO, "miniupnp: %s server stop", self->config->name);

    if (!daemon_stop(&self->server_process))
    {
        LOG(WARN, "miniupnp: Error stopping %s server process.", self->config->name);
        /*TODO: analyse case when daemon not stopped */
        return false;
    }

    /*
     * Flush all firewall rules upon miniupnpd termination. Please do note that this does not terminate forwarded
     * connections.
     *
     * miniupnpd does not have a "flush rules at exit" action. To implement that, all existing firewall backends
     * would have to be updated. Without the ability to test them all, it is more appropriate to handle this case here.
     *
     * The iptscript.sh is part of miniupnpe and is used by miniupnpd for firewall management. It has an `fini` action
     * which should do what we want.
     */
    rc = execsh_log(LOG_SEVERITY_DEBUG, MUPNP_IPTSCRIPT_CMD, "fini");
    if (rc != 0)
    {
        LOG(ERR, "miniupnp: %s: Error flushing miniupnpd firewall rules (via iptscript.sh).", self->config->name);
    }

    return true;
}

bool mupnp_server_started(const mupnp_server_t *self)
{
    bool started;
    (void)daemon_is_started(&self->server_process, &started);
    return started;
}
