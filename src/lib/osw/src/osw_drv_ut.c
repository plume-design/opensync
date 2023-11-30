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

#include "osw_ut.h"
#include <osw_conf.h>
#include <osw_mux.h>

struct osw_drv_ut_frame_tx {
    unsigned int submitted_cnt;
    unsigned int failed_cnt;
    unsigned int dropped_cnt;
};

struct osw_drv_ut_state_obs {
    struct osw_state_observer obs;
    bool busy;
};

struct osw_drv_ut_sta {
    const char *vif_name;
    struct osw_hwaddr mac_addr;
    int duration;
};

struct osw_drv_ut_drv_priv {
    const char *phy_name;
    const char *vif_names[16];
    struct osw_drv_ut_sta stas[16];
    struct osw_drv *drv;
};

struct osw_drv_ut_vif {
    const char *vif_name;
    struct osw_drv_vif_state state;
};

static struct osw_drv_ut_vif vifs[] = {
    {
        .vif_name = "vif1",
        .state = {
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u.ap = {
                .psk_list = {
                    .list = (struct osw_ap_psk[]){
                        { .key_id = 1, .psk.str = "hello" },
                        { .key_id = 2, .psk.str = "world" },
                    },
                    .count = 2,
                },
                .acl = {
                    .list = (struct osw_hwaddr[]) {
                        { .octet = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } },
                        { .octet = { 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb } },
                        { .octet = { 0xcc, 0xdd, 0xee, 0xff, 0x10, 0x20 } },
                    },
                    .count = 3,
                },
            },
        },
    },
};

static struct osw_drv_ut_drv_priv drv1_priv[] = {
    { "phy1", { "vif1", "vif2" }, {}, NULL },
    { "phy2", { "vif3" }, {}, NULL },
    { NULL, {}, {}, NULL },
};
static struct osw_drv_ut_drv_priv drv2_priv[] = {
    { "phy3", { "vif4" }, {}, NULL },
    { NULL, {}, {}, NULL },
};

static bool
osw_drv_ut_drv_phy_exists(const struct osw_drv_ut_drv_priv *priv,
                          const char *phy_name)
{
    for (; priv->phy_name; priv++)
        if (strcmp(priv->phy_name, phy_name) == 0)
            return true;

    return false;
}

static bool
osw_drv_ut_drv_vif_exists(const struct osw_drv_ut_drv_priv *priv,
                          const char *phy_name,
                          const char *vif_name)
{
    size_t i;
    for (; priv->phy_name; priv++)
        if (strcmp(priv->phy_name, phy_name) == 0)
            for (i = 0; i < ARRAY_SIZE(priv->vif_names) && priv->vif_names[i] != NULL; i++)
                if (strcmp(priv->vif_names[i], vif_name) == 0)
                    return true;

    return false;
}

static bool
osw_drv_ut_drv_sta_connected(const struct osw_drv_ut_drv_priv *priv,
                             const char *phy_name,
                             const char *vif_name,
                             const struct osw_hwaddr *mac_addr)
{
    size_t i;
    for (; priv->phy_name; priv++)
        if (strcmp(priv->phy_name, phy_name) == 0)
            for (i = 0; i < ARRAY_SIZE(priv->stas); i++)
                if (priv->stas[i].vif_name != NULL)
                    if (strcmp(priv->stas[i].vif_name, vif_name) == 0)
                        if (memcmp(&priv->stas[i].mac_addr, mac_addr, sizeof(*mac_addr)) == 0)
                            return true;

    return false;
}

static int
osw_drv_ut_drv_sta_duration(const struct osw_drv_ut_drv_priv *priv,
                            const char *phy_name,
                            const char *vif_name,
                            const struct osw_hwaddr *mac_addr)
{
    size_t i;
    for (; priv->phy_name; priv++)
        if (strcmp(priv->phy_name, phy_name) == 0)
            for (i = 0; i < ARRAY_SIZE(priv->stas); i++)
                if (priv->stas[i].vif_name != NULL)
                    if (strcmp(priv->stas[i].vif_name, vif_name) == 0)
                        if (memcmp(&priv->stas[i].mac_addr, mac_addr, sizeof(*mac_addr)) == 0)
                            return priv->stas[i].duration;

    return 0;
}

static void
osw_drv_ut_drv_init_1_cb(struct osw_drv *drv)
{
    drv1_priv[0].drv = drv;
    osw_drv_set_priv(drv, drv1_priv);
}

static void
osw_drv_ut_drv_init_2_cb(struct osw_drv *drv)
{
    drv2_priv[0].drv = drv;
    osw_drv_set_priv(drv, drv2_priv);
}

static void
osw_drv_ut_drv_get_phy_list_cb(struct osw_drv *drv,
                               osw_drv_report_phy_fn_t *report_phy_fn,
                               void *fn_priv)
{
    const struct osw_drv_ut_drv_priv *priv = osw_drv_get_priv(drv);
    for (; priv->phy_name != NULL; priv++)
        if (strlen(priv->phy_name) > 0)
            report_phy_fn(priv->phy_name, fn_priv);
}

static void
osw_drv_ut_drv_get_vif_list_cb(struct osw_drv *drv,
                               const char *phy_name,
                               osw_drv_report_vif_fn_t *report_vif_fn,
                               void *fn_priv)
{
    const struct osw_drv_ut_drv_priv *priv = osw_drv_get_priv(drv);
    const char *const*vif_name;
    for (; priv->phy_name; priv++)
        if (strcmp(priv->phy_name, phy_name) == 0)
            for (vif_name = priv->vif_names; *vif_name != NULL; vif_name++)
                if (strlen(*vif_name) > 0)
                    report_vif_fn(*vif_name, fn_priv);
}

static void
osw_drv_ut_drv_get_sta_list_cb(struct osw_drv *drv,
                               const char *phy_name,
                               const char *vif_name,
                               osw_drv_report_sta_fn_t *report_sta_fn,
                               void *fn_priv)
{
    const struct osw_drv_ut_drv_priv *priv = osw_drv_get_priv(drv);
    size_t i;
    for (; priv->phy_name; priv++)
        if (strcmp(priv->phy_name, phy_name) == 0)
            for (i = 0; i < ARRAY_SIZE(priv->stas); i++)
                if (priv->stas[i].vif_name != NULL)
                    if (strcmp(priv->stas[i].vif_name, vif_name) == 0)
                        report_sta_fn(&priv->stas[i].mac_addr, fn_priv);
}

static void
osw_drv_ut_drv_request_phy_state_cb(struct osw_drv *drv,
                                    const char *phy_name)
{
    const struct osw_drv_ut_drv_priv *priv = osw_drv_get_priv(drv);
    const struct osw_drv_phy_state info = {
        .exists = osw_drv_ut_drv_phy_exists(priv, phy_name),
    };
    printf("%s: phy_name=%s exists=%d\n", __func__, phy_name, info.exists);
    osw_drv_report_phy_state(priv->drv, phy_name, &info);
}

