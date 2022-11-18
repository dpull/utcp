// Copyright DPULL, Inc. All Rights Reserved.

#pragma once
#include "bit_buffer.h"
#include "utcp_def.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern struct utcp_config* utcp_get_config();

enum log_level
{
	NoLogging = 0,
	Fatal,
	Error,
	Warning,
	Display,
	Log,
	Verbose,
};

static inline void utcp_log(enum log_level level, const char* fmt, ...)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_log)
	{
		va_list marker;
		va_start(marker, fmt);
		utcp_config->on_log(level, fmt, marker);
		va_end(marker);
	}
}

static inline bool write_magic_header(struct bitbuf* bitbuf)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->MagicHeaderBits == 0)
		return true;
	return bitbuf_write_bits(bitbuf, &utcp_config->MagicHeader, utcp_config->MagicHeaderBits);
}

static inline bool read_magic_header(struct bitbuf* bitbuf)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->MagicHeaderBits == 0)
		return true;
	uint32_t MagicHeader;
	if (!bitbuf_read_bits(bitbuf, &MagicHeader, utcp_config->MagicHeaderBits))
		return false;
	return MagicHeader == utcp_config->MagicHeader;
}

static inline bool try_use_debug_cookie(uint8_t* OutCookie)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->enable_debug_cookie)
		memcpy(OutCookie, utcp_config->debug_cookie, sizeof(utcp_config->debug_cookie));
	return utcp_config->enable_debug_cookie;
}

static inline int64_t utcp_gettime_ms(void)
{
	struct utcp_config* utcp_config = utcp_get_config();
	return utcp_config->ElapsedTime / 1000;
}

static inline double utcp_gettime(void)
{
	struct utcp_config* utcp_config = utcp_get_config();
	return ((double)utcp_config->ElapsedTime) / 1000 / 1000 / 1000;
}

static inline void utcp_raw_send(struct utcp_fd* fd, const void* buffer, size_t len)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_raw_send)
	{
		utcp_config->on_raw_send(fd, fd->userdata, buffer, (int)len);
	}
}

static inline void utcp_accept(struct utcp_fd* fd, bool reconnect)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_accept)
	{
		utcp_config->on_accept(fd, fd->userdata, reconnect);
	}
}

static inline void utcp_recv(struct utcp_fd* fd, struct utcp_bunch* bunches[], int bunches_count)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_recv)
	{
		utcp_config->on_recv(fd, fd->userdata, bunches, bunches_count);
	}
}

static inline void utcp_delivery_status(struct utcp_fd* fd, int32_t packet_id, bool ack)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_delivery_status)
	{
		utcp_config->on_delivery_status(fd, fd->userdata, packet_id, ack);
	}
}
