#pragma once
#include "abstract/utcp.hpp"
#include "socket.h"
#include <cassert>
#include <cstring>
#include <string>

struct ue_codec
{
	void reset(uint8_t* data, size_t len)
	{
		this->pos = data;
		this->end = data + len;
	}

	ue_codec& operator<<(const uint8_t value)
	{
		assert(pos + sizeof(value) < end);
		*pos = value;
		pos++;
		return *this;
	}

	ue_codec& operator>>(uint8_t& value)
	{
		assert(pos + sizeof(value) < end);
		value = *pos;
		pos++;
		return *this;
	}

	ue_codec& operator<<(const uint32_t value)
	{
		assert(pos + sizeof(value) <= end);
		*((uint32_t*)pos) = value;
		pos += sizeof(value);
		return *this;
	}

	ue_codec& operator>>(uint32_t& value)
	{
		assert(pos + sizeof(value) <= end);
		value = *((uint32_t*)pos);
		pos += sizeof(value);
		return *this;
	}

	ue_codec& operator<<(const std::string& value)
	{
		auto size = (uint32_t)value.size();
		*this << size;
		assert(pos + size <= end);
		memcpy(pos, value.c_str(), size);
		pos += size;
		return *this;
	}
	ue_codec& operator>>(std::string& value)
	{
		uint32_t size;
		*this >> size;
		assert(pos + size <= end);
		value = std::string((char*)pos, size);
		pos += size;
		return *this;
	}

	uint8_t* pos;
	uint8_t* end;
};

class ds_connection : public utcp::conn
{
  public:
	void bind(socket_t fd, struct sockaddr_storage* addr, socklen_t addr_len);

  protected:
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
};