static void
osw_drv_ut_drv_request_vif_state_cb(struct osw_drv *drv,
                                    const char *phy_name,
                                    const char *vif_name)
{
    const struct osw_drv_ut_drv_priv *priv = osw_drv_get_priv(drv);
    struct osw_drv_vif_state info = {0};
    size_t i;

    for (i = 0; i < ARRAY_SIZE(vifs); i++)
        if (strcmp(vifs[i].vif_name, vif_name) == 0)
            break;

    info.exists = osw_drv_ut_drv_vif_exists(priv, phy_name, vif_name);
    info.vif_type = OSW_VIF_AP;

    if (i != ARRAY_SIZE(vifs))
        memcpy(&info, &vifs[i].state, sizeof(info));


    printf("%s: vif_name=%s exists=%d %d\n", __func__, vif_name, info.exists, osw_drv_ut_drv_vif_exists(priv, phy_name, vif_name));
    osw_drv_report_vif_state(priv->drv, phy_name, vif_name, &info);
}

static void
osw_drv_ut_drv_request_sta_state_cb(struct osw_drv *drv,
                                    const char *phy_name,
                                    const char *vif_name,
                                    const struct osw_hwaddr *mac_addr)
{
    const struct osw_drv_ut_drv_priv *priv = osw_drv_get_priv(drv);
    const struct osw_drv_sta_state info = {
        .connected = osw_drv_ut_drv_sta_connected(priv, phy_name, vif_name, mac_addr),
        .connected_duration_seconds = osw_drv_ut_drv_sta_duration(priv, phy_name, vif_name, mac_addr),
    };
    printf("%s: sta_addr=" OSW_HWADDR_FMT " connected=%d\n", __func__, OSW_HWADDR_ARG(mac_addr), info.connected);
    osw_drv_report_sta_state(priv->drv, phy_name, vif_name, mac_addr, &info);
}

static void
osw_drv_ut_drv_request_config_cb(struct osw_drv *drv,
                                 struct osw_drv_conf *conf)
{
    const struct osw_drv_ut_drv_priv *priv = osw_drv_get_priv(drv);
    const struct osw_drv_phy_config *phy = conf->phy_list;
    size_t n_phy = conf->n_phy_list;

    printf("%s: priv=%p, n_phy=%zd begin\n", __func__, priv, n_phy);
    for (; n_phy > 0; n_phy--, phy++)
        printf("%s: phy_name=%s\n", __func__, phy->phy_name);

    printf("%s: priv=%p, n_phy=%zd end\n", __func__, priv, n_phy);
    osw_drv_conf_free(conf);
}

// FIXME rename
static void
osw_drv_ut_core1_op_phy_added_cb(struct osw_state_observer *observer, const struct osw_state_phy_info *info)
{
    printf("%s: %s\n", __func__, info->phy_name);
}

static void
osw_drv_ut_core1_op_phy_removed_cb(struct osw_state_observer *observer, const struct osw_state_phy_info *info)
{
    printf("%s: %s\n", __func__, info->phy_name);
}

static void
osw_drv_ut_core1_op_phy_changed_cb(struct osw_state_observer *observer, const struct osw_state_phy_info *info)
{
    printf("%s: %s\n", __func__, info->phy_name);
}

static void
osw_drv_ut_core1_op_vif_added_cb(struct osw_state_observer *observer, const struct osw_state_vif_info *info)
{
    printf("%s: %s/%s\n", __func__, info->phy->phy_name, info->vif_name);
}

static void
osw_drv_ut_core1_op_vif_removed_cb(struct osw_state_observer *observer, const struct osw_state_vif_info *info)
{
    printf("%s: %s/%s\n", __func__, info->phy->phy_name, info->vif_name);
}

static void
osw_drv_ut_core1_op_vif_changed_cb(struct osw_state_observer *observer, const struct osw_state_vif_info *info)
{
    printf("%s: %s/%s\n", __func__, info->phy->phy_name, info->vif_name);
}

static void
osw_drv_ut_core1_op_sta_connected_cb(struct osw_state_observer *observer, const struct osw_state_sta_info *info)
{
    printf("%s: %s/%s/" OSW_HWADDR_FMT "\n", __func__, info->vif->phy->phy_name, info->vif->vif_name, OSW_HWADDR_ARG(info->mac_addr));
}

static void
osw_drv_ut_core1_op_sta_disconnected_cb(struct osw_state_observer *observer, const struct osw_state_sta_info *info)
{
    printf("%s: %s/%s/" OSW_HWADDR_FMT "\n", __func__, info->vif->phy->phy_name, info->vif->vif_name, OSW_HWADDR_ARG(info->mac_addr));
}

static void
osw_drv_ut_core1_op_sta_changed_cb(struct osw_state_observer *observer, const struct osw_state_sta_info *info)
{
    printf("%s: %s/%s/" OSW_HWADDR_FMT "\n", __func__, info->vif->phy->phy_name, info->vif->vif_name, OSW_HWADDR_ARG(info->mac_addr));
}

static void
osw_drv_ut_state_idle_cb(struct osw_state_observer *observer)
{
    struct osw_drv_ut_state_obs *obj = container_of(observer, struct osw_drv_ut_state_obs, obs);
    printf("%s: was busy = %d\n", __func__, obj->busy);
    obj->busy = false;
}

static void
osw_drv_ut_state_busy_cb(struct osw_state_observer *observer)
{
    struct osw_drv_ut_state_obs *obj = container_of(observer, struct osw_drv_ut_state_obs, obs);
    printf("%s: was busy = %d\n", __func__, obj->busy);
    obj->busy = true;
}

static void
osw_drv_ut_work(struct osw_drv_ut_state_obs *obs)
{
    int n = 50;
    do {
        printf("%s: work %d\n", __func__,  n);
        ev_run(EV_DEFAULT_ EVRUN_ONCE);
    } while (n-- > 0 && obs->busy == true);

    printf("%s: work done\n", __func__);
    assert(n > 0);
}

static void
osw_drv_ut_push_frame_tx_submit_cb(struct osw_drv *drv,
                                   const char *phy_name,
                                   const char *vif_name,
                                   struct osw_drv_frame_tx_desc *desc)
{
    osw_drv_report_frame_tx_state_submitted(drv);
}

static void
osw_drv_ut_push_frame_tx_nop_cb(struct osw_drv *drv,
                                const char *phy_name,
                                const char *vif_name,
                                struct osw_drv_frame_tx_desc *desc)
{
    /* Don't react on frame */
}

static void
osw_drv_ut_push_frame_tx_fail_cb(struct osw_drv *drv,
                                 const char *phy_name,
                                 const char *vif_name,
                                 struct osw_drv_frame_tx_desc *desc)
{
    osw_drv_report_frame_tx_state_failed(drv);
}

