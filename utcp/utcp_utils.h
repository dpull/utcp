// Copyright DPULL, Inc. All Rights Reserved.

#pragma once
#include "bit_buffer.h"
#include "utcp_def.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern struct utcp_config* utcp_get_config();

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


enum log_level
{
	/** Not used */
	NoLogging = 0,

	/** Always prints a fatal error to console (and log file) and crashes (even if logging is disabled) */
	Fatal,

	/**
	 * Prints an error to console (and log file).
	 * Commandlets and the editor collect and report errors. Error messages result in commandlet failure.
	 */
	Error,

	/**
	 * Prints a warning to console (and log file).
	 * Commandlets and the editor collect and report warnings. Warnings can be treated as an error.
	 */
	Warning,

	/** Prints a message to console (and log file) */
	Display,

	/** Prints a message to a log file (does not print to console) */
	Log,

	/**
	 * Prints a verbose message to a log file (if Verbose logging is enabled for the given category,
	 * usually used for detailed logging)
	 */
	Verbose,
};

static inline void utcp_log(enum log_level level, const char* fmt, ...)
{
	struct utcp_config* utcp_config = utcp_get_config();
	if (utcp_config->on_log)
	{
		char log_buffer[16 * 1024];

		va_list marker;
		va_start(marker, fmt);
		vsnprintf(log_buffer, sizeof(log_buffer), fmt, marker);
		va_end(marker);
		log_buffer[sizeof(log_buffer) - 1] = '\0';
		utcp_config->on_log(level, log_buffer);
	}
}