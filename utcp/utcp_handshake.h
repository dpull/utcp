// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "utcp_def_internal.h"

// The design of handshake: https://blog.dpull.com/post/2022-11-13-utcp_handshake

int process_connectionless_packet(struct utcp_listener* fd, const char* address, const uint8_t* buffer, int len);

void handshake_begin(struct utcp_connection* fd);
int handshake_incoming(struct utcp_connection* fd, struct bitbuf* bitbuf);
void handshake_update(struct utcp_connection* fd);

static inline bool is_client(struct utcp_connection* fd)
{
	return fd->challenge_data != NULL;
}

static inline bool is_connected(struct utcp_connection* fd)
{
	return !is_client(fd) || (is_client(fd) && fd->challenge_data->state == Initialized);
}