static void
osw_drv_ut_frame_tx_result_cb(struct osw_drv_frame_tx_desc *desc,
                              enum osw_frame_tx_result result,
                              void *caller_priv)
{
    assert(caller_priv != NULL);

    struct osw_drv_ut_frame_tx *frame = (struct osw_drv_ut_frame_tx*) caller_priv;

    switch (result) {
        case OSW_FRAME_TX_RESULT_SUBMITTED:
            frame->submitted_cnt++;
            break;
        case OSW_FRAME_TX_RESULT_FAILED:
            frame->failed_cnt++;
            break;
        case OSW_FRAME_TX_RESULT_DROPPED:
            frame->dropped_cnt++;
            break;
    }
}

OSW_UT(osw_drv_ut_1)
{
    static const struct osw_drv_ops drv1 = {
        .name = "drv1",
        .init_fn = osw_drv_ut_drv_init_1_cb,
        .get_phy_list_fn = osw_drv_ut_drv_get_phy_list_cb,
        .get_vif_list_fn = osw_drv_ut_drv_get_vif_list_cb,
        .get_sta_list_fn = osw_drv_ut_drv_get_sta_list_cb,
        .request_phy_state_fn = osw_drv_ut_drv_request_phy_state_cb,
        .request_vif_state_fn = osw_drv_ut_drv_request_vif_state_cb,
        .request_sta_state_fn = osw_drv_ut_drv_request_sta_state_cb,
        .request_config_fn = osw_drv_ut_drv_request_config_cb,
    };
    static const struct osw_drv_ops drv2 = {
        .name = "drv2",
        .init_fn = osw_drv_ut_drv_init_2_cb,
        .get_phy_list_fn = osw_drv_ut_drv_get_phy_list_cb,
        .get_vif_list_fn = osw_drv_ut_drv_get_vif_list_cb,
        .get_sta_list_fn = osw_drv_ut_drv_get_sta_list_cb,
        .request_phy_state_fn = osw_drv_ut_drv_request_phy_state_cb,
        .request_vif_state_fn = osw_drv_ut_drv_request_vif_state_cb,
        .request_sta_state_fn = osw_drv_ut_drv_request_sta_state_cb,
        .request_config_fn = osw_drv_ut_drv_request_config_cb,
    };
    static struct osw_drv_ut_state_obs obs1 = {
        .busy = false,
        .obs = {
            .name = "drv ut 1 state",
            .idle_fn = osw_drv_ut_state_idle_cb,
            .busy_fn = osw_drv_ut_state_busy_cb,
            .phy_added_fn = osw_drv_ut_core1_op_phy_added_cb,
            .phy_removed_fn = osw_drv_ut_core1_op_phy_removed_cb,
            .phy_changed_fn = osw_drv_ut_core1_op_phy_changed_cb,
            .vif_added_fn = osw_drv_ut_core1_op_vif_added_cb,
            .vif_removed_fn = osw_drv_ut_core1_op_vif_removed_cb,
            .vif_changed_fn = osw_drv_ut_core1_op_vif_changed_cb,
            .sta_connected_fn = osw_drv_ut_core1_op_sta_connected_cb,
            .sta_disconnected_fn = osw_drv_ut_core1_op_sta_disconnected_cb,
            .sta_changed_fn = osw_drv_ut_core1_op_sta_changed_cb,
        },
    };

    osw_drv_init();
    osw_drv_register_ops(&drv1);
    osw_drv_register_ops(&drv2);
    osw_state_register_observer(&obs1.obs);
    osw_drv_ut_work(&obs1);
    assert(drv1_priv[0].drv != NULL);
    assert(drv2_priv[0].drv != NULL);
    assert(osw_state_phy_lookup("phy1") != NULL);
    assert(osw_state_phy_lookup("phy2") != NULL);
    assert(osw_state_phy_lookup("phy3") != NULL);

    {
        printf("checking conf from state\n");

        struct ds_tree *phy_tree = osw_conf_build_from_state();
        struct osw_conf_phy *phy;
        struct osw_conf_vif *vif;
        struct osw_conf_psk *psk;
        struct osw_conf_acl *acl;
        size_t n_phy = 0;
        size_t n_vif;
        size_t n_psk;
        size_t n_acl;
        int key_id;

        ds_tree_foreach(phy_tree, phy) n_phy++;
        assert(n_phy == 3);

        phy = ds_tree_find(phy_tree, "phy1"); assert(phy != NULL);
        vif = ds_tree_find(&phy->vif_tree, "vif1"); assert(vif != NULL); assert(vif->phy == phy);
        key_id = 1; psk = ds_tree_find(&vif->u.ap.psk_tree, &key_id);
        assert(psk != NULL);
        assert(strcmp(psk->ap_psk.psk.str, vifs[0].state.u.ap.psk_list.list[0].psk.str) == 0);
        assert(strcmp(psk->ap_psk.psk.str, vifs[0].state.u.ap.psk_list.list[1].psk.str) != 0);
        key_id = 2; psk = ds_tree_find(&vif->u.ap.psk_tree, &key_id);
        assert(psk != NULL);
        assert(strcmp(psk->ap_psk.psk.str, vifs[0].state.u.ap.psk_list.list[0].psk.str) != 0);
        assert(strcmp(psk->ap_psk.psk.str, vifs[0].state.u.ap.psk_list.list[1].psk.str) == 0);
        acl = ds_tree_find(&vif->u.ap.acl_tree, vifs[0].state.u.ap.acl.list[0].octet); assert(acl != NULL);
        acl = ds_tree_find(&vif->u.ap.acl_tree, vifs[0].state.u.ap.acl.list[1].octet); assert(acl != NULL);
        acl = ds_tree_find(&vif->u.ap.acl_tree, vifs[0].state.u.ap.acl.list[2].octet); assert(acl != NULL);
        n_psk = 0; ds_tree_foreach(&vif->u.ap.psk_tree, psk) n_psk++; assert(n_psk == 2);
        n_acl = 0; ds_tree_foreach(&vif->u.ap.acl_tree, acl) n_acl++; assert(n_acl == 3);
        vif = ds_tree_find(&phy->vif_tree, "vif2"); assert(vif != NULL); assert(vif->phy == phy);
        acl = ds_tree_find(&vif->u.ap.acl_tree, vifs[0].state.u.ap.acl.list[0].octet); assert(acl == NULL);
        n_psk = 0; ds_tree_foreach(&vif->u.ap.psk_tree, psk) n_psk++; assert(n_psk == 0);
        n_acl = 0; ds_tree_foreach(&vif->u.ap.acl_tree, acl) n_acl++; assert(n_acl == 0);
        n_vif = 0; ds_tree_foreach(&phy->vif_tree, vif) n_vif++; assert(n_vif == 2);

        phy = ds_tree_find(phy_tree, "phy2"); assert(phy != NULL);
        vif = ds_tree_find(&phy->vif_tree, "vif3"); assert(vif != NULL); assert(vif->phy == phy);
        n_psk = 0; ds_tree_foreach(&vif->u.ap.psk_tree, psk) n_psk++; assert(n_psk == 0);
        n_acl = 0; ds_tree_foreach(&vif->u.ap.acl_tree, acl) n_acl++; assert(n_acl == 0);
        n_vif = 0; ds_tree_foreach(&phy->vif_tree, vif) n_vif++; assert(n_vif == 1);

        phy = ds_tree_find(phy_tree, "phy3"); assert(phy != NULL);
        vif = ds_tree_find(&phy->vif_tree, "vif4"); assert(vif != NULL); assert(vif->phy == phy);
        n_psk = 0; ds_tree_foreach(&vif->u.ap.psk_tree, psk) n_psk++; assert(n_psk == 0);
        n_acl = 0; ds_tree_foreach(&vif->u.ap.acl_tree, acl) n_acl++; assert(n_acl == 0);
        n_vif = 0; ds_tree_foreach(&phy->vif_tree, vif) n_vif++; assert(n_vif == 1);
    }

    #if 0
    {
        struct osw_drv_phy_config phy[4] = {0};
        struct osw_drv_conf conf = {
            .phy_list = phy,
            .n_phy_list = ARRAY_SIZE(phy),
        };
        phy[0].phy_name = "phy1";
        phy[1].phy_name = "phy3";
        phy[2].phy_name = "phy2";
        phy[3].phy_name = "phy4";
        osw_req_config(&conf);
        //osw_core_request_config(&conf);
    }
    #endif

    printf("remove phy2\n");
    drv1_priv[1].phy_name = "";
    osw_drv_report_phy_changed(drv1_priv[0].drv, "phy2");
    osw_drv_ut_work(&obs1);
    assert(osw_state_phy_lookup("phy2") == NULL);
    assert(osw_state_vif_lookup("phy2", "vif3") == NULL);

    printf("add phy4\n");
    drv1_priv[1].phy_name = "phy4";
    osw_drv_report_phy_changed(drv1_priv[0].drv, "phy4");
    osw_drv_ut_work(&obs1);
    assert(osw_state_phy_lookup("phy4") != NULL);
    assert(osw_state_vif_lookup("phy4", "vif3") != NULL);

    printf("blip phy5\n");
    drv1_priv[1].phy_name = "";
    osw_drv_report_phy_changed(drv1_priv[0].drv, "phy5");
    osw_drv_ut_work(&obs1);
    assert(osw_state_phy_lookup("phy4") == NULL);
    assert(osw_state_phy_lookup("phy5") == NULL);
    assert(osw_state_vif_lookup("phy4", "vif3") == NULL);

    printf("inject bad csa event\n");
    osw_drv_report_vif_channel_change_started(drv1_priv[0].drv, "phy10", "vif10", NULL);
    osw_drv_ut_work(&obs1);
    assert(osw_state_phy_lookup("phy10") == NULL);
    assert(osw_state_vif_lookup("phy10", "vif10") == NULL);

    printf("inject sta\n");
    drv1_priv[0].stas[0].vif_name = "vif1";
    drv1_priv[0].stas[0].mac_addr.octet[0] = 0x00;
    drv1_priv[0].stas[0].mac_addr.octet[1] = 0x11;
    drv1_priv[0].stas[0].mac_addr.octet[2] = 0x22;
    drv1_priv[0].stas[0].mac_addr.octet[3] = 0x33;
    drv1_priv[0].stas[0].mac_addr.octet[4] = 0x44;
    drv1_priv[0].stas[0].mac_addr.octet[5] = 0x55;
    drv1_priv[0].stas[0].duration = 2; /* simulates connection happened 2 seconds ago */
    osw_drv_report_sta_changed(drv1_priv[0].drv, "phy1", "vif1", &drv1_priv[0].stas[0].mac_addr);
    osw_drv_ut_work(&obs1);
    assert(osw_state_sta_lookup("phy1", "vif1", &drv1_priv[0].stas[0].mac_addr) != NULL);

    printf("inject 2nd sta, other vif\n");
    drv2_priv[0].stas[0].vif_name = "vif4";
    drv2_priv[0].stas[0].mac_addr.octet[0] = 0x00;
    drv2_priv[0].stas[0].mac_addr.octet[1] = 0x11;
    drv2_priv[0].stas[0].mac_addr.octet[2] = 0x22;
    drv2_priv[0].stas[0].mac_addr.octet[3] = 0x33;
    drv2_priv[0].stas[0].mac_addr.octet[4] = 0x44;
    drv2_priv[0].stas[0].mac_addr.octet[5] = 0x55;
    drv2_priv[0].stas[0].duration = 1; /* simulates connection happened 1 seconds ago */
    osw_drv_report_sta_changed(drv2_priv[0].drv, "phy3", "vif4", &drv2_priv[0].stas[0].mac_addr);
    osw_drv_ut_work(&obs1);
    assert(osw_state_sta_lookup("phy1", "vif1", &drv1_priv[0].stas[0].mac_addr) != NULL);
    assert(osw_state_sta_lookup("phy3", "vif4", &drv2_priv[0].stas[0].mac_addr) != NULL);
    assert(osw_state_sta_lookup_newest(&drv2_priv[0].stas[0].mac_addr) != NULL);
    assert(osw_state_sta_lookup("phy3", "vif4", &drv2_priv[0].stas[0].mac_addr) ==
           osw_state_sta_lookup_newest(&drv2_priv[0].stas[0].mac_addr));

    printf("flip 2nd sta\n");
    drv2_priv[0].stas[0].duration = 5; /* simulates connection happened 5 seconds ago */
    osw_drv_report_sta_changed(drv2_priv[0].drv, "phy3", "vif4", &drv2_priv[0].stas[0].mac_addr);
    osw_drv_ut_work(&obs1);
    assert(osw_state_sta_lookup("phy1", "vif1", &drv1_priv[0].stas[0].mac_addr) != NULL);
    assert(osw_state_sta_lookup("phy3", "vif4", &drv2_priv[0].stas[0].mac_addr) != NULL);
    assert(osw_state_sta_lookup_newest(&drv2_priv[0].stas[0].mac_addr) != NULL);
    assert(osw_state_sta_lookup("phy1", "vif1", &drv2_priv[0].stas[0].mac_addr) ==
           osw_state_sta_lookup_newest(&drv2_priv[0].stas[0].mac_addr));

    printf("reconnect 2nd sta\n");
    drv2_priv[0].stas[0].duration = 1; /* simulates connection happened 1 seconds ago */
    osw_drv_report_sta_changed(drv2_priv[0].drv, "phy3", "vif4", &drv2_priv[0].stas[0].mac_addr);
    osw_drv_ut_work(&obs1);

    printf("unregister\n");
    osw_drv_unregister_ops(&drv1);
    osw_drv_unregister_ops(&drv2);
    osw_drv_unregister_ops(&drv2); /* intentional double unregister */
    osw_drv_ut_work(&obs1);
    assert(osw_state_phy_lookup("phy1") == NULL);
    assert(osw_state_phy_lookup("phy3") == NULL);

    printf("unregister2\n");
    osw_drv_unregister_ops(&drv2); /* intentional triple unregister */
    osw_drv_ut_work(&obs1);
}

