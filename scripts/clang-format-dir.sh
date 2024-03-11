#!/usr/bin/env bash

# Copyright (c) 2015, Plume Design Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Plume Design Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
