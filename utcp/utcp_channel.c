#include "utcp_channel.h"
#include "utcp_channel_internal.h"
#include "utcp_def_internal.h"
#include <assert.h>
#include <string.h>

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

static void utcp_close_channel(struct utcp_channels* utcp_channels, int ChIndex)
{
	if (!utcp_channels->Channels[ChIndex])
		return;
	free_utcp_channel(utcp_channels->Channels[ChIndex]);
	utcp_channels->Channels[ChIndex] = NULL;
	opened_channels_remove(&utcp_channels->open_channels, ChIndex);
}

void utcp_channels_uninit(struct utcp_channels* utcp_channels)
{
	for (int i = 0; i < _countof(utcp_channels->Channels); ++i)
	{
		if (utcp_channels->Channels[i])
		{
			utcp_close_channel(utcp_channels, i);
		}
	}

	assert(utcp_channels->open_channels.num == 0);
	opened_channels_uninit(&utcp_channels->open_channels);
}

struct utcp_channel* utcp_channels_get_channel(struct utcp_channels* utcp_channels, struct utcp_bunch* utcp_bunch)
{
	struct utcp_channel* utcp_channel = utcp_channels->Channels[utcp_bunch->ChIndex];
	if (!utcp_channel)
	{
		if (utcp_bunch->bOpen)
		{
			utcp_channel = alloc_utcp_channel(utcp_channels->InitInReliable, utcp_channels->InitOutReliable);
			utcp_channels->Channels[utcp_bunch->ChIndex] = utcp_channel;
			opened_channels_add(&utcp_channels->open_channels, utcp_bunch->ChIndex);

			utcp_log(Log, "create channel:%hu", utcp_bunch->ChIndex);
		}
		else
		{
			assert(false);
			utcp_log(Warning, "utcp_get_channel failed");
		}
	}
	if (utcp_bunch->bClose && utcp_channel)
	{
		mark_channel_close(utcp_channel, utcp_bunch->CloseReason);
		utcp_channels->bHasChannelClose = true;
	}
	return utcp_channel;
}

void utcp_channels_on_ack(struct utcp_channels* utcp_channels, int32_t AckPacketId)
{
	struct utcp_bunch_node* utcp_bunch_node[UTCP_RELIABLE_BUFFER];
	for (int j = 0; j < utcp_channels->open_channels.num; ++j)
	{
		uint16_t ChIndex = utcp_channels->open_channels.channels[j];
		assert(utcp_channels->Channels[ChIndex]);

		struct utcp_channel* utcp_channel = utcp_channels->Channels[ChIndex];
		int count = remove_ougoing_data(utcp_channel, AckPacketId, utcp_bunch_node, _countof(utcp_bunch_node));
		for (int i = 0; i < count; ++i)
		{
			free_utcp_bunch_node(utcp_bunch_node[i]);
		}
	}
}

void utcp_channels_on_nak(struct utcp_channels* utcp_channels, int32_t NakPacketId, write_bunch_fn WriteBitsToSendBuffer, struct utcp_connection* fd)
{
	struct utcp_bunch_node* utcp_bunch_node[UTCP_RELIABLE_BUFFER];
	for (int j = 0; j < utcp_channels->open_channels.num; ++j)
	{
		uint16_t ChIndex = utcp_channels->open_channels.channels[j];
		assert(utcp_channels->Channels[ChIndex]);

		// UChannel::ReceivedNak
		struct utcp_channel* utcp_channel = utcp_channels->Channels[ChIndex];
		int count = remove_ougoing_data(utcp_channel, NakPacketId, utcp_bunch_node, _countof(utcp_bunch_node));
		for (int i = 0; i < count; ++i)
		{
			int32_t packet_id = WriteBitsToSendBuffer(fd, (char*)utcp_bunch_node[i]->bunch_data, utcp_bunch_node[i]->bunch_data_len);
			utcp_bunch_node[i]->packet_id = packet_id;
			add_ougoing_data(utcp_channel, utcp_bunch_node[i]);

			utcp_log(Log, "ReceivedNak resending %d-->%d", NakPacketId, packet_id);
		}
	}
}

void utcp_delay_close_channel(struct utcp_channels* utcp_channels)
{
	if (!utcp_channels->bHasChannelClose)
		return;
	utcp_channels->bHasChannelClose = false;

	for (int i = utcp_channels->open_channels.num; i > 0; --i)
	{
		uint16_t ChIndex = utcp_channels->open_channels.channels[i - 1];
		if (utcp_channels->Channels[ChIndex] && !utcp_channels->Channels[ChIndex]->bClose)
			continue;

		if (utcp_channels->Channels[ChIndex])
		{
			utcp_close_channel(utcp_channels, ChIndex);
		}
		else
		{
			opened_channels_remove(&utcp_channels->open_channels, ChIndex);
			utcp_log(Warning, "fd->Channels is null:%hu", ChIndex);
		}
	}
}