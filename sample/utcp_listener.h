#pragma once
#include "abstract/utcp.hpp"
#include "udp_socket.h"
#include <unordered_map>
#include <vector>

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

class udp_utcp_listener : public utcp::listener
{
  public:
	udp_utcp_listener();
	~udp_utcp_listener();

	bool listen(const char* ip, int port);

	void tick();
	void post_tick();

  protected:
	virtual void on_accept(bool reconnect) override;
	virtual void on_outgoing(const void* data, int len) override;
	virtual utcp::conn* new_conn() = 0;
	void proc_recv_queue();

  protected:
	udp_socket socket;
	std::unordered_map<struct sockaddr_in, utcp::conn*, sockaddr_in_Hash, sockaddr_in_Equal> clients;
};

template <typename T> class udp_utcp_listener_impl : public udp_utcp_listener
{
  protected:
	virtual utcp::conn* new_conn() override
	{
		return new T;
	}

	virtual void accept(utcp::conn* c, bool reconnect) override
	{
		static_cast<T*>(c)->bind(socket.socket_fd, &socket.dest_addr, socket.dest_addr_len);
		udp_utcp_listener::accept(c, reconnect);
	}
};
