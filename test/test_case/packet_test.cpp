
#include "test_utils.h"
extern "C" {
#include "utcp/utcp_packet.h"
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
	uint8_t packet_hello[] = {0x22, 0x45, 0x13, 0x0, 0x80, 0xA4, 0x4, 0xA, 0x0, 0x2, 0x60, 0x6A, 0x2C, 0xC4, 0x0, 0x0, 0x0, 0x0, 0x6};

	utcp_connection_rtti fd;
	utcp_sequence_init(fd.get(), 8849, 13375);

	int32_t packet_id = utcp_peep_packet_id(fd.get(), packet_hello, sizeof(packet_hello));
	ASSERT_EQ(packet_id, 8849);

	ASSERT_TRUE(utcp_incoming(fd.get(), packet_hello, sizeof(packet_hello)));

	ASSERT_EQ(last_recv.size(), 1);
	ASSERT_EQ(last_recv[0].bOpen, 1);
	ASSERT_EQ(last_recv[0].bClose, 0);
	ASSERT_EQ(last_recv[0].DataBitsLen, sizeof(Hello) * 8);
	
	auto hello = (Hello*)(last_recv[0].Data);
	ASSERT_EQ(hello->MsgType, 0);
	ASSERT_EQ(hello->IsLittleEndian, 1);
	ASSERT_EQ(hello->RemoteNetworkVersion, 1645622576);
	ASSERT_EQ(hello->EncryptionTokenStrLen, 0);

	ASSERT_FALSE(utcp_incoming(fd.get(), packet_hello, sizeof(packet_hello)));
}
