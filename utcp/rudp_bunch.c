#include "rudp_bunch.h"
#include "bit_buffer.h"
#include "rudp_def.h"
#include <assert.h>
#include <string.h>

#define BITBUF_READ_BIT(VAR)                                                                                                                                   \
	do                                                                                                                                                         \
	{                                                                                                                                                          \
		uint8_t __VALUE;                                                                                                                                       \
		if (!bitbuf_read_bit(bitbuf, &__VALUE))                                                                                                                \
			return false;                                                                                                                                      \
		VAR = __VALUE;                                                                                                                                         \
	} while (0);

static inline int32_t BestSignedDifference(int32_t Value, int32_t Reference, int32_t Max)
{
	return ((Value - Reference + Max / 2) & (Max - 1)) - Max / 2;
}

static inline int32_t MakeRelative(int32_t Value, int32_t Reference, int32_t Max)
{
	return Reference + BestSignedDifference(Value, Reference, Max);
}

bool rudp_bunch_read(struct rudp_bunch* rudp_bunch, struct bitbuf* bitbuf, struct rudp_fd* fd)
{
	memset(rudp_bunch, 0, sizeof(*rudp_bunch));
	uint8_t bControl;

	BITBUF_READ_BIT(bControl);

	if (bControl)
	{
		BITBUF_READ_BIT(rudp_bunch->bOpen);
		BITBUF_READ_BIT(rudp_bunch->bClose);
	}

	if (rudp_bunch->bClose)
	{
		uint32_t CloseReason;
		if (!bitbuf_read_int(bitbuf, &CloseReason, 15))
			return false;
		rudp_bunch->CloseReason = CloseReason;
	}
	BITBUF_READ_BIT(rudp_bunch->bIsReplicationPaused);
	BITBUF_READ_BIT(rudp_bunch->bReliable);

	uint32_t ChIndex;
	if (!bitbuf_read_int_packed(bitbuf, &ChIndex))
		return false;

	if (ChIndex >= DEFAULT_MAX_CHANNEL_SIZE)
		return false;
	rudp_bunch->ChIndex = ChIndex;

	BITBUF_READ_BIT(rudp_bunch->bHasPackageMapExports);
	BITBUF_READ_BIT(rudp_bunch->bHasMustBeMappedGUIDs);
	BITBUF_READ_BIT(rudp_bunch->bPartial);

	if (rudp_bunch->bReliable)
	{
		uint32_t ChSequence = 0;
		if (!bitbuf_read_int(bitbuf, &ChSequence, MAX_CHSEQUENCE))
		{
			return false;
		}
		rudp_bunch->ChSequence = MakeRelative(ChSequence, fd->InReliable[rudp_bunch->ChIndex], MAX_CHSEQUENCE);
	}
	else if (rudp_bunch->bPartial)
	{
		// If this is an unreliable partial bunch, we simply use packet sequence since we already have it
		rudp_bunch->ChSequence = fd->InPacketId;
	}
	else
	{
		assert(rudp_bunch->ChSequence == 0);
		rudp_bunch->ChSequence = 0;
	}

	if (rudp_bunch->bReliable || rudp_bunch->bOpen)
	{
		BITBUF_READ_BIT(rudp_bunch->bHardcoded);
		if (!rudp_bunch->bHardcoded)
		{
			// TODO 暂时不支持
			assert(false);
			return false;
		}

		uint32_t NameIndex = 0;
		if (!bitbuf_read_int_packed(bitbuf, &NameIndex))
			return false;
		rudp_bunch->NameIndex = NameIndex;
	}

	uint32_t BunchDataBits; // 80
	if (!bitbuf_read_int(bitbuf, &BunchDataBits, MaxPacket * 8))
		return false;
	rudp_bunch->DataBitsLen = BunchDataBits;
	if (!bitbuf_read_bits(bitbuf, rudp_bunch->Data, rudp_bunch->DataBitsLen))
		return false;
	return true;
}

bool rudp_bunch_write(struct rudp_bunch* rudp_bunch, struct bitbuf* bitbuf, struct rudp_fd* fd)
{
	return false;
}
