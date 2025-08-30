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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "memutil.h"
#include "log.h"
#include "ovsdb_utils.h"
#include "network_metadata.h"
#include "network_metadata_report.h"
#include "network_metadata.pb-c.h"

#define MAX_STRLEN 256

/**
 * @brief Frees the pointer to serialized data
 *
 * Frees the dynamically allocated pointer to serialized data (pb->buf).
 *
 * @param pb a pointer to a serialized data container
 * @return none
 */
void free_packed_buffer(struct packed_buffer *pb)
{
    if (pb == NULL) return;
    CHECK_DOUBLE_FREE(pb);

    FREE(pb->buf);
}


/**
 * @brief duplicates a string and returns true if successful
 *
 * wrapper around string duplication when the source string might be
 * a null pointer.
 *
 * @param src source string to duplicate. Might be NULL.
 * @param dst destination string pointer
 * @return true if duplicated, false otherwise
 */
static bool str_duplicate(char *src, char **dst)
{
    if (src == NULL)
    {
        *dst = NULL;
        return true;
    }

    *dst = STRNDUP(src, MAX_STRLEN);
    if (*dst == NULL)
    {
        LOGE("%s: could not duplicate %s", __func__, src);
        return false;
    }

    return true;
}


/**
 * @brief set a protobuf integer field when its value is not zero.
 *
 * If the value to set is 0, the field is marked as non present.
 *
 * @param src source value.
 * @param dst destination value pointer
 * @param present destination presence pointer
 * @return none
 */
static void set_uint32(uint32_t src, uint32_t *dst,
                       protobuf_c_boolean *present)
{

    *present = false;
    if (src == 0) return;

    *dst = src;
    *present = true;

    return;
}


/**
 * @brief Allocates and sets an observation point protobuf.
 *
 * Uses the node info to fill a dynamically allocated
 * observation point protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_op() for this purpose.
 *
 * @param node info used to fill up the protobuf.
 * @return a pointer to a observation point protobuf structure
 */
static Traffic__ObservationPoint * set_node_info(struct node_info *node)
{
    Traffic__ObservationPoint *pb;
    bool ret;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    traffic__observation_point__init(pb);

    /* Set the protobuf fields */
    ret = str_duplicate(node->node_id, &pb->nodeid);
    if (!ret) goto err_free_pb;

    ret = str_duplicate(node->location_id, &pb->locationid);
    if (!ret) goto err_free_node_id;

    return pb;

err_free_node_id:
    FREE(pb->nodeid);

err_free_pb:
    FREE(pb);

    return NULL;
}


/**
 * @brief Free an observation point protobuf structure.
 *
 * Free dynamically allocated fields.
 *
 * @param pb observation point structure to free
 * @return none
 */
static void free_pb_op(Traffic__ObservationPoint *pb)
{
    if (pb == NULL) return;
    CHECK_DOUBLE_FREE(pb);

    FREE(pb->nodeid);
    FREE(pb->locationid);
}


/**
 * @brief Generates an observation point serialized protobuf
 *
 * Uses the information pointed by the info parameter to generate
 * a serialized observation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to the serialized data.
 */
struct packed_buffer * serialize_node_info(struct node_info *node)
{
    Traffic__ObservationPoint *pb;
    struct packed_buffer *serialized;
    void *buf;
    size_t len;

    if (node == NULL) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    /* Allocate and set observation point protobuf */
    pb = set_node_info(node);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__observation_point__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto err_free_pb;

    /* Serialize protobuf */
    serialized->len = traffic__observation_point__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    free_pb_op(pb);
    FREE(pb);

    /* Return serialized content */
    return serialized;

err_free_pb:
    free_pb_op(pb);
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}


/**
 * @brief Generates a flow tags serialized protobuf
 *
 * Uses the information pointed by the flow_tags parameter to generate
 * a serialized flow tags buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param counters info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
serialize_flow_tags(struct flow_tags *flow_tags)
{
    Traffic__FlowTags *pb;
    struct packed_buffer *serialized;
    void *buf;
    size_t len;

    if (flow_tags == NULL) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    /* Allocate and set flow stats protobuf */
    pb = set_flow_tags(flow_tags);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__flow_tags__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto err_free_pb;

    /* Serialize protobuf */
    serialized->len = traffic__flow_tags__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    free_pb_flow_tags(pb);
    FREE(pb);

    /* Return serialized content */
    return serialized;

err_free_pb:
    free_pb_flow_tags(pb);
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}

/**
 * @brief Generates a data_reports serialized protobuf
 *
 * Uses the information pointed by the data_reports parameter to generate
 * a serialized data report buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param data_reports info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
serialize_data_report_tags(struct data_report_tags *data_reports)
{
    struct packed_buffer *serialized;
    Traffic__DataReportTag *pb;
    void *buf;
    size_t len;

    if (data_reports == NULL) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(struct packed_buffer));

    /* Allocate and set data report protobuf */
    pb = set_data_report_tags(data_reports);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__data_report_tag__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    /* Serialize protobuf */
    serialized->len = traffic__data_report_tag__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    free_pb_data_report_tags(pb);
    FREE(pb);

    /* Return serialized content */
    return serialized;

err_free_pb:
    free_pb_data_report_tags(pb);
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}

/**
 * @brief Allocates and sets a data report tags protobuf.
 *
 * Uses the data reports info to fill a dynamically allocated
 * flow key protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_data_report_tags() for this purpose.
 *
 * @param data_reports info used to fill up the protobuf
 * @return a pointer to a data report tags protobuf structure
 */
