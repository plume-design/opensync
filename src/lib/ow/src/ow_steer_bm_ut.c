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

#include <osw_ut.h>

struct ow_steer_bm_ut_mem_attr {
    bool valid;
    int *next;
    int *cur;
};

struct ow_steer_bm_ut_ptr_attr {
    bool valid;
    const int *next;
    const int *cur;
};

struct ow_steer_bm_ut_object {
    struct ow_steer_bm_ut_mem_attr mem_attr;
    struct ow_steer_bm_ut_ptr_attr ptr_attr;
};

#define OW_STEER_BM_UT_CALL_STATE_OBS(fn, ...) \
    do { \
        struct osw_state_observer *i; \
        ds_dlist_foreach(&g_osw_state_observer_list, i) \
            if (i->fn != NULL) \
                i->fn(i, ## __VA_ARGS__); \
    } while (0)

typedef int
ow_steer_bm_ut_cmp_fn_t(const void *,
                        const void *);

static size_t
ow_steer_bm_ut_ds_dlist_count_all(struct ds_dlist *list,
                                  const void *data,
                                  ow_steer_bm_ut_cmp_fn_t *cmp_fn)
{
    ASSERT(list != NULL, "");
    ASSERT(data != NULL, "");
    ASSERT(cmp_fn != NULL, "");

    void *element;
    size_t n = 0;

    for (element = ds_dlist_head(list); element != NULL; element = ds_dlist_next(list, element))
        if (cmp_fn(element, data) == 0)
            n++;

    return n;
}

static size_t
ow_steer_bm_ut_ds_tree_count_all(struct ds_tree *tree,
                                  const void *data,
                                  ow_steer_bm_ut_cmp_fn_t *cmp_fn)
{
    ASSERT(tree != NULL, "");
    ASSERT(data != NULL, "");
    ASSERT(cmp_fn != NULL, "");

    void *element;
    size_t n = 0;

    for (element = ds_tree_head(tree); element != NULL; element = ds_tree_next(tree, element))
        if (cmp_fn(element, data) == 0)
            n++;

    return n;
}

OSW_UT(ow_steer_bm_attr_helpers)
{
    struct ow_steer_bm_ut_object object_buf;
    struct ow_steer_bm_ut_object *object = &object_buf;
    memset(object, 0, sizeof(*object));

    /*
     *  Memory (dynamically allocated) attribute
     */
    {
        /* Initial value */
        OW_STEER_BM_MEM_ATTR_UPDATE(object, mem_attr);

        OSW_UT_EVAL(mem_attr_state.changed == false);
        OSW_UT_EVAL(mem_attr_state.present == false);
        OSW_UT_EVAL(object->mem_attr.valid == true);
        OSW_UT_EVAL(object->mem_attr.next == NULL);
        OSW_UT_EVAL(object->mem_attr.cur == NULL);
    }

    {
        /* Set to 5 */
        object->mem_attr.valid = false;
        FREE(object->mem_attr.next);
        object->mem_attr.next = CALLOC(1, sizeof(*object->mem_attr.next));
        *object->mem_attr.next = 5;

        OW_STEER_BM_MEM_ATTR_UPDATE(object, mem_attr);

        OSW_UT_EVAL(mem_attr_state.changed == true);
        OSW_UT_EVAL(mem_attr_state.present == true);
        OSW_UT_EVAL(object->mem_attr.valid == true);
        OSW_UT_EVAL(object->mem_attr.next != NULL);
        OSW_UT_EVAL(*object->mem_attr.next == 5);
        OSW_UT_EVAL(object->mem_attr.cur != NULL);
        OSW_UT_EVAL(*object->mem_attr.cur == 5);
    }

    {
        /* Set to 13 */
        object->mem_attr.valid = false;
        FREE(object->mem_attr.next);
        object->mem_attr.next = CALLOC(1, sizeof(*object->mem_attr.next));
        *object->mem_attr.next = 13;

        OW_STEER_BM_MEM_ATTR_UPDATE(object, mem_attr);

        OSW_UT_EVAL(mem_attr_state.changed == true);
        OSW_UT_EVAL(mem_attr_state.present == true);
        OSW_UT_EVAL(object->mem_attr.valid == true);
        OSW_UT_EVAL(object->mem_attr.next != NULL);
        OSW_UT_EVAL(*object->mem_attr.next == 13);
        OSW_UT_EVAL(object->mem_attr.cur != NULL);
        OSW_UT_EVAL(*object->mem_attr.cur == 13);
    }

    {
        /* Set to 13 again */
        object->mem_attr.valid = false;
        FREE(object->mem_attr.next);
        object->mem_attr.next = CALLOC(1, sizeof(*object->mem_attr.next));
        *object->mem_attr.next = 13;

        OW_STEER_BM_MEM_ATTR_UPDATE(object, mem_attr);

        OSW_UT_EVAL(mem_attr_state.changed == false);
        OSW_UT_EVAL(mem_attr_state.present == true);
        OSW_UT_EVAL(object->mem_attr.valid == true);
        OSW_UT_EVAL(object->mem_attr.next != NULL);
        OSW_UT_EVAL(*object->mem_attr.next == 13);
        OSW_UT_EVAL(object->mem_attr.cur != NULL);
        OSW_UT_EVAL(*object->mem_attr.cur == 13);
    }

    {
        /* Set to NULL */
        object->mem_attr.valid = false;
        FREE(object->mem_attr.next);
        object->mem_attr.next = NULL;

        OW_STEER_BM_MEM_ATTR_UPDATE(object, mem_attr);

        OSW_UT_EVAL(mem_attr_state.changed == true);
        OSW_UT_EVAL(mem_attr_state.present == false);
        OSW_UT_EVAL(object->mem_attr.valid == true);
        OSW_UT_EVAL(object->mem_attr.next == NULL);
        OSW_UT_EVAL(object->mem_attr.cur == NULL);
    }

    /*
     *  Pointer attribute
     */
    {
        /* Initial value */
        OW_STEER_BM_PTR_ATTR_UPDATE(object, ptr_attr);

        OSW_UT_EVAL(ptr_attr_state.changed == false);
        OSW_UT_EVAL(ptr_attr_state.present == false);
        OSW_UT_EVAL(object->ptr_attr.valid == true);
        OSW_UT_EVAL(object->ptr_attr.next == NULL);
        OSW_UT_EVAL(object->ptr_attr.cur == NULL);
    }

    {
        /* Set to 0xAAAA */
        object->ptr_attr.valid = false;
        object->ptr_attr.next = (const int*) 0xAAAA;

        OW_STEER_BM_PTR_ATTR_UPDATE(object, ptr_attr);

        OSW_UT_EVAL(ptr_attr_state.changed == true);
        OSW_UT_EVAL(ptr_attr_state.present == true);
        OSW_UT_EVAL(object->ptr_attr.valid == true);
        OSW_UT_EVAL(object->ptr_attr.next == (const int*) 0xAAAA);
        OSW_UT_EVAL(object->ptr_attr.cur == (const int*) 0xAAAA);
    }

    {
        /* Set to 0xBBBB */
        object->ptr_attr.valid = false;
        object->ptr_attr.next = (const int*) 0xBBBB;

        OW_STEER_BM_PTR_ATTR_UPDATE(object, ptr_attr);

        OSW_UT_EVAL(ptr_attr_state.changed == true);
        OSW_UT_EVAL(ptr_attr_state.present == true);
        OSW_UT_EVAL(object->ptr_attr.valid == true);
        OSW_UT_EVAL(object->ptr_attr.next == (const int*) 0xBBBB);
        OSW_UT_EVAL(object->ptr_attr.cur == (const int*) 0xBBBB);
    }

    {
        /* Set to 0xBBBB again */
        object->ptr_attr.valid = false;
        object->ptr_attr.next = (const int*) 0xBBBB;

        OW_STEER_BM_PTR_ATTR_UPDATE(object, ptr_attr);

        OSW_UT_EVAL(ptr_attr_state.changed == false);
        OSW_UT_EVAL(ptr_attr_state.present == true);
        OSW_UT_EVAL(object->ptr_attr.valid == true);
        OSW_UT_EVAL(object->ptr_attr.next == (const int*) 0xBBBB);
        OSW_UT_EVAL(object->ptr_attr.cur == (const int*) 0xBBBB);
    }

    {
        /* Set to NULL */
        object->ptr_attr.valid = false;
        object->ptr_attr.next = NULL;

        OW_STEER_BM_PTR_ATTR_UPDATE(object, ptr_attr);

        OSW_UT_EVAL(ptr_attr_state.changed == true);
        OSW_UT_EVAL(ptr_attr_state.present == false);
        OSW_UT_EVAL(object->ptr_attr.valid == true);
        OSW_UT_EVAL(object->ptr_attr.next == NULL);
        OSW_UT_EVAL(object->ptr_attr.cur == NULL);
    }

    FREE(object->mem_attr.next);
    FREE(object->mem_attr.cur);
}

OSW_UT(ow_steer_bm_case1)
{
    /*
     * Scenario:
     * - add group
     * - add VIF
     * - add neighbor
     * - VIF goes up
     * - configure neighbor
     * - remove VIF
     */
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();

    extern struct ds_dlist g_osw_state_observer_list;

    const char *group_a_name = "group_a";
    const struct osw_ifname vif_aa_name = { .buf = { "vif_aa" } };
    const struct osw_hwaddr vif_aa_name_addr = { .octet = { 0x0,  0x0, 0x0,  0x0, 0x0A, 0x0A } };
    const struct osw_hwaddr neighbor_aaa_bssid = { .octet = { 0x0,  0x0, 0x0, 0x0A, 0x0A, 0x0A } };

    struct ow_steer_bm_group *group_a = NULL;
    struct ow_steer_bm_vif *vif_aa = NULL;
    struct ow_steer_bm_neighbor *neighbor_aaa = NULL;

    /* Group "a" is added */
    group_a = ow_steer_bm_get_group(group_a_name);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 0);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 0);

    /* VIF "vif_aa" is added */
    vif_aa = ow_steer_bm_group_get_vif(group_a, vif_aa_name.buf);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_vif_is_ready(vif_aa) == false);
    OSW_UT_EVAL(ow_steer_bm_vif_is_up(vif_aa) == false);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 0);

    /* Neighbor "neighbor_aaa" is added */
    neighbor_aaa = ow_steer_bm_get_neighbor(neighbor_aaa_bssid.octet);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_neighbor_is_ready(neighbor_aaa) == false);
    OSW_UT_EVAL(ow_steer_bm_neighbor_is_up(neighbor_aaa) == false);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 0);

    /* VIF "vif_aa" go up */
    struct osw_drv_vif_state vif_aa_drv_state = {
        .mac_addr = vif_aa_name_addr,
        .vif_type = OSW_VIF_AP,
        .u.ap = {
            .channel = {
                .control_freq_mhz = 2412,
                .width = OSW_CHANNEL_20MHZ,
            },
        },
    };
    struct osw_state_vif_info vif_aa_info = {
        .vif_name = vif_aa_name.buf,
        .drv_state = &vif_aa_drv_state,
    };
    OW_STEER_BM_UT_CALL_STATE_OBS(vif_added_fn, &vif_aa_info);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_vif_is_ready(vif_aa) == true);
    OSW_UT_EVAL(ow_steer_bm_vif_is_up(vif_aa) == true);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    /* Neighbor "neighbor_aaa" go up */
    ow_steer_bm_neighbor_set_vif_name(neighbor_aaa, vif_aa_name.buf);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_neighbor_is_ready(neighbor_aaa) == false);
    OSW_UT_EVAL(ow_steer_bm_neighbor_is_up(neighbor_aaa) == false);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    const uint8_t neighbor_aaa_channel = 1;
    ow_steer_bm_neighbor_set_channel_number(neighbor_aaa, &neighbor_aaa_channel);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_neighbor_is_ready(neighbor_aaa) == false);
    OSW_UT_EVAL(ow_steer_bm_neighbor_is_up(neighbor_aaa) == false);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    const uint8_t neighbor_aaa_op_class = 83;
    ow_steer_bm_neighbor_set_op_class(neighbor_aaa, &neighbor_aaa_op_class);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_neighbor_is_ready(neighbor_aaa) == true);
    OSW_UT_EVAL(ow_steer_bm_neighbor_is_up(neighbor_aaa) == true);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 2);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 2);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    const enum ow_steer_bm_neighbor_ht_mode neighbor_aaa_ht_mode = OW_STEER_BM_NEIGHBOR_HT20;
    ow_steer_bm_neighbor_set_ht_mode(neighbor_aaa, &neighbor_aaa_ht_mode);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_neighbor_is_ready(neighbor_aaa) == true);
    OSW_UT_EVAL(ow_steer_bm_neighbor_is_up(neighbor_aaa) == true);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 2);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &neighbor_aaa_bssid, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 2);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &neighbor_aaa_bssid, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    /* VIF "vif_aa" is removed */
    ow_steer_bm_vif_unset(vif_aa);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_neighbor_is_ready(neighbor_aaa) == false);
    OSW_UT_EVAL(ow_steer_bm_neighbor_is_up(neighbor_aaa) == false);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 0);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 0);
}

