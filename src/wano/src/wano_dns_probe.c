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

#include <arpa/inet.h>

#include <ares.h>
#include <errno.h>
#include <unistd.h>

#include "os.h"
#include "os_time.h"
#include "wano.h"

#define WANO_DNS_PROBE_TIMEOUT_SEC 5
#define WANO_DNS_PROBE_MAX         32 /* Maximum number of parallel DNS probes */

struct wano_dns_probe_entry
{
    char pe_dns_server[INET6_ADDRSTRLEN]; /* DNS server that we're probing */
    ares_channel pe_cares;                /* Concurrent ares channels */
    struct wano_dns_probe *pe_probe;      /* Pointer to the wano_dns_probe structure */
};

struct wano_dns_probe
{
    bool dp_status;                                            /* Current status of DNS probe */
    size_t dp_nprobes;                                         /* Number of active ares channels */
    struct wano_dns_probe_entry dp_probes[WANO_DNS_PROBE_MAX]; /* Probes structure */
};

struct wano_dns_probe_server_list
{
    char sl_servers[INET6_ADDRSTRLEN][WANO_DNS_PROBE_MAX];
    size_t sl_nservers;
};

static bool wano_dns_probe_server_list(struct wano_dns_probe_server_list *sl);
static bool wano_dns_probe_add(struct wano_dns_probe *probe, const char *ifname, const char *server);
static void wano_dns_probe_wait_multi(struct wano_dns_probe *probe, uint32_t timeout_ms);
static void wano_dns_probe_ares_fn(void *arg, int ares_status, int timeouts, struct hostent *hostent);

bool wano_dns_probe_init(void)
{
    return (ares_library_init(ARES_LIB_INIT_ALL) == ARES_SUCCESS);
}

void wano_dns_probe_fini(void)
{
    ares_library_cleanup();
}

/*
 * Execute a DNS probe on the interface `if_name`, returns false if unable to
 * contact the DNS server. If the DNS server replies with any response
 * (including errors), return true.
 *
 * The purpos of this probe is not to test if we can successfully resolve a
 * host, but if any of the DNS servers are working and reachable.
 */
bool wano_dns_probe_run(const char *ifname)
{
    struct wano_dns_probe probe;
    struct wano_dns_probe_server_list sl;
    const char *dns_target = wano_awlan_redirector_addr();

    MEMZERO(probe);

    if (dns_target == NULL || *dns_target == '\0')
    {
        LOG(INFO, "dns_probe: No host to resolve (dns_target) set, skipping DNS probe.");
        return true;
    }

    if (!wano_dns_probe_server_list(&sl))
    {
        LOGW("dns_probe: Unable to retrieve DNS server list, skipping DNS probe.");
        return true;
    }

    for (size_t sli = 0; sli < sl.sl_nservers; sli++)
    {
        if (probe.dp_nprobes >= ARRAY_LEN(probe.dp_probes))
        {
            LOG(NOTICE,
                "dns_probe: Maximum number of DNS probes reached (%d), skipping the rest.",
                ARRAY_LEN(probe.dp_probes));
            break;
        }

        if (!wano_dns_probe_add(&probe, ifname, sl.sl_servers[sli]))
        {
            LOG(WARN, "dns_probe: Error creating probe for DNS server: %s, skipping.", sl.sl_servers[sli]);
            continue;
        }
        LOG(INFO, "dns_probe: Probing DNS server: %s", sl.sl_servers[sli]);
    }

    if (probe.dp_nprobes == 0)
    {
        LOG(NOTICE, "No usable DNS servers found.");
        goto exit;
    }

    /*
     * Wait for the maximum of WANO_DNS_PROBE_TIMEOUT_SEC for the address
     * lookup to complete. If select returns a non-negative value call
     * ares_process which will invoke wano_wan_probe_success_cb that can
     * report failure in cb_success.
     */
    for (int ntries = 0; ntries < WANO_DNS_PROBE_TIMEOUT_SEC; ntries++)
    {
        for (size_t ii = 0; ii < probe.dp_nprobes; ii++)
        {
            ares_gethostbyname(
                    probe.dp_probes[ii].pe_cares,
                    dns_target,
                    AF_INET,
                    wano_dns_probe_ares_fn,
                    (void *)&probe.dp_probes[ii]);
        }

        wano_dns_probe_wait_multi(&probe, 1000);

        if (probe.dp_status) break; /* At least one probe was successful, we're done */
    }

exit:
    for (size_t ii = 0; ii < probe.dp_nprobes; ii++)
        ares_destroy(probe.dp_probes[ii].pe_cares);

    LOG(NOTICE, "DNS probe status: %s\n", probe.dp_status ? "SUCCESS" : "ERROR");
    return probe.dp_status;
}

