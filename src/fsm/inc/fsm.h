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

#ifndef FSM_H_INCLUDED
#define FSM_H_INCLUDED

#include <ev.h>
#include <pcap.h>
#include <sys/sysinfo.h>
#include <time.h>

#include "ds_tree.h"
#include "fsm_policy.h"
#include "os_types.h"
#include "ovsdb_utils.h"
#include "schema.h"
#include "net_header_parse.h"

struct fsm_session;

struct fsm_object
{
        char *object;
        char *version;
        int state;
};


/**
 * @brief session operations

 * The callbacks are provided to or by the plugin
 */
struct fsm_session_ops
{
    /* MQTT json report sending routine. Provided to the plugin */
    void (*send_report)(struct fsm_session *, char *);

    /* MQTT protobuf report sending routine. Provided to the plugin */
    void (*send_pb_report)(struct fsm_session *, char *, void *, size_t);

    /* ovsdb update callback. Provided by the plugin */
    void (*update)(struct fsm_session *);

    /* periodic callback. Provided by the plugin */
    void (*periodic)(struct fsm_session *);

    /*  plugin exit. Provided by the plugin */
    void (*exit)(struct fsm_session *);

    /* other_config parser. Provided to the plugin */
    char * (*get_config)(struct fsm_session *, char *key);

    /* object update notification callback. provided by the plugin */
    void (*object_cb)(struct fsm_session *, struct fsm_object *, int);

    /* object state notification callback. provided to the plugin */
    void (*state_cb)(struct fsm_session *, struct fsm_object *);

    /* object version comparison. provided by the plugin */
    int (*version_cmp_cb)(char *, char *, char *);

    /* Get latest object version. Provided to the plugin */
    struct fsm_object * (*latest_obj_cb)(struct fsm_session *, char *, char *);
};


/**
 * @brief parser plugin specific operations
 *
 * The callbacks are provided to or by the service plugin
 */
struct fsm_parser_ops
{
    /* packet parsing handler. Provided by the plugin */
    void (*handler)(struct fsm_session *, struct net_header_parser *);

    /*
     * service plugin request. Provided to the plugin.
     * Used for backward compatibility:
     * A plugin can use this method to request a service
     * if the cloud did not explicitly provided an entry
     * in its other_config.
     */
    bool (*get_service)(struct fsm_session *);
};


/**
 * @brief web categorization plugin specific operations
 *
 * The callbacks are provided to or by the plugin
 */
struct fsm_web_cat_ops
{
    bool (*categories_check)(struct fsm_session *,
                             struct fsm_policy_req *,
                             struct fsm_policy *);
    bool (*risk_level_check)(struct fsm_session *,
                             struct fsm_policy_req *,
                             struct fsm_policy *);
    char * (*cat2str)(struct fsm_session *, int id);
    void (*get_stats)(struct fsm_session *, struct fsm_url_stats *);
    void (*dns_response)(struct fsm_session *, struct fqdn_pending_req *);
};


/**
 * @brief provider specific operations
 *
 * The callbacks are provided to or by the service plugin
 */

union fsm_plugin_ops
{
    struct fsm_parser_ops parser_ops;
    struct fsm_web_cat_ops web_cat_ops;
};


/**
 * @brief session representation of the OVSDB related table
 *
 * Mirrors the Flow_Service_Manager_Config table contents
 */
struct fsm_session_conf
{
    char *handler;             /* Session unique name */
    char *if_name;             /* Session's tap interface */
    char *pkt_capt_filter;     /* Session's BPF filter */
    char *plugin;              /* Session's service plugin */
    ds_tree_t *other_config;   /* Session's private config */
};


/**
 * @brief session pcaps container
 */
struct fsm_pcaps
{
    pcap_t *pcap;
    struct bpf_program *bpf;
    int pcap_fd;
    ev_io fsm_evio;
    int pcap_datalink;
    int buffer_size;
    int cnt;
    int snaplen;
    int immediate;
    int started;
};


/**
 * @brief supported fsm services.
 *
 * Matches the enumeration defined in the ovsdb schema
 */
enum {
    FSM_UNKNOWN_SERVICE = -1,
    FSM_PARSER = 0,
    FSM_WEB_CAT,
    FSM_DPI,
    FSM_DPI_DISPATCH,
    FSM_DPI_PLUGIN,
};


struct fsm_type
{
    char *ovsdb_type;
    int fsm_type;
};

enum {
    FSM_SERVICE_DELETE = 0,
    FSM_SERVICE_ADD
};


