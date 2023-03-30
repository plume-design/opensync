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


#include "qosm_interface_classifier.h"
#include "qosm_ip_iface.h"
#include "ovsdb_utils.h"
#include "osn_tc.h"
#include "qosm.h"

/**
 * @brief initialize IP_Interface table monitoring
 * @param None
 * @return None
 */
void
qosm_ip_iface_init(void)
{
    struct qosm_mgr *mgr;

    mgr = qosm_get_mgr();
    ds_tree_init(&mgr->qosm_ip_iface_tree, (ds_key_cmp_t *) strcmp,
                 struct qosm_ip_iface, ipi_tnode);
    return;
}

void
qosm_init_debounce_cb(ev_debounce_fn_t *debounce_cb)
{
    struct qosm_mgr *mgr;

    mgr = qosm_get_mgr();
    mgr->debounce_fn_cb = debounce_cb;
}

void
qosm_ip_iface_del(struct schema_IP_Interface *conf)
{
    struct qosm_ip_iface *ipi;
    struct qosm_mgr *mgr;

    TRACE();

    ipi = qosm_ip_iface_get(conf);
    if (ipi == NULL) return;

    mgr = qosm_get_mgr();

    if (ipi->ipi_tc)
    {
        LOGT("%s(): deleting TC configuration for '%s'", __func__, ipi->ipi_ifname);
        osn_tc_del(ipi->ipi_tc);
        ipi->ipi_tc = NULL;
    }

    qosm_ic_list_free(ipi);

    /* Stop any active debounce timers */
    ev_debounce_stop(EV_DEFAULT, &ipi->ipi_debounce);
    ds_tree_remove(&mgr->qosm_ip_iface_tree, ipi);
    FREE(ipi);
}

void
qosm_ipi_classifier_reflink_fn(reflink_t *ref, reflink_t *sender)
{
    struct qosm_ip_iface *ipi = CONTAINER_OF(ref, struct qosm_ip_iface, ipi_classifier_reflink);

    TRACE();

    if (sender == NULL) return;

    qosm_ip_iface_start(ipi);
}

osn_tc_t *
qosm_os_tc_get(struct qosm_ip_iface *ipi)
{
    if (ipi->ipi_tc) return ipi->ipi_tc;

    return osn_tc_new(ipi->ipi_ifname);
}

bool
qosm_os_tc_commit(osn_tc_t *ipi_tc, struct qosm_intf_classifier *ic, bool ingress)
{
    qosm_ic_tmpl_filter_t *tflow;
    bool ret = false;

    LOGT("%s(): interface classifier: %s match: '%s', direction: '%s'",
         __func__,
         ic->ic_token,
         ic->ic_match,
         (ingress == true ? "ingress" : "egress"));

    tflow = qosm_ic_filter_find_by_token(ic->ic_token);
    if (tflow)
    {
        LOGT("%s(): configuring template rule %s", __func__, tflow->token);
        tflow->ingress = ingress;
        ret = qosm_ic_template_filter_update(ipi_tc, OM_ACTION_ADD, tflow);
        goto done;
    }

    /* rule is not a template rule */
    ret = osn_tc_filter_begin(ipi_tc,
                              ic->ic_priority,
                              ingress,
                              ic->ic_match,
                              ic->ic_action);

    ret |= osn_tc_filter_end(ipi_tc);

done:
    LOGT("%s(): commiting TC configuration for %s %s",
         __func__, ic->ic_token,
         (ret != 0 ? "success" : "failed"));

    return ret;
}

static void
qosm_commit_classifers(struct qosm_ip_iface *ipi, osn_tc_t *ipi_tc)
{
    struct intf_classifier_entry *ic_entry;
    struct qosm_intf_classifier *ic;
    bool success;

    TRACE();

    ic_entry = ds_tree_head(&ipi->ipi_intf_classifier_tree);
    while(ic_entry != NULL)
    {
        ic = ic_entry->ic;
        success = qosm_os_tc_commit(ipi_tc, ic, ic_entry->ingress);
        if (!success)
            LOGN("%s(): TC commit failed for %s", __func__, ic->ic_token);

        ic_entry = ds_tree_next(&ipi->ipi_intf_classifier_tree, ic_entry);
    }
}

/*
 * Debounce timer function callback; this is where the TC is configured
 */
void
qosm_ip_iface_debounce_fn(struct ev_loop *loop, ev_debounce *w, int revent)
{
    struct qosm_ip_iface *ipi;
    bool success;

    /* get the base address of ipi structure */
    ipi = CONTAINER_OF(w, struct qosm_ip_iface, ipi_debounce);

    LOGT("%s(): configuring TC rules for %s", __func__, ipi->ipi_ifname);

    /* if ipi_tc is not NULL, that means tc rules are already created,
     * delete and configure the rules again */
    if (ipi->ipi_tc != NULL)
    {
        LOGT("%s(): deleting TC configuration", __func__ );
        osn_tc_del(ipi->ipi_tc);
        ipi->ipi_tc = NULL;
    }

    /* create new tc object */
    ipi->ipi_tc = osn_tc_new(ipi->ipi_ifname);
    if (ipi->ipi_tc == NULL)
    {
        LOGE("%s(): error creating TC config object for %s", __func__, ipi->ipi_ifname);
        goto error;
    }

    success = osn_tc_begin(ipi->ipi_tc);
    if (!success)
    {
        LOGE("%s(): error initializing TC config object %s", __func__, ipi->ipi_ifname);
        goto error;
    }

    /* walk through the list of all Interface classifier and commit the rules */
    qosm_commit_classifers(ipi, ipi->ipi_tc);

    success = osn_tc_end(ipi->ipi_tc);
    if (!success)
    {
        LOGE("%s(): error completing TC configuration %s", __func__, ipi->ipi_ifname);
        goto error;
    }

    success = osn_tc_apply(ipi->ipi_tc);
    if (!success)
    {
        LOGE("%s(): error applying TC configuration %s", __func__, ipi->ipi_ifname);
        goto error;
    }

    return;

error:
    LOGN("%s(): failed to apply TC configuration for %s", __func__, ipi->ipi_ifname);
}

