#!/usr/bin/env bash
set -e

SUMMARY="""clang-format-dir.sh script will check/fix format of all files in a directory using clang-format
If .clang-format-ignore file is present in source directory, it will be used to ignore some files"""

set -o nounset
set -o pipefail

CLANG_FORMAT="clang-format${LLVM_VERSION:+-$LLVM_VERSION}"
CLANG_FORMAT_ARGS="-fallback-style=none -style=file"
FIND_GLOBAL_ARGS=""
SRC_DIR="./src"
FIX_MODE="0"
VERBOSE="0"

DIR=$(dirname "$(readlink -f "$0")")

usage() {
    cat << EOF
$SUMMARY

Usage: [ENVOPT] $0 [OPTIONS]

ENVOPT:
    LLVM_VERSION=16 : clang version (if none is provided tools are called without version suffix)

OPTIONS:
    -s, --src-dir
        Source directory (default is $SRC_DIR)

        This folder must contain a .clang-format file
        This folder can contain single .clang-format-ignore file (list of relative paths to files that are ignored)

    -f, --fix
        Fix format issues in files in-place

    -v, --verbose
        Verbose mode (show differences)

    -nr, --no-recurse
        Do not recurse into subdirectories
EOF

    exit 0
}

abort()
{
    echo "ERROR: $1" >&2
    exit ${2:-1}
}

while true
do
    case "${1:-}" in
        --)
            shift
            break
            ;;
        -h|--help)
            usage
            ;;
        -f|--fix)
            FIX_MODE="1"
            ;;
        -nr|--no-recurse)
            FIND_GLOBAL_ARGS="${FIND_GLOBAL_ARGS} -maxdepth 1"
            ;;
        -s|--src-dir)
            SRC_DIR=$2
            shift
            ;;
        -v|--verbose)
            VERBOSE="1"
            ;;
        -*|--*)
            usage
            abort "Unknown option: $1"
            ;;
        *)
            break
    esac
    shift
done

FORMAT_FILE=".clang-format"
IGNORE_FILE=".clang-format-ignore"

($CLANG_FORMAT --version) || abort "Failed to get clang-format version... (are you running in docker?)"

(
    set -e
    set -o nounset
    set -o pipefail

    echo "Source dir: $SRC_DIR"

    cd $SRC_DIR

    if [ ! -e "$FORMAT_FILE" ]; then
        abort "$FORMAT_FILE not found."
    fi

    FILES=$(find $FIND_GLOBAL_ARGS -type f \( -iname "*.c" -or -iname "*.h" \) | sort)
    COUNT=$(echo "$FILES" | grep -vc '^$' || true)

    echo "Find matched $COUNT file(s)."

    if [ "$COUNT" == "0" ]; then
        exit 0
    fi

    if [ -e "$IGNORE_FILE" ]; then
        FILES=$(echo "$FILES" | grep -Fxv -f "$IGNORE_FILE" || true)
        COUNT=$(echo "$FILES" | grep -vc '^$' || true)

        echo "After applying ignore file, $COUNT file(s) to check/format..."

        if [ "$COUNT" == "0" ]; then
            exit 0
        fi
    fi

    FAILURES=0
    set +e

    echo "Checking/fixing $COUNT file(s)..."
    for F in $FILES; do
        if [ "$FIX_MODE" == "1" ]; then
            $CLANG_FORMAT $CLANG_FORMAT_ARGS -i -- "$F" || FAILURES=$(( FAILURES + 1 ))
        else
            OUTPUT=$(diff -p -u "$F" <($CLANG_FORMAT $CLANG_FORMAT_ARGS -- $F))

            if [ "$?" -gt 0 ]; then
                FAILURES=$(( FAILURES + 1 ))

                if [ "$VERBOSE" == "0" ]; then
                    echo "Format failed for: $F"
                else
                    echo "$OUTPUT";
                fi;
            fi
        fi
    done

    if [ "$FAILURES" -gt "0" ]; then
        abort "Format check/fix failed on $FAILURES file(s)..."
    else
        echo "Done!"
    fi
)
