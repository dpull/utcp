// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "3rd/dl_list.h"
#include "utcp_def.h"
#include <stdint.h>
#include <stdlib.h>

struct utcp_bunch_node
{
	struct dl_list_node dl_list_node;

	union {
		struct utcp_bunch utcp_bunch;
		struct
		{
			int32_t packet_id;
			uint16_t bunch_data_len;
			uint8_t bunch_data[UDP_MTU_SIZE];
		};
	};
};

struct utcp_channel
{
	struct dl_list_node InPartialBunch;

	struct dl_list_node InRec;
	struct dl_list_node OutRec;

	int32_t NumInRec;  // Number of packets in InRec.
	int32_t NumOutRec; // Number of packets in OutRec.

	int32_t OutReliable;
	int32_t InReliable;

	uint8_t bClose : 1;
	uint8_t CloseReason : 4;
};

struct utcp_opened_channels
{
	int32_t cap;
	int32_t num;
	uint16_t* channels;
};

struct utcp_channels
{
	struct utcp_channel* Channels[UTCP_MAX_CHANNELS];
	struct utcp_opened_channels open_channels;
	int32_t InitOutReliable;
	int32_t InitInReliable;
	uint8_t bHasChannelClose;
};