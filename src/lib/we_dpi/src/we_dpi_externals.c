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

#include <errno.h>

#include "fsm_dpi_utils.h"
#include "osp_ps.h"
#include "qm_conn.h"
#include "we.h"
#include "we_dpi_conntrack.h"
#include "we_dpi_plugin.h"

int dpi_ext_exec(we_state_t s, void *arg)
{
    char *bytecode;
    void *globals;
    int len;
    int res;
    struct we_dpi_agent_userdata *user = arg;
    struct we_dpi_session *dpi = (struct we_dpi_session *)(user->fsm->handler_ctxt);
    we_state_t agent = dpi->we_state;

    we_read(s, we_top(s), WE_TAB, &globals);
    len = we_read(s, we_top(s) - 1, WE_BUF, &bytecode);

    res = we_pushstr(agent, len, bytecode);
    if (res < 0)
    {
        LOGE("%s: Failed to push new bytecode for agent: %d\n", __func__, res);
        /* globals */
        we_pop(s);
        /* bytecode */
        we_pop(s);
        /* this */
        we_pop(s);
        /* return -1 */
        we_pushnum(s, -1);
        return 0;
    }
    we_pushtab(agent, NULL);

    int reg = we_pusharr(agent, NULL);
    we_pushnum(agent, 0);
    we_pushtab(agent, globals);
    we_set(agent, reg);

    we_popr(agent, 2);
    we_popr(agent, 1);
    we_popr(agent, 0);

    while (we_top(agent) > 2)
        we_pop(agent);

    LOGD("%s: performing an update\n", __func__);
    if (we_top(agent) != 2) LOGD("%s: we_top not what we expected. Got %d\n", __func__, we_top(agent));
    assert(we_top(agent) == 2);

    return EAGAIN;
}

int dpi_ext_exit(we_state_t s, void *user)
{
    (void)user;
    assert(we_type(s, we_top(s)) == WE_NUM);
    we_popr(s, we_top(s));
    we_pop(s);
    return ECANCELED;
}

static char *_we_strdup(we_state_t s, int r)
{
    char *str;
    int len = we_read(s, r, WE_BUF, &str);
    if (len < 0) return NULL;
    return strndup(str, len);
}

/* int open(str file, str mode) */
int dpi_ext_open(we_state_t s, void *user)
{
    FILE *fp;
    char *mode, *file;
    (void)user;
    mode = _we_strdup(s, we_top(s));
    if (mode == NULL)
    {
        return -ENOMEM;
    }
    we_pop(s);
    file = _we_strdup(s, we_top(s));
    if (file == NULL)
    {
        free(mode);
        return -ENOMEM;
    }
    we_pop(s);
    we_pop(s);
    if (!strcmp(file, "stdin"))
    {
        fp = freopen(NULL, mode, stdin);
    }
    else if (!strcmp(file, "stdout"))
    {
        fp = freopen(NULL, mode, stdout);
    }
    else if (!strcmp(file, "stderr"))
    {
        fp = freopen(NULL, mode, stderr);
    }
    else
    {
        fp = fopen(file, mode);
    }
    free(mode), free(file);
    we_pushnum(s, (intptr_t)fp);
    return 0;
}

/* int close(int file) */
int dpi_ext_close(we_state_t s, void *user)
{
    FILE *fp;
    int64_t ptr;
    (void)user;
    we_read(s, we_top(s), WE_NUM, &ptr);
    we_pop(s);
    we_pop(s);
    fp = (FILE *)(intptr_t)ptr;
    if (fp == stdin || fp == stdout || fp == stderr)
    {
        we_pushnum(s, 0);
    }
    else
    {
        we_pushnum(s, fclose(fp));
    }
    return 0;
}

/* str read(int file, int len) */
int dpi_ext_read(we_state_t s, void *user)
{
    const int64_t SZ = 4096;
    FILE *fp;
    int64_t ptr, len;
    uint8_t *buf;
    size_t rlen;

    (void)user;
    we_read(s, we_top(s), WE_NUM, &len);
    we_pop(s);
    we_read(s, we_top(s), WE_NUM, &ptr);
    we_pop(s);
    we_pop(s);

    if (len < 0) len = SZ;

    if (we_pushstr(s, len, NULL) < 0)
    {
        we_pushstr(s, 0, NULL);
        return 0;
    }
    fp = (FILE *)(intptr_t)ptr;
    we_read(s, we_top(s), WE_BUF, &buf);
    rlen = fread(buf, 1, len, fp);
    if ((int64_t)rlen < len)
    {
        we_trim(s, we_top(s), -(len - rlen));
    }
    return 0;
}

/* int write(int file, str buf) */
int dpi_ext_write(we_state_t s, void *user)
{
    uint8_t *buf;
    int64_t res = 0;
    (void)user;
    assert(we_type(s, we_top(s)) == WE_BUF);
    assert(we_type(s, we_top(s) - 1) == WE_NUM);
    int len = we_read(s, we_top(s), WE_BUF, &buf);
    if (len)
    {
        we_read(s, we_top(s) - 1, WE_NUM, &res);
        res = fwrite(buf, 1, len, (FILE *)(intptr_t)res);
    }
    we_pop(s), we_pop(s), we_pop(s);
    we_pushnum(s, res);
    return 0;
}

