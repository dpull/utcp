#include "rudp_packet_notify.h"
#include "bit_buffer.h"
#include "rudp_def.h"
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

#define SequenceNumberBits 14
#define HistoryWordCountBits 4
#define SeqMask ((1 << SequenceNumberBits) - 1)
#define HistoryWordCountMask ((1 << HistoryWordCountBits) - 1)
#define AckSeqShift HistoryWordCountBits
#define SeqShift (AckSeqShift + SequenceNumberBits)

static inline uint16_t PackedHeader_GetSeq(uint32_t Packed)
{
	return Packed >> SeqShift & SeqMask;
}
static inline uint16_t PackedHeader_GetAckedSeq(uint32_t Packed)
{
	return Packed >> AckSeqShift & SeqMask;
}
static inline size_t PackedHeader_GetHistoryWordCount(uint32_t Packed)
{
	return Packed & HistoryWordCountMask;
}

static uint32_t PackedHeader_Pack(uint16_t Seq, uint16_t AckedSeq, size_t HistoryWordCount)
{
	uint32_t Packed = 0u;

	Packed |= Seq << SeqShift;
	Packed |= AckedSeq << AckSeqShift;
	Packed |= HistoryWordCount & HistoryWordCountMask;

	return Packed;
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

int32_t GetSequenceDelta(struct packet_notify* packet_notify, struct notification_header* notification_header)
{
	if (notification_header->Seq > packet_notify->InSeq && notification_header->AckedSeq >= packet_notify->OutAckSeq && packet_notify->OutSeq > notification_header->AckedSeq)
	{
		return notification_header->Seq - packet_notify->InSeq;
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
	packet_notify->OutAckSeq = InitialOutSeq - 1;

	ring_buffer_init(&packet_notify->AckRecord);
}

// FNetPacketNotify::AckSeq
void packet_notify_AckSeq(struct packet_notify* packet_notify, uint16_t AckedSeq, bool IsAck)
{
	assert(AckedSeq == packet_notify->InSeq);

	while (AckedSeq > packet_notify->InAckSeq)
	{
		++packet_notify->InAckSeq;

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


static void HandlePacketNotification(struct rudp_fd* fd, uint16_t AckedSequence, bool bDelivered)
{
	// Increase LastNotifiedPacketId, this is a full packet Id
	++fd->LastNotifiedPacketId;

	// Sanity check
	if ((fd->LastNotifiedPacketId & HistoryWordCountMask) != AckedSequence)
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
int32_t packet_notify_Update(struct rudp_fd* fd, struct packet_notify* packet_notify, struct notification_header* notification_header)
{
	const int32_t InSeqDelta = GetSequenceDelta(packet_notify, notification_header);
	if (InSeqDelta > 0)
	{
		// ProcessReceivedAcks(NotificationData, InFunc);
		// FNetPacketNotify::ProcessReceivedAcks
		if (notification_header->AckedSeq > packet_notify->OutAckSeq)
		{
			int32_t AckCount = notification_header->AckedSeq - packet_notify->OutAckSeq;

			// Update InAckSeqAck used to track the needed number of bits to transmit our ack history
			packet_notify->InAckSeqAck = UpdateInAckSeqAck(packet_notify, AckCount, notification_header->AckedSeq);

			// ExpectedAck = OutAckSeq + 1
			uint16_t CurrentAck = packet_notify->OutAckSeq;
			++CurrentAck;

			// Warn if the received sequence number is greater than our history buffer, since if that is the case we have to treat the data as lost.
			if (AckCount > MaxSequenceHistoryLength)
			{
				/*
				UE_LOG_PACKET_NOTIFY_WARNING(TEXT("Notification::ProcessReceivedAcks - Missed Acks: AckedSeq: %u, OutAckSeq: %u, FirstMissingSeq: %u Count: %u"), NotificationData.AckedSeq.Get(), OutAckSeq.Get(), CurrentAck.Get(),
											 AckCount - (SequenceNumberT::DifferenceT)(SequenceHistoryT::Size));
											 */
				while (AckCount > MaxSequenceHistoryLength)
				{
					--AckCount;
					HandlePacketNotification(fd, CurrentAck, false);
					++CurrentAck;
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
					
					// UE_LOG_PACKET_NOTIFY(TEXT("Notification::ProcessReceivedAcks Seq: %u - IsAck: %u HistoryIndex: %u"), CurrentAck.Get(), NotificationData.History.IsDelivered(AckCount) ? 1u : 0u, AckCount);
					HandlePacketNotification(fd, CurrentAck, IsDelivered);
					++CurrentAck;
				}
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

	return ++packet_notify->OutSeq;
}

// FNetPacketNotify::GetCurrentSequenceHistoryLength
size_t packet_notify_GetCurrentSequenceHistoryLength(struct packet_notify* packet_notify)
{
	if (packet_notify->InAckSeq >= packet_notify->InAckSeqAck)
	{
		return packet_notify->InAckSeq - packet_notify->InAckSeqAck;
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
	size_t CurrentHistoryWordCount = ClAMP((packet_notify_GetCurrentSequenceHistoryLength(packet_notify) + SequenceHistoryBitsPerWord - 1u) / SequenceHistoryBitsPerWord, 1u, SequenceHistoryWordCount);

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
	bitbuf_write_bytes(bitbuf, &PackedHeader, sizeof(PackedHeader));

	// Write ack history
	// TSequenceHistory<HistorySize>::Write
	{
		size_t NumWords = MIN(packet_notify->WrittenHistoryWordCount, SequenceHistoryWordCount);
		bitbuf_write_bytes(bitbuf, &packet_notify->InSeqHistory, NumWords * sizeof(packet_notify->InSeqHistory[0]));
	}

	// TODO log UE_LOG_PACKET_NOTIFY(TEXT("FNetPacketNotify::WriteHeader - Seq %u, AckedSeq %u bReFresh %u HistorySizeInWords %u"), Seq, AckedSeq, bRefresh ? 1u : 0u, WrittenHistoryWordCount);

	return true;
}

// FNetPacketNotify::ReadHeader
int packet_notify_ReadHeader(struct rudp_fd* fd, struct bitbuf* bitbuf, struct notification_header* notification_header)
{
	// Read packed header
	uint32_t PackedHeader = 0;
	if (!bitbuf_read_bytes(bitbuf, &PackedHeader, sizeof(PackedHeader)))
	{
		return -2;
	}

	// unpack
	notification_header->Seq = PackedHeader_GetSeq(PackedHeader);
	notification_header->AckedSeq = PackedHeader_GetAckedSeq(PackedHeader);
	notification_header->HistoryWordCount = PackedHeader_GetHistoryWordCount(PackedHeader) + 1;
	notification_header->HistoryWordCount = MIN(_countof(notification_header->History), notification_header->HistoryWordCount);

	if (!bitbuf_read_bytes(bitbuf, &notification_header->History, notification_header->HistoryWordCount * sizeof(notification_header->History[0])))
	{
		return -3;
	}
	return 0;
}
