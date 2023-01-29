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
#include "qosm.h"

static void
qosm_free_ic_elements(struct qosm_intf_classifier *ic)
{
    FREE(ic->ic_match);
    FREE(ic->ic_action);
    FREE(ic->ic_token);
}

void
qosm_free_ic(struct qosm_intf_classifier *ic)
{
    struct qosm_mgr *mgr;
    TRACE();

    if (ic == NULL) return;

    mgr = qosm_get_mgr();
    ds_tree_remove(&mgr->qosm_intf_classifier_tree, ic);
    qosm_free_ic_elements(ic);

    FREE(ic);
}

static void
qosm_int_classifier_reflink_fn(reflink_t *ref, reflink_t *sender)
{
    struct qosm_intf_classifier *ic = CONTAINER_OF(ref, struct qosm_intf_classifier, ic_reflink);

    TRACE();

    if (sender != NULL) return;

    qosm_ic_template_del_from_schema(ic->ic_token);
    qosm_free_ic(ic);

}

struct qosm_intf_classifier *
qosm_interface_classifier_get(struct schema_Interface_Classifier *conf)
{
    struct qosm_intf_classifier *ic;
    struct qosm_mgr *mgr;
    bool template_rule;

    TRACE();

    mgr = qosm_get_mgr();

     /* check if the interface_classifier obj is already present */
    ic = ds_tree_find(&mgr->qosm_intf_classifier_tree, (void *)conf->_uuid.uuid);
    if (ic) return ic;

    LOGT("%s(): creating interface classifier object for %s", __func__, conf->_uuid.uuid);

    /* interface_classifier for given uuid not present, create a new object */
    ic = CALLOC(1, sizeof(*ic));
    if (ic == NULL) return NULL;

    STRSCPY(ic->ic_uuid.uuid, conf->_uuid.uuid);
    reflink_init(&ic->ic_reflink, "Interface_Classifier");
    reflink_set_fn(&ic->ic_reflink, qosm_int_classifier_reflink_fn);

    /* add new object to interface classifier tree */
    ds_tree_insert(&mgr->qosm_intf_classifier_tree, ic, ic->ic_uuid.uuid);

    /* copy match */
    ic->ic_match = STRDUP(conf->match);
    if (ic->ic_match == NULL) goto alloc_error;

    /* copy action */
    ic->ic_action = STRDUP(conf->action);
    if (ic->ic_action == NULL) goto alloc_error;

    /* copy token */
    ic->ic_token = STRDUP(conf->token);
    if (ic->ic_token == NULL) goto alloc_error;
    ic->ic_priority = conf->priority;

    template_rule = qosm_ic_is_template_rule(conf);
    if (template_rule)
    {
        qosm_ic_template_add_from_schema(conf);
    }

    return ic;

alloc_error:
    qosm_free_ic(ic);
    return NULL;
}

void
qosm_interface_classifier_update(struct qosm_intf_classifier *ic, struct schema_Interface_Classifier *conf)
{
    bool template_rule;

    if (ic == NULL) return;

    /* check and delete if it is a template rule */
    qosm_ic_template_del_from_schema(ic->ic_token);
    /* clear existing configurations.*/
    qosm_free_ic_elements(ic);

    /* update configurations */
    ic->ic_match = STRDUP(conf->match);
    ic->ic_action = STRDUP(conf->action);
    ic->ic_token = STRDUP(conf->token);
    ic->ic_priority = conf->priority;

    /* add to the template tree if it is a template rule */
    template_rule = qosm_ic_is_template_rule(conf);
    if (template_rule) qosm_ic_template_add_from_schema(conf);

    return;

}

void
qosm_ic_list_free(struct qosm_ip_iface *ipi)
{
    struct intf_classifier_entry *to_remove;
    struct intf_classifier_entry *ic_entry;
    struct qosm_intf_classifier *ic;

    /* Disconnect current interface classifiers reflinks */
    ic_entry = ds_tree_head(&ipi->ipi_intf_classifier_tree);
    while(ic_entry != NULL)
    {
        to_remove = ic_entry;
        ic = ic_entry->ic;

        reflink_disconnect(&ipi->ipi_classifier_reflink, &ic->ic_reflink);

        ic_entry = ds_tree_next(&ipi->ipi_intf_classifier_tree, ic_entry);
        ds_tree_remove(&ipi->ipi_intf_classifier_tree, to_remove);
        FREE(to_remove);
    }

}

void
qosm_intf_classifer_init(void)
{
    struct qosm_mgr *mgr;

    mgr = qosm_get_mgr();
    ds_tree_init(&mgr->qosm_intf_classifier_tree, ds_str_cmp,
                 struct qosm_intf_classifier, ic_tnode);
    return;
}
