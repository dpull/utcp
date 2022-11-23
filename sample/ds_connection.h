#pragma once
#include "utcp_connection.h"
#include <cassert>
#include <cstring>
#include <string>

struct encode
{
	void reset(uint8_t* data, size_t len)
	{
		this->pos = data;
		this->end = data + len;
	}

	encode& operator<<(const uint8_t value)
	{
		assert(pos + sizeof(value) < end);
		*pos = value;
		pos++;
		return *this;
	}

	encode& operator>>(uint8_t& value)
	{
		assert(pos + sizeof(value) < end);
		value = *pos;
		pos++;
		return *this;
	}

	encode& operator<<(const uint32_t value)
	{
		assert(pos + sizeof(value) <= end);
		*((uint32_t*)pos) = value;
		pos += sizeof(value);
		return *this;
	}

	encode& operator>>(uint32_t& value)
	{
		assert(pos + sizeof(value) <= end);
		value = *((uint32_t*)pos);
		pos += sizeof(value);
		return *this;
	}

	encode& operator<<(const std::string& value)
	{
		auto size = (uint32_t)value.size();
		*this << size;
		assert(pos + size <= end);
		memcpy(pos, value.c_str(), size);
		pos += size;
		return *this;
	}
	encode& operator>>(std::string& value)
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

class ds_connection : public utcp_connection
{
  public:
	ds_connection();

  protected:
	virtual void on_recv(struct utcp_bunch* const bunches[], int count) override;
	virtual void on_delivery_status(int32_t packet_id, bool ack) override;

  private:
	void send_data();
	void on_msg_hello();
	void on_msg_login();
	void on_msg_netspeed();

  private:
	std::string challenge;
	uint8_t send_buffer[UTCP_MAX_PACKET];
	encode coder;
};
