// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "3rd/dl_list.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct utcp_channel
{
	struct dl_list_node InPartialBunch;

	struct dl_list_node InRec;
	struct dl_list_node OutRec;

	int32_t NumInRec;  // Number of packets in InRec.
	int32_t NumOutRec; // Number of packets in OutRec.

	int32_t OutReliable;
	int32_t InReliable;
};

struct utcp_channel* utcp_get_channel(struct utcp_fd* fd, int ChIndex);
struct utcp_channel* utcp_open_channel(struct utcp_fd* fd, int ChIndex);
void utcp_close_channel(struct utcp_fd* fd, int ChIndex);
void utcp_closeall_channel(struct utcp_fd* fd);

struct utcp_channel* alloc_utcp_channel(int32_t InitInReliable, int32_t InitOutReliable);
void free_utcp_channel(struct utcp_channel* utcp_channel);

struct utcp_bunch_node* alloc_utcp_bunch_node();
void free_utcp_bunch_node(struct utcp_bunch_node* utcp_bunch_node);

bool enqueue_incoming_data(struct utcp_channel* utcp_channel, struct utcp_bunch_node* utcp_bunch_node);
struct utcp_bunch_node* dequeue_incoming_data(struct utcp_channel* utcp_channel, int sequence);