Traffic__DataReportTag *
set_data_report_tags(struct data_report_tags *data_report_tags)
{
    Traffic__DataReportTag *pb;
    struct str_set *tags;
    char **report_tags;
    size_t allocated;
    char **features;
    size_t nelems;
    char **pb_tag;
    size_t i;
    bool ret;

    if (data_report_tags == NULL) return NULL;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the protobuf structure */
    traffic__data_report_tag__init(pb);

    tags = data_report_tags->data_report;
    nelems = tags->nelems;

    report_tags = CALLOC(nelems, sizeof(*report_tags));

    pb->features = report_tags;
    pb_tag = report_tags;
    features = tags->array;
    allocated = 0;
    for (i = 0; i < nelems; i++)
    {
        ret = str_duplicate(*features, pb_tag);
        if (!ret) goto err_free_report_tags;

        allocated++;
        features++;
        pb_tag++;
    }

    pb->n_features = nelems;
    ret = str_duplicate(data_report_tags->id, &pb->id);

    if (!ret) goto err_free_report_tags;

    return pb;

err_free_report_tags:
    pb_tag = pb->features;
    for (i = 0; i < allocated; i++)
    {
        FREE(*pb_tag);
        pb_tag++;
    }
    FREE(report_tags);
    FREE(pb);

    return NULL;
}

/**
 * @brief Free a data report tag protobuf structure.
 *
 * Free dynamically allocated fields.
 *
 * @param pb data report tag structure to free
 * @return none
 */
void
free_pb_data_report_tags(Traffic__DataReportTag *pb)
{
    char **features;
    size_t i;

    if (pb == NULL) return;
    CHECK_DOUBLE_FREE(pb);

    features = pb->features;
    for (i = 0; i < pb->n_features; i++)
    {
        FREE(*features);
        features++;
    }
    FREE(pb->id);
    FREE(pb->features);

}

/**
 * @brief Allocates and sets a flow tags protobuf.
 *
 * Uses the flow tags info to fill a dynamically allocated
 * flow key protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_flow_tags() for this purpose.
 *
 * @param flow_tags info used to fill up the protobuf
 * @return a pointer to a flow tags protobuf structure
 */
Traffic__FlowTags *
set_flow_tags(struct flow_tags *flow_tags)
{
    Traffic__FlowTags *pb;
    size_t allocated;
    size_t nelems;
    char **pb_tag;
    char **tags;
    char **ftag;
    size_t i;
    bool ret;

    if (flow_tags == NULL) return NULL;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    traffic__flow_tags__init(pb);

    /* Set the protobuf fields */
    ret = str_duplicate(flow_tags->vendor, &pb->vendor);
    if (!ret) goto err_free_pb;

    ret = str_duplicate(flow_tags->app_name, &pb->appname);
    if (!ret) goto err_free_vendor;

    /* Done if there are no tags */
    nelems = flow_tags->nelems;
    if (nelems == 0) return pb;

    tags = CALLOC(nelems, sizeof(*tags));
    if (tags == NULL) goto err_free_app;

    pb->apptags = tags;
    pb_tag = tags;
    ftag = flow_tags->tags;
    allocated = 0;
    for (i = 0; i < nelems; i++)
    {
        ret = str_duplicate(*ftag, pb_tag);
        if (!ret) goto err_free_tags;

        allocated++;
        pb_tag++;
        ftag++;
    }
    pb->n_apptags = nelems;

    return pb;

err_free_tags:
    pb_tag = pb->apptags;
    for (i = 0; i < allocated; i++)
    {
        FREE(*pb_tag);
        pb_tag++;
    }
    FREE(tags);

err_free_app:
    FREE(pb->appname);

err_free_vendor:
    FREE(pb->vendor);

err_free_pb:
    FREE(pb);

    return NULL;
}


/**
 * @brief Free a flow tag protobuf structure.
 *
 * Free dynamically allocated fields.
 *
 * @param pb flow tag structure to free
 * @return none
 */
void free_pb_flow_tags(Traffic__FlowTags *pb)
{
    char **pb_tag;
    size_t i;

    if (pb == NULL) return;
    CHECK_DOUBLE_FREE(pb);

    FREE(pb->vendor);
    FREE(pb->appname);

    pb_tag = pb->apptags;
    for (i = 0; i < pb->n_apptags; i++)
    {
        FREE(*pb_tag);
        pb_tag++;
    }
    FREE(pb->apptags);
}

/**
 * @brief Allocates and sets a table of tags protobufs
 *
 * Uses the key info to fill a dynamically allocated
 * table of flow stats protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param key info used to fill up the protobuf table
 * @return a flow tags protobuf pointers table
 */
Traffic__FlowTags
**set_pb_flow_tags(struct flow_key *key)
{
    Traffic__FlowTags **tags_pb_tbl;
    Traffic__FlowTags **tags_pb;
    struct flow_tags **tags;
    size_t i, allocated;

    if (key == NULL) return NULL;

    if (key->num_tags == 0) return NULL;

    /* Allocate the array of flow stats */
    tags_pb_tbl = CALLOC(key->num_tags, sizeof(*tags_pb_tbl));
    if (tags_pb_tbl == NULL) return NULL;

    /* Set each of the stats protobuf */
    tags = key->tags;
    tags_pb = tags_pb_tbl;
    allocated = 0;
    for (i = 0; i < key->num_tags; i++)
    {
        *tags_pb = set_flow_tags(*tags);
        if (*tags_pb == NULL) goto err_free_pb_tags;

        allocated++;
        tags++;
        tags_pb++;
    }

    return tags_pb_tbl;

err_free_pb_tags:
    tags_pb = tags_pb_tbl;
    for (i = 0; i < allocated; i++)
    {
        free_pb_flow_tags(*tags_pb);
        FREE(*tags_pb);
        tags_pb++;
    }
    FREE(tags_pb_tbl);

    return NULL;
}

