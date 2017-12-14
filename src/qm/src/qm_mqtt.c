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

#include <limits.h>
#include <stdio.h>
#include <zlib.h>

#include "os_time.h"
#include "os_nif.h"
#include "mosqev.h"
#include "dppline.h"
#include "target.h"
#include "log.h"
#include "ds_dlist.h"
#include "plume_stats.pb-c.h"

#include "qm.h"

// based on sm_mqtt.c

#define MODULE_ID LOG_MODULE_ID_MAIN

#define STATS_MQTT_PORT         8883
#define STATS_MQTT_QOS          0
#define STATS_MQTT_INTERVAL     60  /* Report interval in seconds */
#define STATS_MQTT_RECONNECT    60  /* Reconnect interval -- seconds */

/* Global MQTT instance */
static mosqev_t         qm_mqtt;
static bool             qm_mosquitto_init = false;
static bool             qm_mosqev_init = false;
static struct ev_timer  qm_mqtt_timer;
static int64_t          qm_mqtt_reconnect_ts = 0;
static char             qm_mqtt_broker[HOST_NAME_MAX];
static char             qm_mqtt_topic[HOST_NAME_MAX];
static int              qm_mqtt_port = STATS_MQTT_PORT;
static int              qm_mqtt_qos = STATS_MQTT_QOS;
static uint8_t          qm_mqtt_compress = 0;

bool qm_mqtt_is_connected()
{
    return mosqev_is_connected(&qm_mqtt);
}

/**
 * Set MQTT settings
 */
void qm_mqtt_set(const char *broker, const char *port, const char *topic, const char *qos, int compress)
{
    const char *new_broker;
    int new_port;
    bool broker_changed = false;


    qm_mqtt_compress = compress;

    // broker address
    new_broker = broker ? broker : "";
    if (strcmp(qm_mqtt_broker, new_broker)) broker_changed = true;
    strlcpy(qm_mqtt_broker, new_broker, sizeof(qm_mqtt_broker));

    // broker port
    new_port = port ? atoi(port) : STATS_MQTT_PORT;
    if (qm_mqtt_port != new_port) broker_changed = true;
    qm_mqtt_port = new_port;

    // qos
    if (qos)
    {
        qm_mqtt_qos = atoi(qos);
    }
    else
    {
        qm_mqtt_qos = STATS_MQTT_QOS;
    }

    // topic
    if (topic != NULL)
    {
        strlcpy(qm_mqtt_topic, topic, sizeof(qm_mqtt_topic));
    }
    else
    {
        qm_mqtt_topic[0] = '\0';
    }

    /* Initialize TLS stuff */
    if (!mosqev_tls_opts_set(&qm_mqtt, SSL_VERIFY_PEER, MOSQEV_TLS_VERSION, mosqev_ciphers))
    {
        LOGE("setting TLS options.\n");
        goto error;
    }

    if (!mosqev_tls_set(&qm_mqtt,
                        target_tls_cacert_filename(),
                        NULL,
                        target_tls_mycert_filename(),
                        target_tls_privkey_filename(),
                        NULL))
    {
        LOGE("setting TLS certificates.\n");
        goto error;
    }

    LOGN("MQTT broker: '%s' port: %d topic: '%s' qos: %d compress: %d",
            qm_mqtt_broker, qm_mqtt_port, qm_mqtt_topic, qm_mqtt_qos, qm_mqtt_compress);

    // reconnect if broker changed and is connected
    if (broker_changed && qm_mqtt_is_connected()) {
        LOGN("MQTT broker changed - reconnecting...");
        mosqev_disconnect(&qm_mqtt);
        qm_mqtt_reconnect();
    }

    return;

error:
    qm_mqtt_stop();
    return;
}

bool qm_mqtt_config_valid(void)
{
    return
        (strlen(qm_mqtt_broker) > 0) &&
        (strlen(qm_mqtt_topic) > 0);
}

void qm_mqtt_stop(void)
{
    ev_timer_stop(EV_DEFAULT, &qm_mqtt_timer);

    if (qm_mosqev_init) mosqev_del(&qm_mqtt);
    if (qm_mosquitto_init) mosquitto_lib_cleanup();

    qm_mosqev_init = qm_mosquitto_init = false;

    LOG(NOTICE, "Closing MQTT connection.");
}

