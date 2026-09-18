#!/bin/sh

export ZOMBIE_THRESHOLD=200

let zombie_cnt="$(ps | egrep Z | wc -l)"
if [ $zombie_cnt -ge $ZOMBIE_THRESHOLD ]; then
    Healthcheck_Fail
fi

Healthcheck_Pass
