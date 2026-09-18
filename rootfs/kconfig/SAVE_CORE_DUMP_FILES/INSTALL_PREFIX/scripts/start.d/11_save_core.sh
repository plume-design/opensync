#!/bin/sh

echo "|${INSTALL_PREFIX}/bin/save_core.sh %e %p %t %s" > /proc/sys/kernel/core_pattern
ulimit -c unlimited

