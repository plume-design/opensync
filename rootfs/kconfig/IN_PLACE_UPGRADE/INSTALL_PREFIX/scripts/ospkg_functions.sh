#!/bin/sh
# {# jinja-parse #}

OPENSYNC_INSTALL_PREFIX={{CONFIG_INSTALL_PREFIX}}

# ospkg tool can be either in /usr/opensync/tools
# or in /ospkg/tools for boot-time preinit
# derive the dir from the cmd
OSPKG_INSTALL_PREFIX=$(dirname $(dirname $(readlink -f "$0")))
INSTALL_PREFIX="$OSPKG_INSTALL_PREFIX"
SCRIPTS_DIR="$INSTALL_PREFIX/scripts"

. "$SCRIPTS_DIR/opensync_functions.sh"

INSTALL_PREFIX="$OSPKG_INSTALL_PREFIX"
SCRIPTS_DIR="$INSTALL_PREFIX/scripts"

for F in $SCRIPTS_DIR/ospkg.d/[0-9]*.sh; do
    if [ -e "$F" ]; then
        . "$F"
    fi
done

