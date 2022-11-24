#include "test_utils.h"
#include "gtest/gtest.h"

TEST(open_channel, insert)
{
	utcp_open_channels_rtti open_channels;
	int all = 10;
	int cnt = 5;

	for (int i = 0; i < cnt; ++i)
	{
		open_channel_add(&open_channels, i * 2);
	}

	for (int i = 0; i < all; ++i)
	{
		ASSERT_TRUE(open_channel_add(&open_channels, i));
		if (i % 2 != 0)
			cnt++;
		ASSERT_EQ(open_channels.get()->num, cnt);
	}

	for (int i = 0; i < all; ++i)
	{
		ASSERT_EQ(open_channels.get()->channels[i], i);
	}
}

TEST(open_channel, remove)
{
	utcp_open_channels_rtti open_channels;
	int all = 10;
	int cnt = 0;

	for (int i = 0; i < all; ++i)
	{
		open_channel_add(&open_channels, i);
		cnt++;
		ASSERT_EQ(open_channels.get()->num, cnt);
	}

	for (int i = 0; i < all; ++i)
	{
		ASSERT_EQ(open_channels.get()->channels[i], i);
	}

	for (int i = 1; i < all; i += 2)
	{
		ASSERT_TRUE(open_channel_remove(&open_channels, i));
		cnt--;
		ASSERT_EQ(open_channels.get()->num, cnt);
	}
	ASSERT_EQ(cnt, 5);

	for (int i = 1; i < cnt; i++)
	{
		ASSERT_EQ(open_channels.get()->channels[i], i * 2);
	}

	for (int i = 1; i < all; i += 2)
	{
		ASSERT_FALSE(open_channel_remove(&open_channels, i));
		ASSERT_EQ(open_channels.get()->num, cnt);
	}

	for (int i = 0; i < all; i += 2)
	{
		ASSERT_TRUE(open_channel_remove(&open_channels, i));
		cnt--;
		ASSERT_EQ(open_channels.get()->num, cnt);
	}
	ASSERT_EQ(cnt, 0);
}
