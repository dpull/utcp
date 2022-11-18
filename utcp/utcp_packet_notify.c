﻿#include "utcp_packet_notify.h"
#include "bit_buffer.h"
#include "utcp_bunch_data.h"
#include "utcp_def.h"
#include "utcp_packet.h"
#include "utcp_sequence_number.h"
#include "utcp_utils.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static inline size_t MIN(size_t a, size_t b)
{
	return a < b ? a : b;
}
static inline size_t ClAMP(const size_t X, const size_t Min, const size_t Max)
{
	return (X < Min) ? Min : (X < Max) ? X : Max;
}

enum
{
	HistoryWordCountBits = 4,
	HistoryWordCountMask = ((1 << HistoryWordCountBits) - 1),
	AckSeqShift = HistoryWordCountBits,
	SeqShift = AckSeqShift + SequenceNumberBits,
};

static uint32_t PackedHeader_Pack(uint16_t Seq, uint16_t AckedSeq, size_t HistoryWordCount)
{
	uint32_t Packed = 0u;

	Packed |= Seq << SeqShift;
	Packed |= AckedSeq << AckSeqShift;
	Packed |= HistoryWordCount & HistoryWordCountMask;

	return Packed;
}

static void PackedHeader_UnPack(uint32_t Packed, uint16_t* Seq, uint16_t* AckedSeq, size_t* HistoryWordCount)
{
	*Seq = (Packed >> SeqShift & SeqNumberMask);
	*AckedSeq = (Packed >> AckSeqShift & SeqNumberMask);
	*HistoryWordCount = (Packed & HistoryWordCountMask);
}

// FNetPacketNotify::UpdateInAckSeqAck
static uint16_t UpdateInAckSeqAck(struct packet_notify* packet_notify, int32_t AckCount, uint16_t AckedSeq)
{
	if (AckCount <= ring_buffer_num_items(&packet_notify->AckRecord))
	{
		union sent_ack_data AckData;
		while (AckCount > 0)
		{
			AckCount--;
			ring_buffer_dequeue(&packet_notify->AckRecord, &AckData.Value);
		}

		// verify that we have a matching sequence number
		if (AckData.OutSeq == AckedSeq)
		{
			return AckData.InAckSeq;
		}
	}

	// Pessimistic view, should never occur but we do want to know about it if it would
	// ensureMsgf(false, TEXT("FNetPacketNotify::UpdateInAckSeqAck - Failed to find matching AckRecord for %u"), AckedSeq.Get());
	return AckedSeq - MaxSequenceHistoryLength;
}

static void ReceivedAck(struct utcp_fd* fd, int32_t AckPacketId)
{
	struct utcp_bunch_node* utcp_bunch_node[RELIABLE_BUFFER];
	int count = remove_outcoming_data(&fd->utcp_bunch_data, AckPacketId, utcp_bunch_node, _countof(utcp_bunch_node));
	for (int i = 0; i < count; ++i)
	{
		free_utcp_bunch_node(&fd->utcp_bunch_data, utcp_bunch_node[i]);
	}
	utcp_delivery_status(fd, AckPacketId, true);
}

static void ReceivedNak(struct utcp_fd* fd, int32_t NakPacketId)
{
	struct utcp_bunch_node* utcp_bunch_node[RELIABLE_BUFFER];
	int count = remove_outcoming_data(&fd->utcp_bunch_data, NakPacketId, utcp_bunch_node, _countof(utcp_bunch_node));
	for (int i = 0; i < count; ++i)
	{
		int32_t packet_id = WriteBitsToSendBuffer(fd, utcp_bunch_node[i]->bunch_data, utcp_bunch_node[i]->bunch_data_len);
		utcp_bunch_node[i]->packet_id = packet_id;
		add_outcoming_data(&fd->utcp_bunch_data, utcp_bunch_node[i]);
	}
	utcp_delivery_status(fd, NakPacketId, false);
}

int32_t GetSequenceDelta(struct packet_notify* packet_notify, struct notification_header* notification_header)
{
	if (seq_num_greater_than(notification_header->Seq, packet_notify->InSeq) && seq_num_greater_equal(notification_header->AckedSeq, packet_notify->OutAckSeq) &&
		seq_num_greater_than(packet_notify->OutSeq, notification_header->AckedSeq))
	{
		return seq_num_diff(notification_header->Seq, packet_notify->InSeq);
	}
	else
	{
		return 0;
	}
}

// FNetPacketNotify::Init
void packet_notify_Init(struct packet_notify* packet_notify, uint16_t InitialInSeq, uint16_t InitialOutSeq)
{
	memset(packet_notify->InSeqHistory, 0, sizeof(packet_notify->InSeqHistory));
	packet_notify->InSeq = InitialInSeq;
	packet_notify->InAckSeq = InitialInSeq;
	packet_notify->InAckSeqAck = InitialInSeq;
	packet_notify->OutSeq = InitialOutSeq;
	packet_notify->OutAckSeq = seq_num_init(InitialOutSeq - 1);

	ring_buffer_init(&packet_notify->AckRecord);
}

