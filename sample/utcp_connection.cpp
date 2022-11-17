#include "utcp_connection.h"
#include <cassert>
#include <cstring>

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

void log(int level, const char* msg)
{
	printf(msg);
	printf("\n");
}

void utcp_connection::config_rudp()
{
	auto config = rudp_get_config();
	config->on_accept = [](struct rudp_fd* fd, void* userdata, bool reconnect) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		static_cast<utcp_connection*>(userdata)->on_accept(reconnect);
	};
	config->on_raw_send = [](struct rudp_fd* fd, void* userdata, const void* data, int len) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		static_cast<utcp_connection*>(userdata)->on_raw_send(data, len);
	};
	config->on_recv = [](struct rudp_fd* fd, void* userdata, const struct rudp_bunch* bunches[], int count) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		static_cast<utcp_connection*>(userdata)->on_recv(bunches, count);
	};
	config->on_delivery_status = [](struct rudp_fd* fd, void* userdata, int32_t packet_id, bool ack) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		static_cast<utcp_connection*>(userdata)->on_delivery_status(packet_id, ack);
	};
	config->on_log = [](int level, const char* msg) { log(level, msg); };
}

utcp_connection::utcp_connection(bool is_client)
{
	rudp_init(&rudp, this, is_client);
}

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

bool utcp_connection::accept(utcp_connection* server)
{
	memcpy(&dest_addr, &server->dest_addr, server->dest_addr_len);
	dest_addr_len = server->dest_addr_len;

	memcpy(rudp.AuthorisedCookie, server->rudp.AuthorisedCookie, sizeof(rudp.AuthorisedCookie));
	memcpy(rudp.LastChallengeSuccessAddress, server->rudp.LastChallengeSuccessAddress, sizeof(rudp.LastChallengeSuccessAddress));

	socket_fd = server->socket_fd;

	rudp_sequence_init(&rudp, server->rudp.LastClientSequence, server->rudp.LastServerSequence);
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

void utcp_connection::call_raw_recv(utcp_connection* conn, uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len)
{
	conn->raw_recv(data, data_len, from_addr, from_addr_len);
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

void utcp_connection::on_accept(bool reconnect)
{
	assert(false);
}

void utcp_connection::on_raw_send(const void* data, int len)
{
	assert(dest_addr_len > 0);
	sendto(socket_fd, (const char*)data, len, 0, (sockaddr*)&dest_addr, dest_addr_len);
}

void utcp_connection::on_recv(const struct rudp_bunch* bunches[], int count)
{

}

void utcp_connection::on_delivery_status(int32_t packet_id, bool ack)
{

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
