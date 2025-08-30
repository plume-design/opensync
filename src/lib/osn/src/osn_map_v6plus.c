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

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <curl/curl.h>
#include <jansson.h>

#include "memutil.h"
#include "log.h"
#include "util.h"
#include "const.h"
#include "target.h"

#include "osn_map_v6plus.h"

struct v6plus_curl_buf
{
    char      *buf;    /* String buffer. Expected to be null-terminated */
    size_t     size;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
    struct v6plus_curl_buf *curl_buf = (struct v6plus_curl_buf *) data;
    size_t new_data_size = size * nmemb;

    curl_buf->buf = REALLOC(curl_buf->buf, curl_buf->size + new_data_size + 1);

    memcpy(curl_buf->buf+curl_buf->size, ptr, new_data_size);

    curl_buf->size += new_data_size;
    curl_buf->buf[curl_buf->size] = '\0';

    return new_data_size;
}

/* Fetch data with CURL from the specified HTTP(S) endpoint.
 * On success, curl_buf->buf will be set to (malloc-ed) HTTP body string value. The caller is
 * responsible for freeing it.
 */
static bool v6plus_api_curl_fetch(
    long *response_code,
    struct v6plus_curl_buf *curl_buf,
    const struct osn_map_v6plus_cfg *cfg,
    CURLoption opt_ipresolve)
{
    CURL *curl = NULL;
    CURLcode rc = CURLE_OK;
    char errbuf[CURL_ERROR_SIZE] = { 0 };

    curl = curl_easy_init();
    if (curl == NULL)
    {
        LOG(ERR, "osn_map_v6plus: Error initializing CURL");
        return false;
    }

    rc &= curl_easy_setopt(curl, CURLOPT_URL, cfg->vp_endpoint_url);
    rc &= curl_easy_setopt(curl, CURLOPT_IPRESOLVE, opt_ipresolve);
    rc &= curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    rc &= curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    rc &= curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);
    rc &= curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    rc &= curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);

    if (rc != CURLE_OK)
    {
        LOG(ERR, "osn_map_v6plus: Error setting up CURL");
        goto end;
    }

    LOG(DEBUG, "osn_map_v6plus: Transferring from: %s", cfg->vp_endpoint_url);

    rc = curl_easy_perform(curl);
    if (rc != CURLE_OK)
    {
        LOG(TRACE, "osn_map_v6plus: Failed transferring data from endpoint %s: %s (%s)",
                cfg->vp_endpoint_url, curl_easy_strerror(rc), errbuf);
        goto end;
    }

    rc = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, response_code);

end:
    if (curl != NULL)
    {
        curl_easy_cleanup(curl);
    }
    if (rc != CURLE_OK)
    {
        FREE(curl_buf->buf);
        curl_buf->buf = NULL;
    }

    return (rc == CURLE_OK);
}

/*
 * The v6plus endpoint uses JSONP - JSON with Padding (https://en.wikipedia.org/wiki/JSONP).
 * A callback function may be specified, such as /get_rules?callback=func and then the returned
 * response would be 'func({JSON})'. For OpenSync there's no added value to specify such a callback,
 * hence we don't make use of it (and expect the configured URL to not include it either). If no
 * parameters are specified, then instead of the callback name a question mark '?' is returned in
 * the response, i.e. '?({JSON})'. To allow JSON parsing we replace the JSONP-specific characters
 * (and/or the '?') in the response with whitespace.
 */
static void v6plus_respond_prepare(char *respond_str)
{
    char *c;

    /* The respond string may begin with a '?'. Replace it with whitespace: */
    if ((c = index(respond_str, '?')) != NULL)
    {
        *c = ' ';
    }
    /* v6plus JSON may be enclosed in '(' and ')'. Replace them with whitespace: */
    if ((c = index(respond_str, '(')) != NULL)
    {
        *c = ' ';
    }
    if ((c = rindex(respond_str, ')')) != NULL)
    {
        *c = ' ';
    }
}

