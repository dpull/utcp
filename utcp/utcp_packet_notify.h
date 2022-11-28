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

int packet_notify_ReadHeader(struct bitbuf* bitbuf, struct notification_header* notification_header);
int32_t GetSequenceDelta(struct packet_notify* packet_notify, struct notification_header* notification_header);

typedef void (*HandlePacketNotificationFn)(void* fd, uint16_t AckedSequence, bool bDelivered);
int32_t packet_notify_Update(HandlePacketNotificationFn handle, void* fd, struct packet_notify* packet_notify, struct notification_header* notification_header);

void packet_notify_AckSeq(struct packet_notify* packet_notify, uint16_t AckedSeq, bool IsAck);
uint16_t packet_notify_CommitAndIncrementOutSeq(struct packet_notify* packet_notify);
bool packet_notify_fill_notification_header(struct packet_notify* packet_notify, struct notification_header* notification_header, bool bRefresh);

int packet_header_read(struct packet_header* packet_header, struct bitbuf* bitbuf);
bool packet_header_write(struct packet_header* packet_header, struct bitbuf* bitbuf);
