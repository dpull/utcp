#include "utcp_listener.h"
#include "ds_connection.h"
#include <cassert>
#include <cstring>

static bool sockaddr2str(sockaddr_in* addr, char ipstr[], int size)
{
	if (!inet_ntop(addr->sin_family, &addr->sin_addr, ipstr, size))
		return false;
	auto len = strlen(ipstr);
	auto port = ntohs(addr->sin_port);
	snprintf(ipstr + len, size - len, ":%d", port);
	return true;
}

udp_utcp_listener::udp_utcp_listener()
{
}

udp_utcp_listener::~udp_utcp_listener()
{
	for (auto& it : clients)
	{
		delete it.second;
	}
}

bool udp_utcp_listener::listen(const char* ip, int port)
{
	return socket.listen(ip, port);
}

void udp_utcp_listener::tick()
{
	proc_recv_queue();
	for (auto& it : clients)
	{
		it.second->update();
	}
}

void udp_utcp_listener::post_tick()
{
	proc_recv_queue();
	for (auto& it : clients)
	{
		it.second->flush_incoming_cache();
		it.second->send_flush();
	}
}

void udp_utcp_listener::on_accept(bool reconnect)
{
	utcp::conn* conn = nullptr;
	if (!reconnect)
	{
		conn = new_conn();
	}
	else
	{
		auto auth_cookie = get_auth_cookie();
		if (!auth_cookie)
			return;

		for (auto it = clients.begin(); it != clients.end(); ++it)
		{
			if (it->second->same_auth_cookie(auth_cookie))
			{
				conn = it->second;
				clients.erase(it);
				break;
			}
		}

		if (!conn)
			return;
	}

	auto it = clients.insert(std::make_pair(*(sockaddr_in*)&socket.dest_addr, conn));
	assert(it.second);

	accept(conn, reconnect);
}

void udp_utcp_listener::on_outgoing(const void* data, int len)
{
	assert(socket.dest_addr_len > 0);
	sendto(socket.socket_fd, (const char*)data, len, 0, (sockaddr*)&socket.dest_addr, socket.dest_addr_len);
}

void udp_utcp_listener::proc_recv_queue()
{
	auto& proc_queue = socket.swap();
	char ipstr[INET6_ADDRSTRLEN + 8];

	for (auto& datagram : proc_queue)
	{
		assert(datagram.from_addr_len == sizeof(sockaddr_in));
		auto it = clients.find(*(sockaddr_in*)&datagram.from_addr);
		if (it != clients.end())
		{
			it->second->incoming(datagram.data, datagram.data_len);
			continue;
		}

		if (sockaddr2str((sockaddr_in*)&datagram.from_addr, ipstr, sizeof(ipstr)))
		{
			socket.dest_addr_len = datagram.from_addr_len;
			memcpy(&socket.dest_addr, &datagram.from_addr, socket.dest_addr_len);
			incoming(ipstr, datagram.data, datagram.data_len);
		}
		socket.dest_addr_len = 0;
	}

	proc_queue.clear();
}
