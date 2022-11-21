#include "gtest/gtest.h"
extern "C" {
#include "utcp/utcp_channel.h"
}
#include <vector>

template <typename T, T* (*AllocFn)(), void (*FreeFn)(T*)> struct utcp_raii
{
	utcp_raii()
	{
		node = AllocFn();
	}
	~utcp_raii()
	{
		FreeFn(node);
	}
	T* node;
};

static utcp_channel* alloc_utcp_channel_zero()
{
	return alloc_utcp_channel(0, 0);
}

using utcp_bunch_node_raii = utcp_raii<utcp_bunch_node, alloc_utcp_bunch_node, free_utcp_bunch_node>;
using utcp_channel_rtti = utcp_raii<utcp_channel, alloc_utcp_channel_zero, free_utcp_channel>;

TEST(channel, order_incoming)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii node1;
	utcp_bunch_node_raii node2;
	utcp_bunch_node_raii node3;
	utcp_bunch_node_raii node4;
	
}
