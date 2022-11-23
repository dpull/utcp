// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "utcp_def.h"
#include "utcp_packet_notify_def.h"
#include <stdbool.h>
#include <stdint.h>

int packet_notify_ReadHeader(struct utcp_connection* fd, struct bitbuf* bitbuf, struct notification_header* notification_header);
int32_t GetSequenceDelta(struct packet_notify* packet_notify, struct notification_header* notification_header);
int32_t packet_notify_Update(struct utcp_connection* fd, struct packet_notify* packet_notify, struct notification_header* notification_header);
void packet_notify_AckSeq(struct packet_notify* packet_notify, uint16_t AckedSeq, bool IsAck);
bool packet_notify_WriteHeader(struct packet_notify* packet_notify, struct bitbuf* bitbuf, bool bRefresh);
void HandlePacketNotification(struct utcp_connection* fd, uint16_t AckedSequence, bool bDelivered);
uint16_t packet_notify_CommitAndIncrementOutSeq(struct packet_notify* packet_notify);