bool qm_mqtt_publish(mosqev_t *mqtt, qm_item_t *qi)
{
    long mlen = qi->size;
    void *mbuf = qi->buf;
    char *topic = qm_mqtt_topic;
    int qos = qm_mqtt_qos;
    bool do_compress = qm_mqtt_compress;

    // override default topic
    if (qi->topic && *qi->topic) topic = qi->topic;

    // override default qos
    if (qi->req.set_qos) qos = qi->req.qos_val;

    // override default compression
    switch (qi->req.compress) {
        default:
        case QM_REQ_COMPRESS_IF_CFG:  do_compress = qm_mqtt_compress; break;
        case QM_REQ_COMPRESS_DISABLE: do_compress = false; break;
        case QM_REQ_COMPRESS_FORCE:   do_compress = true; break;
    }
    unsigned char *buf = NULL;
    int ret;
    if (do_compress)
    {
        /*
         * cppcheck detects the code below as a memory leak.
         * mosqev_publish() will ultimately copy the data to its own buffers.
         */
        long len;
        len = compressBound(mlen);
        // allocate 1/4 more than needed
        len += len / 4;
        buf = malloc(len);
        if (!buf) {
            LOGE("DPP: allocate compress buf (%lu): out of mem.", len);
            return false;
        }
        ret = compress(buf, (unsigned long*)&len, mbuf, mlen);
        if (ret != Z_OK)
        {
            LOGE("DPP: compression error %d", ret);
            goto exit;
        }
        LOGD("DPP: Publishing uncompressed: %ld compressed: %ld reduction: %d%%",
                    mlen, len, (int)(100 - 100 * len / mlen));
        mlen = len;
        mbuf = buf;
    }
    LOGI("MQTT: Publishing %ld bytes", mlen);
    ret =  mosqev_publish(mqtt, NULL, topic, mlen, mbuf, qos, false);
exit:
    if (buf) free(buf);
    return ret;
}

