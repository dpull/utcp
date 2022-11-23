// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "utcp_def.h"

void utcp_closeall_channel(struct utcp_connection* fd);
void packet_notify_Init(struct packet_notify* packet_notify, uint16_t InitialInSeq, uint16_t InitialOutSeq);
int ReceivedPacket(struct utcp_connection* fd, struct bitbuf* bitbuf);
int PeekPacketId(struct utcp_connection* fd, struct bitbuf* bitbuf);
int WriteBitsToSendBuffer(struct utcp_connection* fd, char* buffer, int bits_len);
void WritePacketHeader(struct utcp_connection* fd, struct bitbuf* bitbuf);
void WriteFinalPacketInfo(struct utcp_connection* fd, struct bitbuf* bitbuf);
bool check_can_send(struct utcp_connection* fd, struct utcp_bunch* bunches[], int bunches_count);
int32_t SendRawBunch(struct utcp_connection* fd, struct utcp_bunch* bunch);
