#!/bin/sh

# The "TARGET_PATH_DISABLE_FATAL_STATE" file is created when we wish to disable
# CM fatal state reboot. On certain targets the root file system may not be
# writable, so we create the parent directory during pre-install. That way
# we may mount tmpfs during runtime and create the file to disable reboot.

ROOTFS="$1"
KCONFIG_PATH=${INSTALL_PREFIX}/etc/kconfig

. "${ROOTFS}/${KCONFIG_PATH}"

if test -n "$CONFIG_TARGET_PATH_DISABLE_FATAL_STATE"; then
    DISABLE_FATAL_STATE_DIR=$(dirname "$CONFIG_TARGET_PATH_DISABLE_FATAL_STATE")
    mkdir -p "${ROOTFS}/${DISABLE_FATAL_STATE_DIR}"
fi