OSW_UT(ow_steer_bm_case2)
{
    /*
     * Scenario:
     * - add group
     * - add VIF
     * - add neighbor
     * - VIF goes up
     * - configure neighbor
     * - remove group
     */
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();

    extern struct ds_dlist g_osw_state_observer_list;

    const char *group_a_name = "group_a";
    const struct osw_ifname vif_aa_name = { .buf = { "vif_aa" } };
    const struct osw_hwaddr vif_aa_name_addr = { .octet = { 0x0,  0x0, 0x0,  0x0, 0x0A, 0x0A } };
    const struct osw_hwaddr neighbor_aaa_bssid = { .octet = { 0x0,  0x0, 0x0, 0x0A, 0x0A, 0x0A } };

    struct ow_steer_bm_group *group_a = NULL;
    struct ow_steer_bm_vif *vif_aa = NULL;
    struct ow_steer_bm_neighbor *neighbor_aaa = NULL;

    /* Group "a" is added */
    group_a = ow_steer_bm_get_group(group_a_name);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 0);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 0);

    /* VIF "vif_aa" is added */
    vif_aa = ow_steer_bm_group_get_vif(group_a, vif_aa_name.buf);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_vif_is_ready(vif_aa) == false);
    OSW_UT_EVAL(ow_steer_bm_vif_is_up(vif_aa) == false);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 0);

    /* Neighbor "neighbor_aaa" is added */
    neighbor_aaa = ow_steer_bm_get_neighbor(neighbor_aaa_bssid.octet);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_neighbor_is_ready(neighbor_aaa) == false);
    OSW_UT_EVAL(ow_steer_bm_neighbor_is_up(neighbor_aaa) == false);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 0);

    /* VIF "vif_aa" go up */
    struct osw_drv_vif_state vif_aa_drv_state = {
        .mac_addr = vif_aa_name_addr,
        .vif_type = OSW_VIF_AP,
        .u.ap = {
            .channel = {
                .control_freq_mhz = 2412,
                .width = OSW_CHANNEL_20MHZ,
            },
        },
    };
    struct osw_state_vif_info vif_aa_info = {
        .vif_name = vif_aa_name.buf,
        .drv_state = &vif_aa_drv_state,
    };
    OW_STEER_BM_UT_CALL_STATE_OBS(vif_added_fn, &vif_aa_info);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_vif_is_ready(vif_aa) == true);
    OSW_UT_EVAL(ow_steer_bm_vif_is_up(vif_aa) == true);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    /* Neighbor "neighbor_aaa" go up */
    ow_steer_bm_neighbor_set_vif_name(neighbor_aaa, vif_aa_name.buf);
    const uint8_t neighbor_aaa_channel = 1;
    ow_steer_bm_neighbor_set_channel_number(neighbor_aaa, &neighbor_aaa_channel);
    const uint8_t neighbor_aaa_op_class = 83;
    ow_steer_bm_neighbor_set_op_class(neighbor_aaa, &neighbor_aaa_op_class);
    const enum ow_steer_bm_neighbor_ht_mode neighbor_aaa_ht_mode = OW_STEER_BM_NEIGHBOR_HT20;
    ow_steer_bm_neighbor_set_ht_mode(neighbor_aaa, &neighbor_aaa_ht_mode);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_neighbor_is_ready(neighbor_aaa) == true);
    OSW_UT_EVAL(ow_steer_bm_neighbor_is_up(neighbor_aaa) == true);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 2);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &neighbor_aaa_bssid, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 2);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &neighbor_aaa_bssid, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    /* Group "group_a" is removed */
    ow_steer_bm_group_unset(group_a);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_neighbor_is_ready(neighbor_aaa) == false);
    OSW_UT_EVAL(ow_steer_bm_neighbor_is_up(neighbor_aaa) == false);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 0);
}