enum fsm_dpi_state
{
    FSM_DPI_CLEAR    = 0,
    FSM_DPI_INSPECT  = 1,
    FSM_DPI_PASSTHRU = 2,
    FSM_DPI_DROP     = 3,
};


enum fsm_object_state
{
    FSM_OBJ_ACTIVE = 0,
    FSM_OBJ_ERROR,
    FSM_OBJ_OBSOLETE,
    FSM_OBJ_LOAD_FAILED,
};


/**
 * @brief dpi dispatcher specifics
 */
struct fsm_dpi_dispatcher
{
    struct net_header_parser net_parser;
    struct net_md_aggregator *aggr;
    struct fsm_session *session;
    ds_tree_t plugin_sessions;
    time_t periodic_ts;
};


/**
 * @brief dpi plugin specifics
 */
struct fsm_dpi_plugin
{
    struct fsm_session *session;
    char *targets;
    char *excluded_targets;
    bool bound;
    ds_tree_node_t dpi_node;
};

/**
 * @brief dpi plugin specifics
 */
union fsm_dpi_context
{
    struct fsm_dpi_dispatcher dispatch;
    struct fsm_dpi_plugin plugin;
};


/**
 * @brief per flow dpi plugin specifics
 */
struct fsm_dpi_flow_info
{
    struct fsm_session *session;
    int decision;
    ds_tree_node_t dpi_node;
};


/**
 * @brief session container.
 *
 * The session is the main structure exchanged with the service plugins.
 */
struct fsm_session
{
    struct fsm_session_conf *conf;   /* ovsdb configuration */
    struct fsm_session_ops ops;      /* session function pointers */
    union fsm_plugin_ops *p_ops;     /* plugin function pointers */
    struct fsm_pcaps *pcaps;         /* pcaps container */
    ds_tree_t *mqtt_headers;         /* mqtt headers from AWLAN_Node */
    char *name;                      /* convenient session name pointer */
    char *topic;                     /* convenient mqtt topic pointer */
    char *location_id;               /* convenient mqtt location id pointer */
    char *node_id;                   /* convenient mqtt node id pointer */
    void *handler_ctxt;              /* session private context */
    void *handle;                    /* plugin dso handle */
    char *dso;                       /* plugin dso path */
    bool flood_tap;                  /* openflow flood enabled or not */
    int type;                        /* Session'service type */
    int64_t report_count;            /* mqtt reports counter */
    struct ev_loop *loop;            /* event loop */
    struct fsm_session *service;     /* service provider */
    ds_tree_node_t fsm_node;         /* Seesion manager node handle */
    char bridge[64];                 /* underlying bridge name */
    char tx_intf[64];                /* plugin's TX interface */
    union fsm_dpi_context *dpi;      /* fsm dpi context */
    char *provider;
    struct fsm_policy_client policy_client;
    struct fsm_session *provider_plugin;
    struct fsm_web_cat_ops *provider_ops;
};


/**
 * @brief manager's global context
 *
 * The manager keeps track of the various sessions.
 */
struct fsm_mgr
{
    ds_tree_t *mqtt_headers;  /* MQTT headers presented by ovsdb */
    char *location_id;        /* convenient mqtt location id pointer */
    char *node_id;            /* convenient mqtt node id pointer */
    struct ev_loop *loop;     /* event loop */
    ds_tree_t fsm_sessions;   /*  tree of fsm _sessions */
    ev_timer timer;           /* manager's event timer */
    time_t periodic_ts;       /* manager's periodic timestamp */
    char pid[16];             /* manager's pid */
    struct sysinfo sysinfo;   /* system information */
    uint64_t max_mem;         /* max amount of memory allowed in MB */
    bool (*init_plugin)(struct fsm_session *); /* DSO plugin init */
    bool (*flood_mod)(struct fsm_session *);   /* tap flood mode update */
    int (*get_br)(char *if_name, char *bridge, size_t len); /* get lan bridge */
    int (*set_dpi_state)(struct net_header_parser *net_hdr,
                         enum fsm_dpi_state state);
};


/**
 * @brief manager's memory usage counters
 */
struct mem_usage
{
    int curr_real_mem;
    char curr_real_mem_unit[8];
    int peak_real_mem;
    int curr_virt_mem;
    char curr_virt_mem_unit[8];
    int peak_virt_mem;
};


/**
 * @brief fsm manager accessor
 */
struct fsm_mgr *
fsm_get_mgr(void);


/**
 * @brief fsm sessions tree accessor
 */