/**
 * @brief Allocates and sets a table of Data Report protobuf's
 *
 * Uses the key info to fill a dynamically allocated
 * table of data report protobuf'ss.
 * The caller is responsible for freeing the returned pointer
 *
 * @param key info used to fill up the protobuf table
 * @return a data report tags protobuf pointers table
 */
Traffic__DataReportTag **
set_pb_report_tags(struct flow_key *key)
{
    Traffic__DataReportTag **report_tags_pb_tbl;
    Traffic__DataReportTag **report_tags_pb;
    struct data_report_tags **report_tags;
    size_t i, allocated;

    if (key == NULL) return NULL;

    if (key->num_data_report == 0) return NULL;

    /* Allocate the array of report tags */
    report_tags_pb_tbl = CALLOC(key->num_data_report, sizeof(*report_tags_pb_tbl));
    if (report_tags_pb_tbl == NULL) return NULL;

    report_tags = key->data_report;
    report_tags_pb = report_tags_pb_tbl;
    allocated = 0;

    for (i = 0; i < key->num_data_report; i++)
    {
        *report_tags_pb = set_data_report_tags(*report_tags);
        if (report_tags_pb == NULL) goto err_free_pb_data_report_tags;

        allocated++;
        report_tags++;
        report_tags_pb++;
    }

    return report_tags_pb_tbl;

err_free_pb_data_report_tags:
    report_tags_pb = report_tags_pb_tbl;
    for (i = 0; i < allocated; i++)
    {
        free_pb_data_report_tags(*report_tags_pb);
        FREE(*report_tags_pb);
        report_tags_pb++;
    }

    FREE(report_tags_pb_tbl);

    return NULL;
}

/**
 * @brief Allocates and sets a vendor key/value protobuf.
 *
 * Uses the vendor kv_pair info to fill a dynamically allocated
 * flow key protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_vendor_kv() for this purpose.
 *
 * @param key/vakue pair info used to fill up the protobuf
 * @return a pointer to a flow counters protobuf structure
 */
Traffic__VendorDataKVPair *
set_vendor_kv(struct vendor_data_kv_pair *kv_pair)
{
    Traffic__VendorDataKVPair *pb;
    bool ret;

    if (kv_pair == NULL) return NULL;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    traffic__vendor_data__kvpair__init(pb);

    /* Set the protobuf fields */
    ret = str_duplicate(kv_pair->key, &pb->key);
    if (!ret) goto err_free_pb;

    if (kv_pair->value_type == NET_VENDOR_STR)
    {
        ret = str_duplicate(kv_pair->str_value, &pb->val_str);
        if (!ret) goto err_free_key;
    }
    else if (kv_pair->value_type == NET_VENDOR_U32)
    {
        pb->has_val_u32 = true;
        pb->val_u32 = kv_pair->u32_value;
    }
    else if (kv_pair->value_type == NET_VENDOR_U64)
    {
        pb->has_val_u64 = true;
        pb->val_u64 = kv_pair->u64_value;
    }
    else goto err_free_str_val;

    return pb;

err_free_str_val:
    FREE(pb->val_str);

err_free_key:
    FREE(pb->key);

err_free_pb:
    FREE(pb);

    return NULL;
}


/**
 * @brief Free a flow tag protobuf structure.
 *
 * Free dynamically allocated fields.
 *
 * @param pb flow tag structure to free
 * @return none
 */
void
free_pb_vendor_kv(Traffic__VendorDataKVPair *pb)
{
    CHECK_DOUBLE_FREE(pb);

    FREE(pb->key);
    FREE(pb->val_str);

    return;
}


/**
 * @brief Allocates and sets a table of vendor_kv_pair protobufs
 *
 * Uses the key info to fill a dynamically allocated
 * table of vendor data key/value pairs protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param vdr_data info used to fill up the protobuf table
 * @return a vendor key/value protobuf pointers table
 */
Traffic__VendorDataKVPair **
set_pb_vendor_kv_pairs(struct flow_vendor_data *vdr_data)
{
    Traffic__VendorDataKVPair **kv_pair_pb_tbl;
    Traffic__VendorDataKVPair **kv_pairs_pb;
    struct vendor_data_kv_pair **kv_pairs;
    size_t i, allocated;

    if (vdr_data == NULL) return NULL;

    if (vdr_data->nelems == 0) return NULL;

    /* Allocate the array of flow stats */
    kv_pair_pb_tbl = CALLOC(vdr_data->nelems,
                            sizeof(*kv_pair_pb_tbl));
    if (kv_pair_pb_tbl == NULL) return NULL;

    /* Set each of the stats protobuf */
    kv_pairs = vdr_data->kv_pairs;
    kv_pairs_pb = kv_pair_pb_tbl;
    allocated = 0;
    for (i = 0; i < vdr_data->nelems; i++)
    {
        *kv_pairs_pb = set_vendor_kv(*kv_pairs);
        if (*kv_pairs_pb == NULL) goto err_free_pb_kv_pairs;

        allocated++;
        kv_pairs++;
        kv_pairs_pb++;
    }

    return kv_pair_pb_tbl;

err_free_pb_kv_pairs:
    kv_pairs_pb = kv_pair_pb_tbl;
    for (i = 0; i < allocated; i++)
    {
        free_pb_vendor_kv(*kv_pairs_pb);
        FREE(*kv_pairs_pb);
        kv_pairs_pb++;
    }
    FREE(kv_pair_pb_tbl);

    return NULL;
}


