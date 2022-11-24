#include "utcp_packet.h"
#include "bit_buffer.h"
#include "utcp.h"
#include "utcp_bunch.h"
#include "utcp_channel.h"
#include "utcp_handshake.h"
#include "utcp_packet_notify.h"
#include "utcp_utils.h"
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
	MAX_SINGLE_BUNCH_SIZE_BITS = (UTCP_MAX_PACKET * 8) - MAX_BUNCH_HEADER_BITS - MAX_PACKET_TRAILER_BITS - MAX_PACKET_HEADER_BITS - MaxPacketHandlerBits,
};

static inline int32_t BestSignedDifference(int32_t Value, int32_t Reference, int32_t Max)
{
	return ((Value - Reference + Max / 2) & (Max - 1)) - Max / 2;
}

static inline int32_t MakeRelative(int32_t Value, int32_t Reference, int32_t Max)
{
	return Reference + BestSignedDifference(Value, Reference, Max);
}

static struct utcp_channel* utcp_get_channel(struct utcp_connection* fd, struct utcp_bunch* utcp_bunch)
{
	struct utcp_channel* utcp_channel = fd->Channels[utcp_bunch->ChIndex];
	if (!utcp_channel)
	{
		if (utcp_bunch->bOpen)
		{
			utcp_channel = alloc_utcp_channel(fd->InitInReliable, fd->InitOutReliable);
			fd->Channels[utcp_bunch->ChIndex] = utcp_channel;
			open_channel_add(&fd->open_channels, utcp_bunch->ChIndex);
		}
		else
		{
			assert(false);
			utcp_log(Warning, "utcp_get_channel failed");
		}
	}
	if (utcp_bunch->bClose && utcp_channel)
	{
		mark_channel_close(utcp_channel, utcp_bunch->CloseReason);
	}
	return utcp_channel;
}

static void utcp_close_channel(struct utcp_connection* fd, int ChIndex)
{
	if (!fd->Channels[ChIndex])
		return;
	free_utcp_channel(fd->Channels[ChIndex]);
	fd->Channels[ChIndex] = NULL;
	open_channel_remove(&fd->open_channels, ChIndex);
}

void utcp_closeall_channel(struct utcp_connection* fd)
{
	for (int i = 0; i < _countof(fd->Channels); ++i)
	{
		if (fd->Channels[i])
		{
			utcp_close_channel(fd, i);
		}
	}
	assert(fd->open_channels.num == 0);
}

// UChannel::ReceivedNextBunch
// 原本ReceivedNextBunch返回值是bDeleted, 为了简化, 该函数做释放或者引用
static bool ReceivedNextBunch(struct utcp_connection* fd, struct utcp_bunch_node* utcp_bunch_node, bool* bOutSkipAck)
{
	// We received the next bunch. Basically at this point:
	//	-We know this is in order if reliable
	//	-We dont know if this is partial or not
	// If its not a partial bunch, of it completes a partial bunch, we can call ReceivedSequencedBunch to actually handle it

	// Note this bunch's retirement.

	struct utcp_bunch* utcp_bunch = &utcp_bunch_node->utcp_bunch;
	struct utcp_channel* utcp_channel = utcp_get_channel(fd, utcp_bunch);
	assert(utcp_channel);

	if (utcp_bunch->bReliable)
	{
		// Reliables should be ordered properly at this point
		assert(utcp_bunch->ChSequence == utcp_channel->InReliable + 1);
		utcp_channel->InReliable = utcp_bunch->ChSequence;
	}

	struct utcp_bunch* HandleBunch[MaxSequenceHistoryLength];
	HandleBunch[0] = utcp_bunch;
	int HandleBunchCount = 1;
	bool bPartial = utcp_bunch->bPartial;
	if (bPartial)
	{
		enum merge_partial_result ret = merge_partial_data(utcp_channel, utcp_bunch_node, bOutSkipAck);
		if (ret == partial_merge_succeed)
		{
			return true;
		}
		else if (ret == partial_available)
		{
			HandleBunchCount = get_partial_bunch(utcp_channel, HandleBunch, _countof(HandleBunch));
			assert(HandleBunchCount > 0);
		}
		else
		{
			free_utcp_bunch_node(utcp_bunch_node);
			if (ret == partial_merge_fatal)
			{
				// TODO close CONN
			}
			return false;
		}
	}

	for (int i = 0; i < HandleBunchCount; ++i)
	{
		struct utcp_bunch* cur_utcp_bunch = HandleBunch[i];
		utcp_log(Verbose, "recv bunch, bOpen=%d, bClose=%d, NameIndex=%d, ChIndex=%d, NumBits=%d", cur_utcp_bunch->bOpen, cur_utcp_bunch->bClose, cur_utcp_bunch->NameIndex,
				 cur_utcp_bunch->ChIndex, cur_utcp_bunch->DataBitsLen);
	}

	utcp_recv_bunch(fd, HandleBunch, HandleBunchCount);
	if (bPartial)
	{
		assert(HandleBunchCount > 1);
		clear_partial_data(utcp_channel);
	}
	else
	{
		free_utcp_bunch_node(utcp_bunch_node);
	}
	return true;
}

