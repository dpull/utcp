#pragma once
#include "abstract/utcp.hpp"
#include "udp_socket.h"
#include <cassert>
#include <cstring>
#include <string>

class echo_connection : public utcp::conn
{
  public:
	void bind(socket_t fd, struct sockaddr_storage* addr, socklen_t addr_len, bool has_watermark);
	bool async_connnect(const char* ip, int port);
	void send(int num);

	virtual void update() override;

  protected:
	virtual void on_connect(bool reconnect) override;
	virtual void on_disconnect(int close_reason) override;
	virtual void on_outgoing(const void* data, int len) override;
	virtual void on_recv_bunch(struct utcp_bunch* const bunches[], int count) override;
	virtual void on_delivery_status(int32_t packet_id, bool ack) override;

	void proc_recv_queue();

  private:
	udp_socket socket;
};
