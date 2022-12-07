// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "utcp_def_internal.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

struct utcp_bunch_node* alloc_utcp_bunch_node();
void free_utcp_bunch_node(struct utcp_bunch_node* utcp_bunch_node);

bool enqueue_incoming_data(struct utcp_channel* utcp_channel, struct utcp_bunch_node* utcp_bunch_node);
struct utcp_bunch_node* dequeue_incoming_data(struct utcp_channel* utcp_channel, int sequence);

void add_ougoing_data(struct utcp_channel* utcp_channel, struct utcp_bunch_node* utcp_bunch_node);
int remove_ougoing_data(struct utcp_channel* utcp_channel, int32_t packet_id, struct utcp_bunch_node* bunch_node[], int bunch_node_size);

enum merge_partial_result
{
	partial_merge_fatal = -2, // Close connection...
	partial_merge_failed = -1,
	partial_merge_succeed = 0,
	partial_available = 1,
};
enum merge_partial_result merge_partial_data(struct utcp_channel* utcp_channel, struct utcp_bunch_node* utcp_bunch_node, bool* bOutSkipAck);
void clear_partial_data(struct utcp_channel* utcp_channel);
int get_partial_bunch(struct utcp_channel* utcp_channel, struct utcp_bunch* bunches[], int bunches_size);

void utcp_channels_uninit(struct utcp_channels* utcp_channels);
struct utcp_channel* utcp_channels_get_channel(struct utcp_channels* utcp_channels, struct utcp_bunch* utcp_bunch);
void utcp_channels_on_ack(struct utcp_channels* utcp_channels, int32_t AckPacketId);
typedef int (*write_bunch_fn)(struct utcp_connection* fd, const uint8_t* Bits, const int32_t SizeInBits, const uint8_t* ExtraBits, const int32_t ExtraSizeInBits);
void utcp_channels_on_nak(struct utcp_channels* utcp_channels, int32_t NakPacketId, write_bunch_fn WriteBitsToSendBuffer, struct utcp_connection* fd);
void utcp_delay_close_channel(struct utcp_channels* utcp_channels);


