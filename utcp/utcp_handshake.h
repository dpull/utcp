// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "utcp_def.h"

void UpdateSecret(struct utcp_fd* fd);
void NotifyHandshakeBegin(struct utcp_fd* fd);
int IncomingConnectionless(struct utcp_fd* fd, const char* address, struct bitbuf* bitbuf);
void SendChallengeResponse(struct utcp_fd* fd, uint8_t InSecretId, double InTimestamp, uint8_t InCookie[COOKIE_BYTE_SIZE]);
bool HasPassedChallenge(struct utcp_fd* fd, const char* address, bool* bOutRestartedHandshake);
void ResetChallengeData(struct utcp_fd* fd);
void CapHandshakePacket(struct utcp_fd* fd, struct bitbuf* bitbuf);

int Incoming(struct utcp_fd* fd, struct bitbuf* bitbuf);

static inline void utcp_set_state(struct utcp_fd* fd, enum utcp_state state)
{
	fd->state = state;
}

int Outgoing(struct bitbuf* bitbuf);
