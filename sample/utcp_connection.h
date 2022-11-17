#pragma once
#include "socket.h"
#include "utcp/rudp.h"
#include <cstdio>
#include <mutex>
#include <queue>
#include <thread>

#define DISABLE_FUNCTION                                                                                                                                                           \
	{                                                                                                                                                                              \
		throw;                                                                                                                                                                     \
	}

constexpr int MAX_PACKET_BUFFER_SIZE = 1800;
void log(int level, const char* msg);

struct utcp_packet_view
{
	struct sockaddr_storage from_addr;
	socklen_t from_addr_len;

	uint32_t handle;

	uint16_t data_len;
	uint8_t data[MAX_PACKET_BUFFER_SIZE];

	bool operator()(const utcp_packet_view* l, const utcp_packet_view* r) const
	{
		return l->handle > r->handle;
	}
};

struct utcp_packet_view_ordered_queue
{
	void push(utcp_packet_view* view)
	{
		queue.push(view);
	}

	utcp_packet_view* pop(int handle = -1)
	{
		if (queue.empty())
			return nullptr;
		auto view = queue.top();
		if (handle != -1 && view->handle != handle)
			return nullptr;
		queue.pop();
		return view;
	}

	std::priority_queue<utcp_packet_view*, std::vector<utcp_packet_view*>, utcp_packet_view> queue;
};

class utcp_connection
{
  public:
	static void config_rudp();

	utcp_connection(bool is_client);
	~utcp_connection();

	virtual bool accept(utcp_connection* listener, bool reconnect);

	virtual void tick();
	virtual void after_tick();

	virtual void raw_recv(utcp_packet_view* view);
	virtual int send(char* bunch, int count);

  protected:
	virtual void on_accept(bool reconnect) DISABLE_FUNCTION;
	virtual void on_raw_send(const void* data, int len);
	virtual void on_recv(const struct rudp_bunch* bunches[], int count);
	virtual void on_delivery_status(int32_t packet_id, bool ack);

  private:
	void proc_ordered_cache(bool flushing_order_cache);

  protected:
	socket_t socket_fd = INVALID_SOCKET;

	struct sockaddr_storage dest_addr;
	socklen_t dest_addr_len = 0;

	rudp_fd rudp;

	utcp_packet_view_ordered_queue* ordered_cache = nullptr;
};
