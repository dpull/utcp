#include "utcp/utcp.h"
extern "C" {
#include "utcp/utcp_bunch.h"
}
#include "gtest/gtest.h"
#include <memory>

TEST(bunch, read_write)
{
	struct utcp_bunch utcp_bunch1;
	memset(&utcp_bunch1, 0, sizeof(utcp_bunch1));
	utcp_bunch1.ChType = 0;
	utcp_bunch1.ChSequence = 0;
	utcp_bunch1.ChIndex = 543;
	utcp_bunch1.DataBitsLen = 789;
	utcp_bunch1.bOpen = 0;
	utcp_bunch1.bClose = 1;
	utcp_bunch1.bDormant = 1;
	utcp_bunch1.bIsReplicationPaused = 1;
	utcp_bunch1.bReliable = 0;
	utcp_bunch1.bHasPackageMapExports = 0;
	utcp_bunch1.bHasMustBeMappedGUIDs = 1;
	utcp_bunch1.bPartial = 1;
	utcp_bunch1.bPartialInitial = 0;
	utcp_bunch1.bPartialFinal = 0;

	uint8_t buffer[UDP_MTU_SIZE];
	struct bitbuf bitbuf1;

	ASSERT_TRUE(bitbuf_write_init(&bitbuf1, buffer, sizeof(buffer)));
	ASSERT_TRUE(utcp_bunch_write_header(&utcp_bunch1, &bitbuf1));
	ASSERT_TRUE(bitbuf_write_bits(&bitbuf1, utcp_bunch1.Data, utcp_bunch1.DataBitsLen));
	bitbuf_write_end(&bitbuf1);

	struct utcp_bunch utcp_bunch2;
	memset(&utcp_bunch2, 0, sizeof(utcp_bunch2));

	struct bitbuf bitbuf2;
	ASSERT_TRUE(bitbuf_read_init(&bitbuf2, buffer, bitbuf_num_bytes(&bitbuf1)));
	ASSERT_EQ(bitbuf1.num - 1, bitbuf2.size);
	ASSERT_TRUE(utcp_bunch_read(&utcp_bunch2, &bitbuf2));
	ASSERT_EQ(0, memcmp(&utcp_bunch1, &utcp_bunch2, sizeof(utcp_bunch1)));
}