/* Parse v6plus MAP rule server HTTP respond into an OSN MAP rulelist object. */
static bool v6plus_rule_server_respond_parse(char *respond_str, osn_map_v6plus_rulelist_t **_rule_list)
{
    osn_map_v6plus_rulelist_t *rule_list;
    osn_map_rule_t map_rule;
    osn_ip6_addr_t dmr;
    json_error_t js_err;
    json_t *js_respond;
    json_t *js_dmr;
    json_t *js_id;
    json_t *js_fmr_array;
    json_t *js_fmr;
    const char *str_dmr = NULL;
    const char *str_id = NULL;
    bool rv = false;
    size_t index;

    v6plus_respond_prepare(respond_str);

    *_rule_list = NULL;

    /* Expect JSON in a respond, decode it: */
    js_respond = json_loads(respond_str, 0, &js_err);
    if (js_respond == NULL)
    {
        LOG(ERR, "osn_map_v6plus: Error decoding as JSON: error='%s', json_str=%s",
                js_err.text,
                respond_str);
        return false;
    }
    LOG(DEBUG, "osn_map_v6plus: Decoded as JSON: '%s'", respond_str);

    /* Parse the JSON with MAP rules into OSN MAP rule list object: */
    rule_list = osn_map_v6plus_rulelist_new();

    /* Parse DMR string: */
    js_dmr = json_object_get(js_respond, "dmr");
    if (js_dmr == NULL || (str_dmr = json_string_value(js_dmr)) == NULL)
    {
        LOG(ERR, "osn_map_v6plus: No DMR string in JSON response or error");
        goto out;
    }
    LOG(TRACE, "osn_map_v6plus: Parsing JSON response: DMR=%s", str_dmr);
    if (!osn_ip6_addr_from_str(&dmr, str_dmr))
    {
        LOG(ERR, "osn_map_v6plus: Error parsing DMR as IPv6 addr: %s", str_dmr);
        goto out;
    }

    /* Parse unique user ID: */
    js_id = json_object_get(js_respond, "id");
    if (js_id != NULL &&  (str_id = json_string_value(js_id)) != NULL)
    {
        // Save the unique user ID:
        STRSCPY(rule_list->pl_user_id, str_id);

        LOG(TRACE, "osn_map_v6plus: Parsed unique user ID: %s",  rule_list->pl_user_id);
    }

    /* Parse array of FMRs (MAP rules): */
    js_fmr_array = json_object_get(js_respond, "fmr");
    if (js_fmr_array == NULL || !json_is_array(js_fmr_array))
    {
        LOG(ERR, "osn_map_v6plus: No FMR array in JSON response or error");
        goto out;
    }

    json_array_foreach(js_fmr_array, index, js_fmr)
    {
        const char *ipv6prefix = NULL;
        const char *ipv4prefix = NULL;
        int ea_len = 0;
        int psid_offset = 0;

        LOG(TRACE, "osn_map_v6plus: Parsing JSON response: MAP rule %zu: %s", index, json_dumps(js_fmr, 0));

        if (json_unpack_ex(js_fmr, &js_err, 0, "{s:s, s:s, s:i, s:i}",
                "ipv6", &ipv6prefix, "ipv4", &ipv4prefix,
                "psid_offset", &psid_offset, "ea_length", &ea_len) != 0)
        {
            LOG(ERR, "osn_map_v6plus: Error parsing MAP rule JSON: error='%s', json_str='%s'",
                    js_err.text, json_dumps(js_fmr, 0));
            goto out;
        }

        map_rule = OSN_MAP_RULE_INIT;

        if (!osn_ip6_addr_from_str(&map_rule.om_ipv6prefix, ipv6prefix))
        {
            LOG(ERR, "osn_map_v6plus: Error parsing as IPv6 prefix: %s", ipv6prefix);
            goto out;
        }

        if (!osn_ip_addr_from_str(&map_rule.om_ipv4prefix, ipv4prefix))
        {
            LOG(ERR, "osn_map_v6plus: Error parsing as IPv4 prefix: %s", ipv4prefix);
            goto out;
        }

        map_rule.om_ea_len = ea_len;
        map_rule.om_psid_offset = psid_offset;

        map_rule.om_is_fmr = true; /* All the v6plus MAP rules are also FMR rules */

        map_rule.om_dmr = dmr; /* All the MAP rules in the JSON share the same DMR, already parsed */

        /* Add the parsed MAP rule to the list: */
        osn_map_rulelist_add_rule((osn_map_rulelist_t *)rule_list, &map_rule);
    }

    /* Save the JSON string representation, for easier saving to persistence: */
    rule_list->pl_raw_str = STRDUP(respond_str);

    rv = true;
out:
    json_decref(js_respond);

    if (rv)
    {
        *_rule_list = rule_list;
    }
    else
    {
        osn_map_v6plus_rulelist_del(rule_list);
    }
    return rv;
}

bool osn_map_v6plus_rulelist_parse(char *rules_json_str, osn_map_v6plus_rulelist_t **rule_list)
{
    return v6plus_rule_server_respond_parse(rules_json_str, rule_list);
}

bool osn_map_v6plus_fetch_rules(
    long *response_code,
    osn_map_v6plus_rulelist_t **rule_list,
    const struct osn_map_v6plus_cfg *_cfg)

