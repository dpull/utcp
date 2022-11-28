#pragma once
extern "C" {
#include "utcp/utcp.h"
#include "utcp/utcp_channel.h"
#include "utcp/utcp_channel_internal.h"
#include "utcp/utcp_def.h"
}
#include <cstring>

template <typename T, T* (*AllocFn)(), void (*FreeFn)(T*)> struct utcp_raii
{
	utcp_raii()
	{
		if (AllocFn)
			p = AllocFn();
		else
			p = new T;
	}
	~utcp_raii()
	{
		if (!p)
			return;
		if (FreeFn)
			FreeFn(p);
		else
			delete p;
	}

	T* operator&()
	{
		return p;
	}

	T* get()
	{
		return p;
	}

	void reset()
	{
		p = nullptr;
	}

	T* p;
};

inline utcp_channel* alloc_utcp_channel_zero()
{
	return alloc_utcp_channel(0, 0);
}

inline utcp_connection* new_utcp_connection()
{
	auto conn = new utcp_connection;
	utcp_init(conn, nullptr);
	return conn;
}

inline void delete_utcp_connection(utcp_connection* fd)
{
	utcp_uninit(fd);
	delete fd;
}

inline utcp_listener* new_utcp_listener()
{
	auto listener = new utcp_listener;
	utcp_listener_init(listener, nullptr);
	return listener;
}

inline utcp_opened_channels* new_open_channels()
{
	auto open_channels = new utcp_opened_channels;
	memset(open_channels, 0, sizeof(*open_channels));
	return open_channels;
}

inline void delete_open_channels(utcp_opened_channels* open_channels)
{
	open_channel_uninit(open_channels);
	delete open_channels;
}

using utcp_bunch_node_raii = utcp_raii<utcp_bunch_node, alloc_utcp_bunch_node, free_utcp_bunch_node>;
using utcp_channel_rtti = utcp_raii<utcp_channel, alloc_utcp_channel_zero, free_utcp_channel>;
using utcp_connection_rtti = utcp_raii<utcp_connection, new_utcp_connection, delete_utcp_connection>;
using utcp_listener_rtti = utcp_raii<utcp_listener, new_utcp_listener, nullptr>;
using utcp_open_channels_rtti = utcp_raii<utcp_opened_channels, new_open_channels, delete_open_channels>;
