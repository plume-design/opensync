#
# Copyright (c) 2020, Charter, Inc.
# All rights reserved.
#
# This module contains unpublished, confidential, proprietary material. The use
# and dissemination of this material are governed by a license. The above
# copyright notice does not evidence any actual or intended publication of this
# material.
#

gen_maptconfig()
{
	local tag=${1}
	local value=${2}
	local mod=${3}

cat << EOF
  {
    "op": "insert",
    "table": "Node_Config",
    "row":
    {
      "key": "${tag}",
	  "value": "${value}",
	  "module": "${mod}",
	  "persist": true
    }
  }
EOF
}

gen_maptstate()
{
	local tag=${1}
	local value=${2}
	local mod=${3}

cat << EOF
  {
    "op": "insert",
    "table": "Node_State",
    "row":
    {
      "key": "${tag}",
	  "value": "${value}",
	  "module": "${mod}",
	  "persist": true
    }
  }
EOF
}
operations="$(gen_maptconfig 'maptParams' '{\"support\":\"true\",\"interface\":\"br-wan\"}' 'MAPTM')"
operations+=",$(gen_maptstate 'maptMode' 'Dual-Stack' 'MAPTM')"


cat << EOF
[
  "Open_vSwitch",${operations}
]
EOF
