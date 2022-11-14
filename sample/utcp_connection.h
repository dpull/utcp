#pragma once
#include "socket.h"
#include <cstdio>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

constexpr int MAX_PACKET_BUFFER_SIZE = 1800;

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
	~utcp_connection();

	bool listen(const char* ip, int port);
	bool connnect(const char* ip, int port);

	virtual void tick();
	virtual void after_tick();

	virtual int send(char* bunch, int count);

	virtual void raw_recv(uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len);

  protected:
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
};

class utcp_listener : public utcp_connection
{
  protected:
	virtual void tick() override;
	virtual void after_tick() override;
	virtual void raw_recv(uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len) override;
	virtual int send(char* bunch, int count) override;

  private:
	std::unordered_map<struct sockaddr_in, utcp_connection*, sockaddr_in_Hash, sockaddr_in_Equal> clients;

	std::vector<utcp_packet_view*> recv_unordered_queue;
	std::vector<utcp_packet_view*> proc_unordered_queue;
};
