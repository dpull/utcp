// Copyright DPULL, Inc. All Rights Reserved.

#pragma once
#include "bit_buffer.h"
#include "utcp_def_internal.h"
#include "utcp_utils.h"
#include <stdint.h>
#include <string.h>

struct ack_data
{
	int32_t AckPacketId;
	uint8_t bHasServerFrameTime;
	uint8_t FrameTimeByte;
	uint32_t InKBytesPerSecond;
};

// UNetConnection::ReceivedPacket
static inline int ack_data_read(struct bitbuf* bitbuf, struct ack_data* ack_data)
{
	memset(ack_data, 0, sizeof(*ack_data));

	// This is an acknowledgment.
	if (!bitbuf_read_int(bitbuf, (uint32_t*)&ack_data->AckPacketId, UTCP_MAX_PACKETID))
	{
		return AckSequenceMismatch;
	}

	if (!bitbuf_read_bit(bitbuf, &ack_data->bHasServerFrameTime))
	{
		utcp_log(Warning, "Failed to read extra PacketHeader information.%d", 1);
		return ReadHeaderExtraFail;
	}

	if (ack_data->bHasServerFrameTime)
	{
		if (!bitbuf_read_bytes(bitbuf, &ack_data->FrameTimeByte, 1))
		{
			utcp_log(Warning, "Failed to read extra PacketHeader information.%d", 2);
			return ReadHeaderExtraFail;
		}
	}

	if (!bitbuf_read_int_packed(bitbuf, &ack_data->InKBytesPerSecond))
	{
		utcp_log(Warning, "Failed to read extra PacketHeader information.%d", 3);
		return ReadHeaderExtraFail;
	}

	return 0;
}

// UNetConnection::SendAck
static inline int ack_data_write(struct bitbuf* bitbuf, struct ack_data* ack_data)
{
	if (!bitbuf_write_int(bitbuf, ack_data->AckPacketId, UTCP_MAX_PACKETID))
		return -1;

	if (!bitbuf_write_bit(bitbuf, ack_data->bHasServerFrameTime))
		return -2;

	if (ack_data->bHasServerFrameTime)
	{
		if (!bitbuf_write_bytes(bitbuf, &ack_data->FrameTimeByte, 1))
			return -3;
	}

	if (!bitbuf_write_int_packed(bitbuf, ack_data->InKBytesPerSecond))
		return -4;
	return 0;
}