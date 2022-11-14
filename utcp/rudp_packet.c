#include "rudp_packet.h"
#include "bit_buffer.h"
#include "rudp.h"
#include "rudp_bunch.h"
#include "rudp_handshake.h"
#include "rudp_packet_notify.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum
{
	NumBitsForJitterClockTimeInHeader = 10,

	MAX_PACKET_TRAILER_BITS = 1,
	MAX_PACKET_RELIABLE_SEQUENCE_HEADER_BITS = 32 /*PackedHeader*/ + MaxSequenceHistoryLength,
	MAX_PACKET_INFO_HEADER_BITS = 1 /*bHasPacketInfo*/ + NumBitsForJitterClockTimeInHeader + 1 /*bHasServerFrameTime*/ + 8 /*ServerFrameTime*/,
	MAX_PACKET_HEADER_BITS = MAX_PACKET_RELIABLE_SEQUENCE_HEADER_BITS + MAX_PACKET_INFO_HEADER_BITS,
};

static void check_bit(struct bitbuf* bitbuf, uint8_t exp)
{
	uint8_t val;
	if (!bitbuf_read_bit(bitbuf, &val))
		assert(false);
	assert(val == exp);
}

static void check_int(struct bitbuf* bitbuf, uint32_t exp)
{
	uint32_t val;
	if (!bitbuf_read_int_packed(bitbuf, &val))
		assert(false);
	assert(val == exp);
}

static int ParseBunch(struct rudp_fd* fd, struct bitbuf* bitbuf, bool* bSkipAck)
{
	struct rudp_bunch rudp_bunch;
	if (!rudp_bunch_read(&rudp_bunch, bitbuf, fd))
		return -1;
	return 0;
}

// UNetConnection::ReceivedPacket
int ReceivedPacket(struct rudp_fd* fd, struct bitbuf* bitbuf)
{
	struct notification_header notification_header;
	int ret = packet_notify_ReadHeader(fd, bitbuf, &notification_header);
	if (ret)
	{
		return ret;
	}
	uint8_t bHasPacketInfoPayload = true;
	if (!bitbuf_read_bit(bitbuf, &bHasPacketInfoPayload)) // true
		return -4;
	uint32_t PacketJitterClockTimeMS = 0;
	if (!bitbuf_read_int(bitbuf, &PacketJitterClockTimeMS, 1 << NumBitsForJitterClockTimeInHeader))
	{
		return -2;
	} // 1023
	assert(PacketJitterClockTimeMS == 1023);

	// 1
	int32_t PacketSequenceDelta = GetSequenceDelta(&fd->packet_notify, &notification_header);
	if (PacketSequenceDelta <= 0)
	{
		// Protect against replay attacks
		// We already protect against this for reliable bunches, and unreliable properties
		// The only bunch we would process would be unreliable RPC's, which could allow for replay attacks
		// So rather than add individual protection for unreliable RPC's as well, just kill it at the source,
		// which protects everything in one fell swoop
		return -8;
	}

	const bool bFlushingPacketOrderCache = false;
	if (bFlushingPacketOrderCache)
	{
		// 按照我们的设计, PacketOrderCache 以及 FlushPacketOrderCache 功能由外部实现
		const int32_t MissingPacketCount = PacketSequenceDelta - 1;
		if (MissingPacketCount > 0)
		{
			return 0;
		}
	}

	fd->InPacketId += PacketSequenceDelta;
	// Update incoming sequence data and deliver packet notifications
	// Packet is only accepted if both the incoming sequence number and incoming ack data are valid
	packet_notify_Update(fd, &fd->packet_notify, &notification_header);

	// uint8_t bHasServerFrameTime;
	check_bit(bitbuf, 0);

	bool bSkipAck = false;
	while (bitbuf->num < bitbuf->size)
	{
		bool bLocalSkipAck = false;
		ParseBunch(fd, bitbuf, &bLocalSkipAck);
		if (bLocalSkipAck)
		{
			bSkipAck = true;
		}
	}

	if (bSkipAck)
	{
		packet_notify_AckSeq(&fd->packet_notify, fd->InPacketId, false);
	}
	else
	{
		packet_notify_AckSeq(&fd->packet_notify, fd->InPacketId, true);
	}
	return 0;
}

// UNetConnection::GetFreeSendBufferBits
int64_t GetFreeSendBufferBits(struct rudp_fd* fd)
{
	// If we haven't sent anything yet, make sure to account for the packet header + trailer size
	// Otherwise, we only need to account for trailer size
	const int32_t ExtraBits = (fd->SendBufferBitsNum > 0) ? MAX_PACKET_TRAILER_BITS : MAX_PACKET_HEADER_BITS + MAX_PACKET_TRAILER_BITS;

	const int32_t NumberOfFreeBits = MaxPacket * 8 - (int32_t)(fd->SendBufferBitsNum + ExtraBits);

	assert(NumberOfFreeBits >= 0);

	return NumberOfFreeBits;
}