/*
 * Retrieve current DNS server list
 */
bool wano_dns_probe_server_list(struct wano_dns_probe_server_list *sl)
{
    struct ares_options options;
    int optmask;

    ares_channel dnslist = NULL;
    bool retval = false;

    MEMZERO(*sl);

    /*
     * Use a c-ares instance just to retrieve the server list -- the list
     * from the custom resolv.conf file
     */
    optmask = ARES_OPT_RESOLVCONF;
    options.resolvconf_path = CONFIG_WANO_DNS_PROBE_RESOLV_CONF_PATH;
    if (ares_init_options(&dnslist, &options, optmask) != ARES_SUCCESS)
    {
        LOG(DEBUG, "dns_probe: Error initializing cares, skipping DNS probe.");
        return false;
    }

    struct ares_addr_node *csrv;
    if ((ares_get_servers(dnslist, &csrv) != ARES_SUCCESS) || (csrv == NULL))
    {
        LOG(DEBUG, "dns_probe: Error retrieving list of DNS servers or no DNS servers defined, skipping DNS probe.");
        ares_destroy(dnslist);
        goto error;
    }

    for (; csrv != NULL; csrv = csrv->next)
    {
        if (sl->sl_nservers >= ARRAY_LEN(sl->sl_servers))
        {
            LOG(INFO, "dns_probe: Maximum number of DNS servers reached (%zu), truncating list.", sl->sl_nservers);
            break;
        }

        if (inet_ntop(csrv->family, (void *)&csrv->addr, sl->sl_servers[sl->sl_nservers], sizeof(sl->sl_servers[0]))
            == NULL)
        {
            LOG(INFO,
                "dns_probe: Error converting address (%zu) to string, skipping: %s",
                sl->sl_nservers,
                strerror(errno));
            continue;
        }
        sl->sl_nservers++;
    }

    retval = (sl->sl_nservers > 0);

error:
    if (dnslist != NULL) ares_destroy(dnslist);
    return retval;
}

/*
 * Add the interface/server combination to probe
 */
bool wano_dns_probe_add(struct wano_dns_probe *probe, const char *ifname, const char *server)
{
    int optmask;
    struct ares_options options;

    optmask = 0;
    optmask |= ARES_OPT_LOOKUPS;
    options.lookups = "b";
    optmask |= ARES_OPT_SERVERS;
    options.nservers = 0;
    options.servers = NULL;
    optmask |= ARES_OPT_TRIES;
    options.tries = 1;
    optmask |= ARES_OPT_TIMEOUT;
    options.timeout = WANO_DNS_PROBE_TIMEOUT_SEC;

    if (ares_init_options(&probe->dp_probes[probe->dp_nprobes].pe_cares, &options, optmask) != ARES_SUCCESS)
    {
        LOG(NOTICE, "dns_probe: Error initializing DNS probe for server: %s. Skipping.", server);
        return false;
    }

    /* Bind ares sockets to the provisioned interface */
    ares_set_local_dev(probe->dp_probes[probe->dp_nprobes].pe_cares, ifname);

    if (ares_set_servers_csv(probe->dp_probes[probe->dp_nprobes].pe_cares, server) != ARES_SUCCESS)
    {
        LOG(NOTICE, "dns_probe: Error setting DNS probe server: %s. Skipping.", server);
        ares_destroy(probe->dp_probes[probe->dp_nprobes].pe_cares);
        return false;
    }

    STRSCPY(probe->dp_probes[probe->dp_nprobes].pe_dns_server, server);
    probe->dp_probes[probe->dp_nprobes].pe_probe = probe;
    probe->dp_nprobes++;
    return true;
}

/*
 * Wait for DNS probes (across multiple c-ares channels) to complete with the
 * given timeout
 */