OSW_UT(ow_steer_bm_case3)
{
    /*
     * Scenario:
     * - add group
     * - add VIF
     * - add neighbor
     * - VIF goes up
     * - configure neighbor
     * - VIF goes down
     */
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();

    extern struct ds_dlist g_osw_state_observer_list;

    const char *group_a_name = "group_a";
    const struct osw_ifname vif_aa_name = { .buf = { "vif_aa" } };
    const struct osw_hwaddr vif_aa_name_addr = { .octet = { 0x0,  0x0, 0x0,  0x0, 0x0A, 0x0A } };
    const struct osw_hwaddr neighbor_aaa_bssid = { .octet = { 0x0,  0x0, 0x0, 0x0A, 0x0A, 0x0A } };

    struct ow_steer_bm_group *group_a = NULL;
    struct ow_steer_bm_vif *vif_aa = NULL;
    struct ow_steer_bm_neighbor *neighbor_aaa = NULL;

    /* Group "a" is added */
    group_a = ow_steer_bm_get_group(group_a_name);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 0);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 0);

    /* VIF "vif_aa" is added */
    vif_aa = ow_steer_bm_group_get_vif(group_a, vif_aa_name.buf);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_vif_is_ready(vif_aa) == false);
    OSW_UT_EVAL(ow_steer_bm_vif_is_up(vif_aa) == false);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 0);

    /* Neighbor "neighbor_aaa" is added */
    neighbor_aaa = ow_steer_bm_get_neighbor(neighbor_aaa_bssid.octet);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_neighbor_is_ready(neighbor_aaa) == false);
    OSW_UT_EVAL(ow_steer_bm_neighbor_is_up(neighbor_aaa) == false);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 0);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 0);

    /* VIF "vif_aa" go up */
    struct osw_drv_vif_state vif_aa_drv_state = {
        .mac_addr = vif_aa_name_addr,
        .vif_type = OSW_VIF_AP,
        .u.ap = {
            .channel = {
                .control_freq_mhz = 2412,
                .width = OSW_CHANNEL_20MHZ,
            },
        },
    };
    struct osw_state_vif_info vif_aa_info = {
        .vif_name = vif_aa_name.buf,
        .drv_state = &vif_aa_drv_state,
    };
    OW_STEER_BM_UT_CALL_STATE_OBS(vif_added_fn, &vif_aa_info);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_vif_is_ready(vif_aa) == true);
    OSW_UT_EVAL(ow_steer_bm_vif_is_up(vif_aa) == true);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    /* Neighbor "neighbor_aaa" go up */
    ow_steer_bm_neighbor_set_vif_name(neighbor_aaa, vif_aa_name.buf);
    const uint8_t neighbor_aaa_channel = 1;
    ow_steer_bm_neighbor_set_channel_number(neighbor_aaa, &neighbor_aaa_channel);
    const uint8_t neighbor_aaa_op_class = 83;
    ow_steer_bm_neighbor_set_op_class(neighbor_aaa, &neighbor_aaa_op_class);
    const enum ow_steer_bm_neighbor_ht_mode neighbor_aaa_ht_mode = OW_STEER_BM_NEIGHBOR_HT20;
    ow_steer_bm_neighbor_set_ht_mode(neighbor_aaa, &neighbor_aaa_ht_mode);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_neighbor_is_ready(neighbor_aaa) == true);
    OSW_UT_EVAL(ow_steer_bm_neighbor_is_up(neighbor_aaa) == true);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 2);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &neighbor_aaa_bssid, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 2);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &vif_aa_name_addr, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &neighbor_aaa_bssid, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    /* VIF "vif_aa" goes down */
    OW_STEER_BM_UT_CALL_STATE_OBS(vif_removed_fn, &vif_aa_info);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    OSW_UT_EVAL(ow_steer_bm_vif_is_ready(vif_aa) == false);
    OSW_UT_EVAL(ow_steer_bm_vif_is_up(vif_aa) == false);

    OSW_UT_EVAL(ds_tree_len(&group_a->vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&group_a->vif_tree, &vif_aa_name) == vif_aa);
    OSW_UT_EVAL(ds_tree_len(&group_a->bss_tree) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_tree_count_all(&group_a->bss_tree, &neighbor_aaa_bssid, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);

    OSW_UT_EVAL(ds_tree_len(&g_group_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_group_tree, group_a_name) == group_a);

    OSW_UT_EVAL(ds_tree_len(&g_vif_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_vif_tree, &vif_aa_name) == vif_aa);

    OSW_UT_EVAL(ds_tree_len(&g_neighbor_tree) == 1);
    OSW_UT_EVAL(ds_tree_find(&g_neighbor_tree, &neighbor_aaa_bssid) == neighbor_aaa);

    OSW_UT_EVAL(ds_tree_len(&g_client_tree) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_sta_list) == 0);

    OSW_UT_EVAL(ds_dlist_len(&g_bss_list) == 1);
    OSW_UT_EVAL(ow_steer_bm_ut_ds_dlist_count_all(&g_bss_list, &neighbor_aaa_bssid, (ow_steer_bm_ut_cmp_fn_t*) osw_hwaddr_cmp) == 1);
}