/**
 * @brief Generates a vendor kvpair serialized protobuf
 *
 * Uses the information pointed by the kv pair parameter to generate
 * a serialized vendor kvpair buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param counters info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
serialize_vdr_kvpair(struct vendor_data_kv_pair *vendor_kvpair)
{
    Traffic__VendorDataKVPair *pb;
    struct packed_buffer *serialized;
    void *buf;
    size_t len;

    if (vendor_kvpair == NULL) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    /* Allocate and set flow stats protobuf */
    pb = set_vendor_kv(vendor_kvpair);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__vendor_data__kvpair__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto err_free_pb;

    /* Serialize protobuf */
    serialized->len = traffic__vendor_data__kvpair__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    free_pb_vendor_kv(pb);
    FREE(pb);

    /* Return serialized content */
    return serialized;

err_free_pb:
    free_pb_vendor_kv(pb);
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}


/**
 * @brief Allocates and sets a vendor key/value protobuf.
 *
 * Uses the vendor kv_pair info to fill a dynamically allocated
 * flow key protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_vendor_kv() for this purpose.
 *
 * @param counters info used to fill up the protobuf
 * @return a pointer to a flow counters protobuf structure
 */
Traffic__VendorData *
set_vendor_data(struct flow_vendor_data *vendor_data)
{
    Traffic__VendorData *pb;
    bool ret;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    traffic__vendor_data__init(pb);

    /* Set the protobuf fields */
    ret = str_duplicate(vendor_data->vendor, &pb->vendor);
    if (!ret) goto err_free_pb;

    if (vendor_data->nelems == 0) return pb;

    pb->vendorkvpair = set_pb_vendor_kv_pairs(vendor_data);
    if (pb == NULL) goto err_free_vendor;

    pb->n_vendorkvpair = vendor_data->nelems;

    return pb;

err_free_vendor:
    FREE(pb->vendor);

err_free_pb:
    FREE(pb);

    return NULL;
}


/**
 * @brief Free a flow tag protobuf structure.
 *
 * Free dynamically allocated fields.
 *
 * @param pb flow tag structure to free
 * @return none
 */
void
free_pb_vendor_data(Traffic__VendorData *pb)
{
    size_t i;

    if (pb == NULL) return;
    CHECK_DOUBLE_FREE(pb);

    for (i = 0; i < pb->n_vendorkvpair; i++)
    {
        free_pb_vendor_kv(pb->vendorkvpair[i]);
        FREE(pb->vendorkvpair[i]);
    }
    FREE(pb->vendorkvpair);
    FREE(pb->vendor);

    return;
}


/**
 * @brief Allocates and sets a table of vendor data protobufs
 *
 * Uses the key info to fill a dynamically allocated
 * table of vendor data protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param key info used to fill up the protobuf table
 * @return a flow tags protobuf pointers table
 */
Traffic__VendorData **
set_pb_vendor_data(struct flow_key *key)
{
    Traffic__VendorData **vd_pb_tbl;
    Traffic__VendorData **vd_pb;
    struct flow_vendor_data **vd;
    size_t i, allocated;

    if (key == NULL) return NULL;

    if (key->num_vendor_data == 0) return NULL;

    /* Allocate the array of flow stats */
    vd_pb_tbl = CALLOC(key->num_vendor_data, sizeof(*vd_pb_tbl));
    if (vd_pb_tbl == NULL) return NULL;

    /* Set each of the stats protobuf */
    vd = key->vdr_data;
    vd_pb = vd_pb_tbl;
    allocated = 0;
    for (i = 0; i < key->num_vendor_data; i++)
    {
        *vd_pb = set_vendor_data(*vd);
        if (*vd_pb == NULL) goto err_free_pb_tags;

        allocated++;
        vd++;
        vd_pb++;
    }

    return vd_pb_tbl;

err_free_pb_tags:
    vd_pb = vd_pb_tbl;
    for (i = 0; i < allocated; i++)
    {
        free_pb_vendor_data(*vd_pb);
        FREE(*vd_pb);
        vd_pb++;
    }
    FREE(vd_pb_tbl);

    return NULL;
}

Traffic__FlowState *
set_pb_flowstate(struct flow_state *key)
{
    Traffic__FlowState *pb;

    if (key == NULL) return NULL;

    pb = CALLOC(1, sizeof(*pb));

    if (pb == NULL) return NULL;

    traffic__flow_state__init(pb);

    if (key->fstart)
    {
        pb->has_flowstart = true;
        pb->flowstart = true;
    }

    if (key->fend)
    {
        pb->has_flowend = true;
        pb->flowend = true;
    }

    if (key->first_obs > 0)
    {
        pb->has_firstobservedat = true;
        pb->firstobservedat = key->first_obs;
    }

    if (key->last_obs > 0)
    {
        pb->has_lastobservedat = true;
        pb->lastobservedat = key->last_obs;
    }

    return pb;
}


