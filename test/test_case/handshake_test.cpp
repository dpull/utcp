#include "utcp/rudp.h"
#include "utcp/rudp_def.h"
#include "gtest/gtest.h"
#include <memory>

static uint8_t handshake_step1[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8};
static std::vector<uint8_t> last_send;
static bool new_conn;

struct handshake : public ::testing::Test
{
	virtual void SetUp() override
	{
		new_conn = false;
		last_send.clear();

		auto config = rudp_get_config();
		config->on_raw_send = [](struct rudp_fd* fd, void* userdata, const void* data, int len) {
			last_send.insert(last_send.end(), (const uint8_t*)data, (const uint8_t*)data + len);
		};
		config->on_accept = [](struct rudp_fd* fd, void* userdata, bool reconnect) {
			assert(!reconnect);
			assert(!new_conn);
			new_conn = true;
		};
	}

	virtual void TearDown() override
	{
		auto config = rudp_get_config();
		config->on_raw_send = nullptr;
		config->on_accept = nullptr;
	}
};

TEST_F(handshake, accept)
{
	std::unique_ptr<rudp_fd> fd(new rudp_fd);
	rudp_init(fd.get(), nullptr, false);

	rudp_add_time(10 * 1000 * 1000);
	rudp_update(fd.get());

	int ret = rudp_connectionless_incoming(fd.get(), "127.0.0.1:12345", handshake_step1, sizeof(handshake_step1));
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(last_send.size(), 29);
	ASSERT_FALSE(new_conn);

	rudp_add_time(10 * 1000 * 1000);
	rudp_update(fd.get());

	std::vector<uint8_t> send2 = last_send;
	last_send.clear();
	ret = rudp_connectionless_incoming(fd.get(), "127.0.0.1:12345", send2.data(), (int)send2.size());
	ASSERT_EQ(ret, 0);
	ASSERT_EQ(last_send.size(), 29);
	ASSERT_TRUE(new_conn);
}

TEST_F(handshake, client)
{
	std::unique_ptr<rudp_fd> fd(new rudp_fd);
	rudp_init(fd.get(), nullptr, true);
	ASSERT_EQ(last_send.size(), sizeof(handshake_step1));
	ASSERT_EQ(memcmp(handshake_step1, last_send.data(), last_send.size()), 0);
}