OSW_UT(ow_steer_bm_stats_probe)
{
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();
    osw_timer_disarm(&g_stats_timer);

    const struct osw_hwaddr sta_addr = { .octet = { 0xBA, 0xDC, 0x0D, 0xEB, 0xAD, 0xC0 }, };
    const struct osw_hwaddr bssid = { .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D }, };
    const struct osw_ssid ssid = { .buf = "ssid_0", .len = strlen("ssid_0") };
    const struct osw_ssid empty_ssid = { .buf = "", .len = strlen("") };
    const char *group_id = "group_0";
    const char *vif_name = "vif_0";
    const void *placeholder_invalid_ptr = (void *) 0xB0BAF00D;
    struct ow_steer_bm_group *group = ow_steer_bm_get_group(group_id);
    OSW_UT_EVAL(group != NULL);
    struct ow_steer_bm_vif *vif = ow_steer_bm_group_get_vif(group, vif_name);
    OSW_UT_EVAL(vif != NULL);
    struct ow_steer_bm_client *client = ow_steer_bm_get_client(sta_addr.octet);
    OSW_UT_EVAL(client != NULL);
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif_info = {
        .vif_name = vif_name,
        .drv_state = &drv_vif_state,
    };
    ow_steer_bm_vif_set_vif_info(vif, &vif_info);
    struct ow_steer_bm_sta *bm_sta;
    ds_dlist_foreach(&g_sta_list, bm_sta) {
        if (memcmp(&bm_sta->addr, &sta_addr, OSW_HWADDR_LEN) == 0) break;
    }
    OSW_UT_EVAL(bm_sta != NULL);

    /* bcast */
    struct osw_drv_report_vif_probe_req probe_req = {
        .sta_addr = sta_addr,
        .ssid = empty_ssid
    };
    ow_steer_bm_state_obs_vif_probe_cb((struct osw_state_observer *)placeholder_invalid_ptr,
                                       &vif_info,
                                       &probe_req);

    struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 1);
    OSW_UT_EVAL(client_vif_stats->probe_bcast_cnt == 1);

    struct ow_steer_bm_event_stats *client_event_stats = &client_vif_stats->event_stats[0];
    OSW_UT_EVAL(client_event_stats->type == PROBE);
    OSW_UT_EVAL(client_event_stats->probe_bcast == true);

    /* directed */
    probe_req.ssid = ssid;
    ow_steer_bm_state_obs_vif_probe_cb((struct osw_state_observer *)placeholder_invalid_ptr,
                                       &vif_info,
                                       &probe_req);

    client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 2);
    OSW_UT_EVAL(client_vif_stats->probe_bcast_cnt == 1);

    client_event_stats = &client_vif_stats->event_stats[1];
    OSW_UT_EVAL(client_event_stats->type == PROBE);
    OSW_UT_EVAL(client_event_stats->probe_bcast == false);
}

