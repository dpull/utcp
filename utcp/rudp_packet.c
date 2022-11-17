#include "rudp_packet.h"
#include "bit_buffer.h"
#include "rudp.h"
#include "rudp_bunch.h"
#include "rudp_bunch_data.h"
#include "rudp_config.h"
#include "rudp_handshake.h"
#include "rudp_packet_notify.h"
#include <assert.h>

enum
{
	NumBitsForJitterClockTimeInHeader = 10,

	MAX_PACKET_TRAILER_BITS = 1,
	MAX_PACKET_RELIABLE_SEQUENCE_HEADER_BITS = 32 /*PackedHeader*/ + MaxSequenceHistoryLength,
	MAX_PACKET_INFO_HEADER_BITS = 1 /*bHasPacketInfo*/ + NumBitsForJitterClockTimeInHeader + 1 /*bHasServerFrameTime*/ + 8 /*ServerFrameTime*/,
	MAX_PACKET_HEADER_BITS = MAX_PACKET_RELIABLE_SEQUENCE_HEADER_BITS + MAX_PACKET_INFO_HEADER_BITS,
	MAX_BUNCH_HEADER_BITS = 256,
	MaxPacketHandlerBits = 2,
	MAX_SINGLE_BUNCH_SIZE_BITS = (MaxPacket * 8) - MAX_BUNCH_HEADER_BITS - MAX_PACKET_TRAILER_BITS - MAX_PACKET_HEADER_BITS - MaxPacketHandlerBits,
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

static inline int32_t BestSignedDifference(int32_t Value, int32_t Reference, int32_t Max)
{
	return ((Value - Reference + Max / 2) & (Max - 1)) - Max / 2;
}

static inline int32_t MakeRelative(int32_t Value, int32_t Reference, int32_t Max)
{
	return Reference + BestSignedDifference(Value, Reference, Max);
}

// UChannel::ReceivedNextBunch
// 原本ReceivedNextBunch返回值是bDeleted, 为了简化, 该函数做释放或者引用
static bool ReceivedNextBunch(struct rudp_fd* fd, struct rudp_bunch_node* rudp_bunch_node, bool* bOutSkipAck)
{
	// We received the next bunch. Basically at this point:
	//	-We know this is in order if reliable
	//	-We dont know if this is partial or not
	// If its not a partial bunch, of it completes a partial bunch, we can call ReceivedSequencedBunch to actually handle it

	// Note this bunch's retirement.

	struct rudp_bunch* rudp_bunch = &rudp_bunch_node->rudp_bunch;
	if (rudp_bunch->bReliable)
	{
		// Reliables should be ordered properly at this point
		assert(rudp_bunch->ChSequence == fd->InReliable[rudp_bunch->ChIndex] + 1);
		fd->InReliable[rudp_bunch->ChIndex] = rudp_bunch->ChSequence;
	}

	struct rudp_bunch* HandleBunch[MaxSequenceHistoryLength];
	HandleBunch[0] = rudp_bunch;
	int HandleBunchCount = 1;
	bool bPartial = rudp_bunch->bPartial;
	if (bPartial)
	{
		int ret = merge_partial_data(&fd->rudp_bunch_data, rudp_bunch_node, bOutSkipAck);
		if (ret == 0)
		{
			return true;
		}
		else if (ret == 1)
		{
			HandleBunchCount = get_partial_bunch(&fd->rudp_bunch_data, HandleBunch, HandleBunchCount);
		}
		else
		{
			assert(ret == -1);
			free_rudp_bunch_node(&fd->rudp_bunch_data, rudp_bunch_node);
			return false;
		}
	}

	rudp_recv(fd, HandleBunch, HandleBunchCount);
	if (bPartial)
	{
		assert(HandleBunchCount > 1);
		clear_partial_data(&fd->rudp_bunch_data);
	}
	else
	{
		free_rudp_bunch_node(&fd->rudp_bunch_data, rudp_bunch_node);
	}
	return true;
}

static int ReceivedRawBunch(struct rudp_fd* fd, struct bitbuf* bitbuf, bool* bOutSkipAck)
{
	struct rudp_bunch_node* rudp_bunch_node = alloc_rudp_bunch_node(&fd->rudp_bunch_data);
	if (!rudp_bunch_node)
		return -1;

	int ret = 0;
	uint16_t ChIndex = 0;
	do
	{
		struct rudp_bunch* rudp_bunch = &rudp_bunch_node->rudp_bunch;
		ChIndex = rudp_bunch->ChIndex;

		if (!rudp_bunch_read(rudp_bunch, bitbuf))
		{
			ret = -2;
			break;
		}

		if (rudp_bunch->bReliable)
		{
			rudp_bunch->ChSequence = MakeRelative(rudp_bunch->ChSequence, fd->InReliable[ChIndex], MAX_CHSEQUENCE);
		}
		else if (rudp_bunch->bPartial)
		{
			// If this is an unreliable partial bunch, we simply use packet sequence since we already have it
			rudp_bunch->ChSequence = fd->InPacketId;
		}

		// Ignore if reliable packet has already been processed.
		if (rudp_bunch->bReliable && rudp_bunch->ChSequence <= fd->InReliable[rudp_bunch->ChIndex])
		{
			rudp_log(Log, "ReceivedRawBunch: Received outdated bunch (Channel %d Current Sequence %i)", rudp_bunch->ChIndex, fd->InReliable[rudp_bunch->ChIndex]);
			continue;
		}

		if (rudp_bunch->bReliable && rudp_bunch->ChSequence != fd->InReliable[rudp_bunch->ChIndex] + 1)
		{
			// If this bunch has a dependency on a previous unreceived bunch, buffer it.
			assert(!rudp_bunch->bOpen);
			
			// Verify that UConnection::ReceivedPacket has passed us a valid bunch.
			assert(rudp_bunch->ChSequence > fd->InReliable[rudp_bunch->ChIndex]);

			if (enqueue_incoming_data(&fd->rudp_bunch_data, rudp_bunch_node))
				rudp_bunch_node = NULL;
			break;
		}

		assert(rudp_bunch_node->dl_list_node.prev == NULL);
		assert(rudp_bunch_node->dl_list_node.next == NULL);
		ReceivedNextBunch(fd, rudp_bunch_node, bOutSkipAck);
		assert(rudp_bunch_node->dl_list_node.prev != NULL);
		assert(rudp_bunch_node->dl_list_node.next != NULL);
		rudp_bunch_node = NULL;
	} while (false);

	if (rudp_bunch_node)
	{
		assert(rudp_bunch_node->dl_list_node.prev == NULL);
		assert(rudp_bunch_node->dl_list_node.next == NULL);
		free_rudp_bunch_node(&fd->rudp_bunch_data, rudp_bunch_node);
		rudp_bunch_node = NULL;
	}

	while (true)
	{
		rudp_bunch_node = dequeue_incoming_data(&fd->rudp_bunch_data, fd->InReliable[ChIndex] + 1);
		if (!rudp_bunch_node)
			break;
		// Just keep a local copy of the bSkipAck flag, since these have already been acked and it doesn't make sense on this context
		// Definitely want to warn when this happens, since it's really not possible
		bool bLocalSkipAck = false;
		assert(rudp_bunch_node->dl_list_node.prev == NULL);
		assert(rudp_bunch_node->dl_list_node.next == NULL);
		ReceivedNextBunch(fd, rudp_bunch_node, &bLocalSkipAck);
		assert(rudp_bunch_node->dl_list_node.prev != NULL);
		assert(rudp_bunch_node->dl_list_node.next != NULL);
		rudp_bunch_node = NULL;
	}
	return ret;
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
	if (!bitbuf_read_bit(bitbuf, &bHasPacketInfoPayload)) 
		return -4;
	uint32_t PacketJitterClockTimeMS = 0;
	if (!bitbuf_read_int(bitbuf, &PacketJitterClockTimeMS, 1 << NumBitsForJitterClockTimeInHeader))
	{
		return -2;
	}

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

	const bool bPacketOrderCacheActive = false;
	if (bPacketOrderCacheActive)
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
		ReceivedRawBunch(fd, bitbuf, &bLocalSkipAck);
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

int PeekPacketId(struct rudp_fd* fd, struct bitbuf* bitbuf)
{
	struct notification_header notification_header;
	int ret = packet_notify_ReadHeader(fd, bitbuf, &notification_header);
	if (ret)
	{
		return ret;
	}
	int32_t PacketSequenceDelta = GetSequenceDelta(&fd->packet_notify, &notification_header);
	if (PacketSequenceDelta <= 0)
	{
		return -8;
	}
	return fd->InPacketId + PacketSequenceDelta;
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
	// Write Jitter clock time
	// 暂时不移植这个功能了
}

bool check_can_send(struct rudp_fd* fd, struct rudp_bunch* bunches[], int bunches_count)
{
	for (int i = 0; i < bunches_count; ++i)
	{
		if (!bunches[i]->DataBitsLen > MAX_SINGLE_BUNCH_SIZE_BITS)
			return false;

		if (bunches_count == 1)
		{
			if (bunches[i]->bPartial)
				return false;
		}
		else
		{
			if (!bunches[i]->bPartial)
				return false;

			if (i == 0)
			{
				if (!bunches[i]->bPartialInitial)
					return false;
			}
			else if (i == bunches_count - 1)
			{
				if (!bunches[i]->bPartialFinal)
					return false;
			}
			else
			{
				if (bunches[i]->bPartialInitial || bunches[i]->bPartialFinal)
					return false;
			}
		}
	}

	const bool bOverflowsReliable = (fd->rudp_bunch_data.NumOutRec + bunches_count >= RELIABLE_BUFFER);
	if (bOverflowsReliable)
		return false;
	return true;
}

// UNetConnection::SendRawBunch
int32_t SendRawBunch(struct rudp_fd* fd, struct rudp_bunch* bunch)
{
	//  UChannel::PrepBunch
	bunch->ChSequence = 0; 
	if (bunch->bReliable)
		bunch->ChSequence = ++fd->OutReliable[bunch->ChIndex];

	uint8_t buffer[MaxPacket];
	struct bitbuf bitbuf;
	if (!bitbuf_write_init(&bitbuf, buffer, sizeof(buffer)))
	{
		assert(false);
		return -1;
	}

	if (!rudp_bunch_write_header(bunch, &bitbuf))
	{
		assert(false);
		return -1;
	}

	// If the bunch does not fit in the current packet,
	// flush packet now so that we can report collected stats in the correct scope
	PrepareWriteBitsToSendBuffer(fd, (int32_t)bitbuf.num, bunch->DataBitsLen);

	// Write the bits to the buffer and remember the packet id used
	int32_t PacketId = WriteBitsToSendBufferInternal(fd, buffer, (int32_t)bitbuf.num, bunch->Data, bunch->DataBitsLen);
	if (PacketId < 0)
	{
		assert(false);
		return -1;
	}

	if (bunch->bReliable)
	{
		struct rudp_bunch_node* rudp_bunch_node = alloc_rudp_bunch_node(&fd->rudp_bunch_data);
		struct bitbuf bitbuf_all;
		bitbuf_write_init(&bitbuf_all, rudp_bunch_node->bunch_data, sizeof(rudp_bunch_node->bunch_data));
		bitbuf_write_bits(&bitbuf_all, buffer, bitbuf.num);
		bitbuf_write_bits(&bitbuf_all, bunch->Data, bunch->DataBitsLen);

		rudp_bunch_node->packet_id = PacketId;
		rudp_bunch_node->bunch_data_len = (uint16_t)bitbuf_all.num;
		add_outcoming_data(&fd->rudp_bunch_data, rudp_bunch_node);
	}

	return PacketId;
}

// UNetConnection::WriteBitsToSendBuffer
int WriteBitsToSendBuffer(struct rudp_fd* fd, char* buffer, int bits_len)
{
	PrepareWriteBitsToSendBuffer(fd, 0, bits_len);
	return WriteBitsToSendBufferInternal(fd, NULL, 0, buffer, bits_len);
}
