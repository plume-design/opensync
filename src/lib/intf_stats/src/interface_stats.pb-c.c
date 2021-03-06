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

/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: interface_stats.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "interface_stats.pb-c.h"
void   interfaces__intf_stats__observation_point__init
                     (Interfaces__IntfStats__ObservationPoint         *message)
{
  static Interfaces__IntfStats__ObservationPoint init_value = INTERFACES__INTF_STATS__OBSERVATION_POINT__INIT;
  *message = init_value;
}
size_t interfaces__intf_stats__observation_point__get_packed_size
                     (const Interfaces__IntfStats__ObservationPoint *message)
{
  assert(message->base.descriptor == &interfaces__intf_stats__observation_point__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t interfaces__intf_stats__observation_point__pack
                     (const Interfaces__IntfStats__ObservationPoint *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &interfaces__intf_stats__observation_point__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t interfaces__intf_stats__observation_point__pack_to_buffer
                     (const Interfaces__IntfStats__ObservationPoint *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &interfaces__intf_stats__observation_point__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Interfaces__IntfStats__ObservationPoint *
       interfaces__intf_stats__observation_point__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Interfaces__IntfStats__ObservationPoint *)
     protobuf_c_message_unpack (&interfaces__intf_stats__observation_point__descriptor,
                                allocator, len, data);
}
void   interfaces__intf_stats__observation_point__free_unpacked
                     (Interfaces__IntfStats__ObservationPoint *message,
                      ProtobufCAllocator *allocator)
{
  assert(message->base.descriptor == &interfaces__intf_stats__observation_point__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   interfaces__intf_stats__intf_stats__init
                     (Interfaces__IntfStats__IntfStats         *message)
{
  static Interfaces__IntfStats__IntfStats init_value = INTERFACES__INTF_STATS__INTF_STATS__INIT;
  *message = init_value;
}
size_t interfaces__intf_stats__intf_stats__get_packed_size
                     (const Interfaces__IntfStats__IntfStats *message)
{
  assert(message->base.descriptor == &interfaces__intf_stats__intf_stats__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t interfaces__intf_stats__intf_stats__pack
                     (const Interfaces__IntfStats__IntfStats *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &interfaces__intf_stats__intf_stats__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t interfaces__intf_stats__intf_stats__pack_to_buffer
                     (const Interfaces__IntfStats__IntfStats *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &interfaces__intf_stats__intf_stats__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Interfaces__IntfStats__IntfStats *
       interfaces__intf_stats__intf_stats__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Interfaces__IntfStats__IntfStats *)
     protobuf_c_message_unpack (&interfaces__intf_stats__intf_stats__descriptor,
                                allocator, len, data);
}
void   interfaces__intf_stats__intf_stats__free_unpacked
                     (Interfaces__IntfStats__IntfStats *message,
                      ProtobufCAllocator *allocator)
{
  assert(message->base.descriptor == &interfaces__intf_stats__intf_stats__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   interfaces__intf_stats__observation_window__init
                     (Interfaces__IntfStats__ObservationWindow         *message)
{
  static Interfaces__IntfStats__ObservationWindow init_value = INTERFACES__INTF_STATS__OBSERVATION_WINDOW__INIT;
  *message = init_value;
}
size_t interfaces__intf_stats__observation_window__get_packed_size
                     (const Interfaces__IntfStats__ObservationWindow *message)
{
  assert(message->base.descriptor == &interfaces__intf_stats__observation_window__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t interfaces__intf_stats__observation_window__pack
                     (const Interfaces__IntfStats__ObservationWindow *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &interfaces__intf_stats__observation_window__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t interfaces__intf_stats__observation_window__pack_to_buffer
                     (const Interfaces__IntfStats__ObservationWindow *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &interfaces__intf_stats__observation_window__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Interfaces__IntfStats__ObservationWindow *
       interfaces__intf_stats__observation_window__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Interfaces__IntfStats__ObservationWindow *)
     protobuf_c_message_unpack (&interfaces__intf_stats__observation_window__descriptor,
                                allocator, len, data);
}
void   interfaces__intf_stats__observation_window__free_unpacked
                     (Interfaces__IntfStats__ObservationWindow *message,
                      ProtobufCAllocator *allocator)
{
  assert(message->base.descriptor == &interfaces__intf_stats__observation_window__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   interfaces__intf_stats__intf_report__init
                     (Interfaces__IntfStats__IntfReport         *message)
{
  static Interfaces__IntfStats__IntfReport init_value = INTERFACES__INTF_STATS__INTF_REPORT__INIT;
  *message = init_value;
}
size_t interfaces__intf_stats__intf_report__get_packed_size
                     (const Interfaces__IntfStats__IntfReport *message)
{
  assert(message->base.descriptor == &interfaces__intf_stats__intf_report__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t interfaces__intf_stats__intf_report__pack
                     (const Interfaces__IntfStats__IntfReport *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &interfaces__intf_stats__intf_report__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t interfaces__intf_stats__intf_report__pack_to_buffer
                     (const Interfaces__IntfStats__IntfReport *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &interfaces__intf_stats__intf_report__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Interfaces__IntfStats__IntfReport *
       interfaces__intf_stats__intf_report__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Interfaces__IntfStats__IntfReport *)
     protobuf_c_message_unpack (&interfaces__intf_stats__intf_report__descriptor,
                                allocator, len, data);
}
void   interfaces__intf_stats__intf_report__free_unpacked
                     (Interfaces__IntfStats__IntfReport *message,
                      ProtobufCAllocator *allocator)
{
  assert(message->base.descriptor == &interfaces__intf_stats__intf_report__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
static const ProtobufCFieldDescriptor interfaces__intf_stats__observation_point__field_descriptors[2] =
{
  {
    "node_id",
    1,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Interfaces__IntfStats__ObservationPoint, node_id),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "location_id",
    2,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Interfaces__IntfStats__ObservationPoint, location_id),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned interfaces__intf_stats__observation_point__field_indices_by_name[] = {
  1,   /* field[1] = location_id */
  0,   /* field[0] = node_id */
};
static const ProtobufCIntRange interfaces__intf_stats__observation_point__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 2 }
};
const ProtobufCMessageDescriptor interfaces__intf_stats__observation_point__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "interfaces.intf_stats.ObservationPoint",
  "ObservationPoint",
  "Interfaces__IntfStats__ObservationPoint",
  "interfaces.intf_stats",
  sizeof(Interfaces__IntfStats__ObservationPoint),
  2,
  interfaces__intf_stats__observation_point__field_descriptors,
  interfaces__intf_stats__observation_point__field_indices_by_name,
  1,  interfaces__intf_stats__observation_point__number_ranges,
  (ProtobufCMessageInit) interfaces__intf_stats__observation_point__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor interfaces__intf_stats__intf_stats__field_descriptors[6] =
{
  {
    "if_name",
    1,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Interfaces__IntfStats__IntfStats, if_name),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "tx_bytes",
    2,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT64,
    offsetof(Interfaces__IntfStats__IntfStats, has_tx_bytes),
    offsetof(Interfaces__IntfStats__IntfStats, tx_bytes),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "rx_bytes",
    3,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT64,
    offsetof(Interfaces__IntfStats__IntfStats, has_rx_bytes),
    offsetof(Interfaces__IntfStats__IntfStats, rx_bytes),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "tx_packets",
    4,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT64,
    offsetof(Interfaces__IntfStats__IntfStats, has_tx_packets),
    offsetof(Interfaces__IntfStats__IntfStats, tx_packets),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "rx_packets",
    5,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT64,
    offsetof(Interfaces__IntfStats__IntfStats, has_rx_packets),
    offsetof(Interfaces__IntfStats__IntfStats, rx_packets),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "role",
    6,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Interfaces__IntfStats__IntfStats, role),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned interfaces__intf_stats__intf_stats__field_indices_by_name[] = {
  0,   /* field[0] = if_name */
  5,   /* field[5] = role */
  2,   /* field[2] = rx_bytes */
  4,   /* field[4] = rx_packets */
  1,   /* field[1] = tx_bytes */
  3,   /* field[3] = tx_packets */
};
static const ProtobufCIntRange interfaces__intf_stats__intf_stats__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 6 }
};
const ProtobufCMessageDescriptor interfaces__intf_stats__intf_stats__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "interfaces.intf_stats.IntfStats",
  "IntfStats",
  "Interfaces__IntfStats__IntfStats",
  "interfaces.intf_stats",
  sizeof(Interfaces__IntfStats__IntfStats),
  6,
  interfaces__intf_stats__intf_stats__field_descriptors,
  interfaces__intf_stats__intf_stats__field_indices_by_name,
  1,  interfaces__intf_stats__intf_stats__number_ranges,
  (ProtobufCMessageInit) interfaces__intf_stats__intf_stats__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor interfaces__intf_stats__observation_window__field_descriptors[3] =
{
  {
    "started_at",
    1,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT64,
    offsetof(Interfaces__IntfStats__ObservationWindow, has_started_at),
    offsetof(Interfaces__IntfStats__ObservationWindow, started_at),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "ended_at",
    2,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT64,
    offsetof(Interfaces__IntfStats__ObservationWindow, has_ended_at),
    offsetof(Interfaces__IntfStats__ObservationWindow, ended_at),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "intf_stats",
    3,
    PROTOBUF_C_LABEL_REPEATED,
    PROTOBUF_C_TYPE_MESSAGE,
    offsetof(Interfaces__IntfStats__ObservationWindow, n_intf_stats),
    offsetof(Interfaces__IntfStats__ObservationWindow, intf_stats),
    &interfaces__intf_stats__intf_stats__descriptor,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned interfaces__intf_stats__observation_window__field_indices_by_name[] = {
  1,   /* field[1] = ended_at */
  2,   /* field[2] = intf_stats */
  0,   /* field[0] = started_at */
};
static const ProtobufCIntRange interfaces__intf_stats__observation_window__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 3 }
};
const ProtobufCMessageDescriptor interfaces__intf_stats__observation_window__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "interfaces.intf_stats.ObservationWindow",
  "ObservationWindow",
  "Interfaces__IntfStats__ObservationWindow",
  "interfaces.intf_stats",
  sizeof(Interfaces__IntfStats__ObservationWindow),
  3,
  interfaces__intf_stats__observation_window__field_descriptors,
  interfaces__intf_stats__observation_window__field_indices_by_name,
  1,  interfaces__intf_stats__observation_window__number_ranges,
  (ProtobufCMessageInit) interfaces__intf_stats__observation_window__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor interfaces__intf_stats__intf_report__field_descriptors[3] =
{
  {
    "reported_at",
    1,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT64,
    offsetof(Interfaces__IntfStats__IntfReport, has_reported_at),
    offsetof(Interfaces__IntfStats__IntfReport, reported_at),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "observation_point",
    2,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_MESSAGE,
    0,   /* quantifier_offset */
    offsetof(Interfaces__IntfStats__IntfReport, observation_point),
    &interfaces__intf_stats__observation_point__descriptor,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "observation_windows",
    3,
    PROTOBUF_C_LABEL_REPEATED,
    PROTOBUF_C_TYPE_MESSAGE,
    offsetof(Interfaces__IntfStats__IntfReport, n_observation_windows),
    offsetof(Interfaces__IntfStats__IntfReport, observation_windows),
    &interfaces__intf_stats__observation_window__descriptor,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned interfaces__intf_stats__intf_report__field_indices_by_name[] = {
  1,   /* field[1] = observation_point */
  2,   /* field[2] = observation_windows */
  0,   /* field[0] = reported_at */
};
static const ProtobufCIntRange interfaces__intf_stats__intf_report__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 3 }
};
const ProtobufCMessageDescriptor interfaces__intf_stats__intf_report__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "interfaces.intf_stats.IntfReport",
  "IntfReport",
  "Interfaces__IntfStats__IntfReport",
  "interfaces.intf_stats",
  sizeof(Interfaces__IntfStats__IntfReport),
  3,
  interfaces__intf_stats__intf_report__field_descriptors,
  interfaces__intf_stats__intf_report__field_indices_by_name,
  1,  interfaces__intf_stats__intf_report__number_ranges,
  (ProtobufCMessageInit) interfaces__intf_stats__intf_report__init,
  NULL,NULL,NULL    /* reserved[123] */
};