/* int time(clockid id) */
int dpi_ext_time(we_state_t s, void *user)
{
    (void)user;
    int64_t clockid;
    struct timespec tp = {0};
    we_read(s, we_top(s), WE_NUM, &clockid);
    switch (clockid)
    {
        case 0:
            clock_gettime(CLOCK_REALTIME, &tp);
            break;
        case 1:
            clock_gettime(CLOCK_MONOTONIC, &tp);
            break;
    }
    we_pop(s); /* clockid */
    we_pop(s); /* this */
    we_pushnum(s, (int64_t)tp.tv_sec * 1000000LL + tp.tv_nsec / 1000LL);
    return 0;
}

/* int memcpy(str dst, str src) */
int dpi_ext_memcpy(we_state_t s, void *user)
{
    (void)user;
    /* This external is unused by the integration */
    we_pop(s); /* src */
    we_pop(s); /* dst */
    we_pushnum(s, 0);
    return 0;
}

/* int pub(str key, str value) */
int dpi_ext_publish_mqtt(we_state_t s, void *user)
{
    char topic_buffer[256]; /* This should match the size of the schema */
    qm_response_t mqtt_response;
    int topic_length;
    int value_length;
    char *topic;
    char *value;
    bool result = false;
    (void)user;

    value_length = we_read(s, we_top(s), WE_BUF, &value);
    topic_length = we_read(s, we_top(s) - 1, WE_BUF, &topic);

    if (topic_length <= 0 || topic_length > (int)(sizeof(topic_buffer) - 1)) goto end;
    if (value_length <= 0) goto end;

    STRSCPY_LEN(topic_buffer, topic, topic_length);

    result = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, topic_buffer, value, value_length, &mqtt_response);

end:
    we_pop(s); /* value */
    we_pop(s); /* topic */
    we_pop(s); /* this */
    we_pushnum(s, result ? 1 : 0);
    return 0;
}

int dpi_ext_log(we_state_t s, void *arg)
{
    (void)arg;
    int64_t level;
    int msglen;
    char *msg;

    msglen = we_read(s, we_top(s), WE_BUF, &msg);
    we_read(s, we_top(s) - 1, WE_NUM, &level);

    switch (level)
    {
        case LOG_SEVERITY_TRACE:
            LOGT("%.*s", msglen, msg);
            break;
        case LOG_SEVERITY_DEBUG:
            LOGD("%.*s", msglen, msg);
            break;
        case LOG_SEVERITY_INFO:
            LOGI("%.*s", msglen, msg);
            break;
        case LOG_SEVERITY_ERR:
            LOGE("%.*s", msglen, msg);
            break;
        default:
            LOGD("%s: Bad log level %jd", __func__, level);
            break;
    }

    /* msg */
    we_pop(s);
    /* level */
    we_pop(s);
    /* this */
    we_pop(s);

    we_pushnum(s, 0); /* return 0; */
    return 0;
}

int dpi_ext_ps_open(we_state_t s, void *arg)
{
    osp_ps_t *ps = NULL;
    int64_t flags;
    char *store;

    we_read(s, we_top(s), WE_NUM, &flags);
    store = _we_strdup(s, we_top(s) - 1);
    we_pop(s); /* flags */
    we_pop(s); /* store */
    we_pop(s); /* this */
    if (!store) goto end;

    LOGD("%s: osp_ps_open %s %jd\n", __func__, store, flags);
    ps = osp_ps_open(store, flags);
    LOGD("%s: osp_ps_open complete %s %jd %p\n", __func__, store, flags, ps);
    LOGD("%s: ptr: %p\n", __func__, ps);
    if (ps == NULL)
    {
        LOGE("%s: failed to osp_ps_open\n", __func__);
    }

    free(store);
end:
    we_pushnum(s, (intptr_t)ps);
    return 0;
}

int dpi_ext_ps_close(we_state_t s, void *arg)
{
    int64_t ptr;
    osp_ps_t *ps;

    we_read(s, we_top(s), WE_NUM, &ptr);
    if (ptr)
    {
        ps = (osp_ps_t *)(intptr_t)ptr;
        LOGD("%s: ptr: %p\n", __func__, ps);
        osp_ps_close(ps);
    }

    we_pop(s); /* ptr */
    we_pop(s); /* this */
    we_pushnum(s, 0);
    return 0;
}

