#include "utcp_channel.h"
#include <assert.h>
#include <string.h>

#include "utcp_def.h"
#include "utcp_utils.h"

struct utcp_channel* utcp_get_channel(struct utcp_fd* fd, int ChIndex)
{
	return fd->Channels[ChIndex];
}

struct utcp_channel* utcp_open_channel(struct utcp_fd* fd, int ChIndex)
{
	assert(!fd->Channels[ChIndex]);
	fd->Channels[ChIndex] = alloc_utcp_channel(fd->InitInReliable, fd->InitOutReliable);
	return fd->Channels[ChIndex];
}

void utcp_close_channel(struct utcp_fd* fd, int ChIndex)
{
	if (!fd->Channels[ChIndex])
		return;
	free_utcp_channel(fd->Channels[ChIndex]);
	fd->Channels[ChIndex] = NULL;
}

void utcp_closeall_channel(struct utcp_fd* fd)
{
	for (int i = 0; i < _countof(fd->Channels); ++i)
	{
		if (fd->Channels[i])
		{
			utcp_close_channel(fd, i);
		}
	}
}

struct utcp_channel* alloc_utcp_channel(int32_t InitInReliable, int32_t InitOutReliable)
{
	struct utcp_channel* utcp_channel = (struct utcp_channel*)malloc(sizeof(*utcp_channel));
	memset(utcp_channel, 0, sizeof(*utcp_channel));

	utcp_channel->InReliable = InitInReliable;
	utcp_channel->OutReliable = InitOutReliable;
	dl_list_init(&utcp_channel->InRec);
	dl_list_init(&utcp_channel->InPartialBunch);
	dl_list_init(&utcp_channel->OutRec);
	return utcp_channel;
}

void free_utcp_channel(struct utcp_channel* utcp_channel)
{
	while (!dl_list_empty(&utcp_channel->InRec))
	{
		struct dl_list_node* dl_list_node = dl_list_pop_front(&utcp_channel->InRec);
		struct utcp_bunch_node* cur_utcp_bunch_node = CONTAINING_RECORD(dl_list_node, struct utcp_bunch_node, dl_list_node);
		free(cur_utcp_bunch_node);
	}

	free(utcp_channel);
}

struct utcp_bunch_node* alloc_utcp_bunch_node()
{
	struct utcp_bunch_node* utcp_bunch_node = (struct utcp_bunch_node*)malloc(sizeof(*utcp_bunch_node));
	memset(&utcp_bunch_node->dl_list_node, 0, sizeof(utcp_bunch_node->dl_list_node));
	return utcp_bunch_node;
}

void free_utcp_bunch_node(struct utcp_bunch_node* utcp_bunch_node)
{
	assert(!utcp_bunch_node->dl_list_node.next);
	assert(!utcp_bunch_node->dl_list_node.prev);
	free(utcp_bunch_node);
}

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
	dl_list_push_front(dl_list_node, &utcp_bunch_node->dl_list_node);
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
		return NULL;
	}

	dl_list_pop_front(&utcp_channel->InRec);
	utcp_channel->NumInRec--;
	utcp_log(Verbose, "dequeue_incoming_data: ChIndex=%d ChSeq=%d", utcp_bunch_node->utcp_bunch.ChIndex, utcp_bunch_node->utcp_bunch.ChSequence);
	return utcp_bunch_node;
}
