#include "rudp_bunch_data.h"
#include "bit_buffer.h"
#include "rudp_def.h"
#include <assert.h>
#include <string.h>

void init_rudp_bunch_data(struct rudp_bunch_data* rudp_bunch_data)
{
	memset(rudp_bunch_data, 0, sizeof(*rudp_bunch_data));

	dl_list_init(&rudp_bunch_data->free_list);
	for (int i = 0; i < _countof(rudp_bunch_data->cache); ++i)
	{
		struct rudp_bunch_node* rudp_bunch_node = &rudp_bunch_data->cache[i];
		dl_list_push_back(&rudp_bunch_data->free_list, &rudp_bunch_node->dl_list_node);
	}

	dl_list_init(&rudp_bunch_data->InRec);
	dl_list_init(&rudp_bunch_data->InPartialBunch);
}

struct rudp_bunch_node* alloc_rudp_bunch_node(struct rudp_bunch_data* rudp_bunch_data)
{
	if (dl_list_empty(&rudp_bunch_data->free_list))
	{
		assert(false);
		return NULL;
	}

	struct dl_list_node* node = dl_list_pop_back(&rudp_bunch_data->free_list);
	struct rudp_bunch_node* rudp_bunch_node = CONTAINING_RECORD(node, struct rudp_bunch_node, dl_list_node);
	return rudp_bunch_node;
}

void free_rudp_bunch_node(struct rudp_bunch_data* rudp_bunch_data, struct rudp_bunch_node* rudp_bunch_node)
{
	dl_list_push_back(&rudp_bunch_data->free_list, &rudp_bunch_node->dl_list_node);
}

bool enqueue_incoming_data(struct rudp_bunch_data* rudp_bunch_data, struct rudp_bunch_node* rudp_bunch_node)
{
	assert(rudp_bunch_node->rudp_bunch.bReliable);

	struct dl_list_node* dl_list_node = rudp_bunch_data->InRec.next;
	while (dl_list_node != &rudp_bunch_data->InRec)
	{
		struct rudp_bunch_node* cur_rudp_bunch_node = CONTAINING_RECORD(dl_list_node, struct rudp_bunch_node, dl_list_node);
		if (rudp_bunch_node->rudp_bunch.ChSequence == cur_rudp_bunch_node->rudp_bunch.ChSequence)
		{
			// Already queued.
			return false;
		}

		if (rudp_bunch_node->rudp_bunch.ChSequence < cur_rudp_bunch_node->rudp_bunch.ChSequence)
		{
			// Stick before this one.
			break;
		}
		dl_list_node = dl_list_node->next;
	}
	assert(dl_list_node);
	dl_list_push_front(dl_list_node, &rudp_bunch_node->dl_list_node);
	return true;
}

struct rudp_bunch_node* dequeue_incoming_data(struct rudp_bunch_data* rudp_bunch_data, int sequence)
{
	if (dl_list_empty(&rudp_bunch_data->InRec))
	{
		return NULL;
	}

	struct dl_list_node* dl_list_node = rudp_bunch_data->InRec.next;
	struct rudp_bunch_node* rudp_bunch_node = CONTAINING_RECORD(dl_list_node, struct rudp_bunch_node, dl_list_node);
	if (rudp_bunch_node->rudp_bunch.ChSequence != sequence)
	{
		return NULL;
	}

	dl_list_pop_front(&rudp_bunch_data->InRec);
	return rudp_bunch_node;
}

static struct rudp_bunch* get_last_partial_bunch(struct rudp_bunch_data* rudp_bunch_data)
{
	if (dl_list_empty(&rudp_bunch_data->InPartialBunch))
		return NULL;

	struct dl_list_node* dl_list_node = rudp_bunch_data->InPartialBunch.prev;
	struct rudp_bunch_node* last_rudp_bunch_node = CONTAINING_RECORD(dl_list_node, struct rudp_bunch_node, dl_list_node);
	return &last_rudp_bunch_node->rudp_bunch;
}