OSW_UT(osw_drv_ut_frame_tx_getters_setters)
{
    static struct osw_drv_ops drv1 = {
        .name = "drv1",
        .init_fn = osw_drv_ut_drv_init_1_cb,
        .get_phy_list_fn = osw_drv_ut_drv_get_phy_list_cb,
        .get_vif_list_fn = osw_drv_ut_drv_get_vif_list_cb,
        .get_sta_list_fn = osw_drv_ut_drv_get_sta_list_cb,
        .request_phy_state_fn = osw_drv_ut_drv_request_phy_state_cb,
        .request_vif_state_fn = osw_drv_ut_drv_request_vif_state_cb,
        .request_sta_state_fn = osw_drv_ut_drv_request_sta_state_cb,
        .request_config_fn = osw_drv_ut_drv_request_config_cb,
        .push_frame_tx_fn = osw_drv_ut_push_frame_tx_submit_cb,
    };

    static struct osw_drv_ut_state_obs obs1 = {
        .busy = false,
        .obs = {
            .idle_fn = osw_drv_ut_state_idle_cb,
            .busy_fn = osw_drv_ut_state_busy_cb,
            .phy_added_fn = osw_drv_ut_core1_op_phy_added_cb,
            .phy_removed_fn = osw_drv_ut_core1_op_phy_removed_cb,
            .phy_changed_fn = osw_drv_ut_core1_op_phy_changed_cb,
            .vif_added_fn = osw_drv_ut_core1_op_vif_added_cb,
            .vif_removed_fn = osw_drv_ut_core1_op_vif_removed_cb,
            .vif_changed_fn = osw_drv_ut_core1_op_vif_changed_cb,
            .name = "drv ut 1 state",
        },
    };

    /* Use only VIF3@PHY2 from driver no. 1 */
    const uint8_t raw_frame[] = { 0xA, 0xB, 0xC, };
    struct osw_drv_ut_frame_tx frame_a = { 0, 0, 0, };
    struct osw_drv_frame_tx_desc *frame_a_desc = osw_drv_frame_tx_desc_new(osw_drv_ut_frame_tx_result_cb, &frame_a);

    osw_time_set_mono_clk(0);
    osw_drv_init();
    osw_drv_register_ops(&drv1);
    osw_state_register_observer(&obs1.obs);
    osw_drv_ut_work(&obs1);

    /*
     * Verify getters & setters
     */
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(osw_drv_frame_tx_desc_get_frame(frame_a_desc) == NULL);
    assert(osw_drv_frame_tx_desc_get_frame_len(frame_a_desc) == 0);
    osw_drv_frame_tx_desc_set_frame(frame_a_desc, raw_frame, sizeof(raw_frame));
    assert(memcmp(raw_frame, osw_drv_frame_tx_desc_get_frame(frame_a_desc), sizeof(raw_frame)) == 0);
    assert(osw_drv_frame_tx_desc_get_frame_len(frame_a_desc) == sizeof(raw_frame));
}

