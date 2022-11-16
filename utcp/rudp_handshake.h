// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "rudp_def.h"

void UpdateSecret(struct rudp_fd* fd);
void NotifyHandshakeBegin(struct rudp_fd* fd);
int IncomingConnectionless(struct rudp_fd* fd, const char* address, struct bitbuf* bitbuf);
void SendChallengeResponse(struct rudp_fd* fd, uint8_t InSecretId, double InTimestamp, uint8_t InCookie[COOKIE_BYTE_SIZE]);
bool HasPassedChallenge(struct rudp_fd* fd, const char* address, bool* bOutRestartedHandshake);
void ResetChallengeData(struct rudp_fd* fd);
void CapHandshakePacket(struct rudp_fd* fd, struct bitbuf* bitbuf);

int Incoming(struct rudp_fd* fd, struct bitbuf* bitbuf);

static inline void rudp_set_state(struct rudp_fd* fd, enum rudp_state state)
{
	fd->state = state;
}

int Outgoing(struct bitbuf* bitbuf);
