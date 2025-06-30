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

#include <ev.h>
#include <inttypes.h>

#include "blem_connectivity_status.h"
#include "const.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "schema_consts.h"
#include "util.h"

#define LOG_PREFIX "[CSb] "

/* Types */

/** Connectivity Status bitfield type */
typedef uint8_t csb_t;

/** Structure of the timer data stored in the debounce timer, see @ref timer_data_get */
typedef union
{
    struct __attribute__((__packed__))
    {
        struct __attribute__((__packed__))
        {
            csb_t cmu; /**< Intermediate status bitfield based on the Connection_Manager_Uplink OVSDB table */
            csb_t mgr; /**< Intermediate status bitfield based on the Manager OVSDB table */
        };
        csb_t current;  /**< Status bitfield computed from `cmu` and `mgr` */
        csb_t reported; /**< Last status bitfield passed to the user callback */
    } status;
    uintptr_t data;
} timer_data_t;

/* Variables */

/** Connection_Manager_Uplink OVSDB table object */
static ovsdb_table_t table_Connection_Manager_Uplink;
/** Manager OVSDB table object */
static ovsdb_table_t table_Manager;

/** Callback function to be called when connectivity status value changes */
static blem_connectivity_status_updated_cb_t g_callback = NULL;

/** Debounce timer used to delay the callback call */
static ev_timer g_debounce_timer;

/* Helper macros */

/**
 * Macro expression for string representation of optional OVSDB integer field value
 *
 * @param[out] dest         Pointer to a buffer where the resulting C-string will be stored.
 * @param[in]  field        OVSDB schema field of which value to convert.
 * @param[in]  default_str  Default string to use if `field` value does not exist.
 *
 * @return `dest` if `<field>_exists` is true and string representation of `field`
 *         was written to `dest`, `default_str` otherwise.
 */