OSW_UT(osw_drv_ut_frame_tx_reuse_desc)
{
    static struct osw_drv_ops drv1 = {
        .name = "drv1",
        .init_fn = osw_drv_ut_drv_init_1_cb,
        .get_phy_list_fn = osw_drv_ut_drv_get_phy_list_cb,
        .get_vif_list_fn = osw_drv_ut_drv_get_vif_list_cb,
        .get_sta_list_fn = osw_drv_ut_drv_get_sta_list_cb,
        .request_phy_state_fn = osw_drv_ut_drv_request_phy_state_cb,
        .request_vif_state_fn = osw_drv_ut_drv_request_vif_state_cb,
        .request_sta_state_fn = osw_drv_ut_drv_request_sta_state_cb,
        .request_config_fn = osw_drv_ut_drv_request_config_cb,
        .push_frame_tx_fn = osw_drv_ut_push_frame_tx_submit_cb,
    };

    static struct osw_drv_ut_state_obs obs1 = {
        .busy = false,
        .obs = {
            .idle_fn = osw_drv_ut_state_idle_cb,
            .busy_fn = osw_drv_ut_state_busy_cb,
            .phy_added_fn = osw_drv_ut_core1_op_phy_added_cb,
            .phy_removed_fn = osw_drv_ut_core1_op_phy_removed_cb,
            .phy_changed_fn = osw_drv_ut_core1_op_phy_changed_cb,
            .vif_added_fn = osw_drv_ut_core1_op_vif_added_cb,
            .vif_removed_fn = osw_drv_ut_core1_op_vif_removed_cb,
            .vif_changed_fn = osw_drv_ut_core1_op_vif_changed_cb,
            .name = "drv ut 1 state",
        },
    };

    /* Use only VIF3@PHY2 */
    const uint8_t raw_frame[] = { 0xA, 0xB, 0xC, };
    struct osw_drv_ut_frame_tx frame_a = { 0, 0, 0, };
    struct osw_drv_frame_tx_desc *frame_a_desc = osw_drv_frame_tx_desc_new(osw_drv_ut_frame_tx_result_cb, &frame_a);

    osw_time_set_mono_clk(0);
    osw_drv_init();
    osw_drv_register_ops(&drv1);
    osw_state_register_observer(&obs1.obs);
    osw_drv_ut_work(&obs1);

    /*
     * Setup frame
     */
    osw_drv_frame_tx_desc_set_frame(frame_a_desc, raw_frame, sizeof(raw_frame));

    /*
     * Try to schedule frame on invalid phy and vif
     */
    assert(osw_mux_frame_tx_schedule("invalid_phy", NULL, frame_a_desc) == false);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(osw_mux_frame_tx_schedule("phy2", "invalid_vif", frame_a_desc) == false);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);

    /*
     * Re-use frame, schedule & submit
     */
    drv1.push_frame_tx_fn = osw_drv_ut_push_frame_tx_submit_cb;
    assert(osw_mux_frame_tx_schedule("phy2", "vif3", frame_a_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == true);
    osw_drv_ut_work(&obs1);

    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(frame_a.submitted_cnt == 1);
    assert(frame_a.failed_cnt == 0);
    assert(frame_a.dropped_cnt == 0);

    /*
     * Re-use frame, schedule & fail
     */
    drv1.push_frame_tx_fn = osw_drv_ut_push_frame_tx_fail_cb;
    assert(osw_mux_frame_tx_schedule("phy2", "vif3", frame_a_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == true);
    osw_drv_ut_work(&obs1);

    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(frame_a.submitted_cnt == 1);
    assert(frame_a.failed_cnt == 1);
    assert(frame_a.dropped_cnt == 0);

    /*
     * Cancel before even scheduling work
     */
    assert(osw_mux_frame_tx_schedule("phy2", "vif3", frame_a_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == true);
    osw_drv_frame_tx_desc_cancel(frame_a_desc);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    osw_drv_ut_work(&obs1);

    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(frame_a.submitted_cnt == 1);
    assert(frame_a.failed_cnt == 1);
    assert(frame_a.dropped_cnt == 1);

    /*
     * Cancel before driver reports anything, but after it was pushed to driver
     */
    drv1.push_frame_tx_fn = osw_drv_ut_push_frame_tx_nop_cb;
    assert(osw_mux_frame_tx_schedule("phy2", "vif3", frame_a_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == true);
    osw_drv_ut_work(&obs1);

    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == true);
    osw_drv_frame_tx_desc_cancel(frame_a_desc);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(frame_a.submitted_cnt == 1);
    assert(frame_a.failed_cnt == 1);
    assert(frame_a.dropped_cnt == 2);

}

OSW_UT(osw_drv_ut_frame_tx_no_drv_feedback_after_push)
{
    static struct osw_drv_ops drv1 = {
        .name = "drv1",
        .init_fn = osw_drv_ut_drv_init_1_cb,
        .get_phy_list_fn = osw_drv_ut_drv_get_phy_list_cb,
        .get_vif_list_fn = osw_drv_ut_drv_get_vif_list_cb,
        .get_sta_list_fn = osw_drv_ut_drv_get_sta_list_cb,
        .request_phy_state_fn = osw_drv_ut_drv_request_phy_state_cb,
        .request_vif_state_fn = osw_drv_ut_drv_request_vif_state_cb,
        .request_sta_state_fn = osw_drv_ut_drv_request_sta_state_cb,
        .request_config_fn = osw_drv_ut_drv_request_config_cb,
        .push_frame_tx_fn = osw_drv_ut_push_frame_tx_nop_cb,
    };

    static struct osw_drv_ut_state_obs obs1 = {
        .busy = false,
        .obs = {
            .idle_fn = osw_drv_ut_state_idle_cb,
            .busy_fn = osw_drv_ut_state_busy_cb,
            .phy_added_fn = osw_drv_ut_core1_op_phy_added_cb,
            .phy_removed_fn = osw_drv_ut_core1_op_phy_removed_cb,
            .phy_changed_fn = osw_drv_ut_core1_op_phy_changed_cb,
            .vif_added_fn = osw_drv_ut_core1_op_vif_added_cb,
            .vif_removed_fn = osw_drv_ut_core1_op_vif_removed_cb,
            .vif_changed_fn = osw_drv_ut_core1_op_vif_changed_cb,
            .name = "drv ut 1 state",
        },
    };

    /* Use only VIF3@PHY2 */
    const uint8_t raw_frame[] = { 0xA, 0xB, 0xC, };
    struct osw_drv_ut_frame_tx frame_a = { 0, 0, 0, };
    struct osw_drv_frame_tx_desc *frame_a_desc = osw_drv_frame_tx_desc_new(osw_drv_ut_frame_tx_result_cb, &frame_a);

    osw_time_set_mono_clk(0);
    osw_drv_init();
    osw_drv_register_ops(&drv1);
    osw_state_register_observer(&obs1.obs);
    osw_drv_ut_work(&obs1);

    /*
     * Setup frame
     */
    osw_drv_frame_tx_desc_set_frame(frame_a_desc, raw_frame, sizeof(raw_frame));

    /*
     * Schedule frame
     */
    assert(osw_mux_frame_tx_schedule("phy2", "vif3", frame_a_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == true);
    osw_drv_ut_work(&obs1);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == true);
    osw_ut_time_advance(OSW_TIME_SEC(OSW_DRV_TX_TIMEOUT_SECONDS + 1.0));
    osw_drv_ut_work(&obs1);

    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(frame_a.submitted_cnt == 0);
    assert(frame_a.failed_cnt == 0);
    assert(frame_a.dropped_cnt == 1);
}

OSW_UT(osw_drv_ut_frame_tx_multiple_frames)
{
    static struct osw_drv_ops drv1 = {
        .name = "drv1",
        .init_fn = osw_drv_ut_drv_init_1_cb,
        .get_phy_list_fn = osw_drv_ut_drv_get_phy_list_cb,
        .get_vif_list_fn = osw_drv_ut_drv_get_vif_list_cb,
        .get_sta_list_fn = osw_drv_ut_drv_get_sta_list_cb,
        .request_phy_state_fn = osw_drv_ut_drv_request_phy_state_cb,
        .request_vif_state_fn = osw_drv_ut_drv_request_vif_state_cb,
        .request_sta_state_fn = osw_drv_ut_drv_request_sta_state_cb,
        .request_config_fn = osw_drv_ut_drv_request_config_cb,
        .push_frame_tx_fn = osw_drv_ut_push_frame_tx_nop_cb,
    };

    static struct osw_drv_ut_state_obs obs1 = {
        .busy = false,
        .obs = {
            .idle_fn = osw_drv_ut_state_idle_cb,
            .busy_fn = osw_drv_ut_state_busy_cb,
            .phy_added_fn = osw_drv_ut_core1_op_phy_added_cb,
            .phy_removed_fn = osw_drv_ut_core1_op_phy_removed_cb,
            .phy_changed_fn = osw_drv_ut_core1_op_phy_changed_cb,
            .vif_added_fn = osw_drv_ut_core1_op_vif_added_cb,
            .vif_removed_fn = osw_drv_ut_core1_op_vif_removed_cb,
            .vif_changed_fn = osw_drv_ut_core1_op_vif_changed_cb,
            .name = "drv ut 1 state",
        },
    };

    /* Use only VIF3@PHY2 */
    const uint8_t raw_frame[] = { 0xA, 0xB, 0xC, };
    struct osw_drv_ut_frame_tx frame_a = { 0, 0, 0, };
    struct osw_drv_ut_frame_tx frame_b = { 0, 0, 0, };
    struct osw_drv_ut_frame_tx frame_c = { 0, 0, 0, };
    struct osw_drv_frame_tx_desc *frame_a_desc = osw_drv_frame_tx_desc_new(osw_drv_ut_frame_tx_result_cb, &frame_a);
    struct osw_drv_frame_tx_desc *frame_b_desc = osw_drv_frame_tx_desc_new(osw_drv_ut_frame_tx_result_cb, &frame_b);
    struct osw_drv_frame_tx_desc *frame_c_desc = osw_drv_frame_tx_desc_new(osw_drv_ut_frame_tx_result_cb, &frame_c);

    osw_time_set_mono_clk(0);
    osw_drv_init();
    osw_drv_register_ops(&drv1);
    osw_state_register_observer(&obs1.obs);
    osw_drv_ut_work(&obs1);

    /*
     * Setup frame
     */
    osw_drv_frame_tx_desc_set_frame(frame_a_desc, raw_frame, sizeof(raw_frame));
    osw_drv_frame_tx_desc_set_frame(frame_b_desc, raw_frame, sizeof(raw_frame));
    osw_drv_frame_tx_desc_set_frame(frame_c_desc, raw_frame, sizeof(raw_frame));

    /*
     * Schedule frames
     */
    assert(osw_mux_frame_tx_schedule("phy2", "vif3", frame_a_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == true);
    assert(osw_mux_frame_tx_schedule("phy2", "vif3", frame_b_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_b_desc) == true);
    assert(osw_mux_frame_tx_schedule("phy2", "vif3", frame_c_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_c_desc) == true);

    osw_ut_time_advance(OSW_TIME_SEC(0));
    osw_drv_ut_work(&obs1);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_b_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_c_desc) == true);

    osw_ut_time_advance(OSW_TIME_SEC(OSW_DRV_TX_TIMEOUT_SECONDS + 1.0));
    osw_drv_ut_work(&obs1);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_b_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_c_desc) == true);
    assert(frame_a.submitted_cnt == 0);
    assert(frame_a.failed_cnt == 0);
    assert(frame_a.dropped_cnt == 1);

    osw_ut_time_advance(OSW_TIME_SEC(OSW_DRV_TX_TIMEOUT_SECONDS + 1.0));
    osw_drv_ut_work(&obs1);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_b_desc) == false);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_c_desc) == true);
    assert(frame_b.submitted_cnt == 0);
    assert(frame_b.failed_cnt == 0);
    assert(frame_b.dropped_cnt == 1);

    osw_ut_time_advance(OSW_TIME_SEC(OSW_DRV_TX_TIMEOUT_SECONDS + 1.0));
    osw_drv_ut_work(&obs1);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_b_desc) == false);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_c_desc) == false);
    assert(frame_c.submitted_cnt == 0);
    assert(frame_c.failed_cnt == 0);
    assert(frame_c.dropped_cnt == 1);
}

