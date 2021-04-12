
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

###############################################################################
# update protobuf c files based on time-events report protobuf description
###############################################################################

# check current directory
RELDIR=$(basename "$(dirname $PWD)")/$(basename $PWD)
if [ "$RELDIR" != "lib/timevt" ]
then
    echo "Please cd to src/lib/timevt folder"
    exit 1
fi

# check if protobuf generator exists
if ! PROTOCC=$(command -v protoc-c); then
    echo "Error, protoc-c protobuf utility not found"
    exit 1
fi

# base name of protobuf definition file
FNAME=time_event
# expected protobuf file path
FPROTO=../../../interfaces/${FNAME}.proto

if $PROTOCC --c_out=. --proto_path="$(dirname $FPROTO)" $FPROTO; then
    mv "${FNAME}.pb-c.c" src/
    mv "${FNAME}.pb-c.h" inc/
    echo "Protobuf c files for $FPROTO successfully generated"
    exit 0
else
    echo "Error generating protobuf c files for $FPROTO"
    exit 1
fi