OSW_UT(ow_steer_bm_stats_connect)
{
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();
    osw_timer_disarm(&g_stats_timer);

    const struct osw_hwaddr sta_addr = { .octet = { 0xBA, 0xDC, 0x0D, 0xEB, 0xAD, 0xC0 }, };
    const struct osw_hwaddr bssid = { .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D }, };
    const char *group_id = "group_0";
    const char *phy_name = "phy_0";
    const char *vif_name = "vif_0";
    struct ow_steer_bm_group *group = ow_steer_bm_get_group(group_id);
    OSW_UT_EVAL(group != NULL);
    struct ow_steer_bm_vif *vif = ow_steer_bm_group_get_vif(group, vif_name);
    OSW_UT_EVAL(vif != NULL);
    struct ow_steer_bm_client *client = ow_steer_bm_get_client(sta_addr.octet);
    OSW_UT_EVAL(client != NULL);
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
        .vif_type = OSW_VIF_AP,
    };
    struct osw_state_phy_info phy_info = {
        .phy_name = phy_name,
    };
    struct osw_state_vif_info vif_info = {
        .vif_name = vif_name,
        .drv_state = &drv_vif_state,
        .phy = &phy_info,
    };
    ow_steer_bm_vif_set_vif_info(vif, &vif_info);
    struct osw_drv_sta_state drv_sta_state = { 0 };
    struct osw_state_sta_info sta_info = {
        .mac_addr = &sta_addr,
        .vif = &vif_info,
        .drv_state = &drv_sta_state,
        .connected_at = 1234,
        .assoc_req_ies = NULL,
        .assoc_req_ies_len = 0
    };
    struct ow_steer_bm_sta *bm_sta;
    ds_dlist_foreach(&g_sta_list, bm_sta) {
        if (memcmp(&bm_sta->addr, &sta_addr, OSW_HWADDR_LEN) == 0) break;
    }
    OSW_UT_EVAL(bm_sta != NULL);

    ow_steer_bm_sta_state_sta_connected_cb(&bm_sta->state_observer, &sta_info);

    struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    /* connected, activity, client_capabilties are generated together */
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 3);
    OSW_UT_EVAL(client_vif_stats->connected == true);
    OSW_UT_EVAL(client_vif_stats->connects == 1);

    struct ow_steer_bm_event_stats *client_event_stats = &client_vif_stats->event_stats[0];
    OSW_UT_EVAL(client_event_stats->type == CONNECT);
}

OSW_UT(ow_steer_bm_stats_connect_snr)
{
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();
    osw_timer_disarm(&g_stats_timer);

    const struct osw_hwaddr sta_addr = { .octet = { 0xBA, 0xDC, 0x0D, 0xEB, 0xAD, 0xC0 }, };
    const struct osw_hwaddr bssid = { .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D }, };
    const char *group_id = "group_0";
    const char *phy_name = "phy_0";
    const char *vif_name = "vif_0";
    struct ow_steer_bm_group *group = ow_steer_bm_get_group(group_id);
    OSW_UT_EVAL(group != NULL);
    struct ow_steer_bm_vif *vif = ow_steer_bm_group_get_vif(group, vif_name);
    OSW_UT_EVAL(vif != NULL);
    struct ow_steer_bm_client *client = ow_steer_bm_get_client(sta_addr.octet);
    OSW_UT_EVAL(client != NULL);
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
        .vif_type = OSW_VIF_AP,
    };
    struct osw_state_phy_info phy_info = {
        .phy_name = phy_name,
    };
    struct osw_state_vif_info vif_info = {
        .vif_name = vif_name,
        .drv_state = &drv_vif_state,
        .phy = &phy_info,
    };
    struct ow_steer_bm_bss bss = {
        .bssid = {
            .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D },
        },
    };
    ow_steer_bm_vif_set_vif_info(vif, &vif_info);
    vif->bss = &bss;
    struct osw_drv_sta_state drv_sta_state = { 0 };
    struct osw_state_sta_info sta_info = {
        .mac_addr = &sta_addr,
        .vif = &vif_info,
        .drv_state = &drv_sta_state,
        .connected_at = 1234,
        .assoc_req_ies = NULL,
        .assoc_req_ies_len = 0
    };
    struct ow_steer_bm_sta *bm_sta;
    ds_dlist_foreach(&g_sta_list, bm_sta) {
        if (memcmp(&bm_sta->addr, &sta_addr, OSW_HWADDR_LEN) == 0) break;
    }
    OSW_UT_EVAL(bm_sta != NULL);

    ow_steer_bm_sta_state_sta_connected_cb(&bm_sta->state_observer, &sta_info);

    struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    /* connected, activity, client_capabilties are generated together */
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 3);
    OSW_UT_EVAL(client_vif_stats->connected == true);
    OSW_UT_EVAL(client_vif_stats->connects == 1);

    struct ow_steer_bm_event_stats *client_event_stats = &client_vif_stats->event_stats[0];
    OSW_UT_EVAL(client_event_stats->type == CONNECT);
    /* Immediately after connect_cb SNR is not known */
    OSW_UT_EVAL(client_event_stats->rssi == 0);

    /* SNR in already registered CONNECT events is filled in
     * after receiving next SNR update from snr observer */
    ow_steer_bm_snr_obs_report_cb(NULL, &sta_addr, &bssid, 76);
    OSW_UT_EVAL(client_event_stats->rssi == 76);
}

OSW_UT(ow_steer_bm_stats_disconnect)
{
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();
    osw_timer_disarm(&g_stats_timer);

    const struct osw_hwaddr sta_addr = { .octet = { 0xBA, 0xDC, 0x0D, 0xEB, 0xAD, 0xC0 }, };
    const struct osw_hwaddr bssid = { .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D }, };
    const char *group_id = "group_0";
    const char *vif_name = "vif_0";
    struct ow_steer_bm_group *group = ow_steer_bm_get_group(group_id);
    OSW_UT_EVAL(group != NULL);
    struct ow_steer_bm_vif *vif = ow_steer_bm_group_get_vif(group, vif_name);
    OSW_UT_EVAL(vif != NULL);
    struct ow_steer_bm_client *client = ow_steer_bm_get_client(sta_addr.octet);
    OSW_UT_EVAL(client != NULL);
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif_info = {
        .vif_name = vif_name,
        .drv_state = &drv_vif_state,
    };
    ow_steer_bm_vif_set_vif_info(vif, &vif_info);
    struct osw_drv_sta_state drv_sta_state = { 0 };
    struct osw_state_sta_info sta_info = {
        .mac_addr = &sta_addr,
        .vif = &vif_info,
        .drv_state = &drv_sta_state,
        .connected_at = 1234,
        .assoc_req_ies = NULL,
        .assoc_req_ies_len = 0
    };
    struct ow_steer_bm_sta *bm_sta;
    ds_dlist_foreach(&g_sta_list, bm_sta) {
        if (memcmp(&bm_sta->addr, &sta_addr, OSW_HWADDR_LEN) == 0) break;
    }
    OSW_UT_EVAL(bm_sta != NULL);

    ow_steer_bm_sta_state_sta_disconnected_cb(&bm_sta->state_observer, &sta_info);

    struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 1);
    OSW_UT_EVAL(client_vif_stats->connected == false);
    OSW_UT_EVAL(client_vif_stats->disconnects == 1);

    struct ow_steer_bm_event_stats *client_event_stats = &client_vif_stats->event_stats[0];
    OSW_UT_EVAL(client_event_stats->type == DISCONNECT);
    OSW_UT_EVAL(client_event_stats->disconnect_src == LOCAL);
    OSW_UT_EVAL(client_event_stats->disconnect_type == DEAUTH);
    OSW_UT_EVAL(client_event_stats->disconnect_reason == 0);
}