void qm_mqtt_queue_append(qm_item_t *qi, qm_item_t *rep)
{
    Sts__Report *rqi = NULL;
    Sts__Report *rpt = NULL;
    int          num;

    // have stats, unpack
    rqi = sts__report__unpack(NULL, qi->size, qi->buf);
    if (!rqi) {
        // decode failed
        goto out;
    }

    // first reprot
    if (rep->size == 0) {
        rpt = rqi;
        rqi = NULL;

        goto first;
    }

    rpt = sts__report__unpack(NULL, rep->size, rep->buf);
    if (!rpt) {
        goto out;
    }

#define APPEND(_name, _type) do {\
        num = rpt->n_##_name;  \
        rpt->n_##_name += rqi->n_##_name; \
        rpt->_name = \
            realloc(rpt->_name, \
                    rpt->n_##_name * sizeof(Sts__##_type*)); \
        memcpy (&rpt->_name[num], \
                rqi->_name, \
                rqi->n_##_name * sizeof(Sts__##_type*)); \
        memset(rqi->_name, \
               0, \
               rqi->n_##_name * sizeof(Sts__##_type*)); \
    } while (0)

    // append messages
    if (rqi->survey) {
        APPEND(survey, Survey);
    }
    if (rqi->neighbors) {
        APPEND(neighbors, Neighbor);
    }
    if (rqi->capacity) {
        APPEND(capacity, Capacity);
    }
    if (rqi->clients) {
        APPEND(clients, ClientReport);
    }
    if (rqi->device) {
        APPEND(device, Device);
    }
    if (rqi->rssi_report) {
        APPEND(rssi_report, RssiReport);
    }
    if (rqi->bs_report) {
        // bs_report allows only one entry
        if (rpt->bs_report) {
            goto out;
        }

        // append message
        rpt->bs_report = rqi->bs_report;
        rqi->bs_report = NULL;
    }
#undef APPEND

first:
    {
        // pack new message
        int size = sts__report__get_packed_size(rpt);
        void *buf = malloc(size);
        if (!buf) goto out;
        size = sts__report__pack(rpt, buf);
        LOGW("merged reports stats %zd + bs %zd = %d", rep->size, qi->size, size);

        // replace message
        if(rep->buf) free(rep->buf);
        rep->buf = buf;
        rep->size = size;
    }

out:
    // cleanup
    if (rpt) sts__report__free_unpacked(rpt, NULL);
    if (rqi) sts__report__free_unpacked(rqi, NULL);
}

void qm_mqtt_publish_queue()
{
    mosqev_t *mqtt = &qm_mqtt;
    // publish messages to mqtt
    LOGD("total %d elements queued for transmission.\n", qm_queue_length());

    qm_item_t  rep;
    qm_item_t *qi = NULL;
    qm_item_t *next = NULL;
    memset(&rep, 0, sizeof(rep));
    for (qi = ds_dlist_head(&g_qm_queue.queue); qi != NULL; qi = next)
    {
        next = ds_dlist_next(&g_qm_queue.queue, qi);
        // drop empty messages
        if (qi->size == 0) {
            qm_queue_remove(qi);
            continue;
        }

        // merge items to single report
        qm_mqtt_queue_append(qi, &rep);

        // remove item
        qm_queue_remove(qi);
    }

    // publish
    if (rep.size) {
        if (!qm_mqtt_publish(mqtt, &rep)) {
            LOGE("Publish report failed.\n");
        }

        // free
        if(rep.buf) free(rep.buf);
        if(rep.topic) free(rep.topic);
    }
}

void qm_mqtt_reconnect()
{
    mosqev_t *mqtt = &qm_mqtt;

    /*
     * Reconnect handler
     */
    if (qm_mqtt_config_valid())
    {
        if (!mosqev_is_connected(mqtt))
        {
            if (qm_mqtt_reconnect_ts < ticks())
            {
                qm_mqtt_reconnect_ts = ticks() + TICKS_S(STATS_MQTT_RECONNECT);

                LOG(DEBUG, "Connecting to %s ...\n", qm_mqtt_broker);
                if (!mosqev_connect(&qm_mqtt, qm_mqtt_broker, qm_mqtt_port))
                {
                    LOGE("Connecting.\n");
                    return;
                }
            }
            else
            {
                LOG(DEBUG, "Not connected, will reconnect in %d secs", (int)TICKS_TO_S(qm_mqtt_reconnect_ts - ticks()));
            }
        }
    }
    else
    {
        /*
         * Config is invalid, but we're connected. Disconnect at once!
         */
        qm_mqtt_reconnect_ts = 0;

        if (mosqev_is_connected(mqtt))
        {
            mosqev_disconnect(mqtt);
            return;
        }
    }
}

void qm_mqtt_send_queue()
{
    // reconnect or disconnect if invalid config
    qm_mqtt_reconnect();

    // Do not report any stats if we're not connected
    if (!qm_mqtt_is_connected())
    {
        return;
    }

    qm_mqtt_publish_queue();
}


void qm_mqtt_timer_handler(struct ev_loop *loop, ev_timer *timer, int revents)
{
    (void)loop;
    (void)timer;
    (void)revents;
    qm_mqtt_send_queue();
}


void qm_mqtt_log(mosqev_t *mqtt, void *data, int lvl, const char *str)
{
    (void)mqtt;
    (void)data;
    (void)lvl;
    LOGD("MQTT LOG: %s\n", str);
}


/*
 * Initialize MQTT library
 */
bool qm_mqtt_init(void)
{
    /* Mosqev doesn't initialize libmosquitto */
    mosquitto_lib_init();
    qm_mosquitto_init = true;

    LOG(INFO,
        "Initializing MQTT library.\n");
    /*
     * Use the device serial number as client ID
     */
    char cID[64];
    if (true != target_id_get(cID, sizeof(cID)))
    {
        LOGE("acquiring device id number\n");
        goto error;
    }

    if (!mosqev_init(&qm_mqtt, cID, EV_DEFAULT, NULL))
    {
        LOGE("initializing MQTT library.\n");
        goto error;
    }

    /* Initialize logging */
    mosqev_log_cbk_set(&qm_mqtt, qm_mqtt_log);

    qm_mosqev_init = true;

    ev_timer_init(&qm_mqtt_timer, qm_mqtt_timer_handler,
            STATS_MQTT_INTERVAL, STATS_MQTT_INTERVAL);

    qm_mqtt_timer.data = &qm_mqtt;

    ev_timer_start(EV_DEFAULT, &qm_mqtt_timer);

    return true;

error:
    qm_mqtt_stop();

    return false;
}

