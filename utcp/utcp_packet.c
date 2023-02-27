#include "utcp_packet.h"
#include "bit_buffer.h"
#include "utcp.h"
#include "utcp_bunch.h"
#include "utcp_channel.h"
#include "utcp_packet_notify.h"
#include "utcp_utils.h"
#include <assert.h>

enum
{
	MAX_PACKET_TRAILER_BITS = 1,
	MAX_PACKET_HEADER_BITS = 15, // = FMath::CeilLogTwo(MAX_PACKETID) + 1 (IsAck)
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
	if (utcp_bunch->bClose && utcp_bunch->ChIndex == 0)
	{
		utcp_mark_close(fd, ControlChannelClose);
	}
	return utcp_channels_get_channel(&fd->channels, utcp_bunch);
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

	struct utcp_bunch* HandleBunch[UTCP_MAX_PARTIAL_BUNCH_COUNT];
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
		utcp_log(Verbose, "[%s]received bunch, bOpen=%d, bClose=%d, NameIndex=%d, ChIndex=%d, NumBits=%d",
				 fd->debug_name, cur_utcp_bunch->bOpen, cur_utcp_bunch->bClose, cur_utcp_bunch->ChType, cur_utcp_bunch->ChIndex, cur_utcp_bunch->DataBitsLen);
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
			utcp_log(Warning, "[%s]Bunch header overflowed", fd->debug_name);
			utcp_mark_close(fd, BunchOverflow);
			break;
		}

		if (utcp_bunch->ChIndex >= UTCP_MAX_CHANNELS)
		{
			utcp_log(Warning, "[%s]Bunch channel index exceeds channel limit", fd->debug_name);
			utcp_mark_close(fd, BunchBadChannelIndex);
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
			utcp_log(Verbose, "ReceivedRawBunch: Received outdated bunch (Channel %d Current Sequence %i)", utcp_bunch->ChIndex, utcp_channel->InReliable);
			break;
		}

		if (utcp_bunch->bReliable && utcp_bunch->ChSequence != utcp_channel->InReliable + 1)
		{
			// If this bunch has a dependency on a previous unreceived bunch, buffer it.
			// assert(!utcp_bunch->bOpen);

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

//  UNetConnection::ReceivedAck
static void ReceivedAck(struct utcp_connection* fd, int32_t AckPacketId)
{
	// Advance OutAckPacketId
	fd->OutAckPacketId = AckPacketId;

	utcp_channels_on_ack(&fd->channels, AckPacketId);
	utcp_delivery_status(fd, AckPacketId, true);
}

// UNetConnection::ReceivedNak
static void ReceivedNak(struct utcp_connection* fd, int32_t NakPacketId)
{
	utcp_channels_on_nak(&fd->channels, NakPacketId, WriteBitsToSendBuffer, fd);
	utcp_delivery_status(fd, NakPacketId, false);
}

// UNetConnection::InitSequence
void utcp_sequence_init(struct utcp_connection* fd, int32_t IncomingSequence, int32_t OutgoingSequence)
{
	fd->InPacketId = IncomingSequence - 1;
	fd->OutPacketId = OutgoingSequence;
	fd->OutAckPacketId = OutgoingSequence - 1;
	fd->LastNotifiedPacketId = fd->OutAckPacketId;

	// Initialize the reliable packet sequence (more useful/effective at preventing attacks)
	fd->channels.InitInReliable = IncomingSequence & (UTCP_MAX_CHSEQUENCE - 1);
	fd->channels.InitOutReliable = OutgoingSequence & (UTCP_MAX_CHSEQUENCE - 1);

	utcp_log(Log, "[%s]init sequence:%d, %d", fd->debug_name, IncomingSequence, OutgoingSequence);
}

// UNetConnection::ReceivedPacket
static bool ReceivedAckPacket(struct utcp_connection* fd, struct bitbuf* bitbuf)
{
	struct ack_data ack_data;
	int ret = ack_data_read(bitbuf, &ack_data);
	if (ret != 0)
	{
		utcp_mark_close(fd, ret);
		return false;
	}

	ack_data.AckPacketId = MakeRelative(ack_data.AckPacketId, fd->OutAckPacketId, UTCP_MAX_PACKETID);
	if (ack_data.AckPacketId > fd->OutAckPacketId)
	{
		for (int32_t NakPacketId = fd->OutAckPacketId + 1; NakPacketId < ack_data.AckPacketId; NakPacketId++)
		{
			utcp_log(Verbose, "[%s]   Received virtual nak %i", fd->debug_name, NakPacketId);
			ReceivedNak(fd, NakPacketId);
		}
	}
	ReceivedAck(fd, ack_data.AckPacketId);
	return true;
}

static void PurgeAcks(struct utcp_connection* fd);

// UNetConnection::SendAck
static void SendAck(struct utcp_connection* fd, int32_t AckPacketId, bool FirstTime)
{
	if (FirstTime)
	{
		PurgeAcks(fd);
		assert(fd->QueuedAcksCount < _countof(fd->QueuedAcks));
		fd->QueuedAcks[fd->QueuedAcksCount] = AckPacketId;
		fd->QueuedAcksCount++;
	}

	utcp_log(Verbose, "[%s]send ack %i", fd->debug_name, AckPacketId);

	uint8_t AckData[32];
	struct bitbuf bitbuf;
	struct ack_data ack_data;

	// We still write the bit in shipping to keep the format the same
	memset(&ack_data, 0, sizeof(ack_data));

	ack_data.AckPacketId = AckPacketId;

	if (!bitbuf_write_init(&bitbuf, AckData, sizeof(AckData)))
	{
		assert(false);
		return;
	}

	if (!bitbuf_write_bit(&bitbuf, 1))
	{
		assert(false);
		return;
	}

	if (ack_data_write(&bitbuf, &ack_data) != 0)
	{
		assert(false);
		return;
	}

	WriteBitsToSendBuffer(fd, AckData, (int)bitbuf.num, NULL, 0);
}

// UNetConnection::PurgeAcks
static void PurgeAcks(struct utcp_connection* fd)
{
	for (int32_t i = 0; i < fd->ResendAcksCount; i++)
	{
		SendAck(fd, fd->ResendAcks[i], false);
	}

	fd->ResendAcksCount = 0;
}

// UNetConnection::ReceivedPacket
bool ReceivedPacket(struct utcp_connection* fd, struct bitbuf* bitbuf)
{
	int32_t PacketId;
	if (!bitbuf_read_int(bitbuf, (uint32_t*)&PacketId, UTCP_MAX_PACKETID))
	{
		utcp_mark_close(fd, ReadHeaderFail);
		return false;
	}
	PacketId = MakeRelative(PacketId, fd->InPacketId, UTCP_MAX_PACKETID);
	if (PacketId <= fd->InPacketId)
	{
		// Protect against replay attacks
		// We already protect against this for reliable bunches, and unreliable properties
		// The only bunch we would process would be unreliable RPC's, which could allow for replay attacks
		// So rather than add individual protection for unreliable RPC's as well, just kill it at the source,
		// which protects everything in one fell swoop
		return true;
	}

	const int32_t PacketsLost = PacketId - fd->InPacketId - 1;
	if (PacketsLost > 10)
	{
		utcp_log(Log, "[%s]High single frame packet loss. PacketsLost: %i", fd->debug_name, PacketsLost);
	}

	fd->InPacketId = PacketId;

	bool bSkipAck = false;
	while (bitbuf->num < bitbuf->size && !fd->bClose)
	{
		uint8_t IsAck;
		if (!bitbuf_read_bit(bitbuf, &IsAck))
		{
			utcp_mark_close(fd, ReadHeaderExtraFail);
			return false;
		}

		if (IsAck)
		{
			if (!ReceivedAckPacket(fd, bitbuf))
				return false;
			continue;
		}

		bool bLocalSkipAck = false;
		ReceivedRawBunch(fd, bitbuf, &bLocalSkipAck);
		if (bLocalSkipAck)
			bSkipAck = true;
	}

	if (!bSkipAck)
	{
		SendAck(fd, PacketId, true);
	}
	return true;
}

int32_t PeekPacketId(struct utcp_connection* fd, struct bitbuf* bitbuf)
{
	int32_t PacketId;
	if (!bitbuf_read_int(bitbuf, (uint32_t*)&PacketId, UTCP_MAX_PACKETID))
		return -1;

	PacketId = MakeRelative(PacketId, fd->InPacketId, UTCP_MAX_PACKETID);
	if (PacketId <= fd->InPacketId)
		return -2;
	return PacketId;
}

// UNetConnection::GetFreeSendBufferBits
static int64_t GetFreeSendBufferBits(struct utcp_connection* fd)
{
	// If we haven't sent anything yet, make sure to account for the packet header + trailer size
	// Otherwise, we only need to account for trailer size
	const int32_t ExtraBits = (fd->SendBufferBitsNum > 0) ? MAX_PACKET_TRAILER_BITS : MAX_PACKET_HEADER_BITS + MAX_PACKET_TRAILER_BITS;

	int NumberOfFreeBits = UTCP_MAX_PACKET * 8 - (int32_t)(fd->SendBufferBitsNum + ExtraBits);
	NumberOfFreeBits += 1; // bHandshakePacket(@WritePacketOutgoingHeader)

	assert(NumberOfFreeBits >= 0);

	return NumberOfFreeBits;
}

// StatelessConnectHandlerComponent::Outgoing
static int WritePacketOutgoingHeader(struct bitbuf* bitbuf)
{
	assert(bitbuf->num == 0);

	uint8_t bHandshakePacket = 0;
	bitbuf_write_bit(bitbuf, bHandshakePacket);
	return 0;
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
		/*
		if (utcp_channel->NumOutRec + 1 >= UTCP_RELIABLE_BUFFER)
		{
			utcp_log(Warning, "Outgoing reliable buffer overflow");
			utcp_mark_close(fd, ReliableBufferOverflow);
		}
		*/
		bunch->ChSequence = ++utcp_channel->OutReliable;
	}

	uint8_t buffer[UTCP_MAX_PACKET];
	struct bitbuf bitbuf;
	if (!bitbuf_write_init(&bitbuf, buffer, sizeof(buffer)))
	{
		assert(false);
		return -1;
	}

	if (!bitbuf_write_bit(&bitbuf, 0))
	{
		assert(false);
		return -1;
	}

	if (!utcp_bunch_write_header(bunch, &bitbuf))
	{
		assert(false);
		return -1;
	}

	// Write the bits to the buffer and remember the packet id used
	int32_t PacketId = WriteBitsToSendBuffer(fd, buffer, (int32_t)bitbuf.num, bunch->Data, bunch->DataBitsLen);
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
int WriteBitsToSendBuffer(struct utcp_connection* fd, const uint8_t* Bits, const int32_t SizeInBits, const uint8_t* ExtraBits, const int32_t ExtraSizeInBits)
{
	const int32_t TotalSizeInBits = SizeInBits + ExtraSizeInBits;

	// Flush if we can't add to current buffer
	if (TotalSizeInBits > GetFreeSendBufferBits(fd))
	{
		utcp_send_flush(fd);
	}

	struct bitbuf bitbuf;

	// Remember start position in case we want to undo this write
	// Store this after the possible flush above so we have the correct start position in the case that we do flush

	// If this is the start of the queue, make sure to add the packet id
	if (fd->SendBufferBitsNum == 0)
	{
		bitbuf_write_reuse(&bitbuf, fd->SendBuffer, 0, sizeof(fd->SendBuffer));
		if (WritePacketOutgoingHeader(&bitbuf) != 0)
		{
			assert(false);
			return -1;
		}
		if (!bitbuf_write_int(&bitbuf, fd->OutPacketId, UTCP_MAX_PACKETID))
		{
			assert(false);
			return -2;
		}
		fd->SendBufferBitsNum = bitbuf.num;
	}

	bitbuf_write_reuse(&bitbuf, fd->SendBuffer, fd->SendBufferBitsNum, sizeof(fd->SendBuffer));

	// Add the bits to the queue
	if (SizeInBits)
	{
		if (!bitbuf_write_bits(&bitbuf, Bits, SizeInBits))
		{
			assert(false);
			return -3;
		}
	}

	// Add any extra bits
	if (ExtraSizeInBits)
	{
		if (!bitbuf_write_bits(&bitbuf, ExtraBits, ExtraSizeInBits))
		{
			assert(false);
			return -4;
		}
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
