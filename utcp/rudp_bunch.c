#include "rudp_bunch.h"
#include "bit_buffer.h"
#include "rudp_def.h"
#include <assert.h>
#include <string.h>

#define BITBUF_READ_BIT(VAR)                                                                                                                                                       \
	do                                                                                                                                                                             \
	{                                                                                                                                                                              \
		uint8_t __VALUE;                                                                                                                                                           \
		if (!bitbuf_read_bit(bitbuf, &__VALUE))                                                                                                                                    \
			return false;                                                                                                                                                          \
		VAR = __VALUE;                                                                                                                                                             \
	} while (0);

#define BITBUF_WRITE_BIT(VAR)                                                                                                                                                      \
	do                                                                                                                                                                             \
	{                                                                                                                                                                              \
		if (!bitbuf_write_bit(bitbuf, VAR))                                                                                                                                        \
			return false;                                                                                                                                                          \
	} while (0);

enum
{
	EChannelCloseReasonMAX = 15
};

// UNetConnection::ReceivedPacket
bool rudp_bunch_read(struct rudp_bunch* rudp_bunch, struct bitbuf* bitbuf)
{
	memset(rudp_bunch, 0, sizeof(*rudp_bunch));
	uint8_t bControl;

	BITBUF_READ_BIT(bControl);

	if (bControl)
	{
		BITBUF_READ_BIT(rudp_bunch->bOpen);
		BITBUF_READ_BIT(rudp_bunch->bClose);
	}

	rudp_bunch->CloseReason = 0;
	if (rudp_bunch->bClose)
	{
		uint32_t CloseReason;
		if (!bitbuf_read_int(bitbuf, &CloseReason, EChannelCloseReasonMAX))
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

	rudp_bunch->ChSequence = 0;
	if (rudp_bunch->bReliable)
	{
		uint32_t ChSequence = 0;
		if (!bitbuf_read_int(bitbuf, &rudp_bunch->ChSequence, MAX_CHSEQUENCE))
		{
			return false;
		}
	}

	rudp_bunch->bPartialInitial = 0;
	rudp_bunch->bPartialFinal = 0;
	if (rudp_bunch->bPartial)
	{
		BITBUF_READ_BIT(rudp_bunch->bPartialInitial);
		BITBUF_READ_BIT(rudp_bunch->bPartialFinal);
	}

	if (rudp_bunch->bReliable || rudp_bunch->bOpen)
	{
		uint8_t bHardcoded;
		BITBUF_READ_BIT(bHardcoded);
		if (!bHardcoded)
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

	uint32_t BunchDataBits;
	if (!bitbuf_read_int(bitbuf, &BunchDataBits, MaxPacket * 8))
		return false;
	rudp_bunch->DataBitsLen = BunchDataBits;
	if (!bitbuf_read_bits(bitbuf, rudp_bunch->Data, rudp_bunch->DataBitsLen))
		return false;
	return true;
}

// UNetConnection::SendRawBunch
bool rudp_bunch_write_header(const struct rudp_bunch* rudp_bunch, struct bitbuf* bitbuf)
{
	const bool bIsOpenOrClose = rudp_bunch->bOpen || rudp_bunch->bClose;
	const bool bIsOpenOrReliable = rudp_bunch->bOpen || rudp_bunch->bReliable;

	BITBUF_WRITE_BIT(bIsOpenOrClose);
	if (bIsOpenOrClose)
	{
		BITBUF_WRITE_BIT(rudp_bunch->bOpen);
		BITBUF_WRITE_BIT(rudp_bunch->bClose);

		if (rudp_bunch->bClose)
		{
			if (!bitbuf_write_int(bitbuf, rudp_bunch->CloseReason, EChannelCloseReasonMAX))
				return false;
		}
	}

	BITBUF_WRITE_BIT(rudp_bunch->bIsReplicationPaused);
	BITBUF_WRITE_BIT(rudp_bunch->bReliable);

	if (!bitbuf_write_int_packed(bitbuf, rudp_bunch->ChIndex))
		return false;

	BITBUF_WRITE_BIT(rudp_bunch->bHasPackageMapExports);
	BITBUF_WRITE_BIT(rudp_bunch->bHasMustBeMappedGUIDs);
	BITBUF_WRITE_BIT(rudp_bunch->bPartial);

	if (rudp_bunch->bReliable)
	{
		if (!bitbuf_write_int_wrapped(bitbuf, rudp_bunch->ChSequence, MAX_CHSEQUENCE))
			return false;
	}

	if (rudp_bunch->bPartial)
	{
		BITBUF_WRITE_BIT(rudp_bunch->bPartialInitial);
		BITBUF_WRITE_BIT(rudp_bunch->bPartialFinal);
	}

	if (bIsOpenOrReliable)
	{
		BITBUF_WRITE_BIT(1);
		if (!bitbuf_write_int_packed(bitbuf, rudp_bunch->NameIndex))
			return false;
	}

	if (!bitbuf_write_int_wrapped(bitbuf, rudp_bunch->DataBitsLen, MaxPacket * 8))
		return false;
	return true;
}