// FNetPacketNotify::AckSeq
void packet_notify_AckSeq(struct packet_notify* packet_notify, uint16_t AckedSeq, bool IsAck)
{
	assert(AckedSeq == packet_notify->InSeq);

	while (seq_num_greater_than(AckedSeq, packet_notify->InAckSeq))
	{
		packet_notify->InAckSeq = seq_num_inc(packet_notify->InAckSeq, 1);

		const bool bReportAcked = packet_notify->InAckSeq == AckedSeq ? IsAck : false;

		// TSequenceHistory<HistorySize>::AddDeliveryStatus
		{
			SequenceHistoryWord Carry = bReportAcked ? 1u : 0u;
			const SequenceHistoryWord ValueMask = 1u << (SequenceHistoryBitsPerWord - 1);

			for (size_t CurrentWordIt = 0; CurrentWordIt < SequenceHistoryWordCount; ++CurrentWordIt)
			{
				const SequenceHistoryWord OldValue = Carry;

				// carry over highest bit in each word to the next word
				Carry = (packet_notify->InSeqHistory[CurrentWordIt] & ValueMask) >> (SequenceHistoryBitsPerWord - 1);
				packet_notify->InSeqHistory[CurrentWordIt] = (packet_notify->InSeqHistory[CurrentWordIt] << 1u) | OldValue;
			}
		}
	}
}

void HandlePacketNotification(struct utcp_fd* fd, uint16_t AckedSequence, bool bDelivered)
{
	// Increase LastNotifiedPacketId, this is a full packet Id
	++fd->LastNotifiedPacketId;

	// Sanity check
	if (seq_num_init(fd->LastNotifiedPacketId) != AckedSequence)
	{
		// UE_LOG(LogNet, Warning, TEXT("LastNotifiedPacketId != AckedSequence"));

		// Close(ENetCloseResult::AckSequenceMismatch);

		return;
	}

	if (bDelivered)
	{
		ReceivedAck(fd, fd->LastNotifiedPacketId);
	}
	else
	{
		ReceivedNak(fd, fd->LastNotifiedPacketId);
	};
}

// FNetPacketNotify::Update
int32_t packet_notify_Update(struct utcp_fd* fd, struct packet_notify* packet_notify, struct notification_header* notification_header)
{
	const int32_t InSeqDelta = GetSequenceDelta(packet_notify, notification_header);
	if (InSeqDelta > 0)
	{
		// ProcessReceivedAcks(NotificationData, InFunc);
		// FNetPacketNotify::ProcessReceivedAcks
		if (seq_num_greater_than(notification_header->AckedSeq, packet_notify->OutAckSeq))
		{
			int32_t AckCount = seq_num_diff(notification_header->AckedSeq, packet_notify->OutAckSeq);

			// Update InAckSeqAck used to track the needed number of bits to transmit our ack history
			packet_notify->InAckSeqAck = UpdateInAckSeqAck(packet_notify, AckCount, notification_header->AckedSeq);

			// ExpectedAck = OutAckSeq + 1
			uint16_t CurrentAck = packet_notify->OutAckSeq;
			CurrentAck = seq_num_inc(CurrentAck, 1);

			// Warn if the received sequence number is greater than our history buffer, since if that is the case we have to treat the data as lost.
			if (AckCount > MaxSequenceHistoryLength)
			{
				/*
				UE_LOG_PACKET_NOTIFY_WARNING(TEXT("Notification::ProcessReceivedAcks - Missed Acks: AckedSeq: %u, OutAckSeq: %u, FirstMissingSeq: %u Count:
				%u"), NotificationData.AckedSeq.Get(), OutAckSeq.Get(), CurrentAck.Get(), AckCount - (SequenceNumberT::DifferenceT)(SequenceHistoryT::Size));
											 */
			}

			while (AckCount > MaxSequenceHistoryLength)
			{
				--AckCount;
				HandlePacketNotification(fd, CurrentAck, false);
				CurrentAck = seq_num_inc(CurrentAck, 1);
			}

			// For sequence numbers contained in the history we lookup the delivery status from the history
			while (AckCount > 0)
			{
				--AckCount;

				// TSequenceHistory<HistorySize>::IsDelivered
				bool IsDelivered;
				{
					assert(AckCount < MaxSequenceHistoryLength);

					const size_t WordIndex = AckCount / SequenceHistoryBitsPerWord;
					const SequenceHistoryWord WordMask = ((SequenceHistoryWord)(1) << (AckCount & (SequenceHistoryBitsPerWord - 1)));

					IsDelivered = (packet_notify->InSeqHistory[WordIndex] & WordMask) != 0u;
				}

				// UE_LOG_PACKET_NOTIFY(TEXT("Notification::ProcessReceivedAcks Seq: %u - IsAck: %u HistoryIndex: %u"), CurrentAck.Get(),
				// NotificationData.History.IsDelivered(AckCount) ? 1u : 0u, AckCount);
				HandlePacketNotification(fd, CurrentAck, IsDelivered);
				CurrentAck = seq_num_inc(CurrentAck, 1);
			}
		}

		// accept sequence
		packet_notify->InSeq = notification_header->Seq;
		return InSeqDelta;
	}
	else
	{
		return 0;
	}
}

