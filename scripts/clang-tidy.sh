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
set -o nounset
set -o pipefail

PROJECT_ROOT="."
COMPILATION_DB_PATH="./compile_commands.json"
VERBOSITY=0
REBUILD=0
: ${CLANG_BIN=clang${LLVM_VERSION:+-$LLVM_VERSION}}
: ${CLANG_TIDY_BIN=clang-tidy${LLVM_VERSION:+-$LLVM_VERSION}}
: ${RUN_CLANG_TIDY_BIN=run-clang-tidy${LLVM_VERSION:+-$LLVM_VERSION}}
: ${PARALLEL_OPS=1}
: ${RUN_CLANG_TIDY_ARGS=""}

usage() {
    cat << EOF
usage: [ENVOPT] $0 [OPTIONS]

ENVOPT:
    LLVM_VERSION: Default version for clang tools. If it is not set, tools are called without version suffix.
    CLANG_BIN: Path to clang binary (default: clang)
    CLANG_TIDY_BIN: Path to clang-tidy binary (default: clang-tidy)
    RUN_CLANG_TIDY_BIN: Path to run-clang-tidy binary (default: run-clang-tidy)
    RUN_CLANG_TIDY_ARGS: Additional arguments passed to run-clang-tidy
    PARALLEL_OPS: Number of parallel jobs for make and clang-tidy (default: 1)

OPTIONS:
    -p, --project-root <path>: Project root directory where Makefile is located
    --cdb <path>: Path to compilation database (default: compile_commands.json)
    -r, --rebuild: Rebuild compilation database (e.g. when new files are added, flags change, etc.)
    -j <num>: Number of parallel jobs for make and clang-tidy (default: 1)
    -v, --verbose: Verbose output
    -h, --help: Show this help message
EOF
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
        -p|--project-root)
            PROJECT_ROOT=$2
            shift
            ;;
        --cdb)
            COMPILATION_DB_PATH=$2
            shift
            ;;
        -r|--rebuild)
            REBUILD=1
            ;;
        -j)
            PARALLEL_OPS=$2
            shift
            ;;
        -v|--verbose)
            VERBOSITY=1
            ;;
        -h|--help)
            usage
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

(
    cd $PROJECT_ROOT

    echo "Using project root: $PROJECT_ROOT"
    echo "Using compilation DB path: $COMPILATION_DB_PATH"
    echo "Using clang-tidy: "$($CLANG_TIDY_BIN --version)
    echo "Using parallel jobs: $PARALLEL_OPS"

    if [ ! -f $COMPILATION_DB_PATH ] || [ $REBUILD == 1 ]; then
        echo "Building compilation database: $COMPILATION_DB_PATH ..."
        make CC=$CLANG_BIN clean
        bear --field-output --cdb $COMPILATION_DB_PATH make CC=$CLANG_BIN -j $PARALLEL_OPS
    else
        echo "Reusing existing compilation database: $COMPILATION_DB_PATH ..."
    fi

    FILES=$(cat $COMPILATION_DB_PATH | jq '.[].file' --raw-output | sort | uniq)

    if [ "$VERBOSITY" -ge 1 ]; then
        echo "Files to be checked:"
        echo "$FILES"
    fi

    if [ -z "$FILES" ]; then
        abort "No input files specified"
    fi

    echo "Running clang-tidy on $(echo $FILES | wc -w) file(s) ..."
    # clang-tidy quiet is not silent: https://github.com/llvm/llvm-project/issues/47042

    echo "$FILES" | xargs $RUN_CLANG_TIDY_BIN -quiet -p $(dirname "$COMPILATION_DB_PATH") -j ${PARALLEL_OPS} $RUN_CLANG_TIDY_ARGS
)
