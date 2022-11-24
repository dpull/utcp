#include "abstract/utcp.hpp"
#include "gtest/gtest.h"

class large_bunch_param_test : public ::testing::TestWithParam<int>
{
};

TEST_P(large_bunch_param_test, test)
{
	std::vector<uint8_t> test_data;
	test_data.reserve(utcp::NetMaxConstructedPartialBunchSizeBytes);
	int test_count = GetParam();
	assert(test_count <= utcp::NetMaxConstructedPartialBunchSizeBytes);

	for (int i = 0; i < test_count; ++i)
	{
		test_data.push_back(rand() % 256);
	}

	utcp::large_bunch large_bunch1(test_data.data(), test_count * 8);
	if (large_bunch1.ExtDataBitsLen == 0)
	{
		ASSERT_EQ(large_bunch1.DataBitsLen, test_count * 8);
		ASSERT_EQ(memcmp(large_bunch1.Data, test_data.data(), test_count), 0);
	}
	else
	{
		ASSERT_EQ(large_bunch1.ExtDataBitsLen, test_count * 8);
		ASSERT_EQ(memcmp(large_bunch1.ExtData, test_data.data(), test_count), 0);
	}

	std::vector<utcp_bunch> bunches;
	std::vector<utcp_bunch*> ref_bunches;

	bunches.reserve(1024);
	ref_bunches.reserve(1024);

	for (auto& bunch : large_bunch1)
	{
		bunches.push_back(bunch);
		ref_bunches.push_back(&bunches[bunches.size() - 1]);
	}
	utcp::large_bunch large_bunch2(ref_bunches.data(), (int)ref_bunches.size());

	if (large_bunch1.ExtDataBitsLen == 0)
	{
		ASSERT_EQ(large_bunch1.DataBitsLen, large_bunch2.DataBitsLen);
		ASSERT_EQ(memcmp(large_bunch1.Data, large_bunch2.Data, test_count), 0);
	}
	else
	{
		ASSERT_EQ(large_bunch1.ExtDataBitsLen, large_bunch2.ExtDataBitsLen);
		ASSERT_EQ(memcmp(large_bunch1.ExtData, large_bunch2.ExtData, test_count), 0);
	}
}

#ifdef LINUX
INSTANTIATE_TEST_CASE_P(test_constructor, large_bunch_param_test, testing::Range(0, utcp::NetMaxConstructedPartialBunchSizeBytes + 1));
#else
INSTANTIATE_TEST_CASE_P(test_constructor, large_bunch_param_test,
						testing::Values(0, utcp::MAX_SINGLE_BUNCH_SIZE_BYTES - 10, utcp::MAX_SINGLE_BUNCH_SIZE_BYTES, utcp::MAX_SINGLE_BUNCH_SIZE_BYTES + 10,
										utcp::NetMaxConstructedPartialBunchSizeBytes - 10, utcp::NetMaxConstructedPartialBunchSizeBytes));
#endif // !LINUX
