#include "udp_socket.h"
#include <cassert>

#ifdef _MSC_VER
struct WSAGuard
{
	WSAGuard()
	{
		WSADATA wsadata;
		WSAStartup(MAKEWORD(2, 2), &wsadata);
	}
	~WSAGuard()
	{
		WSACleanup();
	}
};
static WSAGuard _WSAGuard;
#endif

udp_socket::udp_socket()
{
	recv_queue.reserve(128);
	proc_queue.reserve(128);
}

udp_socket::~udp_socket()
{
	recv_thread_exit_flag = true;
	recv_thread.join();
}

bool udp_socket::listen(const char* ip, int port)
{
	assert(socket_fd == INVALID_SOCKET);

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
	return true;
}

bool udp_socket::connnect(const char* ip, int port)
{
	assert(socket_fd == INVALID_SOCKET);

	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_fd == INVALID_SOCKET)
		return false;

	memset(&dest_addr, 0, sizeof(dest_addr));
	struct sockaddr_in* addripv4 = (struct sockaddr_in*)&dest_addr;
	addripv4->sin_family = AF_INET;
	addripv4->sin_addr.s_addr = inet_addr(ip);
	addripv4->sin_port = htons(port);
	dest_addr_len = sizeof(*addripv4);

	create_recv_thread();
	return true;
}

std::vector<udp_datagram>& udp_socket::swap()
{
	std::lock_guard<decltype(recv_queue_mutex)> lock(recv_queue_mutex);
	recv_queue.swap(proc_queue);
	return proc_queue;
}

void udp_socket::create_recv_thread()
{
	recv_thread_exit_flag = false;
	recv_thread = std::thread([this]() {
		struct sockaddr_storage from_addr;
		uint8_t buffer[UDP_DATAGRAM_SIZE];

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

void udp_socket::proc_recv(uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len)
{
	std::lock_guard<decltype(recv_queue_mutex)> lock(recv_queue_mutex);
	recv_queue.emplace_back(data, data_len, from_addr, from_addr_len);
}