{
    struct v6plus_curl_buf curl_buf = { .size = 0, .buf = NULL};
    struct osn_map_v6plus_cfg cfg = *_cfg;

    if (cfg.vp_user_id[0] != '\0')
    {
        /* Add the previously acquired user ID to URL params */
        STRSCAT(cfg.vp_endpoint_url, "?id=");
        STRSCAT(cfg.vp_endpoint_url, cfg.vp_user_id);
    }

    LOG(DEBUG, "osn_map_v6plus: Acquiring MAP rules, URL=%s", cfg.vp_endpoint_url);

    /* Fetch data with CURL: */
    if (!v6plus_api_curl_fetch(response_code, &curl_buf, &cfg, CURL_IPRESOLVE_WHATEVER))
    {
        LOG(ERR, "osn_map_v6plus: Error getting MAP rules from API endpoint: %s", cfg.vp_endpoint_url);
        return false;
    }

    LOG(INFO, "osn_map_v6plus: MAP rule server response code: %ld", *response_code);

    if (*response_code == 200)
    {
        /* Parse the fetched JSON to a list of MAP rules as osn_map_rulelist_t object: */
        if (!v6plus_rule_server_respond_parse(curl_buf.buf, rule_list))
        {
            LOG(ERR, "osn_map_v6plus: Error parsing MAP rules API endpoint JSON response");
            goto out;
        }

        LOG(NOTICE, "osn_map_v6plus: Fetched MAP rules from API endpoint: %s", cfg.vp_endpoint_url);
    }
    else
    {
        *rule_list = osn_map_v6plus_rulelist_new();
    }

out:
    FREE(curl_buf.buf);
    return true;
}

/* Parse v6plus NTT HGW API endpoint HTTP response and determine the HGW status. */
static bool v6plus_ntt_hgw_api_respond_parse(char *respond_str, osn_map_v6plus_hgw_status_t *hgw_status)
{
    json_error_t js_err;
    json_t *js_respond;
    json_t *js;
    const char *str_js;
    bool rv = false;

    v6plus_respond_prepare(respond_str);

    /*
     * Expect JSON in a respond, decode it:
     * Example JSON: {"name":"jp.ne.enabler.ipv4.setting", "version":"1.2.0", "status":"OFF"}
     */
    js_respond = json_loads(respond_str, 0, &js_err);
    if (js_respond == NULL)
    {
        LOG(ERR, "osn_map_v6plus: Error decoding as JSON: error='%s', json_str=%s",
                js_err.text,
                respond_str);
        return false;
    }
    LOG(DEBUG, "osn_map_v6plus: Decoded as JSON: '%s'", respond_str);

    js = json_object_get(js_respond, "status");
    if (js == NULL || (str_js = json_string_value(js)) == NULL)
    {
        LOG(ERR, "osn_map_v6plus: get NTT HGW status: No status string in JSON response or error");
        goto out;
    }

    if (strcmp(str_js, "OFF") == 0)
    {
        *hgw_status = OSN_MAP_V6PLUS_MAP_OFF;
        LOG(DEBUG, "osn_map_v6plus: get NTT HGW status: HGW YES, v6plus MAP OFF");
    }
    else if (strcmp(str_js, "ON") == 0)
    {
        *hgw_status = OSN_MAP_V6PLUS_MAP_ON;
        LOG(DEBUG, "osn_map_v6plus: get NTT HGW status: HGW YES, v6plus MAP ON");
    }
    else
    {
        *hgw_status = OSN_MAP_V6PLUS_UNKNOWN;
        LOG(ERR, "osn_map_v6plus: get NTT HGW status: Unexpected status: '%s'", str_js);
        goto out;
    }

    js = json_object_get(js_respond, "version");
    if (js != NULL && (str_js = json_string_value(js)) != NULL)
    {
        LOG(DEBUG, "osn_map_v6plus: get NTT HGW status: version=%s", str_js);
    }

    rv = true;
out:
    json_decref(js_respond);
    return rv;
}

bool osn_map_v6plus_ntt_hgw_status_get(
    osn_map_v6plus_hgw_status_t *status,
    const struct osn_map_v6plus_cfg *cfg)
{
    struct v6plus_curl_buf curl_buf = { .size = 0, .buf = NULL};
    const struct osn_map_v6plus_cfg def_cfg = { .vp_endpoint_url = "http://ntt.setup:8888/enabler.ipv4/check" };
    long response_code;

    if (cfg == NULL)
    {
        cfg = &def_cfg;
    }

    LOG(TRACE, "osn_map_v6plus: Acquiring NTT HGW status, URL=%s", cfg->vp_endpoint_url);

    /* Fetch data with CURL: resolving must be IPv4 only */
    if (!v6plus_api_curl_fetch(&response_code, &curl_buf, cfg, CURL_IPRESOLVE_V4))
    {
        LOG(TRACE, "osn_map_v6plus: Could not acquire NTT HGW status from API endpoint: %s. "
                        "Assume there's no upstream HGW", cfg->vp_endpoint_url);
        *status = OSN_MAP_V6PLUS_UNKNOWN;
        goto out;
    }

    if (!v6plus_ntt_hgw_api_respond_parse(curl_buf.buf, status))
    {
        LOG(ERR, "osn_map_v6plus: Error parsing NTT HGW status response");
        *status = OSN_MAP_V6PLUS_UNKNOWN;
        goto out;
    }

out:
    FREE(curl_buf.buf);
    return true;
}

