#include "utcp/rudp.h"
#include "utcp/rudp_def.h"
#include "gtest/gtest.h"
#include <memory>

static std::vector<char> last_send;
static std::vector<struct rudp_bunch> last_recv;

struct packet : public ::testing::Test
{
	virtual void SetUp() override
	{
		last_send.clear();
		last_recv.clear();

		auto config = rudp_get_config();
		config->on_raw_send = [](struct rudp_fd* fd, void* userdata, const void* data, int len) {
			last_send.insert(last_send.end(), (const char*)data, (const char*)data + len);
		};
		config->on_recv = [](struct rudp_fd* fd, void* userdata, const struct rudp_bunch* bunches[], int count) {
			for (int i = 0; i < count; ++i)
			{
				last_recv.push_back(*bunches[i]);
			}
		};
	}

	virtual void TearDown() override
	{
		auto config = rudp_get_config();
		config->on_raw_send = nullptr;
		config->on_recv = nullptr;
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

	std::unique_ptr<rudp_fd> fd(new rudp_fd);
	rudp_init(fd.get(), nullptr, false);
	rudp_sequence_init(fd.get(), 12054, 10245);
	uint8_t packet_hello[] = {0x80, 0x00, 0xb5, 0x78, 1,	0,	  0,	0,	  0xfe, 0x6f, 2, 0xe0, 0xe2, 0xff,
							  0x02, 0x50, 0,	0x20, 0x60, 0xa6, 0x0f, 0x93, 0x11, 0,	  0, 0,	   0x60};
	rudp_incoming(fd.get(), (char*)packet_hello, sizeof(packet_hello));

	ASSERT_EQ(last_recv.size(), 1);
	ASSERT_EQ(last_recv[0].bOpen, 1);
	ASSERT_EQ(last_recv[0].bClose, 0);
	ASSERT_EQ(last_recv[0].DataBitsLen, sizeof(Hello) * 8);

	auto hello = (Hello*)(last_recv[0].Data);
	ASSERT_EQ(hello->MsgType, 0);
	ASSERT_EQ(hello->IsLittleEndian, 1);
	ASSERT_EQ(hello->RemoteNetworkVersion, 2358803763);
	ASSERT_EQ(hello->EncryptionTokenStrLen, 0);
}
