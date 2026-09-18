#!/bin/sh
# {# jinja-parse #}
INSTALL_PREFIX={{INSTALL_PREFIX}}
source ${INSTALL_PREFIX}/scripts/opensync_functions.sh
include_kconfig

for F in $INSTALL_PREFIX/scripts/stop.d/[0-9]*.sh; do
    if [ -e "$F" ]; then
        . "$F"
    fi
done

