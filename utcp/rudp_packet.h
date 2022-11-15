// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "rudp_def.h"

void packet_notify_Init(struct packet_notify* packet_notify, uint16_t InitialInSeq, uint16_t InitialOutSeq);
int ReceivedPacket(struct rudp_fd* fd, struct bitbuf* bitbuf);
int WriteBitsToSendBuffer(struct rudp_fd* fd, char* buffer, int bits_len);
void WritePacketHeader(struct rudp_fd* fd, struct bitbuf* bitbuf);
void WriteFinalPacketInfo(struct rudp_fd* fd, struct bitbuf* bitbuf);
void rudp_on_recv(struct rudp_fd* fd, struct rudp_bunch* bunches[], int bunches_count);
bool check_can_send(struct rudp_fd* fd, const struct rudp_bunch* bunches[], int bunches_count);
int32_t SendRawBunch(struct rudp_fd* fd, const struct rudp_bunch* bunch);