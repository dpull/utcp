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

utcp_listener::utcp_listener() : utcp_connection(false)
{
}

utcp_listener::~utcp_listener()
{
	recv_thread_exit_flag = true;
	if (recv_thread)
	{
		recv_thread->join();
		delete recv_thread;
	}

	if (socket_fd != INVALID_SOCKET)
	{
		closesocket(socket_fd);
	}
}

bool utcp_listener::listen(const char* ip, int port)
{
	assert(socket_fd == INVALID_SOCKET);
	assert(!recv_thread);

	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_fd == INVALID_SOCKET)
		return false;

	int one = 1;
	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one)) == SOCKET_ERROR)
		return false;

	memset(&dest_addr, 0, sizeof(dest_addr));
	struct sockaddr_in* addripv4 = (struct sockaddr_in*)&dest_addr;
	addripv4->sin_family = AF_INET;
	addripv4->sin_addr.s_addr = inet_addr(ip);
	addripv4->sin_port = htons(port);
	dest_addr_len = sizeof(*addripv4);

	if (bind(socket_fd, (struct sockaddr*)&dest_addr, dest_addr_len) == SOCKET_ERROR)
		return false;

	memset(&dest_addr, 0, sizeof(dest_addr));
	dest_addr_len = 0;

	create_recv_thread();
	now = std::chrono::high_resolution_clock::now();
	return true;
}

void utcp_listener::create_recv_thread()
{
	recv_thread_exit_flag = false;

	recv_thread = new std::thread([this]() {
		struct sockaddr_storage from_addr;
		uint8_t buffer[MaxPacket * 2];

		while (!this->recv_thread_exit_flag)
		{
			socklen_t addr_len = sizeof(from_addr);
			ssize_t ret = ::recvfrom(socket_fd, (char*)buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &addr_len);
			if (ret <= 0)
				continue;
			proc_recv(buffer, (int)ret, &from_addr, addr_len);
		}
	});
}

void utcp_listener::proc_recv(uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len)
{
	auto view = new utcp_packet_view;
	view->handle = 0;
	memcpy(view->data, data, data_len);
	view->data_len = data_len;
	memcpy(&view->from_addr, from_addr, from_addr_len);
	view->from_addr_len = from_addr_len;

	{
		std::lock_guard<decltype(recv_queue_mutex)> lock(recv_queue_mutex);
		recv_queue.push_back(view);
	}
}

void utcp_listener::proc_recv_queue()
{
	{
		std::lock_guard<decltype(recv_queue_mutex)> lock(recv_queue_mutex);
		recv_queue.swap(proc_queue);
	}

	char ipstr[NI_MAXHOST + 8];
	for (auto view : proc_queue)
	{
		assert(view->from_addr_len == sizeof(sockaddr_in));
		auto it = clients.find(*(sockaddr_in*)&view->from_addr);
		if (it != clients.end())
		{
			it->second->raw_recv(view);
			continue;
		}

		if (sockaddr2str((sockaddr_in*)&view->from_addr, ipstr, sizeof(ipstr)))
		{
			dump("listener recv", 0, view->data, view->data_len);

			dest_addr_len = view->from_addr_len;
			memcpy(&dest_addr, &view->from_addr, dest_addr_len);
			utcp_connectionless_incoming(&rudp, ipstr, view->data, view->data_len);
		}
		delete view;
		dest_addr_len = 0;
	}
	proc_queue.clear();
}

void utcp_listener::tick()
{
	auto cur_now = std::chrono::high_resolution_clock::now();
	utcp_add_time((cur_now - now).count());
	now = cur_now;

	utcp_update(&rudp);

	proc_recv_queue();
	for (auto& it : clients)
	{
		it.second->tick();
	}
}

void utcp_listener::after_tick()
{
	proc_recv_queue();
	for (auto& it : clients)
	{
		it.second->after_tick();
	}
}

void utcp_listener::on_accept(bool reconnect)
{
	utcp_connection* conn = nullptr;
	if (!reconnect)
	{
		conn = new ds_connection();
	}
	else
	{
		for (auto it = clients.begin(); it != clients.end(); ++it)
		{
			if (it->second->match(this))
			{
				conn = it->second;
				clients.erase(it);
				break;
			}
		}
	}

	auto it = clients.insert(std::make_pair(*(sockaddr_in*)&dest_addr, conn));
	assert(it.second);

	conn->accept(this, reconnect);
}
