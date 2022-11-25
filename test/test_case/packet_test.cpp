﻿
#include "test_utils.h"
extern "C" {
#include "utcp/utcp_handshake.h"
}
#include "gtest/gtest.h"
#include <memory>

static std::vector<uint8_t> last_send;
static std::vector<struct utcp_bunch> last_recv;

struct packet : public ::testing::Test
{
	virtual void SetUp() override
	{
		last_send.clear();
		last_recv.clear();

		auto config = utcp_get_config();
		config->on_outgoing = [](void* fd, void* userdata, const void* data, int len) {
			last_send.insert(last_send.end(), (const char*)data, (const char*)data + len);
		};
		config->on_recv_bunch = [](struct utcp_connection* fd, void* userdata, struct utcp_bunch* const bunches[], int count) {
			for (int i = 0; i < count; ++i)
			{
				last_recv.push_back(*bunches[i]);
			}
		};
	}

	virtual void TearDown() override
	{
		auto config = utcp_get_config();
		config->on_outgoing = nullptr;
		config->on_recv_bunch = nullptr;
	}
};

#pragma pack(1)
// DEFINE_CONTROL_CHANNEL_MESSAGE(Hello, 0, uint8, uint32, FString);
struct Hello
{
	uint8_t MsgType;
	uint8_t IsLittleEndian;
	uint32_t RemoteNetworkVersion;
	uint32_t EncryptionTokenStrLen;
};
#pragma pack()

