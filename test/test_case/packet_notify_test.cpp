#include "utcp/utcp_sequence_number.h"
#include "gtest/gtest.h"

TEST(packet_notify, sequence_number)
{
	uint16_t num1 = seq_num_init(SeqNumberMax - 5);
	ASSERT_EQ(num1, SeqNumberMax - 5);

	uint16_t num2 = seq_num_init(SeqNumberMax);
	ASSERT_EQ(num2, SeqNumberMax);

	uint16_t num3 = seq_num_init(SeqNumberMax + 5);
	ASSERT_EQ(num3, 4);

	ASSERT_TRUE(seq_num_greater_than(num2, num1));
	ASSERT_TRUE(seq_num_greater_than(num3, num1));
	ASSERT_TRUE(seq_num_greater_than(num3, num2));

	ASSERT_EQ(seq_num_diff(num2, num1), 5);
	ASSERT_EQ(seq_num_diff(num3, num1), 10);
	ASSERT_EQ(seq_num_diff(num3, num2), 5);

	ASSERT_EQ(seq_num_diff(num1, num2), -5);
	ASSERT_EQ(seq_num_diff(num1, num3), -10);
	ASSERT_EQ(seq_num_diff(num2, num3), -5);
}
// FNetPacketNotifyTest::RunTest