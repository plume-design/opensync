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

#include <module.h>
#include <inttypes.h>
#include <const.h>
#include <memutil.h>
#include <util.h>
#include <log.h>
#include <osw_module.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_mux.h>
#include <osw_drv_common.h>
#include <osw_drv.h>
#include <osw_drv_mediator.h>
#include <ev.h>

#define NOTE(fmt, ...) LOGI("%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)

#define FRM_INJECT_PATH "/tmp/ow_frm_inject"
#define TX_STATUS_PATH "/tmp/ow_frm_inject_status"

struct ow_frm_inject {
    char phy_name[32];
    char vif_name[32];
    struct osw_hwaddr sta_addr;
    int freq;
    uint8_t *frame_buf;
    int frame_len;
};

struct ow_frm_inject_file {
    struct ow_frm_inject params;
    struct ev_loop *loop;
    ev_stat stat;
};

static bool
ow_frm_inject_build_frame(struct ow_frm_inject *p)
{
    const struct osw_state_vif_info *vif_info;
    uint8_t *base = p->frame_buf;

    if ((vif_info = osw_state_vif_lookup(p->phy_name, p->vif_name)) == NULL)
    {
        LOGW("ow: frm: inject: vif %s not found", p->vif_name);
        return false;
    }

    /* change DA */
    memcpy(base + 4, &p->sta_addr, sizeof(struct osw_hwaddr));
    /* change SA */
    memcpy(base + 10, &vif_info->drv_state->mac_addr, sizeof(struct osw_hwaddr));
    /* change BSSID */
    memcpy(base + 16, &vif_info->drv_state->mac_addr, sizeof(struct osw_hwaddr));

    return true;
}

static const char *
ow_frm_inject_status_to_str(enum osw_frame_tx_result result)
{
    switch(result)
    {
        case OSW_FRAME_TX_RESULT_SUBMITTED:
            return "submitted\n";
        case OSW_FRAME_TX_RESULT_FAILED:
            return "failed\n";
        case OSW_FRAME_TX_RESULT_DROPPED:
            return "dropped\n";
    }
    return "unknown\n";
}

static void
ow_frm_inject_tx_result_cb(struct osw_drv_frame_tx_desc *desc,
                enum osw_frame_tx_result result,
                void *caller_priv)
{
    FILE *f = fopen(TX_STATUS_PATH, "w");
    if (WARN_ON(f == NULL)) return;

    WARN_ON(fputs(ow_frm_inject_status_to_str(result), f));

    fclose(f);
    osw_drv_frame_tx_desc_free(desc);
}

static void ow_frm_inject_try_req_tx(struct ow_frm_inject *p)
{
    struct osw_drv_frame_tx_desc *desc;

    /* prepare transmission descriptor */
    desc = osw_drv_frame_tx_desc_new(ow_frm_inject_tx_result_cb, NULL);
    osw_drv_frame_tx_desc_set_frame(desc, p->frame_buf, p->frame_len);
    if (p->freq != 0)
    {
        const struct osw_channel c = { .control_freq_mhz = p->freq };
        osw_drv_frame_tx_desc_set_channel(desc, &c);
    }

    LOGI("ow: frm: inject: schedule transmission");
    if (!osw_mux_frame_tx_schedule(p->phy_name, p->vif_name, desc))
    {
        LOGE("ow: frm: inject: frame schedule error");
    }
    FREE(p->frame_buf);
}

static bool
ow_frm_inject_file_process_line(struct ow_frm_inject *p, char *line)
{
    const char *k = strsep(&line, "=");
    char *v = strsep(&line, " \n");
    if (k == NULL || v == NULL) return false;

    NOTE("k='%s' v='%s'", k, v);
    if (strcmp(k, "phy_name") == 0)
    {
        if (strcmp(v, "") == 0)
        {
            LOGW("ow: frm: inject: empty phy_name");
            return false;
        }
        STRSCPY_WARN(p->phy_name, v);
        LOGI("ow: frm: inject: phy_name=%s", p->phy_name);
    }
    else if (strcmp(k, "vif_name") == 0)
    {
        if (strcmp(v, "") == 0)
        {
            LOGW("ow: frm: inject: empty vif_name");
            return false;
        }
        STRSCPY_WARN(p->vif_name, v);
        LOGI("ow: frm: inject: vif_name=%s", p->vif_name);
    }
    else if (strcmp(k, "sta_addr") == 0)
    {
        if (osw_hwaddr_from_cstr(v, &p->sta_addr) == false)
        {
            LOGW("ow: frm: inject: invalid sta addr %s", v);
            return false;
        }
    }
    else if (strcmp(k, "freq") == 0)
    {
        p->freq = atoi(v);
    }
    else if (strcmp(k, "frame") == 0)
    {
        const int i = strlen(v);
        p->frame_buf = MALLOC(sizeof(uint8_t) * i/2);
        if (hex2bin(v, i, p->frame_buf, i/2) == -1)
        {
            LOGW("ow: frm: inject: hex2bin conversion error");
            FREE(p->frame_buf);
            return false;
        }
        p->frame_len = i/2;
    }

    return true;
}

static bool ow_frm_inject_file_process_path(struct ow_frm_inject_file *inf)
{
    FILE *f = fopen(FRM_INJECT_PATH, "rb");
    NOTE("path='%s' file=%p", FRM_INJECT_PATH, f);
    char line[4096];
    bool rval;

    if (WARN_ON(f == NULL)) return false;
    memset(&inf->params, 0, sizeof(inf->params));
    while(fgets(line, sizeof(line), f) != NULL)
    {
        rval = ow_frm_inject_file_process_line(&inf->params, line);
        if (rval == false) 
        {
            fclose(f);
            return false;
        }
    }
    fclose(f);
    return true;
}

static void ow_frm_inject_file_stat_cb(EV_P_ ev_stat *arg, int events)
{
    struct ow_frm_inject_file *inf;
    inf = container_of(arg, struct ow_frm_inject_file, stat);
    if (!ow_frm_inject_file_process_path(inf)) return;
    if (!ow_frm_inject_build_frame(&inf->params)) return;
    ow_frm_inject_try_req_tx(&inf->params);
}

static void ow_frm_inject_file_start(EV_P)
{
    struct ow_frm_inject_file *inf = CALLOC(1, sizeof(*inf));

    LOGI("ow: frm: inject: file: path='%s'", FRM_INJECT_PATH);
    inf->loop = EV_A;
    ev_stat_init(&inf->stat, ow_frm_inject_file_stat_cb, FRM_INJECT_PATH, 0);
    ev_stat_start(EV_A_ &inf->stat);
    ev_unref(EV_A);
}

OSW_MODULE(ow_frm_inject_file)
{
    struct ev_loop *loop = OSW_MODULE_LOAD(osw_ev);
    ow_frm_inject_file_start(loop);
    return NULL;
}
