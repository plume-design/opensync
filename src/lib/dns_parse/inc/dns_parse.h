#ifndef DNS_PARSE_H_INCLUDED
#define DNS_PARSE_H_INCLUDED

#include <linux/if_packet.h>
#include <pcap.h>
#include <sys/ioctl.h>
#include <stdint.h>

#include "network.h"
#include "os_types.h"
#include "ds_tree.h"
#include "fsm.h"
#include "fsm_policy.h"


struct web_cat_offline
{
    time_t offline_ts;
    time_t check_offline;
    bool provider_offline;
    uint32_t connection_failures;
};

#define MAX_TAG_VALUES_LEN 64
#define MAX_TAG_NAME_LEN 64
#define MAX_EXCLUDES 100
struct dns_session
{
    uint16_t EXCLUDED[MAX_EXCLUDES];
    uint16_t EXCLUDES;
    char SEP;
    char * RECORD_SEP;
    int AD_ENABLED;
    int NS_ENABLED;
    int COUNTS;
    int PRETTY_DATE;
    int PRINT_RR_NAME;
    int MISSING_TYPE_WARNINGS;
    uint32_t DEDUPS;
    eth_info eth_hdr;
    eth_config eth_config;
    ip_info ip;
    ip_config ip_config;
    transport_info udp;
    tcp_config tcp_config;
    uint32_t dedup_pos;
    struct sockaddr_ll raw_dst;
    os_macaddr_t src_eth_addr;
    int sock_fd;
    int post_eth;
    int data_offset;
    int last_byte_pos;
    struct fsm_session *fsm_context;
    time_t stat_report_ts;
    time_t stat_log_ts;
    bool debug;
    bool cache_ip;
    char *blocker_topic;
    uint8_t debug_pkt_copy[512];
    uint8_t debug_pkt_len;
    int32_t reported_lookup_failures;
    int32_t remote_lookup_retries;
    ds_tree_node_t session_node;
    ds_tree_t session_devices;
    long health_stats_report_interval;
    char *health_stats_report_topic;
    struct fsm_url_stats health_stats;
    struct web_cat_offline cat_offline;
    struct fqdn_pending_req *req;
    bool initialized;
};


/* Holds the information for a dns question. */
typedef struct dns_question
{
    char * name;
    uint16_t type;
    uint16_t cls;
    struct dns_question * next;
} dns_question;

/* Holds the information for a dns resource record. */
typedef struct dns_rr
{
    char * name;
    uint16_t type;
    uint32_t type_pos;
    uint16_t cls;
    const char * rr_name;
    uint16_t ttl;
    uint16_t rdlength;
    uint16_t data_len;
    char * data;
    struct dns_rr * next;
} dns_rr;

/* Holds general DNS information. */
typedef struct
{
    uint16_t id;
    char qr;
    char AA;
    char TC;
    uint8_t Z;
    uint8_t rcode;
    uint8_t opcode;
    uint16_t qdcount;
    dns_question * queries;
    uint16_t ancount;
    dns_rr * answers;
    uint32_t answer_pos;
    uint16_t nscount;
    dns_rr * name_servers;
    uint16_t arcount;
    dns_rr * additional;
} dns_info;


struct dns_cache
{
    bool initialized;
    ds_tree_t fsm_sessions;
    int req_cache_ttl;
    int (*set_forward_context)(struct fsm_session *);
    void (*forward)(struct dns_session *, dns_info *, uint8_t *, int);
    void (*update_tag)(struct fqdn_pending_req *);
    void (*policy_init)(void);
    void (*policy_check)(struct dns_device *, struct fqdn_pending_req *);
};


#define FORCE 1

#define REQ_CACHE_TTL 120

/*
 * Parse DNS from from the given 'packet' byte array starting at offset 'pos',
 * with libpcap header information in 'header'.
 * The parsed information is put in the 'dns' struct, and the
 * new pos in the packet is returned. (0 on error).
 * The config struct gives needed configuration options.
 * force - Force fully parsing the dns data, even if
 *    configuration parameters mean it isn't necessary. If this is false,
 *  the returned position may not correspond with the end of the DNS data.
 */
uint32_t
dns_parse(uint32_t pos, struct pcap_pkthdr *header,
          uint8_t *packet, dns_info * dns,
          struct dns_session *dns_session, uint8_t force);

void
free_rrs(ip_info * ip, transport_info * trns, dns_info * dns,
         struct pcap_pkthdr * header);

void
dns_handler(struct fsm_session *session,
            struct net_header_parser *net_header);

int
dns_plugin_init(struct fsm_session *session);

void
dns_plugin_exit(struct fsm_session *session);

void
dns_remove_req(struct dns_session *dns_session, os_macaddr_t *mac,
               uint16_t req_id);


/**
 * @brief create updated row for OF Tag with newly matched IPs
 *
 * @param        req          request with update fields loaded
 * @param[out]   values       buffer to update values.
 * @param[out]   values_len   length of the values updated.
 * @param[in]    max_capacity the buffer maximum capacity
 * @param[in]    ip_ver       the IP protocol version
 *
 * @return true loaded correctly built struct into output
 * @return false output struct not built
 */
bool
dns_generate_update_tag(struct fqdn_pending_req *req,
                        char values[][MAX_TAG_VALUES_LEN],
                        int *values_len, size_t max_capacity,
                        int ip_ver);

typedef bool (*dns_ovsdb_updater)(const char *, const char *,
                                  const char *, json_t *, ovs_uuid_t *);
/**
 * @brief update Openflow_Tag to map to new row
 *
 * @param       row      new row to be written to Openflow_Tag
 * @param       updater  dependency injection for updating
 *
 * @return      true     succeeded in update
 * @return      false    failed to update
 */
bool
dns_upsert_regular_tag(struct schema_Openflow_Tag *row, dns_ovsdb_updater updater);

/**
 * @brief update Openflow_Tag to map to new row
 *
 * @param       row      new row to be written to Openflow_Tag
 * @param       updater  dependency injection for updating
 *
 * @return      true     succeeded in update
 * @return      false    failed to update
 */
bool
dns_upsert_local_tag(struct schema_Openflow_Local_Tag *row, dns_ovsdb_updater updater);

void
dns_forward(struct dns_session *dns_session, dns_info *dns,
            uint8_t *packet, int len);

void
dns_update_tag(struct fqdn_pending_req *req);

void
dns_periodic(struct fsm_session  *session);

char *
dns_report_cat(int category);

void
fqdn_policy_check(struct dns_device *ds,
                  struct fqdn_pending_req *req);

void
dns_policy_check(struct dns_device *ds,
                 struct fqdn_pending_req *req);

void
dns_retire_reqs(struct fsm_session *session);

struct dns_session *
dns_lookup_session(struct fsm_session *session);

struct dns_session *
dns_get_session(struct fsm_session *session);

struct dns_cache *
dns_get_mgr(void);

void
dns_mgr_init(void);

#endif /* DNS_PARSE_H_INCLUDED */
