#!/bin/sh
# {# jinja-parse #}

# Create a symlink for `logread` to `logread-osync` to ensure that the
# `logread` command is available in the PATH.
# If the system provides its own `logread` implementation, it is still
# used by default, as it precedes the INSTALL_PREFIX directory in PATH.

LOGREAD_OSYNC_PATH="${INSTALL_PREFIX}/tools/logread-osync"
LOGREAD_PATH="${INSTALL_PREFIX}/tools/logread"

# The `LOGREAD_PATH` is not expected to exist by default, but still
# check for it to avoid unnecessary symlink creation or overwriting.
if [ -x "$LOGREAD_OSYNC_PATH" ] && [ ! -e "$LOGREAD_PATH" ]; then
    ln -s "$LOGREAD_OSYNC_PATH" "$LOGREAD_PATH"
fi
