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

# Create CA, no passwd
echo "Generationg fut_rootCA.key"
openssl genrsa -out fut_rootCA.key 4096

# Self sign it
openssl req -x509 -new -nodes -key fut_rootCA.key -sha256 -days 60 -out fut_rootCA.pem

# Create fut-server certificate
echo "Generating fut_sever.key"
openssl genrsa -out fut_server.key 4096

# Self sign it
openssl req -new -key fut_server.key -out fut_server.csr
openssl x509 -req -in fut_server.csr -CA fut_rootCA.pem -CAkey fut_rootCA.key -CAcreateserial -out fut_server.pem -days 30 -sha256

# Create device certificate
echo "Generating fut_device.key"
openssl genrsa -out fut_device.key 4096

# Self sign it
openssl req -new -key fut_device.key -out fut_device.csr
openssl x509 -req -in fut_device.csr -CA fut_rootCA.pem -CAkey fut_rootCA.key -CAcreateserial -out fut_device.pem -days 30 -sha256


