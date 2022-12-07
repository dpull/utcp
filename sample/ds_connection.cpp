#include "ds_connection.h"
#include "sample_config.h"
extern "C" {
#include "utcp/bit_buffer.h"
}
#include <cassert>
#include <cstring>

void ue_codec::reset(uint8_t* data, size_t len)
{
	this->pos = data;
	this->end = data + len;
}

ue_codec& ue_codec::operator<<(const uint8_t value)
{
	assert(pos + sizeof(value) < end);
	*pos = value;
	pos++;
	return *this;
}

ue_codec& ue_codec::operator<<(const uint32_t value)
{
	assert(pos + sizeof(value) <= end);
	*((uint32_t*)pos) = value;
	pos += sizeof(value);
	return *this;
}

ue_codec& ue_codec::operator<<(const std::string& value)
{
	auto size = (uint32_t)value.size();
	*this << size;
	assert(pos + size <= end);
	memcpy(pos, value.c_str(), size);
	pos += size;
	return *this;
}

ue_codec& ue_codec::operator>>(uint8_t& value)
{
	assert(pos + sizeof(value) < end);
	value = *pos;
	pos++;
	return *this;
}

ue_codec& ue_codec::operator>>(uint32_t& value)
{
	assert(pos + sizeof(value) <= end);
	value = *((uint32_t*)pos);
	pos += sizeof(value);
	return *this;
}

ue_codec& ue_codec::operator>>(std::string& value)
{
	uint32_t size;
	*this >> size;
	assert(pos + size <= end);
	value = std::string((char*)pos, size);
	pos += size;
	return *this;
}

void ds_connection::bind(socket_t fd, struct sockaddr_storage* addr, socklen_t addr_len, bool has_watermark)
{
	this->has_watermark = has_watermark;
	memcpy(&dest_addr, addr, addr_len);
	dest_addr_len = addr_len;
	socket_fd = fd;
	set_debug_name("server");
}

void ds_connection::incoming(uint8_t* data, int count)
{
	if (has_watermark)
	{
		data += 8;
		count -= 8;
	}
	if (g_config->is_gp)
	{
		// FIpConnectionID
		data += 4;
		count -= 4;
	}
	utcp::conn::incoming(data, count);
}

void ds_connection::on_disconnect(int close_reason)
{
}

void ds_connection::on_outgoing(const void* data, int len)
{
	assert(dest_addr_len > 0);
	if (g_config->is_gp)
	{
		char real_send_buffer[UDP_MTU_SIZE];
		real_send_buffer[0] = 0;
		memcpy(real_send_buffer + 1, data, len);
		sendto(socket_fd, real_send_buffer, len + 1, 0, (sockaddr*)&dest_addr, dest_addr_len);
	}
	else
	{
		sendto(socket_fd, (const char*)data, len, 0, (sockaddr*)&dest_addr, dest_addr_len);
	}
}

void ds_connection::on_recv_bunch(struct utcp_bunch* const bunches[], int count)
{
	assert(count == 1);
	if (bunches[0]->DataBitsLen == 0)
	{
		assert(bunches[0]->bClose);
		log(log_level::Warning, "channel close");
		return;
	}

	codec.reset(const_cast<uint8_t*>(bunches[0]->Data), bunches[0]->DataBitsLen / 8);

	uint8_t msg_type;
	codec >> msg_type;

	log(log_level::Verbose, "on_recv msg_type=%hhd\n", msg_type);
	switch (msg_type)
	{
	case 0:
		on_msg_hello();
		break;

	case 4:
		on_msg_netspeed();
		break;
	case 5:
		on_msg_login();
		break;

	default:
		log(log_level::Warning, "on_recv unknown msg_type=%hhd\n", msg_type);
	}
}

void ds_connection::on_delivery_status(int32_t packet_id, bool ack)
{
	log(log_level::Verbose, "on_delivery_status:%d %s\n", packet_id, ack ? "ACK" : "NAK");
}

void ds_connection::send_data()
{
	auto len = uint16_t(codec.pos - send_buffer);
	utcp::large_bunch bunch(send_buffer, len);
	bunch.ChType = 2;
	bunch.ChIndex = 0;
	bunch.bReliable = 1;
	bunch.ExtDataBitsLen = 0;

	auto ret = send_bunch(&bunch);
	log(log_level::Verbose, "send bunch %d\n", ret.first);
}

// DEFINE_CONTROL_CHANNEL_MESSAGE(Hello, 0, uint8, uint32, FString); // initial client connection message
// DEFINE_CONTROL_CHANNEL_MESSAGE(Challenge, 3, FString); // server sends client challenge string to verify integrity
void ds_connection::on_msg_hello()
{
	uint8_t IsLittleEndian;
	uint32_t RemoteNetworkVersion;
	std::string EncryptionToken;

	codec >> IsLittleEndian >> RemoteNetworkVersion >> EncryptionToken;

	assert(IsLittleEndian);
	if (EncryptionToken.size() == 0)
	{
		challenge = std::to_string((uint64_t)this);

		codec.reset(send_buffer, sizeof(send_buffer));
		codec << (uint8_t)3 << challenge;
		send_data();
	}
}

// DEFINE_CONTROL_CHANNEL_MESSAGE(Login, 5, FString, FString, FUniqueNetIdRepl, FString); // client requests to be admitted to the game
// DEFINE_CONTROL_CHANNEL_MESSAGE(Welcome, 1, FString, FString, FString); // server tells client they're ok'ed to load the server's level
void ds_connection::on_msg_login()
{
	std::string ClientResponse;
	std::string RequestURL;

	codec >> ClientResponse >> RequestURL;

	codec.reset(send_buffer, sizeof(send_buffer));
	codec << (uint8_t)1 << std::string("/Game/ThirdPerson/Maps/UEDPIE_0_ThirdPersonMap") << std::string("/Script/Example.ExampleGameMode") << std::string();
	send_data();
}

// DEFINE_CONTROL_CHANNEL_MESSAGE(Netspeed, 4, int32); // client sends requested transfer rate
void ds_connection::on_msg_netspeed()
{
	uint32_t Rate;
	codec >> Rate;
}
