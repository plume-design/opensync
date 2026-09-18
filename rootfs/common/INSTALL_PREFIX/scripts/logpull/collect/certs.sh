#!/bin/sh
#
# Collect certs info
#
. "$LOGPULL_LIB"

collect_certs()
{
    PRIV_CERT="$CONFIG_TARGET_PATH_CERT/$CONFIG_TARGET_PATH_PRIV_CERT"
    CERT_CA="$CONFIG_TARGET_PATH_CERT/$CONFIG_TARGET_PATH_CERT_CA"
    OPENSYNC_CAFILE="$CONFIG_TARGET_PATH_OPENSYNC_CERTS/$CONFIG_TARGET_OPENSYNC_CAFILE"

    # This command shows all certificates in case a file contains multiple
    collect_cmd openssl storeutl -noout -text -certs "$PRIV_CERT"
    collect_cmd openssl storeutl -noout -text -certs "$CERT_CA"
    collect_cmd openssl storeutl -noout -text -certs "$OPENSYNC_CAFILE"
}

# Only collect certs info if openssl is available
if ! openssl help >/dev/null 2>&1; then
    echo "openssl not found, skipping certs collection"
    exit 1
fi
collect_certs