ds_tree_t *
fsm_get_sessions(void);


/**
 * @brief frees a FSM session
 *
 * @param session a pointer to a FSM session to free
 */
void
fsm_free_session(struct fsm_session *session);


/**
 * @brief creates the pcap context for the session
 *
 * @param session the fsm session bound to the tap interface
 */
bool
fsm_pcap_open(struct fsm_session *session);


/**
 * @brief fsm manager init routine
 */
void
fsm_init_mgr(struct ev_loop *loop);


/**
 * @brief fsm manager delete routine
 */
void
fsm_reset_mgr(void);


/**
 * @brief deletes the pcap context for the session
 *
 * @param session the fsm session bound to the tap interface
 */
void fsm_pcap_close(struct fsm_session *session);


/**
 * @brief manager's ovsdb registration routine
 */
int
fsm_ovsdb_init(void);


/**
 * @brief manager's periodic work init routine
 */
void
fsm_event_init(void);


/**
 * @brief manager's periodic work exit routine
 */
void
fsm_event_close(void);


/**
 * @brief manager's memory usage counters gathering routine
 *
 * @param mem memory usage counters container
 */
void
fsm_get_memory(struct mem_usage *mem);

/**
 * @brief add a fsm session
 *
 * @param conf the ovsdb Flow_Service_Manager_Config entry
 */
void
fsm_add_session(struct schema_Flow_Service_Manager_Config *conf);


/**
 * @brief delete a fsm session
 *
 * @param conf the ovsdb Flow_Service_Manager_Config entry
 */
void
fsm_delete_session(struct schema_Flow_Service_Manager_Config *conf);


/**
 * @brief modify a fsm session
 *
 * @param conf the ovsdb Flow_Service_Manager_Config entry
 */
void
fsm_modify_session(struct schema_Flow_Service_Manager_Config *conf);


/**
 * @brief gather mqtt records from AWLAN_Node's mqtt headers table
 *
 * Records the mqtt_headers (locationId, nodeId) in the fsm manager
 * Update existing fsm sessions to point to the manager's records.
 * @param awlan AWLAN_Node record
 */
void
fsm_get_awlan_headers(struct schema_AWLAN_Node *awlan);


/**
 * @brief delete recorded mqtt headers
 */
void
fsm_rm_awlan_headers(void);


/**
 * @brief retrieves the value from the provided key in the
 * other_config value
 *
 * @param session the fsm session owning the other_config
 * @param conf_key other_config key to look up
 */
char *
fsm_get_other_config_val(struct fsm_session *session, char *key);


/**
 * @brief free the ovsdb configuration of the session
 *
 * Frees the fsm_session ovsdb conf settings
 * @param session the fsm session to update
 */
void
fsm_free_session_conf(struct fsm_session_conf *conf);


/**
 * @brief parse the session's other config map to find the
 * plugin dso
 *
 * @param session the session to parse
 */
bool
fsm_parse_dso(struct fsm_session *session);


/**
 * @brief send a json report over mqtt
 *
 * Emits and frees a json report
 * @param session the fsm session emitting the report
 * @param report the report to emit
 */
void
fsm_send_report(struct fsm_session *session, char *report);


/**
 * @brief create a web_cat session based on a service plugin
 *
 * Backward compatibility function: a parser plugin may hold the settings
 * for the web categorization service. Create a web categorization session
 * based on these settings.
 * @param session the parser session
 */
bool
fsm_dup_web_cat_session(struct fsm_session *session);

/**
 * @brief fsm_get_web_cat_service
 *
 * @param session the parser session requesting a service
 * @return true if the duplication succeeded, false otherwise
 */
bool
fsm_get_web_cat_service(struct fsm_session *session);


/**
 * @brief associate a web cat session to parser sessions
 *
 * @param session the web cat session
 * @param op add or delete
 */
void
fsm_web_cat_service_update(struct fsm_session *session, int op);

/**
 * @brief sets the session type based on the ovsdb values
 *
 * @param the ovsdb configuration
 * @return an integer representing the type of service
 */
int
fsm_service_type(struct schema_Flow_Service_Manager_Config *conf);

/**
 * @brief check if a fsm session is a dpi session
 *
 * @param session the session to check
 * @return true is the session is either a dpi dispatcher of a dpi plugin,
 *         false otherwise.
 */
bool
fsm_is_dpi(struct fsm_session *session);


/**
 * @brief initializes the dpi resources of a dispatcher session
 *
 * @param session the dispatcher session
 * @return true if the initialization succeeeded, false otherwise
 */