OSW_UT(ow_steer_bm_stats_disconnect_snr)
{
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();
    osw_timer_disarm(&g_stats_timer);

    const struct osw_hwaddr sta_addr = { .octet = { 0xBA, 0xDC, 0x0D, 0xEB, 0xAD, 0xC0 }, };
    const struct osw_hwaddr bssid = { .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D }, };
    const char *group_id = "group_0";
    const char *vif_name = "vif_0";
    struct ow_steer_bm_group *group = ow_steer_bm_get_group(group_id);
    OSW_UT_EVAL(group != NULL);
    struct ow_steer_bm_vif *vif = ow_steer_bm_group_get_vif(group, vif_name);
    OSW_UT_EVAL(vif != NULL);
    struct ow_steer_bm_client *client = ow_steer_bm_get_client(sta_addr.octet);
    OSW_UT_EVAL(client != NULL);
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif_info = {
        .vif_name = vif_name,
        .drv_state = &drv_vif_state,
    };
    struct ow_steer_bm_bss bss = {
        .bssid = {
            .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D },
        },
    };
    ow_steer_bm_vif_set_vif_info(vif, &vif_info);
    vif->bss = &bss;
    struct osw_drv_sta_state drv_sta_state = { 0 };
    struct osw_state_sta_info sta_info = {
        .mac_addr = &sta_addr,
        .vif = &vif_info,
        .drv_state = &drv_sta_state,
        .connected_at = 1234,
        .assoc_req_ies = NULL,
        .assoc_req_ies_len = 0
    };
    struct ow_steer_bm_sta *bm_sta;
    ds_dlist_foreach(&g_sta_list, bm_sta) {
        if (memcmp(&bm_sta->addr, &sta_addr, OSW_HWADDR_LEN) == 0) break;
    }
    OSW_UT_EVAL(bm_sta != NULL);

    ow_steer_bm_snr_obs_report_cb(NULL, &sta_addr, &bssid, 11);
    ow_steer_bm_snr_obs_report_cb(NULL, &sta_addr, &bssid, 22);
    ow_steer_bm_snr_obs_report_cb(NULL, &sta_addr, &bssid, 33);
    ow_steer_bm_snr_obs_report_cb(NULL, &sta_addr, &bssid, 44);
    ow_steer_bm_snr_obs_report_cb(NULL, &sta_addr, &bssid, 55);

    ow_steer_bm_sta_state_sta_disconnected_cb(&bm_sta->state_observer, &sta_info);

    struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 1);
    OSW_UT_EVAL(client_vif_stats->connected == false);
    OSW_UT_EVAL(client_vif_stats->disconnects == 1);

    struct ow_steer_bm_event_stats *client_event_stats = &client_vif_stats->event_stats[0];
    OSW_UT_EVAL(client_event_stats->type == DISCONNECT);
    OSW_UT_EVAL(client_event_stats->disconnect_src == LOCAL);
    OSW_UT_EVAL(client_event_stats->disconnect_type == DEAUTH);
    OSW_UT_EVAL(client_event_stats->disconnect_reason == 0);
    OSW_UT_EVAL(client_event_stats->rssi == 55);
}

OSW_UT(ow_steer_bm_stats_force_kick)
{
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();
    osw_timer_disarm(&g_stats_timer);

    const struct osw_hwaddr sta_addr = { .octet = { 0xBA, 0xDC, 0x0D, 0xEB, 0xAD, 0xC0 }, };
    const struct osw_hwaddr bssid = { .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D }, };
    const char *group_id = "group_0";
    const char *vif_name = "vif_0";
    struct ow_steer_bm_group *group = ow_steer_bm_get_group(group_id);
    OSW_UT_EVAL(group != NULL);
    struct ow_steer_bm_vif *vif = ow_steer_bm_group_get_vif(group, vif_name);
    OSW_UT_EVAL(vif != NULL);
    struct ow_steer_bm_client *client = ow_steer_bm_get_client(sta_addr.octet);
    OSW_UT_EVAL(client != NULL);
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif_info = {
        .vif_name = vif_name,
        .drv_state = &drv_vif_state,
    };
    ow_steer_bm_vif_set_vif_info(vif, &vif_info);
    struct ow_steer_bm_sta *bm_sta;
    ds_dlist_foreach(&g_sta_list, bm_sta) {
        if (memcmp(&bm_sta->addr, &sta_addr, OSW_HWADDR_LEN) == 0) break;
    }
    OSW_UT_EVAL(bm_sta != NULL);

    /* first try */
    struct ow_steer_policy *force_kick_policy_base = ow_steer_policy_force_kick_get_base(bm_sta->force_kick_policy);
    OSW_UT_EVAL(force_kick_policy_base != NULL);

    ow_steer_bm_policy_mediator_trigger_executor_cb(force_kick_policy_base,
                                                    bm_sta);
    ow_steer_bm_sta_kick_state_send_btm_event(bm_sta,
                                              vif_name);

    struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 1);

    struct ow_steer_bm_event_stats *client_event_stats = &client_vif_stats->event_stats[0];
    OSW_UT_EVAL(client_event_stats->type == CLIENT_BTM);

    /* retry */
    ow_steer_bm_sta_kick_state_send_btm_event(bm_sta,
                                              vif_name);

    client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 2);

    client_event_stats = &client_vif_stats->event_stats[1];
    OSW_UT_EVAL(client_event_stats->type == CLIENT_BTM_RETRY);
}

