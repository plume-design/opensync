UNIT_DISABLE := $(if $(CONFIG_MANAGER_FSM),n,y)

UNIT_NAME := test_dns_parse

UNIT_TYPE := TEST_BIN

UNIT_CFLAGS += -Isrc/fsm/inc
UNIT_CFLAGS += -Isrc/lib/imc/inc

UNIT_SRC := test_dns_parse.c
UNIT_SRC += ../../../fsm/src/fsm_ovsdb.c
UNIT_SRC += ../../../fsm/src/fsm_event.c
UNIT_SRC += ../../../fsm/src/fsm_service.c
UNIT_SRC += ../../../fsm/src/fsm_dpi.c
UNIT_SRC += ../../../fsm/src/fsm_oms.c
UNIT_SRC += ../../../fsm/src/fsm_internal.c
UNIT_SRC += ../../../fsm/src/fsm_dpi_client.c
UNIT_SRC += ../../../fsm/src/fsm_nfqueues.c
UNIT_SRC += ../../../fsm/src/fsm_raw.c
UNIT_SRC += $(if $(CONFIG_FSM_DPI_SOCKET), ../../../fsm/src/fsm_dispatch_listener.c)
UNIT_SRC += $(if $(CONFIG_FSM_TAP_INTF), ../../../fsm/src/fsm_pcap.c, ../../../fsm/src/fsm_pcap_stubs.c)

UNIT_DEPS := src/lib/log
UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/json_util
UNIT_DEPS += src/qm/qm_conn
UNIT_DEPS += src/lib/dns_parse
UNIT_DEPS += src/lib/unity
UNIT_DEPS += src/lib/fsm_utils
UNIT_DEPS += src/lib/fsm_policy
UNIT_DEPS += src/lib/oms
UNIT_DEPS += src/lib/gatekeeper_cache
UNIT_DEPS += src/lib/unit_test_utils
UNIT_DEPS += src/lib/network_zone
UNIT_DEPS += src/lib/dpi_intf
UNIT_DEPS += src/lib/dpi_stats
UNIT_DEPS += src/lib/accel_evict_msg
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/http_parse)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/mdns_plugin)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/upnp_parse)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/ndp_parse)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/gatekeeper_plugin)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/walleye)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/ipthreat_dpi)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/fsm_dpi_client)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/fsm_dpi_adt)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/fsm_dpi_dns)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/fsm_dpi_sni)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/fsm_dpi_ndp)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/fsm_dpi_mdns_responder)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/fsm_dpi_adt_upnp)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/wc_null_plugin)
UNIT_DEPS += $(if $(CONFIG_FSM_NO_DSO), src/lib/we_dpi)
