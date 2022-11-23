#pragma once
extern "C" {
#include "utcp/utcp_def.h"
#include "utcp/utcp_channel.h"
#include "utcp/utcp.h"
}

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

inline void delete_utcp_fd(utcp_connection* fd)
{
	utcp_uninit(fd);
	delete fd;
}

using utcp_bunch_node_raii = utcp_raii<utcp_bunch_node, alloc_utcp_bunch_node, free_utcp_bunch_node>;
using utcp_channel_rtti = utcp_raii<utcp_channel, alloc_utcp_channel_zero, free_utcp_channel>;
using utcp_fd_rtti = utcp_raii<utcp_connection, nullptr, delete_utcp_fd>;
using utcp_listener_rtti = utcp_raii<utcp_listener, nullptr, nullptr>;