OSW_UT(ow_steer_bm_stats_sticky_kick_2g)
{
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();
    osw_timer_disarm(&g_stats_timer);

    const struct osw_hwaddr sta_addr = { .octet = { 0xBA, 0xDC, 0x0D, 0xEB, 0xAD, 0xC0 }, };
    const struct osw_hwaddr bssid = { .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D }, };
    const char *group_id = "group_0";
    const char *vif_name = "vif_0";
    struct ow_steer_bm_group *group = ow_steer_bm_get_group(group_id);
    OSW_UT_EVAL(group != NULL);
    struct ow_steer_bm_vif *vif = ow_steer_bm_group_get_vif(group, vif_name);
    OSW_UT_EVAL(vif != NULL);
    struct ow_steer_bm_client *client = ow_steer_bm_get_client(sta_addr.octet);
    OSW_UT_EVAL(client != NULL);
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif_info = {
        .vif_name = vif_name,
        .drv_state = &drv_vif_state,
    };
    ow_steer_bm_vif_set_vif_info(vif, &vif_info);
    struct ow_steer_bm_sta *bm_sta;
    ds_dlist_foreach(&g_sta_list, bm_sta) {
        if (memcmp(&bm_sta->addr, &sta_addr, OSW_HWADDR_LEN) == 0) break;
    }
    OSW_UT_EVAL(bm_sta != NULL);

    /* first try */
    struct ow_steer_policy *sticky_kick_policy_base_2g = ow_steer_policy_snr_xing_get_base(bm_sta->lwm_2g_xing_policy);
    OSW_UT_EVAL(sticky_kick_policy_base_2g != NULL);

    ow_steer_bm_policy_mediator_trigger_executor_cb(sticky_kick_policy_base_2g,
                                                    bm_sta);
    ow_steer_bm_sta_kick_state_send_btm_event(bm_sta,
                                              vif_name);

    struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 1);
    OSW_UT_EVAL(client_vif_stats->sticky_kick_cnt == 1);

    struct ow_steer_bm_event_stats *client_event_stats = &client_vif_stats->event_stats[0];
    OSW_UT_EVAL(client_event_stats->type == CLIENT_STICKY_BTM);

    /* retry */
    ow_steer_bm_sta_kick_state_send_btm_event(bm_sta,
                                              vif_name);

    client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 2);
    OSW_UT_EVAL(client_vif_stats->sticky_kick_cnt == 2);

    client_event_stats = &client_vif_stats->event_stats[1];
    OSW_UT_EVAL(client_event_stats->type == CLIENT_STICKY_BTM_RETRY);
}

OSW_UT(ow_steer_bm_stats_sticky_kick_bottom_2g)
{
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();
    osw_timer_disarm(&g_stats_timer);

    const struct osw_hwaddr sta_addr = { .octet = { 0xBA, 0xDC, 0x0D, 0xEB, 0xAD, 0xC0 }, };
    const struct osw_hwaddr bssid = { .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D }, };
    const char *group_id = "group_0";
    const char *vif_name = "vif_0";
    struct ow_steer_bm_group *group = ow_steer_bm_get_group(group_id);
    OSW_UT_EVAL(group != NULL);
    struct ow_steer_bm_vif *vif = ow_steer_bm_group_get_vif(group, vif_name);
    OSW_UT_EVAL(vif != NULL);
    struct ow_steer_bm_client *client = ow_steer_bm_get_client(sta_addr.octet);
    OSW_UT_EVAL(client != NULL);
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif_info = {
        .vif_name = vif_name,
        .drv_state = &drv_vif_state,
    };
    ow_steer_bm_vif_set_vif_info(vif, &vif_info);
    struct ow_steer_bm_sta *bm_sta;
    ds_dlist_foreach(&g_sta_list, bm_sta) {
        if (memcmp(&bm_sta->addr, &sta_addr, OSW_HWADDR_LEN) == 0) break;
    }
    OSW_UT_EVAL(bm_sta != NULL);

    /* first try */
    struct ow_steer_policy *sticky_kick_policy_base_bot2g = ow_steer_policy_snr_xing_get_base(bm_sta->bottom_lwm_2g_xing_policy);
    OSW_UT_EVAL(sticky_kick_policy_base_bot2g != NULL);

    ow_steer_bm_policy_mediator_trigger_executor_cb(sticky_kick_policy_base_bot2g,
                                                    bm_sta);
    ow_steer_bm_sta_kick_state_send_btm_event(bm_sta,
                                              vif_name);

    struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 1);
    OSW_UT_EVAL(client_vif_stats->sticky_kick_cnt == 1);

    struct ow_steer_bm_event_stats *client_event_stats = &client_vif_stats->event_stats[0];
    OSW_UT_EVAL(client_event_stats->type == CLIENT_STICKY_BTM);

    /* retry */
    ow_steer_bm_sta_kick_state_send_btm_event(bm_sta,
                                              vif_name);

    client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 2);
    OSW_UT_EVAL(client_vif_stats->sticky_kick_cnt == 2);

    client_event_stats = &client_vif_stats->event_stats[1];
    OSW_UT_EVAL(client_event_stats->type == CLIENT_STICKY_BTM_RETRY);
}