bool
fsm_init_dpi_dispatcher(struct fsm_session *session);


/**
 * @brief free the dpi resources of a dispatcher plugin
 *
 * @param session the session to free
 */
void
fsm_free_dpi_dispatcher(struct fsm_session *session);


/**
 * @brief binds existing dpi plugins to to a dispatcher session
 *
 * Upon the creation of a dpi dispatcher plugin, walk through
 * the existing plugins and bind the relevant dpi plugins
 * @param session the dispatcher plugin session
 */
void
fsm_dpi_bind_plugins(struct fsm_session *session);


/**
 * @brief initiates a dpi plugin session
 *
 * @param session a dpi plugin session to initialize
 * @return true if the initialization succeeded, false otherwise
 */
bool
fsm_init_dpi_plugin(struct fsm_session *session);


/**
 * @brief free the dpi resources of a dpi plugin
 *
 * @param session the session to free
 */
void
fsm_free_dpi_plugin(struct fsm_session *session);


/**
 * @brief updates the dpi reources of a dpi session
 *
 * Calls either the dispatcher or the dpi init plugin routine for the session
 * based on its type, or when the session gets updated
 * @param session the session to update
 * @return true if the update succeeded, false otherwise
 */
bool
fsm_update_dpi_context(struct fsm_session *session);


/**
 * @brief Add a dpi plugin to a dispatcher session
 *
 * @param session a dpi plugin session to add to its dispatcher
 * @return true if the addition succeeded, false otherwise
 */
bool
fsm_dpi_add_plugin_to_dispatcher(struct fsm_session *session);


/**
 * @brief released the dpi reources of a dpi session
 *
 * Calls either the dispatcher or the dpi plugin release routine for the session
 * based on its type
 * @param session the session to release
 */
void
fsm_free_dpi_context(struct fsm_session *session);


/**
 * @brief sets up a dispatcher plugin's function pointers
 *
 * Populates the dispatcher with the various function pointers
 * (packet handler, periodic routine)
 * @param session the dpi dispatcher session
 * @return 0 if successful.
 */
int
fsm_dispatch_set_ops(struct fsm_session *session);


/**
 * @brief retrieve the fsm dispatcher session for the given dpi plugin
 *
 * Looks up the dispatcher session as named in the other_config table.
 * @param session the dpi plugin session
 * @return the dispatcher session
 */
struct fsm_session *
fsm_dpi_find_dispatcher(struct fsm_session *session);


void
fsm_dpi_alloc_flow_context(struct fsm_session *session,
                           struct net_md_stats_accumulator *acc);


struct net_md_stats_accumulator *
fsm_net_parser_to_acc(struct net_header_parser *net_parser,
                      struct net_md_aggregator *aggr);


/**
 * @brief routine periodically called
 *
 * Periodically walks the ggregator and removes the outdated flows
 * @param session the dpi dispatcher session
 */
void
fsm_dpi_periodic(struct fsm_session *session);


/**
 * @brief mark the flow for report
 *
 * @param session the dpi plugin session marking the flow
 * @param acc the accumulator to mark for report
 */
void
fsm_dpi_mark_for_report(struct fsm_session *session,
                        struct net_md_stats_accumulator *acc);


/**
 * @brief sets the session type based on the ovsdb values
 *
 * @param session the fsm session
 * @return true if the plugin is bound to tap interface
 */
static inline bool
fsm_plugin_has_intf(struct fsm_session *session)
{
    if (session->type == FSM_WEB_CAT) return false;
    if (session->type == FSM_DPI_PLUGIN) return false;

    return true;
}

/**
 * @brief sets/update the Node_State ovsdb table with max mem usage
 *
 * Advertizes in Node_State the max amount of memory FSM is allowed to use
 * @param module the Node_State module name
 * @param key the Node_State key
 * @param key the Node_State value
 */
void
fsm_set_node_state(const char *module, const char *key, const char *value);


/**
 * @brief set a fsm policy provider
 *
 * Set a web cat provider service as indicated in the ovsdb other_config map.
 * If none is passed, point to wc_null
 * @param session the session to provide a service to.
 */
void
fsm_process_provider(struct fsm_session *session);


bool
fsm_pcap_update(struct fsm_session *session);

/**
 * @brief update the OMS_State table with the given object state
 *
 * @param session the fsm session triggering the call
 * @param object the object advertizing its state
 */
void
fsm_set_object_state(struct fsm_session *session, struct fsm_object *object);

#endif /* FSM_H_INCLUDED */
