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

void rudp_raw_send(struct rudp_fd* fd, char* buffer, size_t len);
void rudp_raw_accept(struct rudp_fd* fd, bool new_conn, char* buffer, size_t len);

int64_t rudp_gettime_ms(void);
double rudp_gettime(void); // GetElapsedTime

inline static void rudp_set_state(struct rudp_fd* fd, enum rudp_state state)
{
	fd->state = state;
}

int Outgoing(struct bitbuf* bitbuf);

bool write_magic_header(struct bitbuf* bitbuf);
bool read_magic_header(struct bitbuf* bitbuf);
bool try_use_debug_cookie(uint8_t* OutCookie);
