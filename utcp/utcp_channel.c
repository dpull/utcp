#include "utcp_channel.h"
#include "utcp_def_internal.h"
#include "utcp_utils.h"
#include <assert.h>
#include <string.h>

struct utcp_channel* alloc_utcp_channel(int32_t InitInReliable, int32_t InitOutReliable)
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

void free_utcp_channel(struct utcp_channel* utcp_channel)
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

void mark_channel_close(struct utcp_channel* utcp_channel, int8_t CloseReason)
{
	assert(CloseReason != -1);
	utcp_channel->CloseReason = CloseReason;
}

struct utcp_bunch_node* alloc_utcp_bunch_node()
{
	struct utcp_bunch_node* utcp_bunch_node = (struct utcp_bunch_node*)utcp_realloc(NULL, sizeof(*utcp_bunch_node));
	memset(&utcp_bunch_node->dl_list_node, 0, sizeof(utcp_bunch_node->dl_list_node));
	return utcp_bunch_node;
}

void free_utcp_bunch_node(struct utcp_bunch_node* utcp_bunch_node)
{
	assert(!utcp_bunch_node->dl_list_node.next);
	assert(!utcp_bunch_node->dl_list_node.prev);
	utcp_realloc(utcp_bunch_node, 0);
}

// UChannel::ReceivedRawBunch
bool enqueue_incoming_data(struct utcp_channel* utcp_channel, struct utcp_bunch_node* utcp_bunch_node)
{
	assert(utcp_bunch_node->utcp_bunch.bReliable);
	struct dl_list_node* dl_list_node = utcp_channel->InRec.next;
	while (dl_list_node != &utcp_channel->InRec)
	{
		struct utcp_bunch_node* cur_utcp_bunch_node = CONTAINING_RECORD(dl_list_node, struct utcp_bunch_node, dl_list_node);
		if (utcp_bunch_node->utcp_bunch.ChSequence == cur_utcp_bunch_node->utcp_bunch.ChSequence)
		{
			// Already queued.
			return false;
		}

		if (utcp_bunch_node->utcp_bunch.ChSequence < cur_utcp_bunch_node->utcp_bunch.ChSequence)
		{
			// Stick before this one.
			break;
		}
		dl_list_node = dl_list_node->next;
	}
	assert(dl_list_node);
	dl_list_push_before(dl_list_node, &utcp_bunch_node->dl_list_node);
	utcp_channel->NumInRec++;
	utcp_log(Verbose, "enqueue_incoming_data: ChIndex=%d ChSeq=%d", utcp_bunch_node->utcp_bunch.ChIndex, utcp_bunch_node->utcp_bunch.ChSequence);
	return true;
}

struct utcp_bunch_node* dequeue_incoming_data(struct utcp_channel* utcp_channel, int sequence)
{
	if (dl_list_empty(&utcp_channel->InRec))
	{
		return NULL;
	}

	struct dl_list_node* dl_list_node = utcp_channel->InRec.next;
	struct utcp_bunch_node* utcp_bunch_node = CONTAINING_RECORD(dl_list_node, struct utcp_bunch_node, dl_list_node);
	if (utcp_bunch_node->utcp_bunch.ChSequence != sequence)
	{
		assert(utcp_bunch_node->utcp_bunch.ChSequence > sequence);
		return NULL;
	}

	struct dl_list_node* dl_list_node_pop = dl_list_pop_next(&utcp_channel->InRec);
	assert(dl_list_node == dl_list_node_pop);

	utcp_channel->NumInRec--;
	utcp_log(Verbose, "dequeue_incoming_data: ChIndex=%d ChSeq=%d", utcp_bunch_node->utcp_bunch.ChIndex, utcp_bunch_node->utcp_bunch.ChSequence);
	return utcp_bunch_node;
}

void add_ougoing_data(struct utcp_channel* utcp_channel, struct utcp_bunch_node* utcp_bunch_node)
{
	assert(utcp_bunch_node->packet_id >= 0);
	dl_list_push_before(&utcp_channel->OutRec, &utcp_bunch_node->dl_list_node);
	utcp_channel->NumOutRec++;
}

