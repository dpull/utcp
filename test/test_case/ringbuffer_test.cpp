#include "utcp/3rd/ringbuffer.h"
#include "gtest/gtest.h"

TEST(ring_buffer, size)
{
	struct ring_buffer_t ring_buffer;
	ring_buffer_init(&ring_buffer);
	uint32_t start = 45678;
	uint32_t loop = 1000;
	for (uint32_t i = 0; i < 1000; ++i)
	{
		if (i < 256)
			ASSERT_EQ(ring_buffer_num_items(&ring_buffer), i);
		else
			ASSERT_EQ(ring_buffer_num_items(&ring_buffer), 255);
		ring_buffer_queue(&ring_buffer, start + i);
	}

	while (!ring_buffer_is_empty(&ring_buffer))
	{
		uint32_t val = 0;
		auto ret = ring_buffer_dequeue(&ring_buffer, &val);
		ASSERT_EQ(ret, 1);
		uint32_t exp = start + loop - 1 - ring_buffer_num_items(&ring_buffer);
		ASSERT_EQ(val, exp);
	}
}