OSW_UT(osw_drv_ut_frame_tx_schedule_remove_vif)
{
    static struct osw_drv_ops drv1 = {
        .name = "drv1",
        .init_fn = osw_drv_ut_drv_init_1_cb,
        .get_phy_list_fn = osw_drv_ut_drv_get_phy_list_cb,
        .get_vif_list_fn = osw_drv_ut_drv_get_vif_list_cb,
        .get_sta_list_fn = osw_drv_ut_drv_get_sta_list_cb,
        .request_phy_state_fn = osw_drv_ut_drv_request_phy_state_cb,
        .request_vif_state_fn = osw_drv_ut_drv_request_vif_state_cb,
        .request_sta_state_fn = osw_drv_ut_drv_request_sta_state_cb,
        .request_config_fn = osw_drv_ut_drv_request_config_cb,
        .push_frame_tx_fn = osw_drv_ut_push_frame_tx_submit_cb,
    };

    static struct osw_drv_ut_state_obs obs1 = {
        .busy = false,
        .obs = {
            .idle_fn = osw_drv_ut_state_idle_cb,
            .busy_fn = osw_drv_ut_state_busy_cb,
            .phy_added_fn = osw_drv_ut_core1_op_phy_added_cb,
            .phy_removed_fn = osw_drv_ut_core1_op_phy_removed_cb,
            .phy_changed_fn = osw_drv_ut_core1_op_phy_changed_cb,
            .vif_added_fn = osw_drv_ut_core1_op_vif_added_cb,
            .vif_removed_fn = osw_drv_ut_core1_op_vif_removed_cb,
            .vif_changed_fn = osw_drv_ut_core1_op_vif_changed_cb,
            .name = "drv ut 1 state",
        },
    };

    /* Use only VIF3@PHY2 */
    const uint8_t raw_frame[] = { 0xA, 0xB, 0xC, };
    struct osw_drv_ut_frame_tx frame_a = { 0, 0, 0, };
    struct osw_drv_frame_tx_desc *frame_a_desc = osw_drv_frame_tx_desc_new(osw_drv_ut_frame_tx_result_cb, &frame_a);

    osw_time_set_mono_clk(0);
    osw_drv_init();
    osw_drv_register_ops(&drv1);
    osw_state_register_observer(&obs1.obs);
    osw_drv_ut_work(&obs1);

    /*
     * Setup frame
     */
    osw_drv_frame_tx_desc_set_frame(frame_a_desc, raw_frame, sizeof(raw_frame));

    /*
     * Schedule frame & remove vif1
     */
    drv1.push_frame_tx_fn = osw_drv_ut_push_frame_tx_submit_cb;
    assert(osw_mux_frame_tx_schedule("phy2", "vif3", frame_a_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == true);
    drv1_priv[1].vif_names[0] = "";
    osw_drv_report_vif_changed(drv1_priv[0].drv, "phy2", "vif3");
    osw_drv_ut_work(&obs1);

    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(frame_a.submitted_cnt == 0);
    assert(frame_a.failed_cnt == 0);
    assert(frame_a.dropped_cnt == 1);
}

OSW_UT(osw_drv_ut_frame_tx_schedule_remove_phy)
{
    static struct osw_drv_ops drv1 = {
        .name = "drv1",
        .init_fn = osw_drv_ut_drv_init_1_cb,
        .get_phy_list_fn = osw_drv_ut_drv_get_phy_list_cb,
        .get_vif_list_fn = osw_drv_ut_drv_get_vif_list_cb,
        .get_sta_list_fn = osw_drv_ut_drv_get_sta_list_cb,
        .request_phy_state_fn = osw_drv_ut_drv_request_phy_state_cb,
        .request_vif_state_fn = osw_drv_ut_drv_request_vif_state_cb,
        .request_sta_state_fn = osw_drv_ut_drv_request_sta_state_cb,
        .request_config_fn = osw_drv_ut_drv_request_config_cb,
        .push_frame_tx_fn = osw_drv_ut_push_frame_tx_submit_cb,
    };

    static struct osw_drv_ut_state_obs obs1 = {
        .busy = false,
        .obs = {
            .idle_fn = osw_drv_ut_state_idle_cb,
            .busy_fn = osw_drv_ut_state_busy_cb,
            .phy_added_fn = osw_drv_ut_core1_op_phy_added_cb,
            .phy_removed_fn = osw_drv_ut_core1_op_phy_removed_cb,
            .phy_changed_fn = osw_drv_ut_core1_op_phy_changed_cb,
            .vif_added_fn = osw_drv_ut_core1_op_vif_added_cb,
            .vif_removed_fn = osw_drv_ut_core1_op_vif_removed_cb,
            .vif_changed_fn = osw_drv_ut_core1_op_vif_changed_cb,
            .name = "drv ut 1 state",
        },
    };

    /* Use only VIF3@PHY2 */
    const uint8_t raw_frame[] = { 0xA, 0xB, 0xC, };
    struct osw_drv_ut_frame_tx frame_a = { 0, 0, 0, };
    struct osw_drv_frame_tx_desc *frame_a_desc = osw_drv_frame_tx_desc_new(osw_drv_ut_frame_tx_result_cb, &frame_a);

    osw_time_set_mono_clk(0);
    osw_drv_init();
    osw_drv_register_ops(&drv1);
    osw_state_register_observer(&obs1.obs);
    osw_drv_ut_work(&obs1);

    /*
     * Setup frame
     */
    osw_drv_frame_tx_desc_set_frame(frame_a_desc, raw_frame, sizeof(raw_frame));

    /*
     * Schedule frame & remove phy1
     */
    drv1.push_frame_tx_fn = osw_drv_ut_push_frame_tx_submit_cb;
    assert(osw_mux_frame_tx_schedule("phy2", "vif3", frame_a_desc) == true);
    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == true);
    drv1_priv[1].phy_name = "";
    osw_drv_report_phy_changed(drv1_priv[0].drv, "phy2");
    osw_drv_ut_work(&obs1);

    assert(osw_drv_frame_tx_desc_is_scheduled(frame_a_desc) == false);
    assert(frame_a.submitted_cnt == 0);
    assert(frame_a.failed_cnt == 0);
    assert(frame_a.dropped_cnt == 1);
}

