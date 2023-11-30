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

#include "lib/dpif.h"
#include "lib/odp-util.h"
#include "lib/dpctl.h"
#include "memutil.h"
#include "lan_dpctl.h"
#include "log.h"


static struct hmap *
get_portno_names(struct dpif *dpif, const struct dpctl_params *dpctl_p)
{
    struct dpif_port_dump port_dump;
    struct dpif_port dpif_port;
    struct hmap *portno_names;

    portno_names = CALLOC(1, (sizeof(*portno_names)));
    if (portno_names == NULL) return NULL;

    hmap_init(portno_names);
    DPIF_PORT_FOR_EACH (&dpif_port, &port_dump, dpif)
    {
        odp_portno_names_set(portno_names, dpif_port.port_no,
                             dpif_port.name);
    }
    return portno_names;
}

#if OVS_PACKAGE_VERNUM >= OVS_VERSION_2_11_1
static void
format_flow(struct ds *ds, const struct dpif_flow *f, struct hmap *ports,
                 struct dpctl_params *dpctl_p)
{
    if (dpctl_p->verbosity && f->ufid_present)
    {
        odp_format_ufid(&f->ufid, ds);
        ds_put_cstr(ds, ", ");
    }
    odp_flow_format(f->key, f->key_len, f->mask, f->mask_len, ports, ds,
                    dpctl_p->verbosity);
    ds_put_cstr(ds, ", ");

    dpif_flow_stats_format(&f->stats, ds);
    if (dpctl_p->verbosity && f->attrs.offloaded)
    {
        ds_put_cstr(ds, ", offloaded:yes");
    }
    if (dpctl_p->verbosity && f->attrs.dp_layer)
    {
        ds_put_format(ds, ", dp:%s", f->attrs.dp_layer);
    }
    ds_put_cstr(ds, ", actions:");
    format_odp_actions(ds, f->actions, f->actions_len, ports);
}
#elif OVS_PACKAGE_VERNUM == OVS_VERSION_2_8_7
static void
format_flow(struct ds *ds, const struct dpif_flow *f, struct hmap *ports,
            char *type, struct dpctl_params *dpctl_p)
{
    if (dpctl_p->verbosity && f->ufid_present)
    {
        odp_format_ufid(&f->ufid, ds);
        ds_put_cstr(ds, ", ");
    }
    odp_flow_format(f->key, f->key_len, f->mask, f->mask_len, ports, ds,
                    dpctl_p->verbosity);
    ds_put_cstr(ds, ", ");

    dpif_flow_stats_format(&f->stats, ds);

    if (dpctl_p->verbosity && !type && f->offloaded)
    {
        ds_put_cstr(ds, ", offloaded:yes");
    }
    ds_put_cstr(ds, ", actions:");
    format_odp_actions(ds, f->actions, f->actions_len, ports);
}
#endif


static void
dpctl_free_portno_names(struct hmap *portno_names)
{
    if (portno_names == NULL) return;

    odp_portno_names_destroy(portno_names);
    hmap_destroy(portno_names);
    FREE(portno_names);
}


#if OVS_PACKAGE_VERNUM >= OVS_VERSION_2_11_1
static void
enable_all_dump_types(struct dump_types *dump_types)
{
    dump_types->ovs = true;
    dump_types->tc = true;
    dump_types->offloaded = true;
    dump_types->non_offloaded = true;
}


static int
populate_dump_types(char *types_list, struct dump_types *dump_types,
                    struct dpctl_params *dpctl_p)
{
    if (!types_list)
    {
        enable_all_dump_types(dump_types);
        return 0;
    }

    char *current_type;

    while (types_list && types_list[0] != '\0')
    {
        current_type = types_list;
        size_t type_len = strcspn(current_type, ",");

        types_list += type_len + (types_list[type_len] != '\0');
        current_type[type_len] = '\0';

        if (!strcmp(current_type, "ovs"))
        {
            dump_types->ovs = true;
        }
        else if (!strcmp(current_type, "tc"))
        {
            dump_types->tc = true;
        }
        else if (!strcmp(current_type, "offloaded"))
        {
            dump_types->offloaded = true;
        }
        else if (!strcmp(current_type, "non-offloaded"))
        {
            dump_types->non_offloaded = true;
        }
        else if (!strcmp(current_type, "all"))
        {
            enable_all_dump_types(dump_types);
        }
        else
        {
            LOGE("%s: EINVAL, Failed to parse type",
                 __func__);
            return EINVAL;
        }
    }
    return 0;
}