bool osn_map_v6plus_operation_report(
    osn_map_v6plus_status_action_t status_action,
    osn_map_v6plus_status_reason_t status_reason,
    const struct osn_map_v6plus_cfg *_cfg)
{
    struct v6plus_curl_buf curl_buf = { .size = 0, .buf = NULL};
    struct osn_map_v6plus_cfg cfg = *_cfg;
    long response_code;
    char status_str[32];

    /* Prepare URL parameters: */
    STRSCAT(cfg.vp_endpoint_url, "?action=");
    snprintf(status_str, sizeof(status_str), "%d", status_action);
    STRSCAT(cfg.vp_endpoint_url, status_str);
    STRSCAT(cfg.vp_endpoint_url, "&reason=");
    snprintf(status_str, sizeof(status_str), "%d", status_reason);
    STRSCAT(cfg.vp_endpoint_url, status_str);
    if (cfg.vp_user_id[0] != '\0')
    {
        STRSCAT(cfg.vp_endpoint_url, "&id=");
        STRSCAT(cfg.vp_endpoint_url, cfg.vp_user_id);
    }

    LOG(DEBUG, "osn_map_v6plus: Making operation report API call, URL=%s", cfg.vp_endpoint_url);

    /* Make the API call with cURL: */
    if (!v6plus_api_curl_fetch(&response_code, &curl_buf, &cfg, CURL_IPRESOLVE_WHATEVER))
    {
        LOG(ERR, "osn_map_v6plus: Error making operation report API call to endpoint URL: %s", cfg.vp_endpoint_url);
        return false;
    }

    if (response_code == 200)
    {
        LOG(INFO, "osn_map_v6plus: operation report: Done; Endpoint URL: %s; server response: '%s'",
                cfg.vp_endpoint_url,
                curl_buf.buf);
    }
    else
    {
        LOG(WARN, "osn_map_v6plus: operation report: Failed; Endpoint URL: %s; server response code: %ld",
                cfg.vp_endpoint_url,
                response_code);
    }

out:
    FREE(curl_buf.buf);
    return true;
}

osn_map_v6plus_rulelist_t *osn_map_v6plus_rulelist_new()
{
    osn_map_v6plus_rulelist_t *v6plus_rulelist = CALLOC(1, sizeof(osn_map_v6plus_rulelist_t));

    ds_dlist_init(&((osn_map_rulelist_t *)v6plus_rulelist)->rl_rule_list, osn_map_rule_t, om_dnode);

    return v6plus_rulelist;
}

void osn_map_v6plus_rulelist_del(osn_map_v6plus_rulelist_t *rule_list)
{
    if (rule_list == NULL) return;

    if (rule_list->pl_raw_str != NULL)
    {
        free(rule_list->pl_raw_str);
    }

    osn_map_rulelist_del((osn_map_rulelist_t *)rule_list);
}

osn_map_v6plus_rulelist_t *osn_map_v6plus_rulelist_copy(const osn_map_v6plus_rulelist_t *rule_list)
{
    osn_map_v6plus_rulelist_t *rule_list_orig = (osn_map_v6plus_rulelist_t *)rule_list;
    osn_map_v6plus_rulelist_t *rule_list_new;
    osn_map_rule_t *map_rule;

    if (rule_list == NULL) return NULL;

    rule_list_new = osn_map_v6plus_rulelist_new();

    /* Copying of parent object specifics: */
    osn_map_rulelist_foreach((osn_map_rulelist_t *)rule_list_orig, map_rule)
    {
        osn_map_rulelist_add_rule((osn_map_rulelist_t *)rule_list_new, map_rule);
    }

    /* Copying of child object attributes: */

    // Unique user ID:
    STRSCPY(rule_list_new->pl_user_id, rule_list_orig->pl_user_id);

    // "raw string" JSON without user ID (for saving purposes)"
    if (rule_list_orig->pl_raw_str != NULL)
    {
        rule_list_new->pl_raw_str = STRDUP(rule_list_orig->pl_raw_str);
    }

    return rule_list_new;
}