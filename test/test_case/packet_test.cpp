#include "utcp/rudp.h"
#include "utcp/rudp_def.h"
#include "gtest/gtest.h"
#include <memory>

static uint8_t packet_hello[] = {0x80, 0x00, 0xb5, 0x78, 1,	   0,	 0,	   0,	 0xfe, 0x6f, 2, 0xe0, 0xe2, 0xff,
								 0x02, 0x50, 0,	   0x20, 0x60, 0xa6, 0x0f, 0x93, 0x11, 0,	 0, 0,	  0x60};
static std::vector<char> last_send;

struct packet : public ::testing::Test
{
	virtual void SetUp() override
	{
		rudp_env_init();
		rudp_env_setcallback([](struct rudp_fd* fd, void* userdata, enum callback_type callback_type, const void* buffer, int len) {
			switch (callback_type)
			{
			case callback_newconn:
				break;
			case callback_reconn:
				break;
			case callback_send:
				last_send.insert(last_send.end(), (const char*)buffer, (const char*)buffer + len);
				break;
			}
			return 0;
		});
	}

	virtual void TearDown() override
	{
		rudp_env_init();
	}
};

TEST_F(packet, accept)
{
	std::unique_ptr<rudp_fd> fd(new rudp_fd);
	rudp_init(fd.get(), nullptr, false);
	rudp_sequence_init(fd.get(), 12054, 10245);
	rudp_incoming(fd.get(), (char*)packet_hello, sizeof(packet_hello));
}