OSW_UT(ow_steer_bm_stats_sticky_kick_5g)
{
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();
    osw_timer_disarm(&g_stats_timer);

    const struct osw_hwaddr sta_addr = { .octet = { 0xBA, 0xDC, 0x0D, 0xEB, 0xAD, 0xC0 }, };
    const struct osw_hwaddr bssid = { .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D }, };
    const char *group_id = "group_0";
    const char *vif_name = "vif_0";
    struct ow_steer_bm_group *group = ow_steer_bm_get_group(group_id);
    OSW_UT_EVAL(group != NULL);
    struct ow_steer_bm_vif *vif = ow_steer_bm_group_get_vif(group, vif_name);
    OSW_UT_EVAL(vif != NULL);
    struct ow_steer_bm_client *client = ow_steer_bm_get_client(sta_addr.octet);
    OSW_UT_EVAL(client != NULL);
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif_info = {
        .vif_name = vif_name,
        .drv_state = &drv_vif_state,
    };
    ow_steer_bm_vif_set_vif_info(vif, &vif_info);
    struct ow_steer_bm_sta *bm_sta;
    ds_dlist_foreach(&g_sta_list, bm_sta) {
        if (memcmp(&bm_sta->addr, &sta_addr, OSW_HWADDR_LEN) == 0) break;
    }
    OSW_UT_EVAL(bm_sta != NULL);

    /* first try */
    struct ow_steer_policy *sticky_kick_policy_base_5g = ow_steer_policy_snr_xing_get_base(bm_sta->lwm_5g_xing_policy);
    OSW_UT_EVAL(sticky_kick_policy_base_5g != NULL);

    ow_steer_bm_policy_mediator_trigger_executor_cb(sticky_kick_policy_base_5g,
                                                    bm_sta);
    ow_steer_bm_sta_kick_state_send_btm_event(bm_sta,
                                              vif_name);

    struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 1);
    OSW_UT_EVAL(client_vif_stats->sticky_kick_cnt == 1);

    struct ow_steer_bm_event_stats *client_event_stats = &client_vif_stats->event_stats[0];
    OSW_UT_EVAL(client_event_stats->type == CLIENT_STICKY_BTM);

    /* retry */
    ow_steer_bm_sta_kick_state_send_btm_event(bm_sta,
                                              vif_name);

    client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 2);
    OSW_UT_EVAL(client_vif_stats->sticky_kick_cnt == 2);

    client_event_stats = &client_vif_stats->event_stats[1];
    OSW_UT_EVAL(client_event_stats->type == CLIENT_STICKY_BTM_RETRY);
}

OSW_UT(ow_steer_bm_stats_steering_kick_2g)
{
    OSW_MODULE_LOAD(ow_steer_bm);
    osw_ut_time_init();
    osw_timer_disarm(&g_stats_timer);

    const struct osw_hwaddr sta_addr = { .octet = { 0xBA, 0xDC, 0x0D, 0xEB, 0xAD, 0xC0 }, };
    const struct osw_hwaddr bssid = { .octet = { 0xCA, 0xFE, 0xCA, 0xFE, 0xF0, 0x0D }, };
    const char *group_id = "group_0";
    const char *vif_name = "vif_0";
    struct ow_steer_bm_group *group = ow_steer_bm_get_group(group_id);
    OSW_UT_EVAL(group != NULL);
    struct ow_steer_bm_vif *vif = ow_steer_bm_group_get_vif(group, vif_name);
    OSW_UT_EVAL(vif != NULL);
    struct ow_steer_bm_client *client = ow_steer_bm_get_client(sta_addr.octet);
    OSW_UT_EVAL(client != NULL);
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif_info = {
        .vif_name = vif_name,
        .drv_state = &drv_vif_state,
    };
    ow_steer_bm_vif_set_vif_info(vif, &vif_info);
    struct ow_steer_bm_sta *bm_sta;
    ds_dlist_foreach(&g_sta_list, bm_sta) {
        if (memcmp(&bm_sta->addr, &sta_addr, OSW_HWADDR_LEN) == 0) break;
    }
    OSW_UT_EVAL(bm_sta != NULL);

    /* first try */
    struct ow_steer_policy *steering_kick_policy_base_2g = ow_steer_bm_policy_hwm_2g_get_base(bm_sta->hwm_2g_policy);
    OSW_UT_EVAL(steering_kick_policy_base_2g != NULL);

    ow_steer_bm_policy_mediator_trigger_executor_cb(steering_kick_policy_base_2g,
                                                    bm_sta);
    ow_steer_bm_sta_kick_state_send_btm_event(bm_sta,
                                              vif_name);

    struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 1);
    OSW_UT_EVAL(client_vif_stats->steering_kick_cnt == 1);

    struct ow_steer_bm_event_stats *client_event_stats = &client_vif_stats->event_stats[0];
    OSW_UT_EVAL(client_event_stats->type == CLIENT_BS_BTM);

    /* retry */
    ow_steer_bm_sta_kick_state_send_btm_event(bm_sta,
                                              vif_name);

    client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    OSW_UT_EVAL(client_vif_stats != NULL);
    OSW_UT_EVAL(client_vif_stats->event_stats_count == 2);
    OSW_UT_EVAL(client_vif_stats->steering_kick_cnt == 2);

    client_event_stats = &client_vif_stats->event_stats[1];
    OSW_UT_EVAL(client_event_stats->type == CLIENT_BS_BTM_RETRY);
}

OSW_UT(ow_steer_bm_is_band_cap_5ghz_only)
{
    /* Taken from a RPI4. It contains Supported Channels
     * listing only 5GHz channels. It does not list 2.4GHz
     * channels, and certainly does not list 6GHz channels
     * which could be mistaken for 6GHz if the parses is
     * buggy.
     */
    const uint8_t ies[] =  {
        0x00, 0x09, 0x73, 0x6c, 0x6f, 0x63, 0x68, 0x31, 0x61, 0x74, 0x74, 0x01,
        0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c, 0x21, 0x02, 0x03,
        0xe0, 0x24, 0x0a, 0x24, 0x04, 0x34, 0x04, 0x64, 0x0c, 0x95, 0x04, 0xa5,
        0x01, 0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00,
        0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00, 0x46,
        0x05, 0x32, 0x48, 0x01, 0x00, 0x00, 0x2d, 0x1a, 0x63, 0x00, 0x17, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x08,
        0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x40, 0xbf, 0x0c, 0x32, 0x50,
        0x80, 0x0f, 0xfe, 0xff, 0x00, 0x00, 0xfe, 0xff, 0x00, 0x00, 0xdd, 0x09,
        0x00, 0x10, 0x18, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0xdd, 0x07, 0x00,
        0x50, 0xf2, 0x02, 0x00, 0x01, 0x00
    };
    const size_t ies_len = ARRAY_SIZE(ies);

    struct osw_assoc_req_info info;
    MEMZERO(info);

    const bool ok = osw_parse_assoc_req_ies(ies, ies_len, &info);
    OSW_UT_EVAL(ok);

    OSW_UT_EVAL(ow_steer_bm_assoc_req_is_band_capable(&info, OSW_BAND_2GHZ) == false);
    OSW_UT_EVAL(ow_steer_bm_assoc_req_is_band_capable(&info, OSW_BAND_5GHZ) == true);
    OSW_UT_EVAL(ow_steer_bm_assoc_req_is_band_capable(&info, OSW_BAND_6GHZ) == false);
}