#define INT_Q_STR(dest, field, default_str)                                          \
    ({                                                                               \
        C_STATIC_ASSERT((sizeof(dest) >= 12), "11+1 chars required for int string"); \
        (field##_exists) ? SPRINTF((dest), "%" PRId32, (field)) : ((void)0);         \
        (field##_exists) ? (dest) : (default_str);                                   \
    })

/**
 * Macro expression expanding to `field` if `<field>_exists` is true, or `default_str` otherwise
 *
 * @param[in] field        `PJS_OVS_STRING_Q` or `PJS_OVS_STRING` type OVSDB schema field.
 * @param[in] default_str  Default string to use if provided schema `field` does not exist.
 */
#define STR_Q_STR(field, default_str) ((field##_exists) ? (field) : (default_str))

/* Helper functions */

/** Check if null-terminated strings `a` and `b` are equal */
static bool streq(const char *const a, const char *const b)
{
    return STRSCMP(a, b) == 0;
}

/** Check if this `Connection_Manager_Uplink::if_type` is Ethernet physical interface */
static bool cmu_is_if_type_eth(const char *const if_type)
{
    return streq(if_type, SCHEMA_CONSTS_IF_TYPE_ETH) || streq(if_type, SCHEMA_CONSTS_IF_TYPE_VLAN);
}

/** Check if this `Connection_Manager_Uplink::if_type` is Wi-Fi physical interface */
static bool cmu_is_if_type_wifi(const char *const if_type)
{
    return streq(if_type, SCHEMA_CONSTS_IF_TYPE_VIF) || streq(if_type, SCHEMA_CONSTS_IF_TYPE_GRE);
}

/** Check if this `Connection_Manager_Uplink::ipv4` or `ipv6` IP state is active */
static bool cmu_is_ip_active(const char *const ip_state)
{
    return streq(ip_state, "active");
}

/** Get the bitfield value for the status `bit` */
static csb_t status_bitfield(const ble_connectivity_status_bit_t bit)
{
    return (1u << bit);
}

/** Check if the status `bit` in the `status` bitfield is set */
static bool status_bit_get(const csb_t status, const ble_connectivity_status_bit_t bit)
{
    return (status & (1u << bit)) != 0;
}

/** Check if the `status` bitfield is valid, i.e., not in the unknown state */
static bool status_is_valid(const csb_t status)
{
    return !status_bit_get(status, BLE_ONBOARDING_STATUS_BIT__UNKNOWN);
}

/** Get the pointer to the status data structure stored in the `timer` */
static timer_data_t *timer_data_get(ev_timer *const timer)
{
    /* timer_data_t is designed to fit into the timer's `data` field *value*, ensure it fits */
    C_STATIC_ASSERT(sizeof(timer->data) >= sizeof(timer_data_t), "timer_data_t does not fit in ev_timer.data");

    /* Return *pointer to* the `data` field, which is repurposed as the union value and not a valid user-data pointer */
    return (timer_data_t *)(uintptr_t *)&(timer->data);
}

/* Main logic */

static void debounce_timer_callback(struct ev_loop *const loop, ev_timer *const timer, const int r_events)
{
    timer_data_t *const td = timer_data_get(timer);
    (void)r_events;

    ev_timer_stop(loop, timer);

    LOGI(LOG_PREFIX "0x%02X -> 0x%02X", td->status.reported, td->status.current);

    if (g_callback != NULL)
    {
        /* Callback should always receive valid status value */
        g_callback(status_is_valid(td->status.reported) ? td->status.reported : 0, td->status.current);
    }
    td->status.reported = td->status.current;
}

/**
 * Update the BLE connectivity status and handle debounce timer
 *
 * @param[in] mon  OVSDB update monitor that triggered this update (for logging).
 */
static void update_connectivity_status(const ovsdb_update_monitor_t *const mon)
{
    timer_data_t *const td = timer_data_get(&g_debounce_timer);
    csb_t new_status = 0;

    if (status_is_valid(td->status.cmu))
    {
        /* Connection_Manager_Uplink table provides all flags except the cloud connectivity */
        new_status |= (td->status.cmu & ~status_bitfield(BLE_ONBOARDING_STATUS_BIT_CONNECTED_TO_CLOUD));
    }
    if (status_is_valid(td->status.mgr))
    {
        /* Manager table provides only the cloud connectivity flag */
        new_status |= (td->status.mgr & status_bitfield(BLE_ONBOARDING_STATUS_BIT_CONNECTED_TO_CLOUD));
    }

    LOGD(LOG_PREFIX "0x%02X (%02X/%02X) %s 0x%02X (%s %s), debounce %.3f/%.3f s, reported 0x%02X",
         td->status.current,
         td->status.cmu,
         td->status.mgr,
         (new_status == td->status.current) ? "==" : "->",
         new_status,
         mon->mon_table,
         ovsdb_update_type_to_str(mon->mon_type),
         ev_timer_remaining(EV_DEFAULT, &g_debounce_timer),
         g_debounce_timer.repeat,
         td->status.reported);

    /* This function can be called multiple times with the same status value,
     * as various CMU rows changes can still result in the same overall status
     * combination. As we are only reporting such overall status, do not restart
     * debounce timer in such cases when the overall status is in fact stable. */
    if (new_status == td->status.current)
    {
        return;
    }
    td->status.current = new_status;

    /* Debounce feature can cause that `debounce_timer_callback()` is called
     * multiple times with the same status value, e.g., if status changes
     * from A->B->C->A within the debounce period. This could be handled
     * in the debounce callback - handled by skipping the update of the
     * same value. So don't even start the debounce timer in such case. */
    if (new_status == td->status.reported)
    {
        if (ev_is_active(&g_debounce_timer))
        {
            LOGD(LOG_PREFIX "Debounce cancel");
            ev_timer_stop(EV_DEFAULT, &g_debounce_timer);
            ev_clear_pending(EV_DEFAULT, &g_debounce_timer);
        }
        return;
    }

    if (g_debounce_timer.repeat > 0)
    {
        /* If debounce timer is currently running, then the debouncing period will
         * be extended (restarted), otherwise the timer will be started. */
        LOGD(LOG_PREFIX "Debounce %s", ev_is_active(&g_debounce_timer) ? "reset" : "start");
        ev_timer_again(EV_DEFAULT, &g_debounce_timer);
    }
    else
    {
        /* Debounce is disabled, invoke the callback later in this event loop iteration */
        LOGT(LOG_PREFIX "Debounce disabled");
        ev_feed_event(EV_DEFAULT, &g_debounce_timer, 0);
    }
}

/**
 * Get BLE onboarding status bitmask for the given `Connection_Manager_Uplink` entry
 *
 * @param[in] cmu  `Connection_Manager_Uplink` row.
 *
 * @return Bitmask representing connectivity status of the given link.
 */
static csb_t get_cmu_link_ble_status(const struct schema_Connection_Manager_Uplink *const cmu)
{
    csb_t status = 0;

    if (!cmu->has_L2)
    {
        /* No physical link */
        return status;
    }

    if (cmu_is_if_type_eth(cmu->if_type))
    {
        status |= status_bitfield(BLE_ONBOARDING_STATUS_BIT_ETH_PHY_LINK);
    }
    else if (cmu_is_if_type_wifi(cmu->if_type))
    {
        status |= status_bitfield(BLE_ONBOARDING_STATUS_BIT_WIFI_PHY_LINK);
    }
    /* There are no dedicated status bits for other types of physical interfaces */

    if (!cmu->is_used)
    {
        /* Unused link is not used for backhaul or connectivity */
        return status;
    }

    if (status_bit_get(status, BLE_ONBOARDING_STATUS_BIT_ETH_PHY_LINK))
    {
        status |= status_bitfield(BLE_ONBOARDING_STATUS_BIT_BACKHAUL_OVER_ETH);
    }
    else if (status_bit_get(status, BLE_ONBOARDING_STATUS_BIT_WIFI_PHY_LINK))
    {
        status |= status_bitfield(BLE_ONBOARDING_STATUS_BIT_BACKHAUL_OVER_WIFI);
    }

    if (!((cmu->ipv4_exists && cmu_is_ip_active(cmu->ipv4)) || (cmu->ipv6_exists && cmu_is_ip_active(cmu->ipv6))))
    {
        /* Backhaul link needs an active IP to be used for connectivity */
        return status;
    }

    if (cmu->unreachable_router_counter == 0)
    {
        status |= status_bitfield(BLE_ONBOARDING_STATUS_BIT_CONNECTED_TO_ROUTER);
    }

    if (cmu->unreachable_internet_counter == 0)
    {
        status |= status_bitfield(BLE_ONBOARDING_STATUS_BIT_CONNECTED_TO_INTERNET);
    }

    if (cmu->unreachable_cloud_counter == 0)
    {
        /* `Manager.is_connected` is the source of truth for cloud connectivity,
         * but CM also monitors it and updates this counter accordingly. */
        status |= status_bitfield(BLE_ONBOARDING_STATUS_BIT_CONNECTED_TO_CLOUD);
    }

    return status;
}

/**
 * Callback invoked when the `Connection_Manager_Uplink` table content changes
 *
 * @param[in] mon      OVSDB update monitor.
 * @param[in] old_rec  Row before the update.
 * @param[in] new_rec  Row after the update (current state).
 */
static void callback_Connection_Manager_Uplink(
        ovsdb_update_monitor_t *const mon,
        struct schema_Connection_Manager_Uplink *const old_rec,
        struct schema_Connection_Manager_Uplink *const new_rec)
{
    struct schema_Connection_Manager_Uplink *cmu_rows;
    int count = 0;
    csb_t status = 0;
    (void)old_rec;
    (void)new_rec;

    /* CMU table is a main table of Connection Manager, providing information about link states.
     * We could manage internal states of active links here and update it by tracking all CMU
     * row changes, but considering the changes we are observing are not so frequent, it is
     * better to just query the table when needed - enabling the stateless design. */
    cmu_rows = ovsdb_table_select_typed(
            &table_Connection_Manager_Uplink,
            SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2),
            OCLM_BOOL,
            "true",
            &count);

    for (int i = 0; i < count; i++)
    {
        const struct schema_Connection_Manager_Uplink *const cmu = &cmu_rows[i];
        const csb_t link_status = get_cmu_link_ble_status(cmu);
        char int_str[3][12];

        status |= link_status;

        LOGD(LOG_PREFIX "Link[%d]: name=%s, type=%s, L2=%d, L3=%d, used=%d, IPv4=%s, IPv6=%s, "
                        "urc=%s, uic=%s, ucc=%s, status=0x%02X",
             i,
             cmu->if_name,
             cmu->if_type,
             cmu->has_L2,
             cmu->has_L3,
             cmu->is_used,
             STR_Q_STR(cmu->ipv4, "/"),
             STR_Q_STR(cmu->ipv6, "/"),
             INT_Q_STR(int_str[0], cmu->unreachable_router_counter, "/"),
             INT_Q_STR(int_str[1], cmu->unreachable_internet_counter, "/"),
             INT_Q_STR(int_str[2], cmu->unreachable_cloud_counter, "/"),
             link_status);
    }
    FREE(cmu_rows);

    timer_data_get(&g_debounce_timer)->status.cmu = status;

    update_connectivity_status(mon);
}

/**
 * Callback invoked when the `Manager` table content changes
 *
 * @param[in] mon      OVSDB update monitor.
 * @param[in] old_rec  Row before the update.
 * @param[in] new_rec  Row after the update (current state).
 */
static void callback_Manager(
        ovsdb_update_monitor_t *const mon,
        struct schema_Manager *const old_rec,
        struct schema_Manager *const new_rec)
{
    csb_t status = 0;
    (void)old_rec;

    if ((new_rec != NULL) && new_rec->is_connected)
    {
        status |= status_bitfield(BLE_ONBOARDING_STATUS_BIT_CONNECTED_TO_CLOUD);
    }
    timer_data_get(&g_debounce_timer)->status.mgr = status;

    update_connectivity_status(mon);
}

/* Public API */

void blem_connectivity_status_init(const blem_connectivity_status_updated_cb_t callback, const float debounce)
{
    OVSDB_TABLE_INIT_NO_KEY(Connection_Manager_Uplink);
    OVSDB_TABLE_INIT_NO_KEY(Manager);

    g_callback = callback;

    ev_timer_init(&g_debounce_timer, debounce_timer_callback, 0, MAX(0, debounce));
    /* Invalidate all statuses (see `status_is_valid()`) */
    memset(timer_data_get(&g_debounce_timer),
           status_bitfield(BLE_ONBOARDING_STATUS_BIT__UNKNOWN),
           sizeof(timer_data_t));

    /* Only monitor fields which are used in `get_cmu_link_ble_status()` */
    OVSDB_TABLE_MONITOR_F(
            Connection_Manager_Uplink,
            C_VPACK(SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2),
                    SCHEMA_COLUMN(Connection_Manager_Uplink, if_type),
                    SCHEMA_COLUMN(Connection_Manager_Uplink, ipv4),
                    SCHEMA_COLUMN(Connection_Manager_Uplink, ipv6),
                    SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_router_counter),
                    SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_internet_counter),
                    SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_cloud_counter)));
    /* Only monitor fields which are used in `callback_Manager()` */
    OVSDB_TABLE_MONITOR_F(Manager, C_VPACK(SCHEMA_COLUMN(Manager, is_connected)));

    LOGI(LOG_PREFIX "Initialized (debounce %.3f s)", g_debounce_timer.repeat);
}

bool blem_connectivity_status_is_connected_to_internet(const uint8_t status)
{
    return status_is_valid(status) && status_bit_get(status, BLE_ONBOARDING_STATUS_BIT_CONNECTED_TO_INTERNET);
}

bool blem_connectivity_status_is_connected_to_cloud(const uint8_t status)
{
    return status_is_valid(status) && status_bit_get(status, BLE_ONBOARDING_STATUS_BIT_CONNECTED_TO_CLOUD);
}

void blem_connectivity_status_fini(void)
{
    ev_timer_stop(EV_DEFAULT, &g_debounce_timer);
    ovsdb_table_fini(&table_Connection_Manager_Uplink);
    ovsdb_table_fini(&table_Manager);
    g_callback = NULL;
}