static int
ipi_classifier_cmp(const void *_a, const void *_b)
{
    int diff;

    struct intf_classifier_entry *a = (struct intf_classifier_entry *)_a;
    struct intf_classifier_entry *b = (struct intf_classifier_entry *)_b;

    diff = (a->ic - b->ic);

    diff |= (a->ingress == b->ingress);

    return diff;
}

struct qosm_ip_iface *
qosm_ip_iface_get(struct schema_IP_Interface *conf)
{
    struct qosm_ip_iface *ipi;
    struct qosm_mgr *mgr;

    mgr = qosm_get_mgr();

    /* check if the ip_iface obj is already present */
    ipi = ds_tree_find(&mgr->qosm_ip_iface_tree, (void *) conf->_uuid.uuid);
    if (ipi) return ipi;

    /* ip_iface for given uuid not present create a new object */
    ipi = CALLOC(1, sizeof(*ipi));
    if (ipi == NULL) return NULL;

    STRSCPY(ipi->ipi_uuid.uuid, conf->_uuid.uuid);
    STRSCPY(ipi->ipi_ifname, conf->if_name);

    /* create  reflink to classifier changes */
    reflink_init(&ipi->ipi_classifier_reflink, "IP_Interface.classifier");
    reflink_set_fn(&ipi->ipi_classifier_reflink, qosm_ipi_classifier_reflink_fn);

    /* initialize tree to store interface classifier tree this IP_Interface uses */
    ds_tree_init(&ipi->ipi_intf_classifier_tree, ipi_classifier_cmp,
                 struct intf_classifier_entry, ic_node);

    LOGT("%s(): initializing debounce timer.", __func__);
    ev_debounce_init2(&ipi->ipi_debounce,
                      mgr->debounce_fn_cb,
                      QOSM_TC_DEBOUNCE_MIN,
                      QOSM_TC_DEBOUNCE_MAX);

    ds_tree_insert(&mgr->qosm_ip_iface_tree, ipi, ipi->ipi_uuid.uuid);

    return ipi;
}

void
qosm_ip_iface_start(struct qosm_ip_iface *ipi)
{
    TRACE();

    if (ipi == NULL) return;

    /* ic is found in the tag tree and its parent pointer is also available. Invoke
     * debounce timer for reconfiguring the TC */
    ev_debounce_start(EV_DEFAULT, &ipi->ipi_debounce);
}

static struct intf_classifier_entry*
create_classifier_entry(struct qosm_intf_classifier *ic, bool ingress)
{
    struct intf_classifier_entry *ic_entry;

    ic_entry = CALLOC(1, sizeof(*ic_entry));
    if (ic_entry == NULL) return NULL;

    ic_entry->ic = ic;
    ic_entry->ingress = ingress;

    return ic_entry;
}

void
qosm_ip_iface_populate_ic(struct qosm_ip_iface *ipi,
                             ovs_uuid_t uuids[],
                             int len,
                             bool ingress)
{
    struct schema_Interface_Classifier schema_ic;
    struct intf_classifier_entry *ic_entry;
    struct qosm_intf_classifier *ic;
    int i;

    for (i = 0; i < len; i++)
    {
        /* get ic object for the configured classifier uuid */
        STRSCPY(schema_ic._uuid.uuid, (char *)uuids[i].uuid);
        ic = qosm_interface_classifier_get(&schema_ic);
        if (ic == NULL) continue;

        /* create the classifier entry with direction for adding to classifier tree */
        ic_entry = create_classifier_entry(ic, ingress);
        if (ic_entry == NULL) continue;

        qosm_ic_template_set_parent(ipi, ic->ic_token);

        LOGT("%s(): adding %s with %s direction to classifier tree", __func__,
             ic->ic_token, (ingress == true ? "ingress" : "egress"));
        /* single tree is used to store both ingress/egress classifier. classifier entry
         * has a flag to indicate if it should be used as ingress or egress
         */
        ds_tree_insert(&ipi->ipi_intf_classifier_tree, ic_entry, ic_entry->ic);
        reflink_connect(&ipi->ipi_classifier_reflink, &ic->ic_reflink);
    }
}

void
qosm_ip_iface_update_ic(struct qosm_ip_iface *ipi, struct schema_IP_Interface *conf)
{
    int len;

    /* remove interface queue if present */
    qosm_ic_list_free(ipi);

    /* create a new interface list */
    len = conf->ingress_classifier_len + conf->egress_classifier_len;
    if (len == 0)
    {
        LOGT("%s(): returning no classifiers configured", __func__ );
        return;
    }

    qosm_ip_iface_populate_ic(ipi, conf->ingress_classifier, conf->ingress_classifier_len, true);
    qosm_ip_iface_populate_ic(ipi, conf->egress_classifier, conf->egress_classifier_len, false);

}