int merge_partial_data(struct rudp_bunch_data* rudp_bunch_data, struct rudp_bunch_node* rudp_bunch_node, bool* bOutSkipAck)
{
	struct rudp_bunch* rudp_bunch = &rudp_bunch_node->rudp_bunch;
	assert(rudp_bunch->bPartial);

	struct rudp_bunch* HandleBunch = NULL;
	if (rudp_bunch->bPartialInitial)
	{
		// Create new InPartialBunch if this is the initial bunch of a new sequence.

		struct rudp_bunch* last_rudp_bunch = get_last_partial_bunch(rudp_bunch_data);
		if (last_rudp_bunch)
		{
			if (!last_rudp_bunch->bPartialFinal)
			{
				if (last_rudp_bunch->bReliable)
				{
					if (rudp_bunch->bReliable)
					{
						// UE_LOG(LogNetPartialBunch, Warning, TEXT("Reliable partial trying to destroy reliable partial 1. %s"), *Describe());
						// AddToChainResultPtr(Bunch.ExtendedError, ENetCloseResult::PartialInitialReliableDestroy);
						return -1;
					}

					// UE_LOG(LogNetPartialBunch, Log, TEXT("Unreliable partial trying to destroy reliable partial 1"));
					*bOutSkipAck = true;
					return 0;
				}

				// We didn't complete the last partial bunch - this isn't fatal since they can be unreliable, but may want to log it.
				// UE_LOG(LogNetPartialBunch, Verbose, TEXT("Incomplete partial bunch. Channel: %d ChSequence: %d"), InPartialBunch->ChIndex,
				// InPartialBunch->ChSequence);
			}
			clear_partial_data(rudp_bunch_data);
			last_rudp_bunch = NULL;
		}

		assert(dl_list_empty(&rudp_bunch_data->InPartialBunch));
		dl_list_push_back(&rudp_bunch_data->InPartialBunch, &rudp_bunch_node->dl_list_node);
	}
	else
	{
		// Merge in next partial bunch to InPartialBunch if:
		//	-We have a valid InPartialBunch
		//	-The current InPartialBunch wasn't already complete
		//  -ChSequence is next in partial sequence
		//	-Reliability flag matches

		bool bSequenceMatches = false;
		struct rudp_bunch* last_rudp_bunch = get_last_partial_bunch(rudp_bunch_data);
		if (last_rudp_bunch)
		{
			const bool bReliableSequencesMatches = rudp_bunch->ChSequence == last_rudp_bunch->ChSequence + 1;
			const bool bUnreliableSequenceMatches = bReliableSequencesMatches || (rudp_bunch->ChSequence == last_rudp_bunch->ChSequence);

			// Unreliable partial bunches use the packet sequence, and since we can merge multiple bunches into a single packet,
			// it's perfectly legal for the ChSequence to match in this case.
			// Reliable partial bunches must be in consecutive order though
			bSequenceMatches = rudp_bunch->bReliable ? bReliableSequencesMatches : bUnreliableSequenceMatches;
		}

		if (last_rudp_bunch && !last_rudp_bunch->bPartialFinal && bSequenceMatches && last_rudp_bunch->bReliable == rudp_bunch->bReliable)
		{
			// Merge.
			// UE_LOG(LogNetPartialBunch, Verbose, TEXT("Merging Partial Bunch: %d Bytes"), Bunch.GetBytesLeft());

			dl_list_push_back(&rudp_bunch_data->InPartialBunch, &rudp_bunch_node->dl_list_node);

			if (rudp_bunch->bPartialFinal)
			{
				// LogPartialBunch(TEXT("Completed Partial Bunch."), Bunch, *InPartialBunch);
				return 1;
			}
			else
			{
				// LogPartialBunch(TEXT("Received Partial Bunch."), Bunch, *InPartialBunch);
				return 0;
			}
		}
		else
		{
			// Merge problem - delete InPartialBunch. This is mainly so that in the unlikely chance that ChSequence wraps around, we wont merge two completely
			// separate partial bunches.

			// We shouldn't hit this path on 100% reliable connections

			*bOutSkipAck = true; // Don't ack the packet, since we didn't process the bunch

			if (last_rudp_bunch && last_rudp_bunch->bReliable)
			{
				if (rudp_bunch->bReliable)
				{
					// UE_LOG(LogNetPartialBunch, Warning, TEXT("Reliable partial trying to destroy reliable partial 2. %s"), *Describe());

					// Bunch.SetError();
					// AddToChainResultPtr(Bunch.ExtendedError, ENetCloseResult::PartialMergeReliableDestroy);

					return -1;
				}

				// UE_LOG(LogNetPartialBunch, Log, TEXT("Unreliable partial trying to destroy reliable partial 2"));
				return 0;
			}

			if (last_rudp_bunch)
			{
				clear_partial_data(rudp_bunch_data);
			}
		}
	}
	/*
	if (InPartialBunch && IsBunchTooLarge(Connection, InPartialBunch))
	{
		UE_LOG(LogNetPartialBunch, Error, TEXT("Received a partial bunch exceeding max allowed size. BunchSize=%d, MaximumSize=%d"),
			   InPartialBunch->GetNumBytes(), NetMaxConstructedPartialBunchSizeBytes);

		Bunch.SetError();
		AddToChainResultPtr(Bunch.ExtendedError, ENetCloseResult::PartialTooLarge);

		return false;
	}
	*/
	return 0;
}

void clear_partial_data(struct rudp_bunch_data* rudp_bunch_data)
{
	while (!dl_list_empty(&rudp_bunch_data->InPartialBunch))
	{
		struct dl_list_node* dl_list_node = dl_list_pop_back(&rudp_bunch_data->free_list);
		dl_list_push_back(&rudp_bunch_data->free_list, dl_list_node);
	}
}

int get_partial_bunch(struct rudp_bunch_data* rudp_bunch_data, struct rudp_bunch* bunches[], int bunches_size)
{
	int count = 0;
	struct dl_list_node* dl_list_node = rudp_bunch_data->InPartialBunch.next;
	while (dl_list_node != &rudp_bunch_data->InPartialBunch)
	{
		if (count >= bunches_size)
			return -1;

		struct rudp_bunch_node* rudp_bunch_node = CONTAINING_RECORD(dl_list_node, struct rudp_bunch_node, dl_list_node);
		dl_list_node = dl_list_node->next;

		bunches[count] = &rudp_bunch_node->rudp_bunch;
		assert(bunches[count]->bPartial);
		count++;
	}

	assert(count > 1);
	assert(bunches[0]->bPartialInitial);
	assert(bunches[count - 1]->bPartialFinal);
	return true;
}