/**
 * @brief Generates a flow vendor data serialized protobuf
 *
 * Uses the information pointed by the kv pair parameter to generate
 * a serialized vendor kvpair buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param counters info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
serialize_vendor_data(struct flow_vendor_data *vendor_data)
{
    Traffic__VendorData *pb;
    struct packed_buffer *serialized;
    void *buf;
    size_t len;

    if (vendor_data == NULL) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(*pb));
    if (serialized == NULL) return NULL;

    /* Allocate and set flow key protobuf */
    pb = set_vendor_data(vendor_data);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__vendor_data__get_packed_size(pb);
    if (len == 0) goto err_free_pb;;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto err_free_pb;

    serialized->len = traffic__vendor_data__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    free_pb_vendor_data(pb);
    FREE(pb);

    /* Return serialized content */
    return serialized;

err_free_pb:
    free_pb_vendor_data(pb);
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}

/**
 * @brief Generates a flow state serialized protobuf
 *
 * Uses the information pointed by the flowstate parameter to generate
 * a serialized flow_state  buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param flow_state info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
serialize_flow_state(struct flow_state *flow_state)
{
    Traffic__FlowState *pb;
    struct packed_buffer *serialized;
    void *buf;
    size_t len;

    if (flow_state ==  NULL) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(struct packed_buffer));

    /* Allocate and set flow key protobuf */
    pb = set_pb_flowstate(flow_state);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__flow_state__get_packed_size(pb);
    if (len == 0) goto err_free_pb;;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    serialized->len = traffic__flow_state__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    FREE(pb);

    /* Return serialized content */
    return serialized;

err_free_pb:
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}


/**
 * @brief Allocates and sets a flow key protobuf.
 *
 * Uses the key info to fill a dynamically allocated
 * flow key protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_flowkey() for this purpose.
 *
 * @param key info used to fill up the protobuf
 * @return a pointer to a flow key protobuf structure
 */
static Traffic__FlowKey *set_flow_key(struct flow_key *key)
{
    Traffic__FlowKey *pb;
    size_t i;
    bool ret;

    if (key == NULL) return NULL;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    traffic__flow_key__init(pb);

    /* Set the protobuf fields */
    ret = str_duplicate(key->smac, &pb->srcmac);
    if (!ret) goto err_free_pb;

    if (key->isparent_of_smac)
    {
        pb->has_parentofsrcmac = true;
        pb->parentofsrcmac = true;
    }

    ret = str_duplicate(key->dmac, &pb->dstmac);
    if (!ret) goto err_free_srcmac;

    if (key->isparent_of_dmac)
    {
        pb->has_parentofdstmac = true;
        pb->parentofdstmac = true;
    }

    ret = str_duplicate(key->src_ip, &pb->srcip);
    if (!ret) goto err_free_dstmac;

    ret = str_duplicate(key->dst_ip, &pb->dstip);
    if (!ret) goto err_free_srcip;

    set_uint32(key->vlan_id, &pb->vlanid, &pb->has_vlanid);
    set_uint32((uint32_t)key->ethertype, &pb->ethertype, &pb->has_ethertype);
    set_uint32((uint32_t)key->protocol, &pb->ipprotocol, &pb->has_ipprotocol);
    set_uint32((uint32_t)key->sport, &pb->tptsrcport, &pb->has_tptsrcport);
    set_uint32((uint32_t)key->dport, &pb->tptdstport, &pb->has_tptdstport);
    set_uint32((uint32_t)key->direction, &pb->direction, &pb->has_direction);
    set_uint32((uint32_t)key->originator, &pb->originator, &pb->has_originator);

    ret = str_duplicate(key->networkid, &pb->networkzone);
    if (!ret) goto err_free_dstip;

    ret = str_duplicate(key->uplinkname, &pb->uplinkname);
    if (!ret) goto err_free_nwid;

    set_uint32((uint32_t)key->flowmarker, &pb->flowmarker, &pb->has_flowmarker);
    pb->flowstate = set_pb_flowstate(&key->state);

    if (key->num_data_report != 0)
    {
        pb->datareporttag = set_pb_report_tags(key);
        if (pb->datareporttag == NULL) goto err_free_flow_tags;

        pb->n_datareporttag = key->num_data_report;
    }

    /* Exit now if not requested to send vendor data */
    if (!key->state.report_attrs) return pb;

    if (key->num_tags != 0)
    {
        /* Add the flow tags */
        pb->flowtags = set_pb_flow_tags(key);
        if (pb->flowtags == NULL) goto err_free_uplinkname;

        pb->n_flowtags = key->num_tags;
        key->state.report_attrs = false;
    }

    if (key->num_vendor_data != 0)
    {
        pb->vendordata = set_pb_vendor_data(key);
        if (pb->vendordata == NULL) goto err_free_data_report;

        pb->n_vendordata = key->num_vendor_data;
        key->state.report_attrs = false;
    }

    if (key->log && key->acc != NULL)
    {
        LOGD("%s: report for acc prepared ", __func__);
        net_md_log_acc(key->acc, __func__);
    }
    return pb;

err_free_data_report:
    for (i = 0; i < pb->n_datareporttag; i++)
    {
        free_pb_data_report_tags(pb->datareporttag[i]);
        FREE(pb->datareporttag[i]);
    }
    FREE(pb->datareporttag);

err_free_flow_tags:
    for (i = 0; i < pb->n_flowtags; i++)
    {
        free_pb_flow_tags(pb->flowtags[i]);
        FREE(pb->flowtags[i]);
    }
    FREE(pb->flowtags);

err_free_uplinkname:
    FREE(pb->uplinkname);

err_free_nwid:
    FREE(pb->networkzone);

err_free_dstip:
    FREE(pb->dstip);

err_free_srcip:
    FREE(pb->srcip);

err_free_dstmac:
    FREE(pb->dstmac);

err_free_srcmac:
    FREE(pb->srcmac);

err_free_pb:
    FREE(pb);

    return NULL;
}


