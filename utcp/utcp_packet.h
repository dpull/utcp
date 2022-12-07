// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "utcp_def_internal.h"

void utcp_sequence_init(struct utcp_connection* fd, int32_t IncomingSequence, int32_t OutgoingSequence);
bool ReceivedPacket(struct utcp_connection* fd, struct bitbuf* bitbuf);
int32_t PeekPacketId(struct utcp_connection* fd, struct bitbuf* bitbuf);
int WriteBitsToSendBuffer(struct utcp_connection* fd, const uint8_t* Bits, const int32_t SizeInBits, const uint8_t* ExtraBits, const int32_t ExtraSizeInBits);
int32_t SendRawBunch(struct utcp_connection* fd, struct utcp_bunch* bunch);
