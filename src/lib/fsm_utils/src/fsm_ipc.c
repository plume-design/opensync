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

#include "log.h"
#include "kconfig.h"
#include "const.h"
#include "memutil.h"
#include "fsm_ipc.h"

uint64_t g_fsm_io_success_cnt;
uint64_t g_fsm_io_failure_cnt;


#ifndef CONFIG_FSM_IPC_USE_OSBUS

/* IMC */

#define MODULE_ID LOG_MODULE_ID_MISC

#include <dlfcn.h>
#include "imc.h"

/* common */

/**
 * IMC shared library context, when loaded through dlopen()
 */
static struct imc_dso g_imc_context;

/**
 * @brief dynamically load the imc library and initializes its context
 */
static bool
fsm_ipc_load_imc(void)
{
    char *dso = CONFIG_INSTALL_PREFIX"/lib/libimc.so";
    char *init = "imc_init_dso";
    struct stat st;
    char *error;
    int rc;

    rc = stat(dso, &st);
    if (rc != 0) return true; /* All ops will be void */

    dlerror();
    g_imc_context.handle = dlopen(dso, RTLD_NOW);
    if (g_imc_context.handle == NULL)
    {
        LOGE("%s: dlopen %s failed: %s", __func__, dso, dlerror());
        return false;
    }

    dlerror();
    *(void **)(&g_imc_context.init) = dlsym(g_imc_context.handle, init);
    error = dlerror();
    if (error != NULL) {
        LOGE("%s: could not get symbol %s: %s",
             __func__, init, error);
        dlclose(g_imc_context.handle);
        return false;
    }

    g_imc_context.init(&g_imc_context);

    return true;
}


/* server */


/**
 * @brief starts the fsm -> fcm IMC server
 *
 * @param server the imc context of the server
 * @param loop the FCM manager's ev loop
 * @param recv_cb the ct_stats handler for the data received from fsm
 */
static int
fsm_ipc_imc_init_server(struct imc_dso *imc, struct ev_loop *loop,
                     imc_recv recv_cb)
{
    int ret;

    if (imc->imc_init_server == NULL) return 0;

    ret = imc->imc_init_server(imc, loop, recv_cb);

    return ret;
}


/**
 * @brief stops imc_server
 *
 * @param server the imc context of the server
 */
void
fsm_ipc_imc_terminate_server(struct imc_dso *imc)
{
    if (imc->imc_terminate_server == NULL) return;

    imc->imc_terminate_server(imc);
}

/**
 * @brief starts the imc server receiving flow info from fsm
 */
int fsm_ipc_server_init(struct ev_loop *loop, void (*recv_cb)(void *data, size_t len))
{
    struct imc_dso *imc;
    bool ret;
    int rc;

    ret = fsm_ipc_load_imc();
    if (!ret) goto err_init_imc;

    imc = &g_imc_context;

    rc = fsm_ipc_imc_init_server(imc, loop, recv_cb);
    if (rc != 0)
    {
        LOGE("%s: failed to init imc server", __func__);
        goto err_init_imc;

    }

    imc->initialized = true;

    return 0;

err_init_imc:
    return -1;
}


/**
 * @brief stops the imc server receiving flow info from fsm and app time
 */
void
fsm_ipc_server_close(void)
{
    struct imc_dso *imc;

    imc = &g_imc_context;
    fsm_ipc_imc_terminate_server(imc);
}


/* client */


static int
fsm_ipc_imc_init_client(struct imc_dso *imc, imc_free_sndmsg free_cb,
                    void *hint)
{
    int ret;

    if (imc->imc_init_client == NULL)
    {
        imc->imc_free_sndmsg = free_cb;
        return 0;
    }

    ret = imc->imc_init_client(imc, free_cb, hint);

    if (ret)
    {
        LOGD("%s: IMC init client failed", __func__);
    }

    return ret;
}


static void
fsm_ipc_imc_terminate_client(struct imc_dso *imc)
{
    if (imc->imc_terminate_client == NULL) return;

    imc->imc_terminate_client(imc);
}


static int
fsm_ipc_imc_client_send(struct imc_dso *imc, void *data,
                    size_t len, int flags)
{
    int rc;

    if (imc->imc_client_send == NULL)
    {
        imc->imc_free_sndmsg(data, imc->free_msg_hint);

        return 0;
    }

    rc = imc->imc_client_send(imc, data, len, flags);

    return rc;
}


static void
free_send_msg(void *data, void *hint)
{
    FREE(data);
}


