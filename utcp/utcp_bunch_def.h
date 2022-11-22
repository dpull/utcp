// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "3rd/dl_list.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(__linux) || defined(__APPLE__)
#define _countof(array) (sizeof(array) / sizeof(array[0]))
#endif

/*
------------------------------------------------------------------------------
|Ethernet  | IPv4         |UDP    | Data                   |Ethernet checksum|
------------------------------------------------------------------------------
14 bytes    20 bytes     8 bytes    x bytes                4 bytes
			\ (w/o options)                               /
			 \___________________________________________/
								  |
								 MTU
The ethernet data payload is 1500 bytes. IPv4 requires a minimum of 20 bytes for its header.
Alternatively IPv6 requires a minimum of 40 bytes. UDP requires 8 bytes for its header.
That leaves 1472 bytes (ipv4) or 1452 (ipv6) for your data.
*/
#define UDP_MTU_SIZE (1452)
#define BUNCH_NODE_CACHE_MAX_SIZE (1024)
#define UTCP_MAX_PACKET 1024

struct utcp_bunch
{
	int32_t ChSequence; // 内部赋值

	uint32_t NameIndex;
	uint16_t ChIndex;
	uint16_t DataBitsLen;

	uint8_t bOpen : 1;
	uint8_t bClose : 1;
	uint8_t CloseReason : 4;
	uint8_t bIsReplicationPaused : 1;
	uint8_t bReliable : 1;

	uint8_t bHasPackageMapExports : 1;
	uint8_t bHasMustBeMappedGUIDs : 1;
	uint8_t bPartial : 1;
	uint8_t bPartialInitial : 1;
	uint8_t bPartialFinal : 1;

	uint8_t Data[UDP_MTU_SIZE];
};

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
};