/**
 * @brief Free a flow key protobuf structure.
 *
 * Free dynamically allocated fields.
 *
 * @param pb flow key structure to free
 * @return none
 */
static void free_pb_flowkey(Traffic__FlowKey *pb)
{
    size_t i;

    if (pb == NULL) return;
    CHECK_DOUBLE_FREE(pb);

    FREE(pb->srcmac);
    FREE(pb->dstmac);
    FREE(pb->srcip);
    FREE(pb->dstip);
    FREE(pb->uplinkname);
    FREE(pb->networkzone);

    for (i = 0; i < pb->n_flowtags; i++)
    {
        free_pb_flow_tags(pb->flowtags[i]);
        FREE(pb->flowtags[i]);
    }
    FREE(pb->flowtags);

    for (i = 0; i < pb->n_vendordata; i++)
    {
        free_pb_vendor_data(pb->vendordata[i]);
        FREE(pb->vendordata[i]);
    }
    FREE(pb->vendordata);

    for (i = 0; i < pb->n_datareporttag; i++)
    {
        free_pb_data_report_tags(pb->datareporttag[i]);
        FREE(pb->datareporttag[i]);
    }
    FREE(pb->datareporttag);

    FREE(pb->flowstate);
}


/**
 * @brief Generates a flow_key serialized protobuf.
 *
 * Uses the information pointed by the key parameter to generate
 * a serialized flow key buffer.
 * The caller is responsible for freeing the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param key info used to fill up the protobuf
 * @return a pointer to the serialized data.
 */
struct packed_buffer * serialize_flow_key(struct flow_key *key)
{
    Traffic__FlowKey *pb;
    struct packed_buffer *serialized;
    void *buf;
    size_t len;

    if (key == NULL) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    /* Allocate and set flow key protobuf */
    pb = set_flow_key(key);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__flow_key__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto err_free_pb;

    serialized->len = traffic__flow_key__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    free_pb_flowkey(pb);
    FREE(pb);

    /* Return serialized content */
    return serialized;

err_free_pb:
    free_pb_flowkey(pb);
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}


/**
 * @brief Allocates and sets a flow counters protobuf.
 *
 * Uses the counters info to fill a dynamically allocated
 * flow counters protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_flowcount() for this purpose.
 *
 * @param counters info used to fill up the protobuf
 * @return a pointer to a flow counters protobuf structure
 */
static Traffic__FlowCounters *
set_flow_counters(struct flow_counters *counters)
{
    Traffic__FlowCounters *pb;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    traffic__flow_counters__init(pb);

    /* Set the protobuf fields */
    pb->has_packetscount = true;
    pb->packetscount = counters->packets_count;

    pb->has_bytescount = true;
    pb->bytescount = counters->bytes_count;

    return pb;
}


/**
 * @brief Free a flow counters protobuf structure.
 *
 * Free dynamically allocated fields.
 *
 * @param pb flow counters structure to free
 * @return none
 */
static void free_pb_flowcount(Traffic__FlowCounters *pb)
{
    CHECK_DOUBLE_FREE(pb);
}


/**
 * @brief Generates a flow counters serialized protobuf
 *
 * Uses the information pointed by the counter parameter to generate
 * a serialized flow counters buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param counters info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
serialize_flow_counters(struct flow_counters *counters)
{
    Traffic__FlowCounters *pb;
    struct packed_buffer *serialized;
    void *buf;
    size_t len;

    if (counters == NULL) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(struct packed_buffer));
    if (serialized == NULL) return NULL;

    /* Allocate and set a flow counters protobuf */
    pb = set_flow_counters(counters);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__flow_counters__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto err_free_pb;

    /* Serialize protobuf */
    serialized->len = traffic__flow_counters__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    free_pb_flowcount(pb);
    FREE(pb);

    /* Return serialized content */
    return serialized;

err_free_pb:
    free_pb_flowcount(pb);
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}

/**
 * @brief Allocates and sets a flow uplink protobuf.
 *
 * @param uplink info used to fill up the protobuf
 * @return a pointer to a flow uplink protobuf structure
 */
static Traffic__FlowUplink *set_flow_uplink(struct flow_uplink *uplink)
{
    Traffic__FlowUplink *pb;
    bool ret;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    traffic__flow_uplink__init(pb);

    if (uplink->uplink_if_type == NULL) goto err_free_uplinkiftype;

    ret = str_duplicate(uplink->uplink_if_type, &pb->uplinkiftype);
    if (!ret) goto err_free_uplinkiftype;

    pb->has_uplinkchanged = true;
    pb->uplinkchanged = uplink->uplink_changed;

    return pb;

err_free_uplinkiftype:
    FREE(pb);

    return NULL;
}

/**
 * @brief Free a flow uplink protobuf structure.
 *
 * Free dynamically allocated fields.
 *
 * @param pb flow uplink structure to free
 * @return none
 */
static void free_pb_flowuplink(Traffic__FlowUplink *pb)
{
    if (pb == NULL) return;
    CHECK_DOUBLE_FREE(pb);

    FREE(pb->uplinkiftype);
    pb->uplinkchanged = 0;
}


/**
 * @brief Generates a flow_uplink serialized protobuf.
 *
 * Uses the information pointed by the uplink parameter to generate
 * a serialized flow uplink buffer.
 *
 * @param uplink info used to fill up the protobuf
 * @return a pointer to the serialized data.
 */