bool fsm_ipc_init_client(void)
{
    struct imc_dso *imc;
    bool ret;
    int rc;
    ret = fsm_ipc_load_imc();
    if (!ret) return false;
    imc = &g_imc_context;
    rc = fsm_ipc_imc_init_client(imc, free_send_msg, NULL);
    if (rc) return false;
    return true;
}

int fsm_ipc_client_send(void *data, size_t len)
{
    int rc = -1;
    struct imc_dso *imc;

    imc = &g_imc_context;
    if (!imc->initialized) return -1;

    /* Beware, sending the data buf through imc will schedule its freeing */
    rc = fsm_ipc_imc_client_send(imc, data, len, IMC_DONTWAIT);
    if (rc != 0)
    {
        LOGD("%s: could not send message", __func__);
        g_fsm_io_failure_cnt++;
        rc = -1;
        goto err_send;
    }
    g_fsm_io_success_cnt++;
err_send:
    return rc;
}


void fsm_ipc_terminate_client(void)
{
    struct imc_dso *imc;
    imc = &g_imc_context;
    fsm_ipc_imc_terminate_client(imc);
}


#else // CONFIG_FSM_IPC_USE_OSBUS


/* OSBUS */

#define MODULE_ID LOG_MODULE_ID_OSBUS

#include "osbus.h"

#define FSM_IPC_OSBUS_COMPONENT_NAME "FCM"
#define FSM_IPC_OSBUS_DATA "data"
#define FSM_IPC_OSBUS_TOPIC "topic.ipc"


/* server */


static void (*_fsm_ipc_proto_recv_cb)(void *data, size_t len) = NULL;

bool fsm_ipc_osbus_topic_handler(
        osbus_handle_t handle,
        char *topic_path,
        osbus_msg_t *msg,
        void *user_data)
{
    LOGT("%s(%s)", __func__, topic_path);
    const uint8_t *buf = NULL;
    int size = 0;
    if (!osbus_msg_get_prop_binary(msg, FSM_IPC_OSBUS_DATA, &buf, &size)) {
        LOGE("%s error parsing data", __func__);
        return false;
    }
    if (!_fsm_ipc_proto_recv_cb) {
        LOGE("%s no recv callback", __func__);
        return false;
    }
    LOGD("fsm_ipc_proto_recv_cb(%p, %d)", buf, size);
    _fsm_ipc_proto_recv_cb((void*)buf, size);
    return true;
}

int fsm_ipc_server_init(struct ev_loop *loop, void (*recv_cb)(void *data, size_t len))
{
    LOGD("%s", __func__);
    _fsm_ipc_proto_recv_cb = recv_cb;
    if (!osbus_default_handle()) osbus_init();
    if (!osbus_topic_listen(OSBUS_DEFAULT, osbus_path_os(NULL, FSM_IPC_OSBUS_TOPIC), fsm_ipc_osbus_topic_handler, NULL)) {
        LOGE("Failed to register osbus topic");
        return -1;
    }
    return 0;
}

void fsm_ipc_server_close(void)
{
    LOGD("%s", __func__);
    // don't close the bus connection as there can be other users
    // of the bus, just unregister fsm_ipc specific topic handler
    if (!osbus_topic_unlisten(OSBUS_DEFAULT, osbus_path_os(NULL, FSM_IPC_OSBUS_TOPIC), fsm_ipc_osbus_topic_handler)) {
        LOGW("Failed to unregister osbus topic");
    }
    _fsm_ipc_proto_recv_cb = NULL;
}


/* client */


bool fsm_ipc_init_client(void)
{
    LOGD("%s", __func__);
    if (!osbus_default_handle()) return osbus_init();
    return true;
}

int fsm_ipc_client_send(void *data, size_t len)
{
    int rc = -1;
    osbus_msg_t *d = osbus_msg_new_object();
    if (!d) goto err;
    LOGD("%s(%p, %d)", __func__, data, (int)len);
    if (!osbus_msg_set_prop_binary(d, FSM_IPC_OSBUS_DATA, data, len)) {
        goto err;
    }
    if (!osbus_topic_send(OSBUS_DEFAULT,
                osbus_path_os(FSM_IPC_OSBUS_COMPONENT_NAME, FSM_IPC_OSBUS_TOPIC),
                d))
    {
err:
        LOGD("%s: could not send message", __func__);
        g_fsm_io_failure_cnt++;
        rc = -1;
    } else {
        g_fsm_io_success_cnt++;
        rc = 0;
    }
    osbus_msg_free(d);
    FREE(data);
    return rc;
}

void fsm_ipc_terminate_client(void)
{
    LOGD("%s", __func__);
    // don't close the bus connection as there can be other users
    // client has no resources registered, so nothing to be done
}


#endif

