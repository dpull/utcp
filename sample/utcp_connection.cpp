#include "utcp_connection.h"
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

utcp_connection::~utcp_connection()
{
	recv_thread_exit_flag = true;
	if (recv_thread_exit_flag)
	{
		recv_thread->join();
		delete recv_thread;
	}

	if (socket_fd != INVALID_SOCKET)
	{
		closesocket(socket_fd);
	}
}

bool utcp_connection::listen(const char* ip, int port)
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
	return true;
}

bool utcp_connection::connnect(const char* ip, int port)
{
	assert(socket_fd == INVALID_SOCKET);
	assert(!recv_thread);

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

void utcp_connection::tick()
{
	auto view = try_get_packet(1);
	if (!view)
		return;
	printf("tick: %d %s\n", view->handle, view->data);
	delete view;
}

void utcp_connection::after_tick()
{
	while (auto view = get_packet())
	{
		printf("after_tick: %d %s\n", view->handle, view->data);
		delete view;
	}
	printf("after_tick\n");
}

// > 0 packet id
// = 0 send failed
// < 0 tmp pocket id
int utcp_connection::send(char* bunch, int count)
{
	::sendto(socket_fd, bunch, count, 0, (struct sockaddr*)&dest_addr, dest_addr_len);
	return 0;
}

void utcp_connection::create_recv_thread()
{
	recv_thread_exit_flag = false;

	recv_thread = new std::thread([this]() {
		struct sockaddr_storage from_addr;
		uint8_t buffer[MAX_PACKET_BUFFER_SIZE];

		while (!this->recv_thread_exit_flag)
		{
			socklen_t addr_len = sizeof(from_addr);
			ssize_t ret = ::recvfrom(socket_fd, (char*)buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &addr_len);
			if (ret <= 0)
				continue;
			this->raw_recv(buffer, (int)ret, &from_addr, addr_len);
		}
	});
}

void utcp_connection::raw_recv(uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len)
{
	static int test_handle = 0;

	auto view = new utcp_packet_view;
	view->handle = ++test_handle;
	memcpy(view->data, data, data_len);
	view->data_len = data_len;
	memcpy(&view->from_addr, from_addr, from_addr_len);
	view->from_addr_len = from_addr_len;

	{
		std::lock_guard<decltype(recv_queue_mutex)> lock(recv_queue_mutex);
		recv_queue.push(view);
	}
}

utcp_packet_view* utcp_connection::try_get_packet(int handle)
{
	std::lock_guard<decltype(recv_queue_mutex)> lock(recv_queue_mutex);
	if (recv_queue.empty())
		return nullptr;
	auto view = recv_queue.top();
	if (view->handle != handle)
		return nullptr;
	recv_queue.pop();
	return view;
}

utcp_packet_view* utcp_connection::get_packet()
{
	std::lock_guard<decltype(recv_queue_mutex)> lock(recv_queue_mutex);
	if (recv_queue.empty())
		return nullptr;
	auto view = recv_queue.top();
	recv_queue.pop();
	return view;
}

void utcp_listener::tick()
{
	{
		std::lock_guard<decltype(recv_queue_mutex)> lock(recv_queue_mutex);
		recv_unordered_queue.swap(proc_unordered_queue);
	}
	for (auto view : proc_unordered_queue)
	{
		printf("listener::tick: %d %s\n", view->handle, view->data);
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
		it->second->raw_recv(data, data_len, from_addr, from_addr_len);
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