// FNetPacketNotify::CommitAndIncrementOutSeq
uint16_t packet_notify_CommitAndIncrementOutSeq(struct packet_notify* packet_notify)
{
	// we have not written a header...this is a fail.
	assert(packet_notify->WrittenHistoryWordCount != 0);

	// Add entry to the ack-record so that we can update the InAckSeqAck when we received the ack for this OutSeq.
	union sent_ack_data AckData;
	AckData.InAckSeq = packet_notify->WrittenInAckSeq;
	AckData.OutSeq = packet_notify->OutSeq;
	ring_buffer_queue(&packet_notify->AckRecord, AckData.Value);
	packet_notify->WrittenHistoryWordCount = 0u;
	packet_notify->OutSeq = seq_num_inc(packet_notify->OutSeq, 1);
	return packet_notify->OutSeq;
}

// FNetPacketNotify::GetCurrentSequenceHistoryLength
size_t packet_notify_GetCurrentSequenceHistoryLength(struct packet_notify* packet_notify)
{
	if (seq_num_greater_equal(packet_notify->InAckSeq, packet_notify->InAckSeqAck))
	{
		return seq_num_diff(packet_notify->InAckSeq, packet_notify->InAckSeqAck);
	}
	else
	{
		// Worst case send full history
		return MaxSequenceHistoryLength;
	}
}

// FNetPacketNotify::WriteHeader
bool packet_notify_WriteHeader(struct packet_notify* packet_notify, struct bitbuf* bitbuf, bool bRefresh)
{
	// we always write at least 1 word
	size_t CurrentHistoryWordCount =
		ClAMP((packet_notify_GetCurrentSequenceHistoryLength(packet_notify) + SequenceHistoryBitsPerWord - 1u) / SequenceHistoryBitsPerWord, 1u, SequenceHistoryWordCount);

	// We can only do a refresh if we do not need more space for the history
	if (bRefresh && (CurrentHistoryWordCount > packet_notify->WrittenHistoryWordCount))
	{
		return false;
	}

	// How many words of ack data should we write? If this is a refresh we must write the same size as the original header
	packet_notify->WrittenHistoryWordCount = bRefresh ? packet_notify->WrittenHistoryWordCount : CurrentHistoryWordCount;
	// This is the last InAck we have acknowledged at this time
	packet_notify->WrittenInAckSeq = packet_notify->InAckSeq;

	// Pack data into a uint
	uint32_t PackedHeader = PackedHeader_Pack(packet_notify->OutSeq, packet_notify->InAckSeq, packet_notify->WrittenHistoryWordCount - 1);

	// Write packed header
	bitbuf_write_int_byte_order(bitbuf, PackedHeader);

	// Write ack history
	// TSequenceHistory<HistorySize>::Write
	{
		size_t NumWords = MIN(packet_notify->WrittenHistoryWordCount, SequenceHistoryWordCount);
		for (size_t i = 0; i < NumWords; ++i)
		{
			bitbuf_write_int_byte_order(bitbuf, packet_notify->InSeqHistory[i]);
		}
	}

	// TODO log UE_LOG_PACKET_NOTIFY(TEXT("FNetPacketNotify::WriteHeader - Seq %u, AckedSeq %u bReFresh %u HistorySizeInWords %u"), Seq, AckedSeq, bRefresh ? 1u
	// : 0u, WrittenHistoryWordCount);

	return true;
}

// FNetPacketNotify::ReadHeader
int packet_notify_ReadHeader(struct utcp_fd* fd, struct bitbuf* bitbuf, struct notification_header* notification_header)
{
	// Read packed header
	uint32_t PackedHeader = 0;
	if (!bitbuf_read_int_byte_order(bitbuf, &PackedHeader))
	{
		return -2;
	}

	// unpack
	PackedHeader_UnPack(PackedHeader, &notification_header->Seq, &notification_header->AckedSeq, &notification_header->HistoryWordCount);
	notification_header->HistoryWordCount += 1;
	notification_header->HistoryWordCount = MIN(_countof(notification_header->History), notification_header->HistoryWordCount);

	for (int i = 0; i < notification_header->HistoryWordCount; ++i)
	{
		if (!bitbuf_read_int_byte_order(bitbuf, &notification_header->History[i]))
			return -3;
	}

	return 0;
}