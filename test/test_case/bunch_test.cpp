#include "utcp/rudp.h"
extern "C" {
#include "utcp/rudp_bunch.h"
}
#include "gtest/gtest.h"
#include <memory>

TEST(bunch, read_write)
{
	struct rudp_bunch rudp_bunch1;
	memset(&rudp_bunch1, 0, sizeof(rudp_bunch1));
	rudp_bunch1.NameIndex = 0;
	rudp_bunch1.ChSequence = 0;
	rudp_bunch1.ChIndex = 543;
	rudp_bunch1.DataBitsLen = 789;
	rudp_bunch1.bOpen = 0;
	rudp_bunch1.bClose = 1;
	rudp_bunch1.CloseReason = 9;
	rudp_bunch1.bIsReplicationPaused = 1;
	rudp_bunch1.bReliable = 0;
	rudp_bunch1.bHasPackageMapExports = 0;
	rudp_bunch1.bHasMustBeMappedGUIDs = 1;
	rudp_bunch1.bPartial = 1;
	rudp_bunch1.bPartialInitial = 0;
	rudp_bunch1.bPartialFinal = 0;

	uint8_t buffer[MaxPacket];
	struct bitbuf bitbuf1;

	ASSERT_TRUE(bitbuf_write_init(&bitbuf1, buffer, sizeof(buffer)));
	ASSERT_TRUE(rudp_bunch_write_header(&rudp_bunch1, &bitbuf1));
	ASSERT_TRUE(bitbuf_write_bits(&bitbuf1, rudp_bunch1.Data, rudp_bunch1.DataBitsLen));
	bitbuf_write_end(&bitbuf1);

	struct rudp_bunch rudp_bunch2;
	memset(&rudp_bunch2, 0, sizeof(rudp_bunch2));

	struct bitbuf bitbuf2;
	ASSERT_TRUE(bitbuf_read_init(&bitbuf2, buffer, bitbuf_num_bytes(&bitbuf1)));
	ASSERT_EQ(bitbuf1.num - 1, bitbuf2.size);
	ASSERT_TRUE(rudp_bunch_read(&rudp_bunch2, &bitbuf2));
	ASSERT_EQ(0, memcmp(&rudp_bunch1, &rudp_bunch2, sizeof(rudp_bunch1)));
}
