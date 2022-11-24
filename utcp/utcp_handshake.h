// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "utcp_def_internal.h"

void CapHandshakePacket(struct bitbuf* bitbuf);
int Outgoing(struct bitbuf* bitbuf);

int IncomingConnectionless(struct utcp_listener* fd, const char* address, struct bitbuf* bitbuf);
bool HasPassedChallenge(struct utcp_listener* fd, const char* address, bool* bOutRestartedHandshake);
void ResetChallengeData(struct utcp_listener* fd);

void NotifyHandshakeBegin(struct utcp_connection* fd);
void SendChallengeResponse(struct utcp_connection* fd, uint8_t InSecretId, double InTimestamp, uint8_t InCookie[COOKIE_BYTE_SIZE]);
int Incoming(struct utcp_connection* fd, struct bitbuf* bitbuf);
void utcp_sequence_init(struct utcp_connection* fd, int32_t IncomingSequence, int32_t OutgoingSequence);
static inline void utcp_set_state(struct utcp_connection* fd, enum utcp_state state)
{
	fd->state = state;
}

