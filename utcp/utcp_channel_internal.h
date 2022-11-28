// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "utcp_def_internal.h"
#include "utcp_utils.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static inline struct utcp_channel* alloc_utcp_channel(int32_t InitInReliable, int32_t InitOutReliable)
{
	struct utcp_channel* utcp_channel = (struct utcp_channel*)utcp_realloc(NULL, sizeof(*utcp_channel));
	memset(utcp_channel, 0, sizeof(*utcp_channel));

	utcp_channel->InReliable = InitInReliable;
	utcp_channel->OutReliable = InitOutReliable;
	dl_list_init(&utcp_channel->InRec);
	dl_list_init(&utcp_channel->OutRec);
	dl_list_init(&utcp_channel->InPartialBunch);

	return utcp_channel;
}

static inline void free_utcp_channel(struct utcp_channel* utcp_channel)
{
	while (!dl_list_empty(&utcp_channel->InRec))
	{
		struct dl_list_node* dl_list_node = dl_list_pop_next(&utcp_channel->InRec);
		struct utcp_bunch_node* cur_utcp_bunch_node = CONTAINING_RECORD(dl_list_node, struct utcp_bunch_node, dl_list_node);
		utcp_realloc(cur_utcp_bunch_node, 0);
		utcp_channel->NumInRec--;
	}
	assert(utcp_channel->NumInRec == 0);

	while (!dl_list_empty(&utcp_channel->OutRec))
	{
		struct dl_list_node* dl_list_node = dl_list_pop_next(&utcp_channel->OutRec);
		struct utcp_bunch_node* cur_utcp_bunch_node = CONTAINING_RECORD(dl_list_node, struct utcp_bunch_node, dl_list_node);
		utcp_realloc(cur_utcp_bunch_node, 0);
		utcp_channel->NumOutRec--;
	}
	assert(utcp_channel->NumOutRec == 0);

	clear_partial_data(utcp_channel);

	utcp_realloc(utcp_channel, 0);
}

static inline void mark_channel_close(struct utcp_channel* utcp_channel, int8_t CloseReason)
{
	if (!utcp_channel->bClose)
	{
		utcp_channel->bClose = true;
		utcp_channel->CloseReason = CloseReason;
	}
}

static inline void open_channel_uninit(struct utcp_opened_channels* utcp_open_channels)
{
	if (utcp_open_channels->channels)
		utcp_realloc(utcp_open_channels->channels, 0);
}

static inline int binary_search(const void* key, const void* base, size_t num, size_t element_size, int (*compar)(const void*, const void*))
{
	int lo = 0;
	int hi = (int)num - 1;

	while (lo <= hi)
	{
		// i might overflow if lo and hi are both large positive numbers.
		int i = lo + ((hi - lo) >> 1);

		int c = compar(key, (char*)base + i * element_size);
		if (c == 0)
			return i;
		if (c > 0)
			lo = i + 1;
		else
			hi = i - 1;
	}
	return ~lo;
}

static inline int uint16_less(const void* l, const void* r)
{
	return *(uint16_t*)l - *(uint16_t*)r;
}

static bool open_channel_resize(struct utcp_opened_channels* utcp_open_channels)
{
	if (utcp_open_channels->num < utcp_open_channels->cap)
		return true;

	assert(utcp_open_channels->num == utcp_open_channels->cap);

	int cap = utcp_open_channels->cap;
	assert((!!cap) == (!!utcp_open_channels->channels));

	cap = cap != 0 ? cap : 16;
	cap *= 2;
	assert(cap > 0 && cap < DEFAULT_MAX_CHANNEL_SIZE * 2);

	uint16_t* channels = (uint16_t*)utcp_realloc(utcp_open_channels->channels, cap * sizeof(uint16_t));
	if (!channels)
		return false;
	utcp_open_channels->channels = channels;
	utcp_open_channels->cap = cap;
	return true;
}

static inline bool open_channel_add(struct utcp_opened_channels* utcp_open_channels, uint16_t ChIndex)
{
	int pos = binary_search(&ChIndex, utcp_open_channels->channels, utcp_open_channels->num, sizeof(uint16_t), uint16_less);
	if (pos >= 0)
		return true;

	if (!open_channel_resize(utcp_open_channels))
		return false;

	pos = ~pos;

	uint16_t* src = utcp_open_channels->channels + pos;
	uint16_t* dst = src + 1;
	int count = utcp_open_channels->num - pos;
	memmove(dst, src, count * sizeof(uint16_t));
	utcp_open_channels->channels[pos] = ChIndex;
	utcp_open_channels->num++;
	return true;
}

static inline bool open_channel_remove(struct utcp_opened_channels* utcp_open_channels, uint16_t ChIndex)
{
	int pos = binary_search(&ChIndex, utcp_open_channels->channels, utcp_open_channels->num, sizeof(uint16_t), uint16_less);
	if (pos < 0)
		return false;
	uint16_t* dst = utcp_open_channels->channels + pos;
	uint16_t* src = dst + 1;
	int count = utcp_open_channels->num - pos - 1;
	memmove(dst, src, count * sizeof(uint16_t));
	utcp_open_channels->num--;
	return true;
}


