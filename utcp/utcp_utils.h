// Copyright DPULL, Inc. All Rights Reserved.

#pragma once
#include "bit_buffer.h"
#include "utcp_def_internal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct utcp_config* utcp_get_config();

#if defined(__linux) || defined(__APPLE__)
#define _countof(array) (sizeof(array) / sizeof(array[0]))
#endif

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

static inline void* utcp_realloc(void* ptr, size_t size)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (!utcp_config->on_realloc)
	{
		if (size > 0)
			return realloc(ptr, size);
		// if new_size is zero, the behavior is undefined. (since C23)
		free(ptr);
		return NULL;
	}
	else
	{
		return utcp_config->on_realloc(ptr, size);
	}
}

static inline void utcp_dump(const char* debug_name, const char* type, const void* data, int len)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (!utcp_config->EnableDump)
		return;

	char str[UTCP_MAX_PACKET * 8];
	int size = 0;

	for (int i = 0; i < len; ++i)
	{
		if (i != 0)
		{
			int ret = snprintf(str + size, sizeof(str) - size, ", ");
			if (ret < 0)
				break;
			size += ret;
		}

		int ret = snprintf(str + size, sizeof(str) - size, "0x%hhX", ((const uint8_t*)data)[i]);
		if (ret < 0)
			break;
		size += ret;
	}
	str[size] = '\0';
	utcp_log(Verbose, "[%s][DUMP]%s\t%d\t{%s}", debug_name, type, len, str);
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

static inline int64_t utcp_gettime_ms(void)
{
	struct utcp_config* utcp_config = utcp_get_config();
	return utcp_config->ElapsedTime / 1000 + 1000;
}

static inline double utcp_gettime(void)
{
	struct utcp_config* utcp_config = utcp_get_config();
	return ((double)utcp_config->ElapsedTime) / 1000 / 1000 / 1000 + 1;
}

static inline void utcp_listener_outgoing(struct utcp_listener* fd, const void* buffer, size_t len)
{
	utcp_dump("listener", "outgoing", buffer, (int)len);
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_outgoing)
	{
		utcp_config->on_outgoing(fd, fd->userdata, buffer, (int)len);
	}
}

static inline void utcp_connection_outgoing(struct utcp_connection* fd, const void* buffer, size_t len)
{
	utcp_dump(fd->debug_name, "outgoing", buffer, (int)len);
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_outgoing)
	{
		utcp_config->on_outgoing(fd, fd->userdata, buffer, (int)len);
	}
}

static inline void utcp_on_accept(struct utcp_listener* fd, bool reconnect)
{
	utcp_log(Log, "accept:%s, reconnect=%d", fd->LastChallengeSuccessAddress, reconnect);
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_accept)
	{
		utcp_config->on_accept(fd, fd->userdata, reconnect);
	}
}

static inline void utcp_on_connect(struct utcp_connection* fd, bool reconnect)
{
	utcp_log(Log, "[%s]connected, reconnect=%d", fd->debug_name, reconnect);
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_connect)
	{
		utcp_config->on_connect(fd, fd->userdata, reconnect);
	}
}

static inline void utcp_on_disconnect(struct utcp_connection* fd, int close_reason)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_disconnect)
	{
		utcp_config->on_disconnect(fd, fd->userdata, close_reason);
	}
}

static inline void utcp_recv_bunch(struct utcp_connection* fd, struct utcp_bunch* bunches[], int bunches_count)
{
	assert(bunches_count > 0);
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_recv_bunch)
	{
		utcp_config->on_recv_bunch(fd, fd->userdata, bunches, bunches_count);
	}
}

static inline void utcp_delivery_status(struct utcp_connection* fd, int32_t packet_id, bool ack)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_delivery_status)
	{
		utcp_config->on_delivery_status(fd, fd->userdata, packet_id, ack);
	}
}
