#!/bin/false
# {# jinja-parse #}
INSTALL_PREFIX={{INSTALL_PREFIX}}

source ${INSTALL_PREFIX}/scripts/opensync_functions.sh

once LM_FUNCS || return 1

LM_DIR="${INSTALL_PREFIX}/log_archive"
LM_CRASH_DIR="$LM_DIR/crash"

LM_ROTATE_MAX=10

lm_crash_rotate()
{
    # Keep LM_ROTATE_MAX entries maximum
    find "$LM_DIR" -type f -name "crash-$LABEL-*.tar.gz" | sort | head -n "-${LM_ROTATE_MAX}" | xargs -r rm
}

#
# Store filenames passed as argument into a tarball in the crashlog folder
#
lm_crash_store()
{
    LABEL=$1
    shift
    # Create a unique filename
    LAST_N=$( { echo 0; find "${LM_DIR}/crash" -type f -name "crash-$LABEL-*.tar.gz" | grep -oE '[1-9][0-9]*' | sort -n; } | tail -n 1)
    F="$LM_DIR/crash/crash-$LABEL-$(printf "%04d" $((LAST_N + 1))).tar.gz"
    # Store files to the crashlog folder
    log_info "Creating crashlog $F ..."
    tar -czvf "$F" "$@"

    # Remove old entries
    lm_crash_rotate "$LABEL"
}
