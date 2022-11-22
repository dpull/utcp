// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "3rd/dl_list.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "utcp_def.h"

struct utcp_channel* alloc_utcp_channel(int32_t InitInReliable, int32_t InitOutReliable);
void free_utcp_channel(struct utcp_channel* utcp_channel);

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