static void
determine_dpif_flow_dump_types(struct dump_types *dump_types,
                               struct dpif_flow_dump_types *dpif_dump_types)
{
    dpif_dump_types->ovs_flows = dump_types->ovs || dump_types->non_offloaded;
    dpif_dump_types->netdev_flows = dump_types->tc || dump_types->offloaded
                                    || dump_types->non_offloaded;
}
#endif


static int
dump_flows(lan_stats_instance_t *lan_stats_instance, struct dpctl_params *dpctl_p)
{
#if OVS_PACKAGE_VERNUM >= OVS_VERSION_2_11_1
    struct dpif_flow_dump_types dpif_dump_types;
    struct dump_types dump_types;
    char *types_list = NULL;
#endif
    struct dpif_flow_dump_thread *dump_thread;
    char *name, *parse_name, *parse_type;
    struct dpif_flow_dump *flow_dump = NULL;
    struct hmap *portno_names;
    dp_ctl_stats_t stats;
    struct dpif_flow f;
    struct dpif *dpif;
    struct ds ds;
    int error;

    name = OVS_NAME;
    portno_names = NULL;

    dp_parse_name(name, &parse_name, &parse_type);
    error = dpif_open(parse_name, parse_type, &dpif);
    if (error)
    {
        LOGE("%s: Error opening datapath\n", __func__);
        goto out_dpif_close;
    }

    portno_names = get_portno_names(dpif, dpctl_p);
    if (portno_names == NULL)
    {
        LOGE("%s: Error fetching portnames\n", __func__);
        goto out_portfree;
    }

#if OVS_PACKAGE_VERNUM >= OVS_VERSION_2_11_1
    memset(&dump_types, 0, sizeof (dump_types));
    error = populate_dump_types(types_list, &dump_types, dpctl_p);
    if (error)
    {
        goto out_typefree;
    }
    determine_dpif_flow_dump_types(&dump_types, &dpif_dump_types);
#endif

#if OVS_PACKAGE_VERNUM >= OVS_VERSION_2_11_1
    flow_dump = dpif_flow_dump_create(dpif, false, &dpif_dump_types);
#elif OVS_PACKAGE_VERNUM == OVS_VERSION_2_8_7
    flow_dump = dpif_flow_dump_create(dpif, false,  "dpctl");
    char *type = NULL;
#endif
    dump_thread = dpif_flow_dump_thread_create(flow_dump);

    ds_init(&ds);
    memset(&f, 0, sizeof(f));
    while (dpif_flow_dump_next(dump_thread, &f, 1))
    {
        memset(&stats, 0, sizeof(dp_ctl_stats_t));
#if OVS_PACKAGE_VERNUM >= OVS_VERSION_2_11_1
        format_flow(&ds, &f, portno_names, dpctl_p);
#elif OVS_PACKAGE_VERNUM == OVS_VERSION_2_8_7
        format_flow(&ds, &f, portno_names, type, dpctl_p);
#endif
        ds.string[ds.length] = '\0';
        lan_stats_parse_flows(lan_stats_instance, ds.string, &stats);
        lan_stats_add_uplink_info(lan_stats_instance, &stats);
        lan_stats_flows_filter(lan_stats_instance, &stats);
        ds_clear(&ds);
    }

    dpif_flow_dump_thread_destroy(dump_thread);
    error = dpif_flow_dump_destroy(flow_dump);

    if (error)
    {
        LOGE("%s: Failed to dump flows from datapath\n", __func__);
    }

    ds_destroy(&ds);

#if OVS_PACKAGE_VERNUM >= OVS_VERSION_2_11_1
out_typefree:
    FREE(types_list);
#endif

out_portfree:
    dpctl_free_portno_names(portno_names);

out_dpif_close:
    dpif_close(dpif);
    return error;
}


void
lan_stats_collect_flows(lan_stats_instance_t *lan_stats_instance)
{
    struct dpctl_params dpctl_p;
    int error;

    if (lan_stats_instance == NULL) return;

    memset(&dpctl_p, 0, sizeof(dpctl_p));

    dpctl_p.verbosity++;
    dpctl_p.names = true;
    error = dump_flows(lan_stats_instance, &dpctl_p);
    if (error)
    {
        LOGE("%s: Failed to dump flows\n", __func__);
    }
}
