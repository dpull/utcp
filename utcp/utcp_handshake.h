// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "utcp_def_internal.h"

int IncomingConnectionless(struct utcp_listener* fd, const char* address, struct bitbuf* bitbuf);
bool HasPassedChallenge(struct utcp_listener* fd, const char* address, bool* bOutRestartedHandshake);
void ResetChallengeData(struct utcp_listener* fd);

void NotifyHandshakeBegin(struct utcp_connection* fd);
void SendChallengeResponse(struct utcp_connection* fd, uint8_t InSecretId, double InTimestamp, uint8_t InCookie[COOKIE_BYTE_SIZE]);
int handshake_incoming(struct utcp_connection* fd, struct bitbuf* bitbuf);
void utcp_set_state(struct utcp_connection* fd, enum utcp_state state);