TEST_F(packet, accept_hello)
{
	uint8_t packet_hello[] = {0x80, 0x00, 0xb5, 0x78, 0x01, 0x00, 0x00, 0x00, 0xfe, 0x6f, 0x02, 0xe0, 0xe2, 0xff,
							  0x02, 0x50, 0x00, 0x20, 0x60, 0xa6, 0x0f, 0x93, 0x11, 0x00, 0x00, 0x00, 0x60};

	utcp_connection_rtti fd;
	utcp_sequence_init(fd.get(), 12054, 10245);

	int32_t packet_id = utcp_peep_packet_id(fd.get(), packet_hello, sizeof(packet_hello));
	ASSERT_EQ(packet_id, 12054);

	ASSERT_TRUE(utcp_incoming(fd.get(), packet_hello, sizeof(packet_hello)));

	ASSERT_EQ(last_recv.size(), 1);
	ASSERT_EQ(last_recv[0].bOpen, 1);
	ASSERT_EQ(last_recv[0].bClose, 0);
	ASSERT_EQ(last_recv[0].DataBitsLen, sizeof(Hello) * 8);

	auto hello = (Hello*)(last_recv[0].Data);
	ASSERT_EQ(hello->MsgType, 0);
	ASSERT_EQ(hello->IsLittleEndian, 1);
	ASSERT_EQ(hello->RemoteNetworkVersion, 2358803763);
	ASSERT_EQ(hello->EncryptionTokenStrLen, 0);

	ASSERT_FALSE(utcp_incoming(fd.get(), packet_hello, sizeof(packet_hello)));
}
/*
TEST_F(packet, write_hello)
{
	uint8_t packet_hello[] = {0x80, 0x00, 0xb5, 0x78, 0x01, 0x00, 0x00, 0x00, 0xfe, 0x6f, 0x02, 0xe0, 0xe2, 0xff,
							  0x02, 0x50, 0x00, 0x20, 0x60, 0xa6, 0x0f, 0x93, 0x11, 0x00, 0x00, 0x00, 0x60};

	utcp_fd_rtti fd;
	utcp_init(fd.get(), nullptr);
	utcp_sequence_init(fd.get(), 10245, 12054);

	Hello hello;
	hello.MsgType = 0;
	hello.IsLittleEndian = 1;
	hello.RemoteNetworkVersion = 2358803763;
	hello.EncryptionTokenStrLen = 0;

	struct utcp_bunch bunch;
	memset(&bunch, 0, sizeof(bunch));
	bunch.NameIndex = 255;
	bunch.ChIndex = 0;
	bunch.bReliable = 1;
	bunch.bOpen = 1;
	bunch.DataBitsLen = sizeof(hello) * 8;
	memcpy(bunch.Data, &hello, sizeof(hello));
	utcp_send_bunch(fd.get(), &bunch);
	utcp_send_flush(fd.get());
	for (int i = 0; i < last_send.size(); ++i)
	{
		int ii = last_send[i];
		int j = packet_hello[i];
		if (ii != j)
		{
			printf("");
		}
	}
	ASSERT_EQ(sizeof(packet_hello), last_send.size());
	ASSERT_EQ(0, memcmp(packet_hello, last_send.data(), sizeof(packet_hello)));
}
*/
TEST_F(packet, accept_hello_login)
{

	uint8_t packet_hello[] = {0x0, 0x40, 0x18, 0x20, 0x0, 0x0, 0x0, 0x0, 0xFE, 0x6F, 0x2, 0x80, 0x80, 0xFF, 0x2, 0x50, 0x0, 0x20, 0x60, 0xA6, 0xF, 0x93, 0x11, 0x0, 0x0, 0x0, 0x60};
	uint8_t packet_challenge[] = {0x60, 0x80, 0x10, 0x10, 0x2,	0x0,  0x0,	0x0,  0x10, 0x0,  0x2,	0xFE, 0x17, 0x80, 0x4,	0x3,  0xD,
								  0x0,	0x0,  0x0,	0x32, 0x35, 0x34, 0x31, 0x39, 0x32, 0x31, 0x38, 0x31, 0x36, 0x36, 0x38, 0x38, 0x3};
	uint8_t packet_login[] = {0x20, 0x40, 0x28, 0x20, 0x2,	0x0,  0x0,	0x0,  0x76, 0x87, 0x0,	0x28, 0xE0, 0xBF, 0x0,	0x2,  0x29, 0x10, 0x0,	0x0,  0x0,	0x80, 0x1,	0xB8, 0x1,
							  0x0,	0x0,  0xF8, 0x71, 0xA,	0x6B, 0x2B, 0xEB, 0x9,	0x93, 0x1B, 0x43, 0x2B, 0x93, 0x23, 0x1B, 0x43, 0x2B, 0x73, 0x6B, 0x81, 0x1A, 0x82, 0x69, 0xB1,
							  0x29, 0x9A, 0xC9, 0x29, 0xBA, 0xB9, 0xB1, 0xA1, 0xA1, 0x11, 0x92, 0x91, 0xB1, 0x91, 0xC9, 0x81, 0xA1, 0x91, 0x19, 0xBA, 0xB9, 0xC9, 0x99, 0xB1, 0x99,
							  0xC9, 0x11, 0x92, 0x19, 0x82, 0xB9, 0x1,	0x40, 0x88, 0x1,  0x0,	0x0,  0x8,	0x93, 0x1B, 0x43, 0x2B, 0x93, 0x23, 0x1B, 0x43, 0x2B, 0x73, 0x6B, 0x81,
							  0x1A, 0x82, 0x69, 0xB1, 0x29, 0x9A, 0xC9, 0x29, 0xBA, 0xB9, 0xB1, 0xA1, 0xA1, 0x11, 0x92, 0x91, 0xB1, 0x91, 0xC9, 0x81, 0xA1, 0x91, 0x19, 0xBA, 0xB9,
							  0xC9, 0x99, 0xB1, 0x99, 0xC9, 0x11, 0x92, 0x19, 0x82, 0xB9, 0x1,	0x28, 0x0,	0x0,  0x0,	0x70, 0xAA, 0x62, 0x62, 0x2,  0x18};

	uint8_t packet_welcome[] = {0xA0, 0x80, 0x18, 0x10, 0xA,  0x0,	0x0,  0x0,	0x10, 0x0,	0x3,  0xFE, 0x17, 0x80, 0x16, 0x1,	0x2E, 0x0,	0x0,  0x0,	0x2F, 0x47,
								0x61, 0x6D, 0x65, 0x2F, 0x54, 0x68, 0x69, 0x72, 0x64, 0x50, 0x65, 0x72, 0x73, 0x6F, 0x6E, 0x2F, 0x4D, 0x61, 0x70, 0x73, 0x2F, 0x55,
								0x45, 0x44, 0x50, 0x49, 0x45, 0x5F, 0x30, 0x5F, 0x54, 0x68, 0x69, 0x72, 0x64, 0x50, 0x65, 0x72, 0x73, 0x6F, 0x6E, 0x4D, 0x61, 0x70,
								0x1F, 0x0,	0x0,  0x0,	0x2F, 0x53, 0x63, 0x72, 0x69, 0x70, 0x74, 0x2F, 0x45, 0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65, 0x2E, 0x45, 0x78,
								0x61, 0x6D, 0x70, 0x6C, 0x65, 0x47, 0x61, 0x6D, 0x65, 0x4D, 0x6F, 0x64, 0x65, 0x0,	0x0,  0x0,	0x0,  0x3};

	uint8_t packet_netspeed[] = {0x60, 0x40, 0x38, 0x20, 0xE, 0x0, 0x0, 0x0, 0x72, 0x88, 0x0, 0x30, 0xE0, 0xBF, 0x0, 0xA, 0x20, 0x0, 0x35, 0xC, 0x0, 0x18};

	utcp_connection_rtti fd;
	utcp_sequence_init(fd.get(), 1027, 513);

	struct utcp_bunch bunch;
	memset(&bunch, 0, sizeof(bunch));
	bunch.NameIndex = 255;
	bunch.ChIndex = 0;
	bunch.bReliable = 1;

	ASSERT_TRUE(utcp_incoming(fd.get(), packet_hello, sizeof(packet_hello)));
	ASSERT_EQ(last_recv.size(), 1);

	bunch.DataBitsLen = sizeof(packet_challenge) * 8;
	memcpy(bunch.Data, packet_challenge, sizeof(packet_challenge));
	auto ret = utcp_send_bunch(fd.get(), &bunch);
	ASSERT_NE(ret, -1);

	utcp_send_flush(fd.get());

	ASSERT_TRUE(utcp_incoming(fd.get(), packet_login, sizeof(packet_login)));
	ASSERT_EQ(last_recv.size(), 2);

	bunch.DataBitsLen = sizeof(packet_welcome) * 8;
	memcpy(bunch.Data, packet_welcome, sizeof(packet_welcome));
	ret = utcp_send_bunch(fd.get(), &bunch);
	ASSERT_NE(ret, -1);

	utcp_send_flush(fd.get());
}
