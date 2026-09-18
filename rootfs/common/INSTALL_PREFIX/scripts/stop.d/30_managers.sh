#!/bin/sh -e
# {# jinja-parse #}
INSTALL_PREFIX={{INSTALL_PREFIX}}

# NM/WM/SM can interact with Wi-Fi driver therefore
# wpa_supplicant and hostap must be stopped afterwards to
# avoid races and unexpected driver sequences.
# Postpone stopping fm to the very end so that we
# retain as much logs as possible.

echo "killing managers"
${INSTALL_PREFIX}/bin/dm --stop-all --except fm

# From this point on CM is stopped and no one is pinging the
# watchdog. There's about 60s to complete everything
# down below before watchdog reboots the device.