OSW_UT(osw_drv_ut_csa_to_phy)
{
    struct osw_channel_state cs1[] = {
        { .channel = { .control_freq_mhz = 2412 } },
        { .channel = { .control_freq_mhz = 2417 } },
    };
    struct osw_channel_state cs2[] = {
        { .channel = { .control_freq_mhz = 5180 } },
        { .channel = { .control_freq_mhz = 5200 } },
    };
    struct osw_drv_phy p1 = {
        .phy_name = "phy1",
        .cur_state = {
            .channel_states = cs1,
            .n_channel_states = ARRAY_SIZE(cs1),
        },
    };
    struct osw_drv_phy p2 = {
        .phy_name = "phy2",
        .cur_state = {
            .channel_states = cs2,
            .n_channel_states = ARRAY_SIZE(cs2),
        },
    };
    struct ds_tree drvs;
    struct osw_drv d1 = {0};
    struct osw_drv_vif v1 = {
        .vif_name = "vif1",
        .cur_state = { .vif_type = OSW_VIF_STA },
        .phy = &p1,
    };
    struct osw_drv_vif v2 = {
        .vif_name = "vif2",
        .cur_state = { .vif_type = OSW_VIF_STA },
        .phy = &p2,
    };
    const struct osw_channel c2412 = { .control_freq_mhz = 2412 };
    const struct osw_channel c2417 = { .control_freq_mhz = 2417 };
    const struct osw_channel c2437 = { .control_freq_mhz = 2437 };
    const struct osw_channel c5180 = { .control_freq_mhz = 5180 };
    const struct osw_channel c5200 = { .control_freq_mhz = 5200 };
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;

    ds_tree_init(&drvs, ds_void_cmp, struct osw_drv, node);
    ds_tree_init(&d1.phy_tree, ds_str_cmp, struct osw_drv_phy, node);
    ds_tree_init(&p1.vif_tree, ds_str_cmp, struct osw_drv_vif, node);
    ds_tree_init(&p2.vif_tree, ds_str_cmp, struct osw_drv_vif, node);
    ds_tree_insert(&drvs, &d1, &d1);
    ds_tree_insert(&d1.phy_tree, &p1, p1.phy_name);
    ds_tree_insert(&d1.phy_tree, &p2, p2.phy_name);
    ds_tree_insert(&p1.vif_tree, &v1, v1.vif_name);
    ds_tree_insert(&p2.vif_tree, &v2, v2.vif_name);


    OSW_UT_EVAL(osw_drv_phy_supports_channel(&p1, &c2412) == true);
    OSW_UT_EVAL(osw_drv_phy_supports_channel(&p1, &c2417) == true);
    OSW_UT_EVAL(osw_drv_phy_supports_channel(&p1, &c2437) == false);
    OSW_UT_EVAL(osw_drv_phy_supports_channel(&p1, &c5180) == false);
    OSW_UT_EVAL(osw_drv_phy_supports_channel(&p1, &c5200) == false);

    OSW_UT_EVAL(osw_drv_phy_supports_channel(&p2, &c2412) == false);
    OSW_UT_EVAL(osw_drv_phy_supports_channel(&p2, &c2417) == false);
    OSW_UT_EVAL(osw_drv_phy_supports_channel(&p2, &c2437) == false);
    OSW_UT_EVAL(osw_drv_phy_supports_channel(&p2, &c5180) == true);
    OSW_UT_EVAL(osw_drv_phy_supports_channel(&p2, &c5200) == true);

    OSW_UT_EVAL(osw_drv_phy_lookup_for_channel(&drvs, &c2412) == &p1);
    OSW_UT_EVAL(osw_drv_phy_lookup_for_channel(&drvs, &c2417) == &p1);
    OSW_UT_EVAL(osw_drv_phy_lookup_for_channel(&drvs, &c5180) == &p2);
    OSW_UT_EVAL(osw_drv_phy_lookup_for_channel(&drvs, &c5200) == &p2);

    OSW_UT_EVAL(osw_drv_phy_lookup_for_channel(&drvs, &c2437) == NULL);

    osw_drv_report_vif_channel_change_advertised_xphy__(&drvs, &d1, p1.phy_name, v1.vif_name, &c2412, &phy, &vif);
    OSW_UT_EVAL(phy == &p1);

    osw_drv_report_vif_channel_change_advertised_xphy__(&drvs, &d1, p1.phy_name, v1.vif_name, &c5180, &phy, &vif);
    OSW_UT_EVAL(phy == &p2);

    osw_drv_report_vif_channel_change_advertised_xphy__(&drvs, &d1, p1.phy_name, v1.vif_name, &c2437, &phy, &vif);
    OSW_UT_EVAL(phy == NULL);

    osw_drv_report_vif_channel_change_advertised_xphy__(&drvs, &d1, p2.phy_name, v2.vif_name, &c5200, &phy, &vif);
    OSW_UT_EVAL(phy == &p2);

    osw_drv_report_vif_channel_change_advertised_xphy__(&drvs, &d1, p2.phy_name, v2.vif_name, &c2412, &phy, &vif);
    OSW_UT_EVAL(phy == &p1);
}
