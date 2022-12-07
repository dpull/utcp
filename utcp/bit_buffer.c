#include "bit_buffer.h"
#include <assert.h>
#include <string.h>

const uint8_t GShift[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
const uint8_t GMask[8] = {0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f};

static inline uint8_t Shift(uint8_t Cnt)
{
	return (uint8_t)(1 << Cnt);
}

static inline int MAX(int a, int b)
{
	return a > b ? a : b;
}

static bool allow_opt(struct bitbuf* buff, size_t bits_length)
{
	return buff->num + bits_length <= buff->size;
}

static uint32_t CeilLogTwo(uint32_t x)
{
	static const uint8_t log_2[256] = {/* log_2[i] = ceil(log2(i - 1)) */
									   0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
									   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
									   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8,
									   8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
									   8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
									   8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
	int l = 0;
	x--;
	while (x >= 256)
	{
		l += 8;
		x >>= 8;
	}
	return l + log_2[x];
}

static void appBitsCpy(uint8_t* Dest, int32_t DestBit, const uint8_t* Src, int32_t SrcBit, int32_t BitCount)
{
	if (BitCount == 0)
		return;

	// Special case - always at least one bit to copy,
	// a maximum of 2 bytes to read, 2 to write - only touch bytes that are actually used.
	if (BitCount <= 8)
	{
		uint32_t DestIndex = DestBit / 8;
		uint32_t SrcIndex = SrcBit / 8;
		uint32_t LastDest = (DestBit + BitCount - 1) / 8;
		uint32_t LastSrc = (SrcBit + BitCount - 1) / 8;
		uint32_t ShiftSrc = SrcBit & 7;
		uint32_t ShiftDest = DestBit & 7;
		uint32_t FirstMask = 0xFF << ShiftDest;
		uint32_t LastMask = 0xFE << ((DestBit + BitCount - 1) & 7); // Pre-shifted left by 1.
		uint32_t Accu;

		if (SrcIndex == LastSrc)
			Accu = (Src[SrcIndex] >> ShiftSrc);
		else
			Accu = ((Src[SrcIndex] >> ShiftSrc) | (Src[LastSrc] << (8 - ShiftSrc)));

		if (DestIndex == LastDest)
		{
			uint32_t MultiMask = FirstMask & ~LastMask;
			Dest[DestIndex] = ((Dest[DestIndex] & ~MultiMask) | ((Accu << ShiftDest) & MultiMask));
		}
		else
		{
			Dest[DestIndex] = (uint8_t)((Dest[DestIndex] & ~FirstMask) | ((Accu << ShiftDest) & FirstMask));
			Dest[LastDest] = (uint8_t)((Dest[LastDest] & LastMask) | ((Accu >> (8 - ShiftDest)) & ~LastMask));
		}

		return;
	}

	// Main copier, uses byte sized shifting. Minimum size is 9 bits, so at least 2 reads and 2 writes.
	uint32_t DestIndex = DestBit / 8;
	uint32_t FirstSrcMask = 0xFF << (DestBit & 7);
	uint32_t LastDest = (DestBit + BitCount) / 8;
	uint32_t LastSrcMask = 0xFF << ((DestBit + BitCount) & 7);
	uint32_t SrcIndex = SrcBit / 8;
	uint32_t LastSrc = (SrcBit + BitCount) / 8;
	int32_t ShiftCount = (DestBit & 7) - (SrcBit & 7);
	int32_t DestLoop = LastDest - DestIndex;
	int32_t SrcLoop = LastSrc - SrcIndex;
	uint32_t FullLoop;
	uint32_t BitAccu;

	// Lead-in needs to read 1 or 2 source bytes depending on alignment.
	if (ShiftCount >= 0)
	{
		FullLoop = MAX(DestLoop, SrcLoop);
		BitAccu = Src[SrcIndex] << ShiftCount;
		ShiftCount += 8; // prepare for the inner loop.
	}
	else
	{
		ShiftCount += 8; // turn shifts -7..-1 into +1..+7
		FullLoop = MAX(DestLoop, SrcLoop - 1);
		BitAccu = Src[SrcIndex] << ShiftCount;
		SrcIndex++;
		ShiftCount += 8; // Prepare for inner loop.
		BitAccu = (((uint32_t)Src[SrcIndex] << ShiftCount) + (BitAccu)) >> 8;
	}

	// Lead-in - first copy.
	Dest[DestIndex] = (uint8_t)((BitAccu & FirstSrcMask) | (Dest[DestIndex] & ~FirstSrcMask));
	SrcIndex++;
	DestIndex++;

	// Fast inner loop.
	for (; FullLoop > 1; FullLoop--)
	{																		  // ShiftCount ranges from 8 to 15 - all reads are relevant.
		BitAccu = (((uint32_t)Src[SrcIndex] << ShiftCount) + (BitAccu)) >> 8; // Copy in the new, discard the old.
		SrcIndex++;
		Dest[DestIndex] = (uint8_t)BitAccu; // Copy low 8 bits.
		DestIndex++;
	}

	// Lead-out.
	if (LastSrcMask != 0xFF)
	{
		if ((uint32_t)(SrcBit + BitCount - 1) / 8 == SrcIndex) // Last legal byte ?
		{
			BitAccu = (((uint32_t)Src[SrcIndex] << ShiftCount) + (BitAccu)) >> 8;
		}
		else
		{
			BitAccu = BitAccu >> 8;
		}

		Dest[DestIndex] = (uint8_t)((Dest[DestIndex] & LastSrcMask) | (BitAccu & ~LastSrcMask));
	}
}

size_t bitbuf_num_bytes(struct bitbuf* buff)
{
	return (buff->num + 7) >> 3;
}

size_t bitbuf_left_bits(struct bitbuf* buff)
{
	return buff->size - buff->num;
}

size_t bitbuf_left_bytes(struct bitbuf* buff)
{
	return (bitbuf_left_bits(buff) + 7) >> 3;
}

bool bitbuf_write_init(struct bitbuf* buff, uint8_t* buffer, size_t size)
{
	memset(buffer, 0, size);
	buff->buffer = buffer;
	buff->size = size * 8;
	buff->num = 0;
	return true;
}

void bitbuf_write_reuse(struct bitbuf* buff, uint8_t* buffer, size_t num_bits, size_t size)
{
	buff->buffer = buffer;
	buff->size = size * 8;
	buff->num = num_bits;
}

bool bitbuf_write_end(struct bitbuf* buff)
{
	return bitbuf_write_bit(buff, 1);
}

bool bitbuf_write_bit(struct bitbuf* buff, uint8_t value)
{
	if (!allow_opt(buff, 1))
		return false;
	if (value)
		buff->buffer[buff->num >> 3] |= GShift[buff->num & 7];
	buff->num++;
	return true;
}

bool bitbuf_write_bits(struct bitbuf* buff, const void* data, size_t bits_size)
{
	if (!allow_opt(buff, bits_size))
		return false;

	if (bits_size == 1)
	{
		if (((uint8_t*)data)[0] & 0x01)
			buff->buffer[buff->num >> 3] |= GShift[buff->num & 7];
		buff->num++;
	}
	else
	{
		appBitsCpy(buff->buffer, (int)buff->num, data, 0, (int)bits_size);
		buff->num += bits_size;
	}
	return true;
}

bool bitbuf_write_bytes(struct bitbuf* buff, const void* data, size_t size)
{
	size_t bits_size = size * 8;
	if (!allow_opt(buff, bits_size))
		return false;
	appBitsCpy(buff->buffer, (int)buff->num, data, 0, (int)bits_size);
	buff->num += bits_size;
	return true;
}

bool bitbuf_write_int(struct bitbuf* buff, uint32_t value, uint32_t value_max)
{
	assert(value_max >= 2);

	const int32_t LengthBits = CeilLogTwo(value_max);
	uint32_t WriteValue = value;

	if (!allow_opt(buff, LengthBits))
		return false;

	uint32_t NewValue = 0;
	size_t LocalNum = buff->num; // Use local var to avoid LHS

	for (uint32_t Mask = 1; (NewValue + Mask) < value_max && Mask; Mask *= 2, LocalNum++)
	{
		if (WriteValue & Mask)
		{
			buff->buffer[LocalNum >> 3] += GShift[LocalNum & 7];
			NewValue += Mask;
		}
	}

	buff->num = LocalNum;
	return true;
}

bool bitbuf_write_int_packed(struct bitbuf* buff, uint32_t InValue)
{
	uint8_t PackedBytes[20];
	uint32_t Remaining = InValue;
	int Count = 0;
	while (true)
	{
		uint8_t nextByte = Remaining & 0x7f; // Get next 7 bits to write
		Remaining = Remaining >> 7;			 // Update remaining
		nextByte = nextByte << 1;			 // Make room for 'more' bit
		if (Remaining > 0)
		{
			nextByte |= 1; // set more bit
			assert(Count < sizeof(PackedBytes));
			PackedBytes[Count] = nextByte;
			Count++;
		}
		else
		{
			assert(Count < sizeof(PackedBytes));
			PackedBytes[Count] = nextByte;
			Count++;
			break;
		}
	}
	return bitbuf_write_bytes(buff, PackedBytes, Count); // Actually serialize the bytes we made
}

bool bitbuf_write_int_wrapped(struct bitbuf* buff, uint32_t value, uint32_t value_max)
{
	assert(value_max >= 2);

	const int32_t LengthBits = CeilLogTwo(value_max);

	if (!allow_opt(buff, LengthBits))
		return false;

	uint32_t NewValue = 0;

	for (uint32_t Mask = 1; NewValue + Mask < value_max && Mask; Mask *= 2, buff->num++)
	{
		if (value & Mask)
		{
			buff->buffer[buff->num >> 3] += GShift[buff->num & 7];
			NewValue += Mask;
		}
	}
	return true;
}

bool bitbuf_write_int_byte_order(struct bitbuf* buff, uint32_t value)
{
	return bitbuf_write_bytes(buff, &value, sizeof(value));
}

bool bitbuf_read_init(struct bitbuf* buff, const uint8_t* data, size_t len)
{
	if (len == 0)
		return false;

	uint8_t LastByte = data[len - 1];
	if (LastByte == 0)
		return false;

	size_t CountBits = len * 8;
	CountBits--;
	while (!(LastByte & 0x80))
	{
		LastByte *= 2;
		CountBits--;
	}

	buff->buffer = (uint8_t*)data;
	buff->size = CountBits;
	buff->num = 0;
	return true;
}

bool bitbuf_read_bit(struct bitbuf* buff, uint8_t* value)
{
	if (!allow_opt(buff, 1))
		return false;
	uint8_t Bit = !!(buff->buffer[(int32_t)(buff->num >> 3)] & Shift(buff->num & 7));
	buff->num++;

	*value = Bit;
	return true;
}

bool bitbuf_read_bits(struct bitbuf* buff, void* buffer, size_t bits_size)
{
	if (!allow_opt(buff, bits_size))
		return false;

	if (bits_size == 1)
	{
		((uint8_t*)buffer)[0] = 0;
		if (buff->buffer[(int)(buff->num >> 3)] & Shift(buff->num & 7))
			((uint8_t*)buffer)[0] |= 0x01;
		buff->num++;
	}
	else if (bits_size != 0)
	{
		((uint8_t*)buffer)[((bits_size + 7) >> 3) - 1] = 0;
		appBitsCpy((uint8_t*)buffer, 0, buff->buffer, (int32_t)buff->num, (int32_t)bits_size);
		buff->num += bits_size;
	}
	return true;
}

bool bitbuf_read_bytes(struct bitbuf* buff, void* buffer, size_t size)
{
	return bitbuf_read_bits(buff, buffer, size * 8);
}

// FBitReader::SerializeInt
bool bitbuf_read_int(struct bitbuf* buff, uint32_t* value, uint32_t value_max)
{
	// Use local variable to avoid Load-Hit-Store
	uint32_t Value = 0;
	size_t LocalPos = buff->num;
	const size_t LocalNum = buff->size;

	for (uint32_t Mask = 1; (Value + Mask) < value_max && Mask; Mask *= 2, LocalPos++)
	{
		if (LocalPos >= LocalNum)
		{
			return false;
		}

		if (buff->buffer[(int32_t)(LocalPos >> 3)] & Shift(LocalPos & 7))
		{
			Value |= Mask;
		}
	}

	// Now write back
	buff->num = LocalPos;
	*value = Value;
	return true;
}

// FBitReader::SerializeIntPacked
bool bitbuf_read_int_packed(struct bitbuf* buff, uint32_t* value)
{
	*value = 0;
	uint8_t cnt = 0;
	uint8_t more = 1;
	while (more)
	{
		uint8_t NextByte;
		if (!bitbuf_read_bytes(buff, &NextByte, 1))
			return false;

		more = NextByte & 1;			  // Check 1 bit to see if theres more after this
		NextByte = NextByte >> 1;		  // Shift to get actual 7 bit value
		*value += NextByte << (7 * cnt++); // Add to total value
	}
	return true;
}

bool bitbuf_read_int_byte_order(struct bitbuf* buff, uint32_t* value)
{
	return bitbuf_read_bytes(buff, value, sizeof(*value));
}
