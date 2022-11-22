// Copyright DPULL, Inc. All Rights Reserved.

#pragma once
#include "bit_buffer.h"
#include "utcp_def.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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

static inline void* utcp_realloc(void* ptr, size_t size)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_realloc)
		return utcp_config->on_realloc(ptr, size);
	else
		return realloc(ptr, size);
}

static inline void utcp_dump(const char* type, int ext, const void* data, int len)
{
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
	utcp_log(Verbose, "[DUMP]%s-%d\t%d\t{%s}", type, ext, len, str);
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
	utcp_dump("raw_send", 0, buffer, (int)len);
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
	assert(bunches_count > 0);
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

static inline int binary_search(const void* key, const void* base, size_t num, size_t element_size, int (*compar)(const void*, const void*))
{
	int lo = 0;
	int hi = (int)num - 1;

	while (lo <= hi)
	{
		// i might overflow if lo and hi are both large positive numbers.
		int i = lo + ((hi - lo) >> 1);

		int c = compar(key, (char*)base + i * element_size);
		if (c == 0)
			return i;
		if (c > 0)
			lo = i + 1;
		else
			hi = i - 1;
	}
	return ~lo;
}