int dpi_ext_ps_get(we_state_t s, void *arg)
{
    (void)arg;
    osp_ps_t *ps;
    char *key;
    char *val;
    ssize_t valsz = 0;
    ssize_t rc;
    int64_t ptr;

    key = _we_strdup(s, we_top(s));
    we_read(s, we_top(s) - 1, WE_NUM, &ptr);
    we_pop(s); /* key */
    we_pop(s); /* ptr */
    we_pop(s); /* this */

    if (!ptr) goto cleanup;
    ps = (osp_ps_t *)(intptr_t)ptr;

    LOGD("%s: ptr: %p\n", __func__, ps);
    /* Get the length of the value */
    valsz = osp_ps_get(ps, key, NULL, 0);
    if (valsz < 0)
    {
        LOGD("%s: get error\n", __func__);
        goto cleanup;
    }
    else if (valsz == 0)
    {
        LOGD("%s: get key not found\n", __func__);
        goto cleanup;
    }

    if (we_pushstr(s, valsz, NULL) < 0)
    {
        LOGE("%s: failed to push value\n", __func__);
        goto cleanup;
    }

    we_read(s, we_top(s), WE_BUF, &val);

    LOGD("%s: get: %s %zd\n", __func__, key, valsz);
    rc = osp_ps_get(ps, key, val, valsz);

    if (rc != valsz)
    {
        LOGE("%s: failed to get we_devices", __func__);
        /* Pop the result string we pushed */
        we_pop(s);
        /* Push an empty string */
        we_pushstr(s, 0, NULL);
    }

    free(key);
    return 0;
cleanup:
    free(key);
    we_pushstr(s, 0, NULL);
    return 0;
}

int dpi_ext_ps_set(we_state_t s, void *arg)
{
    (void)arg;
    osp_ps_t *ps;
    int64_t ptr;
    char *value;
    int valuesz;
    char *key;

    valuesz = we_read(s, we_top(s), WE_BUF, &value);
    key = _we_strdup(s, we_top(s) - 1);
    we_read(s, we_top(s) - 2, WE_NUM, &ptr);

    if (!ptr) goto cleanup;
    ps = (osp_ps_t *)(intptr_t)ptr;
    LOGD("%s: ptr: %p\n", __func__, ps);

    if (osp_ps_set(ps, key, value, valuesz) != valuesz)
    {
        LOGE("%s: failed to set key", __func__);
        goto cleanup;
    }

cleanup:
    free(key);
    we_pop(s); /* value */
    we_pop(s); /* key */
    we_pop(s); /* ptr */
    we_pop(s); /* this */
    we_pushnum(s, valuesz);
    return 0;
}

/* set_plugin_decision(int decision); */
int dpi_ext_set_plugin_decision(we_state_t s, void *arg)
{
    struct we_dpi_agent_userdata *user = arg;
    int64_t decision;

    we_read(s, we_top(s), WE_NUM, &decision);
    /* decision */
    we_pop(s);
    /* this */
    we_pop(s);
    /* return 0 */
    we_pushnum(s, 0);

    if (user) fsm_dpi_set_plugin_decision(user->fsm, user->np, decision);

    return 0;
}

/* conntrack_get_flows(int family, int zone) */
int dpi_ext_get_conntrack_flows(we_state_t s, void *arg)
{
    (void)arg;
    int64_t zone;
    int64_t family;
    int nentries;
    void *entries;
    nentries = we_read(s, we_top(s), WE_ARR, &entries);
    we_read(s, we_top(s) - 1, WE_NUM, &zone);
    we_read(s, we_top(s) - 2, WE_NUM, &family);
    we_popr(s, 0);
    we_pop(s);
    we_pop(s);
    we_dpi_ct_get_flow_entries(family, s, zone, nentries);
    we_pop(s);
    we_pushnum(s, 0);
    return 0;
}

#define REGISTER_WE_EXTERNAL(num, external) \
    if ((res = we_setup((num), (external))) < 0) return res;

int we_dpi_setup_agent_externals(void)
{
    int res;
    /* Required by the compiler */
    REGISTER_WE_EXTERNAL(1, dpi_ext_exit);
    REGISTER_WE_EXTERNAL(2, dpi_ext_open);
    REGISTER_WE_EXTERNAL(3, dpi_ext_close);
    REGISTER_WE_EXTERNAL(4, dpi_ext_read);
    REGISTER_WE_EXTERNAL(5, dpi_ext_write);
    REGISTER_WE_EXTERNAL(6, dpi_ext_time);
    REGISTER_WE_EXTERNAL(7, dpi_ext_memcpy);
    /* Provided by this plugin */
    REGISTER_WE_EXTERNAL(8, dpi_ext_publish_mqtt);
    REGISTER_WE_EXTERNAL(10, dpi_ext_log);
    REGISTER_WE_EXTERNAL(11, dpi_ext_ps_get);
    REGISTER_WE_EXTERNAL(12, dpi_ext_ps_set);
    REGISTER_WE_EXTERNAL(13, dpi_ext_ps_open);
    REGISTER_WE_EXTERNAL(14, dpi_ext_ps_close);
    REGISTER_WE_EXTERNAL(15, dpi_ext_set_plugin_decision);
    REGISTER_WE_EXTERNAL(16, dpi_ext_exec);
    REGISTER_WE_EXTERNAL(17, dpi_ext_get_conntrack_flows);
    return 0;
}