// Dispatch any waiting bunches.
static void DispatchWaitingBunches(struct utcp_connection* fd, struct utcp_channel* utcp_channel)
{
	for (;;)
	{
		assert(utcp_channel);
		struct utcp_bunch_node* utcp_bunch_node = dequeue_incoming_data(utcp_channel, utcp_channel->InReliable + 1);
		if (!utcp_bunch_node)
			break;

		// Just keep a local copy of the bSkipAck flag, since these have already been acked and it doesn't make sense on this context
		// Definitely want to warn when this happens, since it's really not possible
		bool bLocalSkipAck = false;
		assert(utcp_bunch_node->dl_list_node.prev == NULL);
		assert(utcp_bunch_node->dl_list_node.next == NULL);
		ReceivedNextBunch(fd, utcp_bunch_node, &bLocalSkipAck);
	}
}

static void ReceivedRawBunch(struct utcp_connection* fd, struct bitbuf* bitbuf, bool* bOutSkipAck)
{
	struct utcp_bunch_node* utcp_bunch_node = NULL;
	struct utcp_channel* utcp_channel = NULL;

	do
	{
		utcp_bunch_node = alloc_utcp_bunch_node(fd);
		if (!utcp_bunch_node)
		{
			break;
		}

		struct utcp_bunch* utcp_bunch = &utcp_bunch_node->utcp_bunch;
		if (!utcp_bunch_read(utcp_bunch, bitbuf))
		{
			break;
		}

		utcp_channel = utcp_get_channel(fd, utcp_bunch);
		if (!utcp_channel)
		{
			break;
		}

		if (utcp_bunch->bReliable)
		{
			utcp_bunch->ChSequence = MakeRelative(utcp_bunch->ChSequence, utcp_channel->InReliable, UTCP_MAX_CHSEQUENCE);
		}
		else if (utcp_bunch->bPartial)
		{
			// If this is an unreliable partial bunch, we simply use packet sequence since we already have it
			utcp_bunch->ChSequence = fd->InPacketId;
		}

		// Ignore if reliable packet has already been processed.
		if (utcp_bunch->bReliable && utcp_bunch->ChSequence <= utcp_channel->InReliable)
		{
			utcp_log(Log, "ReceivedRawBunch: Received outdated bunch (Channel %d Current Sequence %i)", utcp_bunch->ChIndex, utcp_channel->InReliable);
			break;
		}

		if (utcp_bunch->bReliable && utcp_bunch->ChSequence != utcp_channel->InReliable + 1)
		{
			// If this bunch has a dependency on a previous unreceived bunch, buffer it.
			assert(!utcp_bunch->bOpen);

			// Verify that UConnection::ReceivedPacket has passed us a valid bunch.
			assert(utcp_bunch->ChSequence > utcp_channel->InReliable);

			if (enqueue_incoming_data(utcp_channel, utcp_bunch_node))
				utcp_bunch_node = NULL;
			break;
		}

		assert(utcp_bunch_node->dl_list_node.prev == NULL);
		assert(utcp_bunch_node->dl_list_node.next == NULL);
		ReceivedNextBunch(fd, utcp_bunch_node, bOutSkipAck);
		utcp_bunch_node = NULL;
	} while (false);

	if (utcp_bunch_node)
	{
		assert(utcp_bunch_node->dl_list_node.prev == NULL);
		assert(utcp_bunch_node->dl_list_node.next == NULL);
		free_utcp_bunch_node(utcp_bunch_node);
		utcp_bunch_node = NULL;
	}

	if (utcp_channel)
	{
		DispatchWaitingBunches(fd, utcp_channel);
	}
}