struct packed_buffer * serialize_flow_uplink(struct flow_uplink *uplink)
{
    struct packed_buffer *serialized;
    Traffic__FlowUplink *pb;
    size_t len;
    void *buf;

    if (uplink == NULL) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    /* Allocate and set flow key protobuf */
    pb = set_flow_uplink(uplink);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__flow_uplink__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto err_free_pb;

    serialized->len = traffic__flow_uplink__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    free_pb_flowuplink(pb);
    FREE(pb);

    /* Return serialized content */
    return serialized;

err_free_pb:
    free_pb_flowuplink(pb);
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}

/**
 * @brief Allocates and sets a flow stats protobuf.
 *
 * Uses the stats info to fill a dynamically allocated
 * flow stats protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_stats() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to a observation point protobuf structure
 */
static Traffic__FlowStats *set_flow_stats(struct flow_stats *stats)
{
    Traffic__FlowStats *pb;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    traffic__flow_stats__init(pb);

    /* Set the protobuf fields */
    pb->flowkey = set_flow_key(stats->key);
    if (pb->flowkey == NULL) goto err_free_pb;

    pb->flowcount = set_flow_counters(stats->counters);
    if (pb == NULL) goto err_free_flow_key;

    return pb;

err_free_flow_uplink:
    FREE(pb->flowcount);

err_free_flow_key:
    free_pb_flowkey(pb->flowkey);
    FREE(pb->flowkey);

err_free_pb:
    FREE(pb);

    return NULL;
}


/**
 * @brief Free a flow stats protobuf structure.
 *
 * Free dynamically allocated fields.
 *
 * @param pb flow stats structure to free
 * @return none
 */
void free_pb_flowstats(Traffic__FlowStats *pb)
{
    if (pb == NULL) return;
    CHECK_DOUBLE_FREE(pb);

    free_pb_flowkey(pb->flowkey);
    FREE(pb->flowkey);
    free_pb_flowcount(pb->flowcount);
    FREE(pb->flowcount);
}


/**
 * @brief Generates a flow stats serialized protobuf.
 *
 * Uses the information pointed by the stats parameter to generate
 * a serialized flow stats buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param stats info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer * serialize_flow_stats(struct flow_stats *stats)
{
    Traffic__FlowStats *pb;
    struct packed_buffer *serialized;
    void *buf;
    size_t len;

    if (stats == NULL) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(struct packed_buffer));
    if (serialized == NULL) return NULL;

    /* Allocate and set flow stats protobuf */
    pb = set_flow_stats(stats);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__flow_stats__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto err_free_pb;

    /* Serialize protobuf */
    serialized->len = traffic__flow_stats__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    free_pb_flowstats(pb);
    FREE(pb);

    /* Return serialized content */
    return serialized;

err_free_pb:
    free_pb_flowstats(pb);
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}


/**
 * @brief Allocates and sets table of stats protobufs
 *
 * Uses the window info to fill a dynamically allocated
 * table of flow stats protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param window info used to fill up the protobuf table
 * @return a flow stats protobuf pointers table
 */
Traffic__FlowStats **set_pb_flow_stats(struct flow_window *window)
{
    Traffic__FlowStats **stats_pb_tbl;
    struct flow_stats **stats;
    Traffic__FlowStats **stats_pb;
    size_t i, allocated;

    if (window == NULL) return NULL;

    if (window->num_stats == 0) return NULL;

    /* Allocate the array of flow stats */
    stats_pb_tbl = CALLOC(window->num_stats, sizeof(*stats_pb_tbl));
    if (stats_pb_tbl == NULL) return NULL;

    /* Set each of the stats protobuf */
    stats = window->flow_stats;
    stats_pb = stats_pb_tbl;
    allocated = 0;
    for (i = 0; i < window->num_stats; i++)
    {
        *stats_pb = set_flow_stats(*stats);
        if (*stats_pb == NULL) goto err_free_pb_stats;

        allocated++;
        stats++;
        stats_pb++;
    }

    return stats_pb_tbl;

err_free_pb_stats:
    stats_pb = stats_pb_tbl;
    for (i = 0; i < allocated; i++)
    {
        free_pb_flowstats(*stats_pb);
        FREE(*stats_pb);
        stats_pb++;
    }
    FREE(stats_pb_tbl);

    return NULL;
}

/**
 * @brief Allocates and sets an observation window protobuf.
 *
 * Uses the stats info to fill a dynamically allocated
 * observation window protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_window() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to a observation point protobuf structure
 */
static Traffic__ObservationWindow * set_pb_window(struct flow_window *window)
{
    Traffic__ObservationWindow *pb;

    /* Allocate protobuf */
    pb  = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize protobuf */
    traffic__observation_window__init(pb);

    /* Set protobuf fields */
    pb->has_startedat = true;
    pb->startedat = window->started_at;

    pb->has_endedat = true;
    pb->endedat = window->ended_at;

    pb->has_droppedflows = (window->dropped_stats != 0);
    pb->droppedflows = window->dropped_stats;

    /* uplink info is optional */
    pb->flowuplink = set_flow_uplink(window->uplink);

    /*
     * Accept windows with no stats, bail if stats are present and
     * the stats table setting failed.
     */
    if (window->num_stats == 0) return pb;

    /* Allocate flow_stats container */
    pb->flowstats = set_pb_flow_stats(window);
    if (pb->flowstats == NULL) goto err_free_pb_uplink;

    pb->n_flowstats = window->num_stats;

    return pb;

err_free_pb_uplink:
    free_pb_flowuplink(pb->flowuplink);
    FREE(pb->flowuplink);

err_free_pb_window:
    FREE(pb);

    return NULL;
}

