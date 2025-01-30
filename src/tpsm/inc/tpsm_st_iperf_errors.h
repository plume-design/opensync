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

#ifndef TPSM_ST_IPERF_ERRORS_H_INCLUDED
#define TPSM_ST_IPERF_ERRORS_H_INCLUDED

/*
 * Copied from iperf3 version 3.17.1
 * https://github.com/esnet/iperf/tree/3.17.1
 * iperf_api.h & iperf_error.c
 * Some error messages, duly commented, needed to be slightly shortened
 * to make matching possible. Either to get a common denominator through
 * all the iperf3 versions or to remove formatters
 */

#define IENONE               0
#define IESERVCLIENT         1
#define IENOROLE             2
#define IESERVERONLY         3
#define IECLIENTONLY         4
#define IEDURATION           5
#define IENUMSTREAMS         6
#define IEBLOCKSIZE          7
#define IEBUFSIZE            8
#define IEINTERVAL           9
#define IEMSS                10
#define IENOSENDFILE         11
#define IEOMIT               12
#define IEUNIMP              13
#define IEFILE               14
#define IEBURST              15
#define IEENDCONDITIONS      16
#define IELOGFILE            17
#define IENOSCTP             18
#define IEBIND               19
#define IEUDPBLOCKSIZE       20
#define IEBADTOS             21
#define IESETCLIENTAUTH      22
#define IESETSERVERAUTH      23
#define IEBADFORMAT          24
#define IEREVERSEBIDIR       25
#define IEBADPORT            26
#define IETOTALRATE          27
#define IETOTALINTERVAL      28
#define IESKEWTHRESHOLD      29
#define IEIDLETIMEOUT        30
#define IERCVTIMEOUT         31
#define IERVRSONLYRCVTIMEOUT 32
#define IESNDTIMEOUT         33
#define IEUDPFILETRANSFER    34
#define IESERVERAUTHUSERS    35

#define IENEWTEST            100
#define IEINITTEST           101
#define IELISTEN             102
#define IECONNECT            103
#define IEACCEPT             104
#define IESENDCOOKIE         105
#define IERECVCOOKIE         106
#define IECTRLWRITE          107
#define IECTRLREAD           108
#define IECTRLCLOSE          109
#define IEMESSAGE            110
#define IESENDMESSAGE        111
#define IERECVMESSAGE        112
#define IESENDPARAMS         113
#define IERECVPARAMS         114
#define IEPACKAGERESULTS     115
#define IESENDRESULTS        116
#define IERECVRESULTS        117
#define IESELECT             118
#define IECLIENTTERM         119
#define IESERVERTERM         120
#define IEACCESSDENIED       121
#define IESETNODELAY         122
#define IESETMSS             123
#define IESETBUF             124
#define IESETTOS             125
#define IESETCOS             126
#define IESETFLOW            127
#define IEREUSEADDR          128
#define IENONBLOCKING        129
#define IESETWINDOWSIZE      130
#define IEPROTOCOL           131
#define IEAFFINITY           132
#define IEDAEMON             133
#define IESETCONGESTION      134
#define IEPIDFILE            135
#define IEV6ONLY             136
#define IESETSCTPDISABLEFRAG 137
#define IESETSCTPNSTREAM     138
#define IESETSCTPBINDX       139
#define IESETPACING          140
#define IESETBUF2            141
#define IEAUTHTEST           142
#define IEBINDDEV            143
#define IENOMSG              144
#define IESETDONTFRAGMENT    145
#define IEBINDDEVNOSUPPORT   146
#define IEHOSTDEV            147
#define IESETUSERTIMEOUT     148
#define IEPTHREADCREATE      150
#define IEPTHREADCANCEL      151
#define IEPTHREADJOIN        152
#define IEPTHREADATTRINIT    153
#define IEPTHREADATTRDESTROY 154

#define IECREATESTREAM  200
#define IEINITSTREAM    201
#define IESTREAMLISTEN  202
#define IESTREAMCONNECT 203
#define IESTREAMACCEPT  204
#define IESTREAMWRITE   205
#define IESTREAMREAD    206
#define IESTREAMCLOSE   207
#define IESTREAMID      208

#define IENEWTIMER    300
#define IEUPDATETIMER 301

struct iperf_error_status
{
    int status;
    const char *errmsg;
};