// UNetConnection::ReceivedPacket
int ReceivedPacket(struct utcp_connection* fd, struct bitbuf* bitbuf)
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

	if (bHasPacketInfoPayload)
	{
		uint32_t PacketJitterClockTimeMS = 0;
		if (!bitbuf_read_int(bitbuf, &PacketJitterClockTimeMS, 1 << NumBitsForJitterClockTimeInHeader))
		{
			return -5;
		}

		// UNetConnection::ReadPacketInfo
		uint8_t bHasServerFrameTime;
		if (!bitbuf_read_bit(bitbuf, &bHasServerFrameTime))
		{
			return -6;
		}

		if (bHasServerFrameTime)
		{
			uint8_t FrameTimeByte = 0;
			bitbuf_read_bytes(bitbuf, &FrameTimeByte, 1);
		}
	}

	int32_t PacketSequenceDelta = GetSequenceDelta(&fd->packet_notify, &notification_header);
	if (PacketSequenceDelta <= 0)
	{
		// Protect against replay attacks
		// We already protect against this for reliable bunches, and unreliable properties
		// The only bunch we would process would be unreliable RPC's, which could allow for replay attacks
		// So rather than add individual protection for unreliable RPC's as well, just kill it at the source,
		// which protects everything in one fell swoop
		utcp_log(Verbose, "'out of order' packet sequences: PacketSeq=%d, NotifyPacketSeq=%d", notification_header.Seq, fd->packet_notify.InSeq);
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

int PeekPacketId(struct utcp_connection* fd, struct bitbuf* bitbuf)
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
int64_t GetFreeSendBufferBits(struct utcp_connection* fd)
{
	// If we haven't sent anything yet, make sure to account for the packet header + trailer size
	// Otherwise, we only need to account for trailer size
	const int32_t ExtraBits = (fd->SendBufferBitsNum > 0) ? MAX_PACKET_TRAILER_BITS : MAX_PACKET_HEADER_BITS + MAX_PACKET_TRAILER_BITS;

	const int32_t NumberOfFreeBits = UTCP_MAX_PACKET * 8 - (int32_t)(fd->SendBufferBitsNum + ExtraBits);

	assert(NumberOfFreeBits >= 0);

	return NumberOfFreeBits;
}

// UNetConnection::WritePacketHeader
void WritePacketHeader(struct utcp_connection* fd, struct bitbuf* bitbuf)
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
void WriteDummyPacketInfo(struct utcp_connection* fd, struct bitbuf* bitbuf)
{
	// The first packet of a frame will include the packet info payload
	const uint8_t bHasPacketInfoPayload = 0;
	bitbuf_write_bit(bitbuf, bHasPacketInfoPayload);
}

// void UNetConnection::PrepareWriteBitsToSendBuffer
void PrepareWriteBitsToSendBuffer(struct utcp_connection* fd, const int32_t SizeInBits, const int32_t ExtraSizeInBits)
{
	const int32_t TotalSizeInBits = SizeInBits + ExtraSizeInBits;

	// Flush if we can't add to current buffer
	if (TotalSizeInBits > GetFreeSendBufferBits(fd))
	{
		utcp_send_flush(fd);
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

		// Update stats for PacketIdBits and ackdata (also including the data used for packet RTT and saturation calculations)
		// ...

		fd->SendBufferBitsNum = bitbuf.num;
	}
}

// UNetConnection::WriteBitsToSendBufferInternal
int32_t WriteBitsToSendBufferInternal(struct utcp_connection* fd, const uint8_t* Bits, const int32_t SizeInBits, const uint8_t* ExtraBits, const int32_t ExtraSizeInBits)
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
		utcp_send_flush(fd);
	}

	return RememberedPacketId;
}

// UNetConnection::WriteFinalPacketInfo
void WriteFinalPacketInfo(struct utcp_connection* fd, struct bitbuf* bitbuf)
{
	// Write Jitter clock time
	// 暂时不移植这个功能了
}

// UNetConnection::SendRawBunch
int32_t SendRawBunch(struct utcp_connection* fd, struct utcp_bunch* bunch)
{
	struct utcp_channel* utcp_channel = utcp_get_channel(fd, bunch);
	if (!utcp_channel)
	{
		return -2;
	}

	//  UChannel::PrepBunch
	bunch->ChSequence = 0;
	if (bunch->bReliable)
	{
		bunch->ChSequence = ++utcp_channel->OutReliable;
	}

	uint8_t buffer[UTCP_MAX_PACKET];
	struct bitbuf bitbuf;
	if (!bitbuf_write_init(&bitbuf, buffer, sizeof(buffer)))
	{
		assert(false);
		return -1;
	}

	if (!utcp_bunch_write_header(bunch, &bitbuf))
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
		struct utcp_bunch_node* utcp_bunch_node = alloc_utcp_bunch_node(fd);
		struct bitbuf bitbuf_all;
		bitbuf_write_init(&bitbuf_all, utcp_bunch_node->bunch_data, sizeof(utcp_bunch_node->bunch_data));
		bitbuf_write_bits(&bitbuf_all, buffer, bitbuf.num);
		bitbuf_write_bits(&bitbuf_all, bunch->Data, bunch->DataBitsLen);

		utcp_bunch_node->packet_id = PacketId;
		utcp_bunch_node->bunch_data_len = (uint16_t)bitbuf_all.num;
		add_ougoing_data(utcp_channel, utcp_bunch_node);
	}

	return PacketId;
}

// UNetConnection::WriteBitsToSendBuffer
int WriteBitsToSendBuffer(struct utcp_connection* fd, char* buffer, int bits_len)
{
	PrepareWriteBitsToSendBuffer(fd, 0, bits_len);
	return WriteBitsToSendBufferInternal(fd, NULL, 0, (uint8_t*)buffer, bits_len);
}
