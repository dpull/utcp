// Copyright DPULL, Inc. All Rights Reserved.

#pragma once
#include "bit_buffer.h"
#include "rudp_def.h"
#include <string.h>

extern struct rudp_config* rudp_get_config();

static inline bool write_magic_header(struct bitbuf* bitbuf)
{
	struct rudp_config* rudp_config = rudp_get_config();
	if (rudp_config->MagicHeaderBits == 0)
		return true;
	return bitbuf_write_bits(bitbuf, &rudp_config->MagicHeader, rudp_config->MagicHeaderBits);
}

static inline bool read_magic_header(struct bitbuf* bitbuf)
{
	struct rudp_config* rudp_config = rudp_get_config();
	if (rudp_config->MagicHeaderBits == 0)
		return true;
	uint32_t MagicHeader;
	if (!bitbuf_read_bits(bitbuf, &MagicHeader, rudp_config->MagicHeaderBits))
		return false;
	return MagicHeader == rudp_config->MagicHeader;
}

static inline bool try_use_debug_cookie(uint8_t* OutCookie)
{
	struct rudp_config* rudp_config = rudp_get_config();
	if (rudp_config->enable_debug_cookie)
		memcpy(OutCookie, rudp_config->debug_cookie, sizeof(rudp_config->debug_cookie));
	return rudp_config->enable_debug_cookie;
}

static inline int64_t rudp_gettime_ms(void)
{
	struct rudp_config* rudp_config = rudp_get_config();
	return rudp_config->ElapsedTime / 1000;
}

static inline double rudp_gettime(void)
{
	struct rudp_config* rudp_config = rudp_get_config();
	return ((double)rudp_config->ElapsedTime) / 1000 / 1000 / 1000;
}

static inline void rudp_raw_send(struct rudp_fd* fd, const void* buffer, size_t len)
{
	struct rudp_config* rudp_config = rudp_get_config();
	if (rudp_config->on_raw_send)
	{
		rudp_config->on_raw_send(fd, fd->userdata, buffer, (int)len);
	}
}

static inline void rudp_accept(struct rudp_fd* fd, bool new_conn)
{
	struct rudp_config* rudp_config = rudp_get_config();
	if (rudp_config->on_accept)
	{
		rudp_config->on_accept(fd, fd->userdata, !new_conn);
	}
}

static inline void rudp_recv(struct rudp_fd* fd, struct rudp_bunch* bunches[], int bunches_count)
{
	struct rudp_config* rudp_config = rudp_get_config();
	if (rudp_config->on_recv)
	{
		rudp_config->on_recv(fd, fd->userdata, bunches, bunches_count);
	}
}

static inline void rudp_delivery_status(struct rudp_fd* fd, int32_t packet_id, bool ack)
{
	struct rudp_config* rudp_config = rudp_get_config();
	if (rudp_config->on_delivery_status)
	{
		rudp_config->on_delivery_status(fd, fd->userdata, packet_id, ack);
	}
}