/**
 * @brief Free an observation window protobuf structure.
 *
 * Free dynamically allocated fields.
 *
 * @param pb flows window structure to free
 * @return none
 */
void free_pb_window(Traffic__ObservationWindow *pb)
{
    size_t i;

    if (pb == NULL) return;

    for (i = 0; i < pb->n_flowstats; i++)
    {
        free_pb_flowstats(pb->flowstats[i]);
        FREE(pb->flowstats[i]);
    }
    FREE(pb->flowstats);
    free_pb_flowuplink(pb->flowuplink);
    FREE(pb->flowuplink);
}


/**
 * @brief Generates an observation window serialized protobuf
 *
 * Uses the information pointed by the window parameter to generate
 * a serialized observation window buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param window info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer * serialize_flow_window(struct flow_window *window)
{
    Traffic__ObservationWindow *pb;
    struct packed_buffer *serialized;
    void *buf;
    size_t len;

    if (window == NULL) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    /* Allocate and set flow stats protobuf */
    pb = set_pb_window(window);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__observation_window__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto err_free_pb;

    /* Serialize protobuf */
    serialized->len = traffic__observation_window__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    free_pb_window(pb);
    FREE(pb);

    /* Return serialized content */
    return serialized;

err_free_pb:
    free_pb_window(pb);
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}


/**
 * @brief Allocates and sets table of observation window protobufs
 *
 * Uses the report info to fill a dynamically allocated
 * table of observation window protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param window info used to fill up the protobuf table
 * @return a flow stats protobuf pointers table
 */
Traffic__ObservationWindow ** set_pb_windows(struct flow_report *report)
{
    Traffic__ObservationWindow **windows_pb_tbl;
    struct flow_window **window;
    Traffic__ObservationWindow **window_pb;
    size_t i, allocated;

    if (report == NULL) return NULL;

    if (report->num_windows == 0) return NULL;

    windows_pb_tbl = CALLOC(report->num_windows, sizeof(*windows_pb_tbl));
    if (windows_pb_tbl == NULL) return NULL;

    window = report->flow_windows;
    window_pb = windows_pb_tbl;
    allocated = 0;
    /* Set each of the window protobuf */
    for (i = 0; i < report->num_windows; i++)
    {
        *window_pb = set_pb_window(*window);
        if (*window_pb == NULL) goto err_free_pb_windows;

        allocated++;
        window++;
        window_pb++;
    }
    return windows_pb_tbl;

err_free_pb_windows:
    for (i = 0; i < allocated; i++)
    {
        free_pb_window(windows_pb_tbl[i]);
        FREE(windows_pb_tbl[i]);
    }
    FREE(windows_pb_tbl);

    return NULL;
}


/**
 * @brief Allocates and sets a flow report protobuf.
 *
 * Uses the report info to fill a dynamically allocated
 * flow report protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_report() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to a observation point protobuf structure
 */
static Traffic__FlowReport * set_pb_report(struct flow_report *report)
{
    Traffic__FlowReport *pb;

    /* Allocate protobuf */
    pb  = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize protobuf */
    traffic__flow_report__init(pb);

    /* Set protobuf fields */
    pb->reportedat = report->reported_at;
    pb->has_reportedat = true;

    pb->observationpoint = set_node_info(report->node_info);
    if (pb->observationpoint == NULL) goto err_free_pb_report;

    /*
     * Accept report with no windows, bail if windows are present and
     * the windows table setting failed.
     */
    if (report->num_windows == 0) return pb;

    /* Allocate observation windows container */
    pb->observationwindow = set_pb_windows(report);
    if (pb->observationwindow == NULL) goto err_free_pb_op;

    pb->n_observationwindow = report->num_windows;

    return pb;

err_free_pb_op:
    free_pb_op(pb->observationpoint);
    FREE(pb->observationpoint);

err_free_pb_report:
    FREE(pb);

    return NULL;
}


/**
 * @brief Free a flow report protobuf structure.
 *
 * Free dynamically allocated fields.
 *
 * @param pb flow report structure to free
 * @return none
 */
static void free_pb_report(Traffic__FlowReport *pb)
{
    size_t i;

    if (pb == NULL) return;
    CHECK_DOUBLE_FREE(pb);

    free_pb_op(pb->observationpoint);
    FREE(pb->observationpoint);

    for (i = 0; i < pb->n_observationwindow; i++)
    {
        free_pb_window(pb->observationwindow[i]);
        FREE(pb->observationwindow[i]);
    }

    FREE(pb->observationwindow);
}

/**
 * @brief Generates a flow report serialized protobuf
 *
 * Uses the information pointed by the report parameter to generate
 * a serialized flow report buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param node info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer * serialize_flow_report(struct flow_report *report)
{
    Traffic__FlowReport *pb = NULL;
    struct packed_buffer *serialized = NULL;
    void *buf;
    size_t len;

    if (report == NULL) return NULL;

    /* Allocate serialization output structure */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    /* Allocate and set flow report protobuf */
    pb = set_pb_report(report);
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = traffic__flow_report__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto err_free_pb;

    serialized->len = traffic__flow_report__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    free_pb_report(pb);
    FREE(pb);

    return serialized;

err_free_pb:
    FREE(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}
