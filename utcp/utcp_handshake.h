// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "utcp_def.h"

void NotifyHandshakeBegin(struct utcp_connection* fd);
int IncomingConnectionless(struct utcp_listener* fd, const char* address, struct bitbuf* bitbuf);
void SendChallengeResponse(struct utcp_connection* fd, uint8_t InSecretId, double InTimestamp, uint8_t InCookie[COOKIE_BYTE_SIZE]);
bool HasPassedChallenge(struct utcp_listener* fd, const char* address, bool* bOutRestartedHandshake);
void ResetChallengeData(struct utcp_listener* fd);
void CapHandshakePacket(struct bitbuf* bitbuf);

int Incoming(struct utcp_connection* fd, struct bitbuf* bitbuf);

static inline void utcp_set_state(struct utcp_connection* fd, enum utcp_state state)
{
	fd->state = state;
}

int Outgoing(struct bitbuf* bitbuf);
