#pragma once

#include "3rd/ringbuffer.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CHSEQUENCE 1024
#define MaxSequenceHistoryLength 256
typedef uint32_t SequenceHistoryWord;
#define SequenceHistoryBitsPerWord (sizeof(SequenceHistoryWord) * 8)
#define SequenceHistoryWordCount (MaxSequenceHistoryLength / SequenceHistoryBitsPerWord)
#define DEFAULT_MAX_CHANNEL_SIZE 32767

union sent_ack_data {
	struct
	{
		uint16_t OutSeq; // Not needed... just to verify that things work as expected
		uint16_t InAckSeq;
	};
	uint32_t Value;
};

struct packet_notify
{
	// Track incoming sequence data
	SequenceHistoryWord InSeqHistory[SequenceHistoryWordCount]; // BitBuffer containing a bitfield describing the history of received packets
	uint16_t InSeq;												// Last sequence number received and accepted from remote
	uint16_t InAckSeq;											// Last sequence number received from remote that we have acknowledged, this is needed since we support accepting a packet but explicitly not acknowledge it as received.
	uint16_t InAckSeqAck;										// Last sequence number received from remote that we have acknowledged and also knows that the remote has received the ack, used to calculate how big our history must be

	// Track outgoing sequence data
	uint16_t OutSeq;	// Outgoing sequence number
	uint16_t OutAckSeq; // Last sequence number that we know that the remote side have received.

	size_t WrittenHistoryWordCount; // Bookkeeping to track if we can update data
	uint16_t WrittenInAckSeq;		// When we call CommitAndIncrementOutSequence this will be committed along with the current outgoing sequence number for bookkeeping

	struct ring_buffer_t AckRecord;
};

struct notification_header
{
	uint16_t Seq;
	uint16_t AckedSeq;
	size_t HistoryWordCount;
	SequenceHistoryWord History[SequenceHistoryWordCount]; // typedef uint32 WordT;
};

#ifdef __cplusplus
}
#endif