#include "utcp/rudp.h"
#include "utcp/rudp_def.h"
#include "gtest/gtest.h"

static int8_t handshake_step1[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8};
static std::vector<char> last_send;
static bool new_conn;

struct handshake : public ::testing::Test
{
	virtual void SetUp() override
	{
		rudp_env_init();
		rudp_env_setcallback([](struct rudp_fd* fd, void* userdata, enum callback_type callback_type, const void* buffer, int len) {
			switch (callback_type)
			{
			case callback_newconn:
				assert(!new_conn);
				new_conn = true;
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

TEST_F(handshake, accept)
{
	new_conn = false;

	std::unique_ptr<rudp_fd> fd(new rudp_fd);
	rudp_init(fd.get(), nullptr, false);

	rudp_env_add_time(10 * 1000 * 1000);
	rudp_update(fd.get());

	last_send.clear();
	int ret = rudp_accept_incoming(fd.get(), "127.0.0.1:12345", (char*)handshake_step1, sizeof(handshake_step1));
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(last_send.size(), 29);
	ASSERT_FALSE(new_conn);

	rudp_env_add_time(10 * 1000 * 1000);
	rudp_update(fd.get());

	std::vector<char> send2 = last_send;
	last_send.clear();
	ret = rudp_accept_incoming(fd.get(), "127.0.0.1:12345", send2.data(), (int)send2.size());
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(last_send.size(), 29);
	ASSERT_TRUE(new_conn);
}

TEST_F(handshake, client)
{
	std::unique_ptr<rudp_fd> fd(new rudp_fd);

	last_send.clear();
	rudp_init(fd.get(), nullptr, true);
	ASSERT_EQ(last_send.size(), sizeof(handshake_step1));
	ASSERT_EQ(memcmp(handshake_step1, last_send.data(), last_send.size()), 0);
}
