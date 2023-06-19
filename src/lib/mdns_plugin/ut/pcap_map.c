/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

struct pcap_map
{
    const char *name;
    const unsigned char *pkt;
    size_t len;
} pmap[] =
{
    { .name = "pkt210", .pkt = pkt210, .len = 168 },
    { .name = "pkt211", .pkt = pkt211, .len = 168 },
    { .name = "pkt1056", .pkt = pkt1056, .len = 188 },
    { .name = "pkt1057", .pkt = pkt1057, .len = 188 },
    { .name = "pkt1059", .pkt = pkt1059, .len = 168 },
    { .name = "pkt1060", .pkt = pkt1060, .len = 168 },
    { .name = "pkt3809", .pkt = pkt3809, .len = 168 },
    { .name = "pkt3811", .pkt = pkt3811, .len = 168 },
    { .name = "pkt3812", .pkt = pkt3812, .len = 168 },
    { .name = "pkt5229", .pkt = pkt5229, .len = 168 },
    { .name = "pkt5230", .pkt = pkt5230, .len = 168 },
    { .name = "pkt5456", .pkt = pkt5456, .len = 168 },
    { .name = "pkt5457", .pkt = pkt5457, .len = 168 },
    { .name = "pkt5459", .pkt = pkt5459, .len = 68 },
    { .name = "pkt5771", .pkt = pkt5771, .len = 188 },
    { .name = "pkt6209", .pkt = pkt6209, .len = 110 },
    { .name = "pkt7184", .pkt = pkt7184, .len = 78 },
    { .name = "pkt8926", .pkt = pkt8926, .len = 88 },
    { .name = "pkt10281", .pkt = pkt10281, .len = 188 },
    { .name = "pkt10282", .pkt = pkt10282, .len = 188 },
    { .name = "pkt10575", .pkt = pkt10575, .len = 54 },
    { .name = "pkt10589", .pkt = pkt10589, .len = 68 },
    { .name = "pkt10601", .pkt = pkt10601, .len = 90 },
    { .name = "pkt10651", .pkt = pkt10651, .len = 88 },
    { .name = "pkt10654", .pkt = pkt10654, .len = 68 },
    { .name = "pkt10745", .pkt = pkt10745, .len = 68 },
    { .name = "pkt10757", .pkt = pkt10757, .len = 165 },
    { .name = "pkt10764", .pkt = pkt10764, .len = 90 },
    { .name = "pkt10772", .pkt = pkt10772, .len = 188 },
    { .name = "pkt10773", .pkt = pkt10773, .len = 188 },
    { .name = "pkt11090", .pkt = pkt11090, .len = 168 },
    { .name = "pkt11129", .pkt = pkt11129, .len = 188 },
    { .name = "pkt11130", .pkt = pkt11130, .len = 188 },
    { .name = "pkt11167", .pkt = pkt11167, .len = 168 },
    { .name = "pkt11168", .pkt = pkt11168, .len = 168 },
    { .name = "pkt11232", .pkt = pkt11232, .len = 188 },
    { .name = "pkt11233", .pkt = pkt11233, .len = 188 },
    { .name = "pkt11243", .pkt = pkt11243, .len = 90 },
    { .name = "pkt11592", .pkt = pkt11592, .len = 168 },
    { .name = "pkt12169", .pkt = pkt12169, .len = 168 },
    { .name = "pkt12170", .pkt = pkt12170, .len = 168 },
    { .name = "pkt12189", .pkt = pkt12189, .len = 188 },
    { .name = "pkt12190", .pkt = pkt12190, .len = 188 },
};