void wano_dns_probe_wait_multi(struct wano_dns_probe *probe, uint32_t timeout_ms)
{
    int64_t dns_timeout;
    int nfds;

    dns_timeout = clock_mono_usec() + timeout_ms * 1000;
    while (!probe->dp_status)
    {
        struct timeval tv;
        fd_set write_fds;
        fd_set read_fds;

        int64_t to_remain = dns_timeout - clock_mono_usec();
        if (to_remain <= 0)
        {
            LOG(DEBUG, "dns_probe: Timeout occurred...");
            break;
        }

        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        nfds = 0;
        for (size_t ii = 0; ii < probe->dp_nprobes; ii++)
        {
            int tnfds = ares_fds(probe->dp_probes[ii].pe_cares, &read_fds, &write_fds);
            if (nfds < tnfds) nfds = tnfds;
        }

        tv.tv_sec = to_remain / 1000000LL;
        tv.tv_usec = to_remain % 1000000LL;
        int rs = select(nfds, &read_fds, &write_fds, NULL, &tv);
        if (rs < 0)
        {
            LOG(WARN, "dns_probe: Error while waiting on probe results, skipping DNS probe: %s", strerror(errno));
            probe->dp_status = true;
            break;
        }
        else if (rs == 0)
        {
            break;
        }

        /*
         * Get list of sockets associated with a c-ares channel. Check if
         * they are set in the fdset and call ares_process_fd() for each
         * fd that is "ready".
         *
         * Note: Handling timeouts across multiple c-ares channels is somewhat
         * complicated so its better to just handle it manually.
         */
        for (size_t ii = 0; ii < probe->dp_nprobes; ii++)
        {
            int fdmask;

            ares_socket_t socks[ARES_GETSOCK_MAXNUM];
            fdmask = ares_getsock(probe->dp_probes[ii].pe_cares, socks, ARRAY_LEN(socks));

            for (int mi = 0; mi < ARRAY_LEN(socks); mi++)
            {
                int readfd = ARES_SOCKET_BAD;
                int writefd = ARES_SOCKET_BAD;

                if (ARES_GETSOCK_READABLE(fdmask, mi) && FD_ISSET(socks[mi], &read_fds))
                {
                    readfd = socks[mi];
                }

                if (ARES_GETSOCK_WRITABLE(fdmask, mi) && FD_ISSET(socks[mi], &write_fds))
                {
                    writefd = socks[mi];
                }

                /*
                 * Never call ares_process_fd() with both sockets set to
                 * ARES_SOCKET_BAD as this will always trigger a timeout even
                 * when called early.
                 */
                if (readfd == ARES_SOCKET_BAD && writefd == ARES_SOCKET_BAD) continue;

                ares_process_fd(probe->dp_probes[ii].pe_cares, readfd, writefd);
            }
        }
    }
}

/*
 * c-ares DNS resolution callback
 */
void wano_dns_probe_ares_fn(void *arg, int ares_status, int timeouts, struct hostent *hostent)
{
    struct wano_dns_probe_entry *pe = arg;
    struct wano_dns_probe *probe = pe->pe_probe;

    switch (ares_status)
    {
        case ARES_SUCCESS:
            LOG(INFO, "dns_probe: DNS server %s: Host resolved.", pe->pe_dns_server);
            probe->dp_status = true;
            break;
        /*
         * The DNS probe is essentially pinging the server -- it doesn't matter if
         * it was able to successfully resolve the host. The following error codes
         * may be returned from the server even if it didn't resolve the host.
         */
        case ARES_ENODATA:
        case ARES_EFORMERR:
        case ARES_ESERVFAIL:
        case ARES_ENOTFOUND:
        case ARES_ENOTIMP:
        case ARES_EREFUSED:
            LOG(WARN,
                "dns_probe: DNS server %s: Unable to resolve host, but seems to be working. Error: %s",
                pe->pe_dns_server,
                ares_strerror(ares_status));
            probe->dp_status = true;
            break;

        case ARES_EDESTRUCTION:
            /* This error happens when a channel is destroyed early, silence it */
            break;

        case ARES_ETIMEOUT:
            /* Timeouts are handled by wano_dns_probe_run() and wano_dns_probe_wait_multi()  */
            break;

        default:
            LOG(WARN, "dns_probe: Ares error status(%d): %s", ares_status, ares_strerror(ares_status));
            break;
    }
}