// UNetConnection::WritePacketHeader
void WritePacketHeader(struct rudp_fd* fd, struct bitbuf* bitbuf)
{
	// If this is a header refresh, we only serialize the updated serial number information
	size_t restore_num = bitbuf->num;
	const bool bIsHeaderUpdate = restore_num > 0u;

	// Header is always written first in the packet
	bitbuf->num = 0;

	// UNetConnection::LowLevelSend-->PacketHandler::Outgoing_Internal-->StatelessConnectHandlerComponent::Outgoing
	// 按照语义进行了修改, 减少内存拷贝
	Outgoing(bitbuf);

	// Write notification header or refresh the header if used space is the same.
	bool bWroteHeader = packet_notify_WriteHeader(&fd->packet_notify, bitbuf, bIsHeaderUpdate);

	// Jump back to where we came from.
	if (bIsHeaderUpdate)
	{
		bitbuf->num = restore_num;

		// if we wrote the header and successfully refreshed the header status we no longer has any dirty acks
		if (bWroteHeader)
		{
			fd->HasDirtyAcks = 0u;
		}
	}
}

// UNetConnection::WriteDummyPacketInfo
void WriteDummyPacketInfo(struct rudp_fd* fd, struct bitbuf* bitbuf)
{
	// The first packet of a frame will include the packet info payload
	const uint8_t bHasPacketInfoPayload = 0;
	bitbuf_write_bit(bitbuf, bHasPacketInfoPayload);
}

// void UNetConnection::PrepareWriteBitsToSendBuffer
void PrepareWriteBitsToSendBuffer(struct rudp_fd* fd, const int32_t SizeInBits, const int32_t ExtraSizeInBits)
{
	const int32_t TotalSizeInBits = SizeInBits + ExtraSizeInBits;

	// Flush if we can't add to current buffer
	if (TotalSizeInBits > GetFreeSendBufferBits(fd))
	{
		rudp_flush(fd);
	}

	// If this is the start of the queue, make sure to add the packet id
	if (fd->SendBufferBitsNum == 0)
	{
		struct bitbuf bitbuf;
		bitbuf_write_reuse(&bitbuf, fd->SendBuffer, fd->SendBufferBitsNum, sizeof(fd->SendBuffer));

		// Write Packet Header, before sending the packet we will go back and rewrite the data
		WritePacketHeader(fd, &bitbuf);

		// Pre-write the bits for the packet info
		WriteDummyPacketInfo(fd, &bitbuf);

		// We do not allow the first bunch to merge with the ack data as this will "revert" the ack data.
		fd->AllowMerge = false;

		// Update stats for PacketIdBits and ackdata (also including the data used for packet RTT and saturation calculations)
		// ...

		fd->SendBufferBitsNum = bitbuf.num;
	}
}

// UNetConnection::WriteBitsToSendBufferInternal
int32_t WriteBitsToSendBufferInternal(struct rudp_fd* fd, const uint8_t* Bits, const int32_t SizeInBits, const uint8_t* ExtraBits,
									  const int32_t ExtraSizeInBits)
{
	struct bitbuf bitbuf;
	bitbuf_write_reuse(&bitbuf, fd->SendBuffer, fd->SendBufferBitsNum, sizeof(fd->SendBuffer));

	if (Bits)
	{
		if (!bitbuf_write_bits(&bitbuf, Bits, SizeInBits))
			return -1;
	}

	if (ExtraBits)
	{
		if (!bitbuf_write_bits(&bitbuf, ExtraBits, ExtraSizeInBits))
			return -2;
	}

	fd->SendBufferBitsNum = bitbuf.num;
	const int32_t RememberedPacketId = fd->OutPacketId;

	// Flush now if we are full
	if (GetFreeSendBufferBits(fd) == 0)
	{
		rudp_flush(fd);
	}

	return RememberedPacketId;
}

// UNetConnection::WriteFinalPacketInfo
void WriteFinalPacketInfo(struct rudp_fd* fd, struct bitbuf* bitbuf)
{
}

// UNetConnection::WriteBitsToSendBuffer
int WriteBitsToSendBuffer(struct rudp_fd* fd, char* buffer, int bits_len)
{
	PrepareWriteBitsToSendBuffer(fd, 0, bits_len);
	return WriteBitsToSendBufferInternal(fd, NULL, 0, buffer, bits_len);
}

void ReceivedAck(struct rudp_fd* fd, int32_t AckPacketId)
{
}

void ReceivedNak(struct rudp_fd* fd, int32_t AckPacketId)
{
}
