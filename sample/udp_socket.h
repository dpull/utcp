#pragma once
#include "socket.h"
#include <mutex>
#include <thread>
#include <vector>

constexpr int UDP_DATAGRAM_SIZE = 2000;

struct udp_datagram
{
	struct sockaddr_storage from_addr;
	socklen_t from_addr_len;
	uint16_t data_len;
	uint8_t data[UDP_DATAGRAM_SIZE];

	udp_datagram(uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len)
	{
		memcpy(this->data, data, data_len);
		this->data_len = data_len;
		memcpy(&this->from_addr, from_addr, from_addr_len);
		this->from_addr_len = from_addr_len;
	}
};

struct udp_socket
{
	udp_socket();
	virtual ~udp_socket();

	bool listen(const char* ip, int port);
	bool connnect(const char* ip, int port);

	std::vector<udp_datagram>& swap();

	volatile int recv_thread_exit_flag = false;
	std::thread recv_thread;

	std::mutex recv_queue_mutex;
	std::vector<udp_datagram> recv_queue;
	std::vector<udp_datagram> proc_queue;

	socket_t socket_fd = INVALID_SOCKET;
	struct sockaddr_storage dest_addr;
	socklen_t dest_addr_len = 0;

  private:
	void create_recv_thread();
	void proc_recv(uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len);
};