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

#ifndef FSM_INTERNAL_H_INCLUDED
#define FSM_INTERNAL_H_INCLUDED

#include "fsm.h"
#include "network_metadata_report.h"
#include "policy_tags.h"

/*
"other_config":["map",[["excluded_devices","${all_gateways}"],["nfqueues","${fsm_nfqueues}"],
    ["tap_type","fsm_tap_nfqueues"]]]

    other_config -> no schema modification just parse the value (string or map or array or whatnot)
    NOTHING == PCAP
    TAP_PCAP == PCAP
    TAP_NFQ == NFQ
    TAP_PCAP | TAP_NFQ == both
*/
enum
{
    FSM_TAP_NONE = 0x00,
    FSM_TAP_PCAP = 0x01,
    FSM_TAP_NFQ = 0x02,
    FSM_TAP_RAW = 0x04,
};

struct reg_client_session
{
    char *name;
    struct fsm_session *session;
    ds_tree_node_t next;
};

struct dpi_client
{
    char *attr;              /* Name of the monitored attribute */
    ds_tree_t reg_sessions;  /* This is a container of struct reg_client_session */
    uint32_t num_sessions;   /* Number of clients monitoring the attribute */
    ds_tree_node_t next;
};

struct fsm_dpi_client_tags
{
    char *name;
    char *client_plugin_name;
    ds_tree_node_t next;
};

/**
 * @brief returns the tapping mode for a session
 *
 * @param session the fsm session to probe
 * @return the tapping mode (each bit enabled represents one tap)
 */
uint32_t
fsm_session_tap_mode(struct fsm_session *session);


/**
 * @brief update pcap settings for the given session
 *
 * @param session the fsm session involved
 * @return true if the pcap settings were successful, false otherwise
 */
bool
fsm_pcap_tap_update(struct fsm_session *session);


/**
 * @brief update nfqueues settings for the given session
 *
 * @param session the fsm session involved
 * @return true if the nfqueue settings were successful, false otherwise
 */
bool
fsm_nfq_tap_update(struct fsm_session *session);


/**
 * @brief update raw socket settings for the given session
 *
 * @param session the fsm session involved
 * @return true if the raw sockets settings were successful, false otherwise
 */
bool
fsm_raw_tap_update(struct fsm_session *session);


/**
 * @brief Initializes the tap context for the given session
 *
 * @param session the fsm session to probe
 * @return true if success, false otherwise
 */
bool
fsm_update_session_tap(struct fsm_session *session);


/**
 * @brief frees fsm tap resources pcap, nfqueues and raw socket
 *
 * @param session the fsm session to probe
 */
void
fsm_free_tap_resources(struct fsm_session *session);


/**
 * @brief initialize nfqueues
 *
 * @param session the fsm session involved
 * @param queue_num nfqueue queue number
 * @return 0 if the nfqueue initialization successful, -1 otherwise
 */
int
fsm_nfqueues_init(struct fsm_session *session, int queue_num);


/**
 * @brief free allocated nfqueue resources
 *
 * @param session the fsm session involved
 */
void
fsm_nfq_close(struct fsm_session *session);


/**
 * @brief check if a fsm session is a dpi client session
 *
 * @param session the session to check
 * @return true is the session is a dpi plugin client,
 *         false otherwise.
 */
bool
fsm_is_dpi_client(struct fsm_session *session);


void
fsm_process_tag_update(om_tag_t *tag,
                       struct ds_tree *removed,
                       struct ds_tree *added,
                       struct ds_tree *updated);

/**
 * @brief initializes a dpi plugin client session
 *
 * @param session the session to initialize
 * @return true if the initialization succeeded, false otherwise
 */
bool
fsm_update_dpi_plugin_client(struct fsm_session *session);

/**
 * @brief registers a dpi client to a dpi plugin for a specific flow attribute
 *
 * @param dpi_plugin_session the dpi plugin to register to
 * @param dpi_client_session the registering dpi client
 * @param attr the flow attribute
 *
 * Stores the flow attribute <-> session on behalf of the dpi plugin,
 * and triggers the dpi specific binding
 */
void
fsm_dpi_register_client(struct fsm_session *dpi_plugin_session,
                        struct fsm_session *dpi_client_session,
                        char *attr);

/**
 * @brief unregister the dpi client for the given attribute
 *
 * @param dpi_plugin_session dpi plugin to unregister
 * @param dpi_client_session the dpi client to unregister
 * @param attr the flow attribute
 */
void
fsm_dpi_unregister_client(struct fsm_session *dpi_plugin_session,
                          struct fsm_session *dpi_client_session,
                          char *attr);

/**
 * @brief wrap plugin initialization
 *
 * @param session the session getting initialized
 * @return true if successful, false otherwise
 *
 * Completes the fsm core initialization of the plugin
 * after the plugin initialization routine was called
 */
bool
fsm_wrap_init_plugin(struct fsm_session *session);

/**
 * @brief registers a dpi client to a dpi plugin for a specific flow attribute
 *
 * @param dpi_plugin_session the dpi plugin to register to
 * @param dpi_client_session the registering dpi client
 * @param attr the flow attribute
 *
 * Stores the flow attribute <-> session on behalf of the dpi plugin,
 * and triggers the dpi specific binding
 */
void
fsm_dpi_unregister_clients(struct fsm_session *dpi_plugin_session);

/**
 * @brief registers dpi clients to a dpi plugin
 *
 * @param dpi_plugin_session the dpi plugin to register to
 */
void
fsm_dpi_register_clients(struct fsm_session *dpi_plugin_session);

/**
 * @brief call back registered client to process a flow attribute and its value
 *
 * @param dpi_plugin_session the dpi plugin session
 * @param attr the attribute to trigger the report
 * @param value the value of the attribute
 * @return the action to take
 */
int
fsm_dpi_call_client(struct fsm_session *dpi_plugin_session, const char *attr,
                    uint8_t type, uint16_t length, const void *value,
                    struct fsm_dpi_plugin_client_pkt_info *pkt_info);

int
fsm_nfq_set_verdict(struct fsm_session *session, int action);

/**
 * @brief build verdict for nfqueue received packets
 *
 * @param buf netlink header buffer
 * @param id nfqueue packet id
 * @param queue_num nfqueue queue number
 * @param verd nfqueue packet verdict
 * @return nlmsghdr pointer to netlink header
*/
struct nlmsghdr *
nfq_build_verdict(char *buf, int id, int queue_num, int verd);


/**
 * @brief free the dpi resources of a dpi_plugin_client session
 *
 * @param session the session to free
 */
void
fsm_free_dpi_plugin_client(struct fsm_session *session);

/*
 * Exposed for unit-test
  */
uint32_t
fsm_tap_type_from_str(char *conf_type);

void
fsm_print_one_dpi_client(struct dpi_client* client);

void
fsm_print_dpi_clients(ds_tree_t *tree);

void
fsm_notify_dispatcher_tap_type(struct fsm_session *session);

void
fsm_notify_identical_sessions(struct fsm_session *session, bool);


/**
 * @brief check if a packet should be processed
 *
 * @param the parsed info for the current packet
 * @param included_targets tag representing the included targets
 * @param excluded_targets tag representing the excluded targets
 *
 * check if a packet should be procesed based on its the source and destination
 */
bool
fsm_dpi_should_process(struct net_header_parser *net_parser,
                       char *included_targets,
                       char *excluded_targets);

#endif /* FSM_INTERNAL_H_INCLUDED */
