#include "echo_connection.h"
#include "sample_config.h"
#include <cassert>
#include <cstring>

void echo_connection::bind(socket_t fd, struct sockaddr_storage* addr, socklen_t addr_len)
{
	memcpy(&socket.dest_addr, addr, addr_len);
	socket.dest_addr_len = addr_len;
	socket.socket_fd = fd;
	set_debug_name("server");
}

bool echo_connection::async_connnect(const char* ip, int port)
{
	if (!socket.connnect(ip, port))
		return false;

	connect();
	set_debug_name("client");
	return true;
}

void echo_connection::send(int num)
{
	utcp::large_bunch bunch((uint8_t*)&num, sizeof(num) * 8);
	bunch.NameIndex = 255;
	bunch.ChIndex = 0;
	bunch.bOpen = 1;
	bunch.bReliable = 1;
	bunch.ExtDataBitsLen = 0;
	bunch.bPartial = 1;
	bunch.bPartialInitial = 1;

	auto ret = send_bunch(&bunch);
	log(log_level::Log, "[%s]send%d\t%d", this->debug_name(), ret.first, num);
	send_flush();

	bunch.bPartialInitial = 0;
	ret = send_bunch(&bunch);
	send_flush();

	bunch.bPartialFinal = 1;
	ret = send_bunch(&bunch);
	send_flush();
}

void echo_connection::update()
{
	proc_recv_queue();
	utcp::conn::update();
}

void echo_connection::on_connect(bool reconnect)
{
	send(1);
}

void echo_connection::on_disconnect(int close_reason)
{
}

void echo_connection::on_outgoing(const void* data, int len)
{
	int loss = rand() % 100;
	if (g_config->outgoing_loss > 0 && loss < g_config->outgoing_loss)
	{
		log(log_level::Log, "[%s]out loss", this->debug_name());
		return;
	}

	assert(socket.dest_addr_len > 0);
	sendto(socket.socket_fd, (const char*)data, len, 0, (sockaddr*)&socket.dest_addr, socket.dest_addr_len);
}

void echo_connection::on_recv_bunch(struct utcp_bunch* const bunches[], int count)
{
	int num;

	assert(count == 3);
	assert(bunches[0]->DataBitsLen == sizeof(num) * 8);
	memcpy(&num, bunches[0]->Data, sizeof(num));
	send(num + 1);
}

void echo_connection::on_delivery_status(int32_t packet_id, bool ack)
{
	log(log_level::Log, "[%s]delivery %d %s", this->debug_name(), packet_id, ack ? "ACK" : "NAK");
}

void echo_connection::proc_recv_queue()
{
	auto& proc_queue = socket.swap();

	for (auto& datagram : proc_queue)
	{
		incoming(datagram.data, datagram.data_len);
	}

	proc_queue.clear();
}
