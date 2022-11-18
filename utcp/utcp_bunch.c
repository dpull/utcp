#include "utcp_bunch.h"
#include "bit_buffer.h"
#include "utcp_def.h"
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
bool utcp_bunch_read(struct utcp_bunch* utcp_bunch, struct bitbuf* bitbuf)
{
	memset(utcp_bunch, 0, sizeof(*utcp_bunch));
	uint8_t bControl;

	BITBUF_READ_BIT(bControl);

	if (bControl)
	{
		BITBUF_READ_BIT(utcp_bunch->bOpen);
		BITBUF_READ_BIT(utcp_bunch->bClose);
	}

	utcp_bunch->CloseReason = 0;
	if (utcp_bunch->bClose)
	{
		uint32_t CloseReason;
		if (!bitbuf_read_int(bitbuf, &CloseReason, EChannelCloseReasonMAX))
			return false;
		utcp_bunch->CloseReason = CloseReason;
	}
	BITBUF_READ_BIT(utcp_bunch->bIsReplicationPaused);
	BITBUF_READ_BIT(utcp_bunch->bReliable);

	uint32_t ChIndex;
	if (!bitbuf_read_int_packed(bitbuf, &ChIndex))
		return false;

	if (ChIndex >= DEFAULT_MAX_CHANNEL_SIZE)
		return false;
	utcp_bunch->ChIndex = ChIndex;

	BITBUF_READ_BIT(utcp_bunch->bHasPackageMapExports);
	BITBUF_READ_BIT(utcp_bunch->bHasMustBeMappedGUIDs);
	BITBUF_READ_BIT(utcp_bunch->bPartial);

	utcp_bunch->ChSequence = 0;
	if (utcp_bunch->bReliable)
	{
		uint32_t ChSequence = 0;
		if (!bitbuf_read_int(bitbuf, (uint32_t*)&utcp_bunch->ChSequence, UTCP_MAX_CHSEQUENCE))
		{
			return false;
		}
	}

	utcp_bunch->bPartialInitial = 0;
	utcp_bunch->bPartialFinal = 0;
	if (utcp_bunch->bPartial)
	{
		BITBUF_READ_BIT(utcp_bunch->bPartialInitial);
		BITBUF_READ_BIT(utcp_bunch->bPartialFinal);
	}

	if (utcp_bunch->bReliable || utcp_bunch->bOpen)
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
		utcp_bunch->NameIndex = NameIndex;
	}

	uint32_t BunchDataBits;
	if (!bitbuf_read_int(bitbuf, &BunchDataBits, UTCP_MAX_PACKET * 8))
		return false;
	utcp_bunch->DataBitsLen = BunchDataBits;
	if (!bitbuf_read_bits(bitbuf, utcp_bunch->Data, utcp_bunch->DataBitsLen))
		return false;
	return true;
}

// UNetConnection::SendRawBunch
bool utcp_bunch_write_header(const struct utcp_bunch* utcp_bunch, struct bitbuf* bitbuf)
{
	const bool bIsOpenOrClose = utcp_bunch->bOpen || utcp_bunch->bClose;
	const bool bIsOpenOrReliable = utcp_bunch->bOpen || utcp_bunch->bReliable;

	BITBUF_WRITE_BIT(bIsOpenOrClose);
	if (bIsOpenOrClose)
	{
		BITBUF_WRITE_BIT(utcp_bunch->bOpen);
		BITBUF_WRITE_BIT(utcp_bunch->bClose);

		if (utcp_bunch->bClose)
		{
			if (!bitbuf_write_int(bitbuf, utcp_bunch->CloseReason, EChannelCloseReasonMAX))
				return false;
		}
	}

	BITBUF_WRITE_BIT(utcp_bunch->bIsReplicationPaused);
	BITBUF_WRITE_BIT(utcp_bunch->bReliable);

	if (!bitbuf_write_int_packed(bitbuf, utcp_bunch->ChIndex))
		return false;

	BITBUF_WRITE_BIT(utcp_bunch->bHasPackageMapExports);
	BITBUF_WRITE_BIT(utcp_bunch->bHasMustBeMappedGUIDs);
	BITBUF_WRITE_BIT(utcp_bunch->bPartial);

	if (utcp_bunch->bReliable)
	{
		if (!bitbuf_write_int_wrapped(bitbuf, utcp_bunch->ChSequence, UTCP_MAX_CHSEQUENCE))
			return false;
	}

	if (utcp_bunch->bPartial)
	{
		BITBUF_WRITE_BIT(utcp_bunch->bPartialInitial);
		BITBUF_WRITE_BIT(utcp_bunch->bPartialFinal);
	}

	if (bIsOpenOrReliable)
	{
		BITBUF_WRITE_BIT(1);
		if (!bitbuf_write_int_packed(bitbuf, utcp_bunch->NameIndex))
			return false;
	}

	if (!bitbuf_write_int_wrapped(bitbuf, utcp_bunch->DataBitsLen, UTCP_MAX_PACKET * 8))
		return false;
	return true;
}