int remove_ougoing_data(struct utcp_channel* utcp_channel, int32_t packet_id, struct utcp_bunch_node* bunch_nodes[], int bunch_nodes_size)
{
	int count = 0;
	struct dl_list_node* dl_list_node = utcp_channel->OutRec.next;
	while (dl_list_node != &utcp_channel->OutRec)
	{
		struct utcp_bunch_node* cur_utcp_bunch_node = CONTAINING_RECORD(dl_list_node, struct utcp_bunch_node, dl_list_node);
		dl_list_node = dl_list_node->next;

		if (packet_id == cur_utcp_bunch_node->packet_id)
		{
			dl_list_erase(&cur_utcp_bunch_node->dl_list_node);
			utcp_channel->NumOutRec--;

			assert(count < bunch_nodes_size);
			bunch_nodes[count] = cur_utcp_bunch_node;
			count++;
			continue;
		}
		assert(packet_id < cur_utcp_bunch_node->packet_id);
		if (packet_id < cur_utcp_bunch_node->packet_id)
			break;
	}
	return count;
}

static struct utcp_bunch* get_last_partial_bunch(struct utcp_channel* utcp_channel)
{
	if (dl_list_empty(&utcp_channel->InPartialBunch))
		return NULL;

	struct dl_list_node* dl_list_node = utcp_channel->InPartialBunch.prev;
	struct utcp_bunch_node* last_utcp_bunch_node = CONTAINING_RECORD(dl_list_node, struct utcp_bunch_node, dl_list_node);
	return &last_utcp_bunch_node->utcp_bunch;
}

// UChannel::ReceivedNextBunch
enum merge_partial_result merge_partial_data(struct utcp_channel* utcp_channel, struct utcp_bunch_node* utcp_bunch_node, bool* bOutSkipAck)
{
	*bOutSkipAck = false;

	struct utcp_bunch* utcp_bunch = &utcp_bunch_node->utcp_bunch;
	assert(utcp_bunch->bPartial);

	struct utcp_bunch* HandleBunch = NULL;
	if (utcp_bunch->bPartialInitial)
	{
		// Create new InPartialBunch if this is the initial bunch of a new sequence.

		struct utcp_bunch* last_utcp_bunch = get_last_partial_bunch(utcp_channel);
		if (last_utcp_bunch)
		{
			if (!last_utcp_bunch->bPartialFinal)
			{
				if (last_utcp_bunch->bReliable)
				{
					if (utcp_bunch->bReliable)
					{
						// UE_LOG(LogNetPartialBunch, Warning, TEXT("Reliable partial trying to destroy reliable partial 1. %s"), *Describe());
						// AddToChainResultPtr(Bunch.ExtendedError, ENetCloseResult::PartialInitialReliableDestroy);

						utcp_log(Warning, "Reliable partial trying to destroy reliable partial 1");
						return partial_merge_fatal;
					}

					// UE_LOG(LogNetPartialBunch, Log, TEXT("Unreliable partial trying to destroy reliable partial 1"));
					utcp_log(Warning, "Unreliable partial trying to destroy reliable partial 1");
					*bOutSkipAck = true;
					return partial_merge_failed;
				}

				// We didn't complete the last partial bunch - this isn't fatal since they can be unreliable, but may want to log it.
				// UE_LOG(LogNetPartialBunch, Verbose, TEXT("Incomplete partial bunch. Channel: %d ChSequence: %d"), InPartialBunch->ChIndex,
				// InPartialBunch->ChSequence);
			}
			clear_partial_data(utcp_channel);
			last_utcp_bunch = NULL;
		}

		assert(dl_list_empty(&utcp_channel->InPartialBunch));
		dl_list_push_before(&utcp_channel->InPartialBunch, &utcp_bunch_node->dl_list_node);

		return partial_merge_succeed;
	}
	else
	{
		// Merge in next partial bunch to InPartialBunch if:
		//	-We have a valid InPartialBunch
		//	-The current InPartialBunch wasn't already complete
		//  -ChSequence is next in partial sequence
		//	-Reliability flag matches

		bool bSequenceMatches = false;
		struct utcp_bunch* last_utcp_bunch = get_last_partial_bunch(utcp_channel);
		if (last_utcp_bunch)
		{
			const bool bReliableSequencesMatches = utcp_bunch->ChSequence == last_utcp_bunch->ChSequence + 1;
			const bool bUnreliableSequenceMatches = bReliableSequencesMatches || (utcp_bunch->ChSequence == last_utcp_bunch->ChSequence);

			// Unreliable partial bunches use the packet sequence, and since we can merge multiple bunches into a single packet,
			// it's perfectly legal for the ChSequence to match in this case.
			// Reliable partial bunches must be in consecutive order though
			bSequenceMatches = utcp_bunch->bReliable ? bReliableSequencesMatches : bUnreliableSequenceMatches;
		}

		if (last_utcp_bunch && !last_utcp_bunch->bPartialFinal && bSequenceMatches && last_utcp_bunch->bReliable == utcp_bunch->bReliable)
		{
			// Merge.
			// UE_LOG(LogNetPartialBunch, Verbose, TEXT("Merging Partial Bunch: %d Bytes"), Bunch.GetBytesLeft());

			dl_list_push_before(&utcp_channel->InPartialBunch, &utcp_bunch_node->dl_list_node);

			if (utcp_bunch->bPartialFinal)
			{
				// LogPartialBunch(TEXT("Completed Partial Bunch."), Bunch, *InPartialBunch);
				return partial_available;
			}
			else
			{
				// LogPartialBunch(TEXT("Received Partial Bunch."), Bunch, *InPartialBunch);
				return partial_merge_succeed;
			}
		}
		else
		{
			// Merge problem - delete InPartialBunch. This is mainly so that in the unlikely chance that ChSequence wraps around, we wont merge two completely
			// separate partial bunches.

			// We shouldn't hit this path on 100% reliable connections

			*bOutSkipAck = true; // Don't ack the packet, since we didn't process the bunch

			if (last_utcp_bunch && last_utcp_bunch->bReliable)
			{
				if (utcp_bunch->bReliable)
				{
					// UE_LOG(LogNetPartialBunch, Warning, TEXT("Reliable partial trying to destroy reliable partial 2. %s"), *Describe());

					// Bunch.SetError();
					// AddToChainResultPtr(Bunch.ExtendedError, ENetCloseResult::PartialMergeReliableDestroy);

					utcp_log(Warning, "Reliable partial trying to destroy reliable partial 2");
					return partial_merge_fatal;
				}

				// UE_LOG(LogNetPartialBunch, Log, TEXT("Unreliable partial trying to destroy reliable partial 2"));
				utcp_log(Warning, "Unreliable partial trying to destroy reliable partial 2");
				return partial_merge_failed;
			}

			if (last_utcp_bunch)
			{
				clear_partial_data(utcp_channel);
			}
			return partial_merge_failed;
		}
	}
}

