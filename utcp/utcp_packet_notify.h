// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "utcp_def_internal.h"
#include "utcp_packet_notify_def.h"
#include <stdbool.h>
#include <stdint.h>

enum
{
	NumBitsForJitterClockTimeInHeader = 10,
};

struct packet_header
{
	struct notification_header notification_header;

	uint8_t bHasPacketInfoPayload;
	uint32_t PacketJitterClockTimeMS;
	uint8_t bHasServerFrameTime;
	uint8_t FrameTimeByte;
};

void packet_notify_init(struct packet_notify* packet_notify, uint16_t InitialInSeq, uint16_t InitialOutSeq);

int packet_notify_read_header(struct bitbuf* bitbuf, struct notification_header* notification_header);
int32_t packet_notify_delta_seq(struct packet_notify* packet_notify, struct notification_header* notification_header);

typedef void (*handle_notify_fn)(void* fd, uint16_t AckedSequence, bool bDelivered);
int32_t packet_notify_update(handle_notify_fn handle, void* fd, struct packet_notify* packet_notify, struct notification_header* notification_header);

void packet_notify_ack_seq(struct packet_notify* packet_notify, uint16_t AckedSeq, bool IsAck);
uint16_t packet_notify_commit_and_inc_outseq(struct packet_notify* packet_notify);
bool packet_notify_fill_notification_header(struct packet_notify* packet_notify, struct notification_header* notification_header, bool bRefresh);

int packet_header_read(struct packet_header* packet_header, struct bitbuf* bitbuf);
bool packet_header_write(struct packet_header* packet_header, struct bitbuf* bitbuf);