struct iperf_error_status iperf_error_list[] = {
    {IENONE, "no error"},
    {IESERVCLIENT, "cannot be both server and client"},
    {IENOROLE, "must either be a client (-c) or server (-s)"},
    {IESERVERONLY, "some option you are trying to set is server only"},
    {IECLIENTONLY, "some option you are trying to set is client only"},
    /* Shortened, removed formatters
    {IEDURATION, "test duration valid values are 0 to %d seconds"},
    {IENUMSTREAMS, "number of parallel streams too large (maximum = %d)"},
    {IEBLOCKSIZE, "block size too large (maximum = %d bytes)"},
    {IEBUFSIZE, "socket buffer size too large (maximum = %d bytes)"},
    {IEINTERVAL, "invalid report interval (min = %g, max = %g seconds)"},
    {IEMSS, "TCP MSS too large (maximum = %d bytes)"},
     */
    {IEDURATION, "test duration valid values"},
    {IENUMSTREAMS, "number of parallel streams too large"},
    {IEBLOCKSIZE, "block size too large"},
    {IEBUFSIZE, "socket buffer size too large"},
    {IEINTERVAL, "invalid report interval"},
    {IEMSS, "TCP MSS too large"},
    {IENOSENDFILE, "this OS does not support sendfile"},
    {IEOMIT, "bogus value for --omit"},
    {IEUNIMP, "an option you are trying to set is not implemented yet"},
    {IEFILE, "unable to open -F file"},
    /* Shortened, removed formatter
    {IEBURST, "invalid burst count (maximum = %d)"},
    */
    {IEBURST, "invalid burst count"},
    {IEENDCONDITIONS, "only one test end condition (-t, -n, -k) may be specified"},
    {IELOGFILE, "unable to open log file"},
    {IENOSCTP, "no SCTP support available"},
    {IEBIND, "--bind must be specified to use --cport"},
    /* Shortened, removed formatters
    {IEUDPBLOCKSIZE, "block size invalid (minimum = %d bytes, maximum = %d bytes)"},
    */
    {IEUDPBLOCKSIZE, "block size invalid"},
    {IEBADTOS, "bad TOS value (must be between 0 and 255 inclusive)"},
    {IESETCLIENTAUTH, "you must specify a username, password, and path to a valid RSA public key"},
    {IESETSERVERAUTH, "you must specify a path to a valid RSA private key and a user credential file"},
    {IEBADFORMAT, "bad format specifier (valid formats are in the set [kmgtKMGT])"},
    {IEREVERSEBIDIR, "cannot be both reverse and bidirectional"},
    {IEBADPORT, "port number must be between 1 and 65535 inclusive"},
    {IETOTALRATE, "total required bandwidth is larger than server limit"},
    {IETOTALINTERVAL, "None"},
    {IESKEWTHRESHOLD, "skew threshold must be a positive number"},
    {IEIDLETIMEOUT, "idle timeout parameter is not positive or larger than allowed limit"},
    {IERCVTIMEOUT, "receive timeout value is incorrect or not in range"},
    {IERVRSONLYRCVTIMEOUT, "client receive timeout is valid only in receiving mode"},
    {IESNDTIMEOUT, "send timeout value is incorrect or not in range"},
    {IEUDPFILETRANSFER, "cannot transfer file using UDP"},
    {IESERVERAUTHUSERS, "cannot access authorized users file"},
    {IENEWTEST, "unable to create a new test"},
    {IEINITTEST, "test initialization failed"},
    {IELISTEN, "unable to start listener for connections"},
    /* Shortened for backward compatibility, due to https://github.com/esnet/iperf/commit/1da1685
     {IECONNECT,
     "unable to connect to server - server may have stopped running or use a different port, firewall issue, etc."},
     */
    {IECONNECT, "unable to connect to server"},
    {IEACCEPT, "unable to accept connection from client"},
    {IESENDCOOKIE, "unable to send cookie to server"},
    {IERECVCOOKIE, "unable to receive cookie at server"},
    {IECTRLWRITE, "unable to write to the control socket"},
    {IECTRLREAD, "unable to read from the control socket"},
    {IECTRLCLOSE, "control socket has closed unexpectedly"},
    /* Shortened for backward compatibility, due to https://github.com/esnet/iperf/commit/1da1685
    {IEMESSAGE, "received an unknown control message (ensure other side is iperf3 and not iperf)"},
     */
    {IEMESSAGE, "received an unknown control message"},
    /* Adapted for backward compatibility, due to https://github.com/esnet/iperf/commit/9d78f79
    {IESENDMESSAGE, "unable to send control message - port may not be available, the other side may have stopped
     running, etc."},
    {IERECVMESSAGE, "unable to receive control message - port may not be available, the other side may have
     stopped running, etc."},
     */
    {IESENDMESSAGE, "unable to send control message"},
    {IERECVMESSAGE, "unable to receive control message"},
    {IESENDPARAMS, "unable to send parameters to server"},
    {IERECVPARAMS, "unable to receive parameters from client"},
    {IEPACKAGERESULTS, "unable to package results"},
    {IESENDRESULTS, "unable to send results"},
    {IERECVRESULTS, "unable to receive results"},
    {IESELECT, "select failed"},
    {IECLIENTTERM, "the client has terminated"},
    {IESERVERTERM, "the server has terminated"},
    {IEACCESSDENIED, "the server is busy running a test. try again later"},
    {IESETNODELAY, "unable to set TCP/SCTP NODELAY"},
    {IESETMSS, "unable to set TCP/SCTP MSS"},
    {IESETBUF, "unable to set socket buffer size"},
    {IESETTOS, "unable to set IP TOS"},
    {IESETCOS, "unable to set IPv6 traffic class"},
    {IESETFLOW, "unable to set IPv6 flow label"},
    {IEREUSEADDR, "unable to reuse address on socket"},
    {IENONBLOCKING, "unable to set socket to non-blocking"},
    {IESETWINDOWSIZE, "unable to set socket window size"},
    {IEPROTOCOL, "protocol does not exist"},
    {IEAFFINITY, "unable to set CPU affinity"},
    {IEDAEMON, "unable to become a daemon"},
    {IESETCONGESTION, "unable to set TCP_CONGESTION: "},
    {IEPIDFILE, "unable to write PID file"},
    {IEV6ONLY, "Unable to set/reset IPV6_V6ONLY"},
    {IESETSCTPDISABLEFRAG, "unable to set SCTP_DISABLE_FRAGMENTS"},
    {IESETSCTPNSTREAM, "unable to set SCTP_INIT num of SCTP streams\n"},
    {IESETSCTPBINDX, "None"},
    {IESETPACING, "unable to set socket pacing"},
    {IESETBUF2, "socket buffer size not set correctly"},
    {IEAUTHTEST, "test authorization failed"},
    {IEBINDDEV, "Unable to bind-to-device (check perror, maybe permissions?)"},
    {IENOMSG, "idle timeout for receiving data"},
    {IESETDONTFRAGMENT, "unable to set IP Do-Not-Fragment flag"},
    /* Shortened, removed formatters
    {IEBINDDEVNOSUPPORT, "`<ip>%%<dev>` is not supported as system does not support bind to device"},
    {IEHOSTDEV, "host device name (ip%%<dev>) is supported (and required) only for IPv6 link-local address"},
    */
    {IEBINDDEVNOSUPPORT, "is not supported as system does not support bind to device"},
    {IEHOSTDEV, "is supported (and required) only for IPv6 link-local address"},
    {IESETUSERTIMEOUT, "unable to set TCP USER_TIMEOUT"},
    {IEPTHREADCREATE, "unable to create thread"},
    {IEPTHREADCANCEL, "unable to cancel thread"},
    {IEPTHREADJOIN, "unable to join thread"},
    {IEPTHREADATTRINIT, "unable to create thread attributes"},
    {IEPTHREADATTRDESTROY, "unable to destroy thread attributes"},
    {IECREATESTREAM, "unable to create a new stream"},
    {IEINITSTREAM, "unable to initialize stream"},
    {IESTREAMLISTEN, "unable to start stream listener"},
    {IESTREAMCONNECT, "unable to connect stream"},
    {IESTREAMACCEPT, "unable to accept stream connection"},
    {IESTREAMWRITE, "unable to write to stream socket"},
    {IESTREAMREAD, "unable to read from stream socket"},
    {IESTREAMCLOSE, "stream socket has closed unexpectedly"},
    {IESTREAMID, "stream has an invalid id"},
    {IENEWTIMER, "unable to create new timer"},
    {IEUPDATETIMER, "unable to update timer"},
};

#endif /* TPSM_ST_IPERF_ERRORS__H_INCLUDED */
