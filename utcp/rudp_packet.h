#pragma once

#include "rudp_def.h"

void packet_notify_Init(struct packet_notify* packet_notify, uint16_t InitialInSeq, uint16_t InitialOutSeq);
int ReceivedPacket(struct rudp_fd* fd, struct bitbuf* bitbuf);
int WriteBitsToSendBuffer(struct rudp_fd* fd, char* buffer, int bits_len);
void WritePacketHeader(struct rudp_fd* fd, struct bitbuf* bitbuf);
void WriteFinalPacketInfo(struct rudp_fd* fd, struct bitbuf* bitbuf);