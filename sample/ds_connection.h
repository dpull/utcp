#pragma once
#include "abstract/utcp.hpp"
#include "socket.h"
#include <cassert>
#include <cstring>
#include <string>

struct ue_codec
{
	void reset(uint8_t* data, size_t len);

	ue_codec& operator<<(const uint8_t value);
	ue_codec& operator>>(uint8_t& value);

	ue_codec& operator<<(const uint32_t value);
	ue_codec& operator>>(uint32_t& value);

	ue_codec& operator<<(const std::string& value);
	ue_codec& operator>>(std::string& value);

	uint8_t* pos;
	uint8_t* end;
};

class ds_connection : public utcp::conn
{
  public:
	void bind(socket_t fd, struct sockaddr_storage* addr, socklen_t addr_len);
	virtual void update() override;
	virtual void send_flush() override;

  protected:
	virtual void on_disconnect(int close_reason) override;
	virtual void on_outgoing(const void* data, int len) override;
	virtual void on_recv_bunch(struct utcp_bunch* const bunches[], int count) override;
	virtual void on_delivery_status(int32_t packet_id, bool ack) override;

  private:
	void send_data();
	void on_msg_hello();
	void on_msg_login();
	void on_msg_netspeed();

  private:
	std::string challenge;
	ue_codec codec;
	socket_t socket_fd = INVALID_SOCKET;
	struct sockaddr_storage dest_addr;
	socklen_t dest_addr_len = 0;
	uint8_t send_buffer[UDP_MTU_SIZE];
	bool disconnect = false;
};
