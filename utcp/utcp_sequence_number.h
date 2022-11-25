// Copyright DPULL, Inc. All Rights Reserved.

#pragma once
#include <stddef.h>
#include <stdint.h>

#ifndef SequenceNumberBits
#define SequenceNumberBits 14
#endif

#ifndef SequenceType
#define SequenceType uint16_t
#endif

#ifndef DifferenceType
#define DifferenceType int32_t
#endif

enum
{
	SeqNumberBits = SequenceNumberBits,
	SeqNumberCount = ((SequenceType)1) << SequenceNumberBits,
	SeqNumberHalf = ((SequenceType)1) << (SequenceNumberBits - 1),
	SeqNumberMax = SeqNumberCount - 1u,
	SeqNumberMask = SeqNumberMax,
};

static inline SequenceType seq_num_init(SequenceType value)
{
	return value & SeqNumberMask;
}

static inline bool seq_num_greater_than(SequenceType l, SequenceType r)
{
	return (l != r) && (((l - r) & SeqNumberMask) < SeqNumberHalf);
}

static inline bool seq_num_greater_equal(SequenceType l, SequenceType r)
{
	return ((l - r) & SeqNumberMask) < SeqNumberHalf;
}

static inline SequenceType seq_num_inc(SequenceType value, SequenceType i)
{
	return seq_num_init(value + i);
}

static inline DifferenceType seq_num_diff(SequenceType a, SequenceType b)
{
	const size_t ShiftValue = sizeof(DifferenceType) * 8 - SequenceNumberBits;
	return (DifferenceType)((a - b) << ShiftValue) >> ShiftValue;
}