void clear_partial_data(struct utcp_channel* utcp_channel)
{
	while (!dl_list_empty(&utcp_channel->InPartialBunch))
	{
		struct dl_list_node* dl_list_node = dl_list_pop_next(&utcp_channel->InPartialBunch);
		struct utcp_bunch_node* utcp_bunch_node = CONTAINING_RECORD(dl_list_node, struct utcp_bunch_node, dl_list_node);
		free_utcp_bunch_node(utcp_bunch_node);
	}
}

int get_partial_bunch(struct utcp_channel* utcp_channel, struct utcp_bunch* bunches[], int bunches_size)
{
	int count = 0;
	struct dl_list_node* dl_list_node = utcp_channel->InPartialBunch.next;
	while (dl_list_node != &utcp_channel->InPartialBunch)
	{
		if (count >= bunches_size)
			return -1;

		struct utcp_bunch_node* utcp_bunch_node = CONTAINING_RECORD(dl_list_node, struct utcp_bunch_node, dl_list_node);
		dl_list_node = dl_list_node->next;

		bunches[count] = &utcp_bunch_node->utcp_bunch;
		assert(bunches[count]->bPartial);
		count++;
	}

	assert(count > 1);
	assert(bunches[0]->bPartialInitial);
	assert(bunches[count - 1]->bPartialFinal);
	return count;
}

static int binary_search(const void* key, const void* base, size_t num, size_t element_size, int (*compar)(const void*, const void*))
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

static int uint16_less(const void* l, const void* r)
{
	return *(uint16_t*)l - *(uint16_t*)r;
}

static bool open_channel_resize(struct utcp_open_channels* utcp_open_channels)
{
	if (utcp_open_channels->num < utcp_open_channels->cap)
		return true;

	assert(utcp_open_channels->num == utcp_open_channels->cap);

	int cap = utcp_open_channels->cap;
	assert((!!cap) == (!!utcp_open_channels->channels));

	cap = cap != 0 ? cap : 16;
	cap *= 2;
	assert(cap > 0 && cap < DEFAULT_MAX_CHANNEL_SIZE * 2);

	uint16_t* channels = utcp_realloc(utcp_open_channels->channels, cap);
	if (!channels)
		return false;
	utcp_open_channels->channels = channels;
	utcp_open_channels->cap = cap;
	return true;
}

void open_channel_uninit(struct utcp_open_channels* utcp_open_channels)
{
	if (utcp_open_channels->channels)
		utcp_realloc(utcp_open_channels->channels, 0);
}

bool open_channel_add(struct utcp_open_channels* utcp_open_channels, uint16_t ChIndex)
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

bool open_channel_remove(struct utcp_open_channels* utcp_open_channels, uint16_t ChIndex)
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
