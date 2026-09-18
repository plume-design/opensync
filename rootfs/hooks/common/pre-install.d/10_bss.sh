#!/bin/sh

# disable healthcheck.d/bss if platform specific bss_is_up is not found

ROOTFS="$1"
SCRIPTS="$ROOTFS$INSTALL_PREFIX/scripts"
BSS_D="healthcheck.bss.d"

for F in "$SCRIPTS/healthcheck.d"/??_bss.sh; do
    test -e "$F" || continue
    grep -q "$BSS_D" "$F" || continue
    if ! stat "$SCRIPTS/$BSS_D"/* >/dev/null 2>&1; then
        rm -f "$F"
        echo "  removed '$F'"
    fi
done

