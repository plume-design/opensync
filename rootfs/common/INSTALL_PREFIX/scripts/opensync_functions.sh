#!/bin/sh
# {# jinja-parse #}

INSTALL_PREFIX={{INSTALL_PREFIX}}
SCRIPTS_DIR="${INSTALL_PREFIX}/scripts"

once()
{
    eval [ -z \${$1} ] || return 1
    readonly "$1"=1
    return 0
}

once OPENSYNC_FUNCS || return 1

include()
{
    source "$SCRIPTS_DIR/$1.funcs.sh"
}

for F in $SCRIPTS_DIR/functions.d/[0-9]*.sh; do
    if [ -e "$F" ]; then
        . "$F"
    fi
done

return 0

