#!/usr/bin/env sh
set -axe

_self=$(readlink -f "$0")
_dir=$(dirname "$_self")
_device=$(readlink -f "$_dir/../../../../")
cd "$_device"
make -C core TARGET=native src/owm ovsdb-create -j $(nproc)
test -n "$1" || set -- -t
cd core
db=/tmp/conf.db
bck=$PWD/$(echo work/*native*/rootfs/usr/opensync/etc/conf.db.bck)
lib=$PWD/$(echo work/*native*/lib)
owm=$PWD/$(echo work/*native*/bin/owm)
cp -v "$bck" "$db"
export LD_LIBRARY_PATH=$PWD/$(echo work/*native*/lib)
export PLUME_OVSDB_SOCK_PATH=/tmp/db.sock
export OW_CORE_LOG_SEVERITY=debug
ovsdb-server --run "
	$owm $*
" --remote=punix:"$PLUME_OVSDB_SOCK_PATH" "$db"
