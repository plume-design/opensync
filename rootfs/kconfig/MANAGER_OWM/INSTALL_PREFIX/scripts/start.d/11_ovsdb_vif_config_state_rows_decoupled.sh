#!/bin/sh -e
# {# jinja-parse #}
INSTALL_PREFIX={{INSTALL_PREFIX}}
CONFIG_DIR="/run/opensync/osw"
CONFIG_FILE=$CONFIG_DIR/"11_ovsdb_vif_config_state_rows_decoupled.conf"

# VIF CONFIG STATE ROWS DECOUPLED is a functionality that allows
# the device to change Wifi_VIF_State table regardless if the Cloud
# has changes anything in Wifi_VIF_Config

mkdir -p $CONFIG_DIR
if ${INSTALL_PREFIX}/tools/osff_get "use_vif_rows_decoupled"; then
    echo "OW_OVSDB_VIF_CONFIG_STATE_ROWS_DECOUPLED=y" > $CONFIG_FILE
else
    echo > $CONFIG_FILE
fi
