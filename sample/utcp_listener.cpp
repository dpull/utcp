#include "utcp_listener.h"
#include <cassert>
#include <cstring>

utcp_listener::utcp_listener() : utcp_connection(false)
{
}

static bool sockaddr2str(sockaddr_in* addr, char ipstr[], int size)
{
	if (!inet_ntop(addr->sin_family, &addr->sin_addr, ipstr, size))
		return false;
	auto len = strlen(ipstr);
	auto port = ntohs(addr->sin_port);
	snprintf(ipstr + len, size - len, ":%d", port);
	return true;
}

void utcp_listener::tick()
{
	{
		std::lock_guard<decltype(recv_queue_mutex)> lock(recv_queue_mutex);
		recv_unordered_queue.swap(proc_unordered_queue);
	}

	char ipstr[NI_MAXHOST + 8];
	for (auto view : proc_unordered_queue)
	{
		assert(view->from_addr_len == sizeof(sockaddr_in));
		if (sockaddr2str((sockaddr_in*)&view->from_addr, ipstr, sizeof(ipstr)))
		{
			dest_addr_len = view->from_addr_len;
			memcpy(&dest_addr, &view->from_addr, dest_addr_len);
			rudp_connectionless_incoming(&rudp, ipstr, view->data, view->data_len);
		}
		delete view;
		dest_addr_len = 0;
	}
	proc_unordered_queue.clear();
}

void utcp_listener::after_tick()
{
}

int utcp_listener::send(char* bunch, int count)
{
	assert(false);
	return 0;
}

void utcp_listener::raw_recv(uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len)
{
	auto it = clients.find(*(struct sockaddr_in*)&from_addr);
	if (it != clients.end())
	{
		call_raw_recv(it->second, data, data_len, from_addr, from_addr_len);
		return;
	}

	auto view = new utcp_packet_view;
	view->handle = 0;
	memcpy(view->data, data, data_len);
	view->data_len = data_len;
	memcpy(&view->from_addr, from_addr, from_addr_len);
	view->from_addr_len = from_addr_len;

	{
		std::lock_guard<decltype(recv_queue_mutex)> lock(recv_queue_mutex);
		recv_unordered_queue.push_back(view);
	}
}

void utcp_listener::on_accept(bool reconnect)
{
	utcp_connection* conn = nullptr;
	if (!reconnect)
	{
		conn = new utcp_connection(false);
	}
	else
	{
		auto it = clients.find(*(sockaddr_in*)&dest_addr);
		if (it == clients.end())
		{
			assert(false);
			return;
		}
		conn = it->second;
		clients.erase(it);
	}

	auto it = clients.insert(std::make_pair(*(sockaddr_in*)&dest_addr, conn));
	assert(it.second);

	conn->accept(this);
}

void utcp_listener::on_recv(const struct rudp_bunch* bunches[], int count)
{
	assert(false);
}

void utcp_listener::on_delivery_status(int32_t packet_id, bool ack)
{
	assert(false);
}
