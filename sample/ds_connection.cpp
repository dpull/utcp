﻿#include "ds_connection.h"
#include "utils.h"
extern "C" {
#include "utcp/bit_buffer.h"
}
#include <cassert>
#include <cstring>

void ds_connection::bind(socket_t fd, struct sockaddr_storage* addr, socklen_t addr_len)
{
	memcpy(&dest_addr, addr, addr_len);
	dest_addr_len = addr_len;
	socket_fd = fd;
}

void ds_connection::on_outgoing(const void* data, int len)
{
	assert(dest_addr_len > 0);
	sendto(socket_fd, (const char*)data, len, 0, (sockaddr*)&dest_addr, dest_addr_len);
}

void ds_connection::on_recv_bunch(struct utcp_bunch* const bunches[], int count)
{
	assert(count == 1);
	if (bunches[0]->DataBitsLen == 0)
	{
		assert(bunches[0]->bClose);
		log("channel close");
		return;
	}

	codec.reset(const_cast<uint8_t*>(bunches[0]->Data), bunches[0]->DataBitsLen / 8);

	uint8_t msg_type;
	codec >> msg_type;
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
		log("on_recv msg_type=%hhd\n", msg_type);
	}
}

void ds_connection::on_delivery_status(int32_t packet_id, bool ack)
{
	log("on_delivery_status:%d %s\n", packet_id, ack ? "ACK" : "NAK");
}

void ds_connection::send_data()
{
	utcp::large_bunch bunch;
	memset(&bunch, 0, sizeof(bunch));
	bunch.NameIndex = 255;
	bunch.ChIndex = 0;
	bunch.bReliable = 1;
	bunch.ExtDataBitsLen = 0;

	auto len = uint16_t(codec.pos - send_buffer);
	bunch.DataBitsLen = len * 8;
	memcpy(bunch.Data, send_buffer, len);

	auto ret = send_bunch(&bunch);
	log("send bunch %d\n", ret.first);
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
