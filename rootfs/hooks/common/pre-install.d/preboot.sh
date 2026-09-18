#!/bin/sh

if [ $# -ne 1 ]; then
    echo "Usage: `basename $0` <rootfs-path>"
    exit 1
fi

ROOTFS="$1"

# Override the system's reboot command if CONFIG_OSP_REBOOT_CLI_OVERRIDE is set
if [ "$CONFIG_OSP_REBOOT_CLI_OVERRIDE" = y ]
then
    mkdir -p "${ROOTFS}/sbin/"
    ln -sf "${INSTALL_PREFIX}/tools/preboot" "${ROOTFS}/sbin/reboot"
fi
