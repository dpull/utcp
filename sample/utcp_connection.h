#pragma once
#include "socket.h"
#include "utcp/rudp.h"
#include <cstdio>
#include <mutex>
#include <queue>
#include <thread>

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

struct sockaddr_in_Hash
{
	std::size_t operator()(const sockaddr_in& in4) const
	{
		size_t out = in4.sin_port;
		out = out << 32;
		out += in4.sin_addr.s_addr;
		return out;
	}
};

struct sockaddr_in_Equal
{
	bool operator()(const sockaddr_in& lhs, const sockaddr_in& rhs) const
	{
		return lhs.sin_addr.s_addr == rhs.sin_addr.s_addr && lhs.sin_port == rhs.sin_port;
	}
};

class utcp_connection
{
  public:
	static void config_rudp();

	utcp_connection(bool is_client);
	~utcp_connection();

	bool listen(const char* ip, int port);
	bool connnect(const char* ip, int port);
	bool accept(utcp_connection* server);

	virtual void tick();
	virtual void after_tick();

	virtual int send(char* bunch, int count);

  protected:
	static void call_raw_recv(utcp_connection* conn, uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len);
	virtual void raw_recv(uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len);

	virtual void on_accept(bool reconnect);
	virtual void on_raw_send(const void* data, int len);
	virtual void on_recv(const struct rudp_bunch* bunches[], int count);
	virtual void on_delivery_status(int32_t packet_id, bool ack);

	utcp_packet_view* try_get_packet(int handle);
	utcp_packet_view* get_packet();
	void create_recv_thread();

	volatile int recv_thread_exit_flag = false;
	std::thread* recv_thread = nullptr;

	socket_t socket_fd = INVALID_SOCKET;

	struct sockaddr_storage dest_addr;
	socklen_t dest_addr_len = 0;

	std::mutex recv_queue_mutex;
	std::priority_queue<utcp_packet_view*, std::vector<utcp_packet_view*>, utcp_packet_view> recv_queue;
	rudp_fd rudp;
};
