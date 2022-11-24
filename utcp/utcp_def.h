// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct utcp_config
{
	void (*on_accept)(struct utcp_listener* fd, void* userdata, bool reconnect);
	void (*on_outgoing)(void* fd, void* userdata, const void* data, int len); // "void* fd" is "struct utcp_listener* fd" or "struct utcp_connection* fd"
	void (*on_recv_bunch)(struct utcp_connection* fd, void* userdata, struct utcp_bunch* const bunches[], int count);
	void (*on_delivery_status)(struct utcp_connection* fd, void* userdata, int32_t packet_id, bool ack);
	void (*on_log)(int level, const char* msg, va_list args);
	void* (*on_realloc)(void* ptr, size_t size);

	int64_t ElapsedTime;
	uint32_t MagicHeader;
	uint8_t MagicHeaderBits;
	uint8_t EnableDump;
};

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

#ifdef __cplusplus
}
#endif
