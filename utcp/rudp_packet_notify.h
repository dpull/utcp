// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "rudp_def.h"
#include "rudp_packet_notify_def.h"
#include <stdbool.h>
#include <stdint.h>

int packet_notify_ReadHeader(struct rudp_fd* fd, struct bitbuf* bitbuf, struct notification_header* notification_header);
int32_t GetSequenceDelta(struct packet_notify* packet_notify, struct notification_header* notification_header);
int32_t packet_notify_Update(struct rudp_fd* fd, struct packet_notify* packet_notify, struct notification_header* notification_header);
void packet_notify_AckSeq(struct packet_notify* packet_notify, uint16_t AckedSeq, bool IsAck);
bool packet_notify_WriteHeader(struct packet_notify* packet_notify, struct bitbuf* bitbuf, bool bRefresh);
void HandlePacketNotification(struct rudp_fd* fd, uint16_t AckedSequence, bool bDelivered);
