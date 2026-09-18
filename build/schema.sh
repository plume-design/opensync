#!/usr/bin/env sh
SCHEMA_FILE=interfaces/opensync.ovsschema
usage()
{
    echo "schema.sh [check|update]"
    exit 1
}

die()
{
    echo "$@"
    exit 1
}

schema_crc()
{
    cat "$SCHEMA_FILE" | sed '/"cksum": +"[0-9 ]+"/d' | cksum
}

json_get_value()
{
    local name="$1"

    gawk -v name="$name" '
        BEGIN {
            retval=1
        }

        match($0, /"([^"]+)": "([^"]+)"/, m) {
            if (m[1] == name)
            {
                print m[2]
                retval=0
                exit
            }
        }

        END {
            exit retval
        }'
}

json_set_value()
{
    local name="$1"
    local val="$2"

    gawk -v name="$name" -v val="$val" '
        BEGIN {
            retval=1
        }

        match($0, /"([^"]+)": "([^"]+)"/, m) {
            if (m[1] == name)
            {
                print "  \"" name "\": \"" val "\","
                retval=0
                next
            }
        }

        {
            print $0
        }

        END {
            exit retval
        }'
}

schema_get_value()
{
    local name=$1

    cat "$SCHEMA_FILE" | json_get_value $name
}

schema_set_value()
{
    local name="$1"
    local value="$2"

    cat "$SCHEMA_FILE" | json_set_value "$name" "$value" > "${SCHEMA_FILE}.tmp" || die "Error setting parameter $name"
    mv "${SCHEMA_FILE}.tmp" "${SCHEMA_FILE}"
}

schema_get_version_git()
{
    DIR=$(dirname "$SCHEMA_FILE")
    SC=$(basename "$SCHEMA_FILE")
    git -C "$DIR" show "HEAD:./$SC" | json_get_value version
}

do_crc()
{
    sed -e '/"cksum": \+"[0-9 ]\+",/d' | cksum
}

schema_crc()
{
    cat "$SCHEMA_FILE" | do_crc
}

schema_check()
{
    CRC=$(schema_get_value cksum)
    CRC2=$(schema_crc)

    [ "$CRC" = "$CRC2" ]
}

schema_update_ver()
{
    local micro
    micro=${VER##*.}
    micro=$((micro + 1))
    echo ${VER%.*}.$micro
}

schema_update()
{
    VER=$(schema_get_version_git)
    NEW_VER=$(schema_update_ver "$VER")

    schema_set_value version "$NEW_VER"
    NEW_CRC="$(schema_crc)"
    schema_set_value cksum "$NEW_CRC"

    schema_check || {
        echo "Calculated CRC: $(schema_crc)"
        echo "New CRC: $NEW_CRC"
        die "Schema version was updated, but CRC check failed."
    }

    echo "Schema version is now $NEW_VER"
}

if [ ! -e .git ]; then
    echo "WARNING: schema not in git" 1>&2
    exit 0
fi

CKSUM=$(schema_get_value cksum) || die 'No recognised "cksum" field in schema'
VER=$(schema_get_version_git) || die 'No recognised "version" field in GIT schema'

case "$1" in
    info)
        echo "Current GIT schema version: $VER, checksumn: $CKSUM"
        ;;

    check)
        schema_check || {
            exit 1
        }
        ;;

    update)
        schema_update || die "Unable to update schema version"
        ;;

    help)
        usage
        ;;
    *)
        die "Unrecognised parameter: $1"
        ;;
esac

exit 0
