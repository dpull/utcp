#include "test_utils.h"
#include "gtest/gtest.h"
#include <vector>

void reset_nodes(utcp_bunch_node_raii nodes[], size_t size)
{
	for (int i = 0; i < size; ++i)
	{
		nodes[i].reset();
	}
}

TEST(channel, same_incoming)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes[1];

	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		node->utcp_bunch.ChSequence = i;
		node->utcp_bunch.bReliable = true;
	}

	ASSERT_TRUE(enqueue_incoming_data(&channel, &nodes[0]));
	ASSERT_FALSE(enqueue_incoming_data(&channel, &nodes[0]));

	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		ASSERT_EQ(dequeue_incoming_data(&channel, i), node);
	}
	ASSERT_EQ((&channel)->NumInRec, 0);
}

TEST(channel, free_incoming)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes[1];

	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		node->utcp_bunch.ChSequence = i;
		node->utcp_bunch.bReliable = true;
	}

	ASSERT_TRUE(enqueue_incoming_data(&channel, &nodes[0]));
	ASSERT_FALSE(enqueue_incoming_data(&channel, &nodes[0]));

	reset_nodes(nodes, std::size(nodes));
	ASSERT_EQ((&channel)->NumInRec, 1);
}

TEST(channel, order_incoming)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes[4];
	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		node->utcp_bunch.ChSequence = i;
		node->utcp_bunch.bReliable = true;
	}

	ASSERT_TRUE(enqueue_incoming_data(&channel, &nodes[2]));
	ASSERT_TRUE(enqueue_incoming_data(&channel, &nodes[0]));
	ASSERT_TRUE(enqueue_incoming_data(&channel, &nodes[1]));
	ASSERT_TRUE(enqueue_incoming_data(&channel, &nodes[3]));

	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		ASSERT_EQ(dequeue_incoming_data(&channel, i), node);
	}
	ASSERT_EQ((&channel)->NumInRec, 0);
}

TEST(channel, ougoing)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes[4];
	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		node->packet_id = i < 2 ? 1 : 2;

		add_ougoing_data(&channel, &nodes[i]);
		ASSERT_EQ((&channel)->NumOutRec, i + 1);
	}
	struct utcp_bunch_node* bunch_node[UTCP_RELIABLE_BUFFER];
	int bunch_node_size;

	bunch_node_size = remove_ougoing_data(&channel, 1, bunch_node, (int)std::size(bunch_node));
	ASSERT_EQ(bunch_node_size, 2);
	ASSERT_EQ((&channel)->NumOutRec, 2);

	bunch_node_size = remove_ougoing_data(&channel, 2, bunch_node, (int)std::size(bunch_node));
	ASSERT_EQ(bunch_node_size, 2);
	ASSERT_EQ((&channel)->NumOutRec, 0);
}

TEST(channel, free_ougoing)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes[1];

	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		node->packet_id = i < 2 ? 1 : 2;
		add_ougoing_data(&channel, node);
		ASSERT_EQ((&channel)->NumOutRec, i + 1);
	}

	reset_nodes(nodes, std::size(nodes));
	ASSERT_EQ((&channel)->NumOutRec, 1);
}

TEST(channel, partial_reliable)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes[4];
	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		node->utcp_bunch.ChSequence = i;
		node->utcp_bunch.bReliable = true;
		node->utcp_bunch.bPartial = true;
		node->utcp_bunch.bPartialInitial = i == 0;
		node->utcp_bunch.bPartialFinal = (i + 1 == std::size(nodes));
	}

	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		bool bOutSkipAck;
		int ret = merge_partial_data(&channel, node, &bOutSkipAck);
		if (i + 1 != std::size(nodes))
		{
			ASSERT_EQ(ret, partial_merge_succeed);
		}
		else
		{
			ASSERT_EQ(ret, partial_available);
		}
		ASSERT_FALSE(bOutSkipAck);
	}

	struct utcp_bunch* handle_bunches[UTCP_MAX_PARTIAL_BUNCH_COUNT];
	ASSERT_EQ(get_partial_bunch(&channel, handle_bunches, (int)std::size(handle_bunches)), 4);

	reset_nodes(nodes, std::size(nodes));
	clear_partial_data(&channel);
}

TEST(channel, partial_reliable_failed)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes[4];
	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		node->utcp_bunch.ChSequence = i * 2;
		node->utcp_bunch.bReliable = true;
		node->utcp_bunch.bPartial = true;
		node->utcp_bunch.bPartialInitial = i == 0;
		node->utcp_bunch.bPartialFinal = (i + 1 == std::size(nodes));
	}

	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		bool bOutSkipAck;
		int ret = merge_partial_data(&channel, node, &bOutSkipAck);
		if (i == 0)
		{
			ASSERT_EQ(ret, partial_merge_succeed);
			ASSERT_FALSE(bOutSkipAck);
			nodes[i].reset();
		}
		else
		{
			ASSERT_EQ(ret, partial_merge_fatal);
			ASSERT_TRUE(bOutSkipAck);
		}
	}
	clear_partial_data(&channel);
}

TEST(channel, partial_reliable_none_initial)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes[2];
	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		node->utcp_bunch.ChSequence = i;
		node->utcp_bunch.bReliable = true;
		node->utcp_bunch.bPartial = true;
		node->utcp_bunch.bPartialInitial = 0;
		node->utcp_bunch.bPartialFinal = 0;
	}

	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		bool bOutSkipAck;
		ASSERT_EQ(merge_partial_data(&channel, node, &bOutSkipAck), partial_merge_failed);
	}
}

TEST(channel, partial_unreliable)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes[4];
	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		node->utcp_bunch.ChSequence = i;
		node->utcp_bunch.bReliable = false;
		node->utcp_bunch.bPartial = true;
		node->utcp_bunch.bPartialInitial = i == 0;
		node->utcp_bunch.bPartialFinal = (i + 1 == std::size(nodes));
	}

	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		bool bOutSkipAck;
		int ret = merge_partial_data(&channel, node, &bOutSkipAck);
		if (i + 1 != std::size(nodes))
			ASSERT_EQ(ret, partial_merge_succeed);
		else
			ASSERT_EQ(ret, partial_available);
		ASSERT_FALSE(bOutSkipAck);
	}

	struct utcp_bunch* handle_bunches[UTCP_MAX_PARTIAL_BUNCH_COUNT];
	ASSERT_EQ(get_partial_bunch(&channel, handle_bunches, (int)std::size(handle_bunches)), std::size(nodes));

	for (int i = 0; i < std::size(nodes); ++i)
	{
		ASSERT_EQ(handle_bunches[i]->ChSequence, i);
	}

	reset_nodes(nodes, std::size(nodes));

	clear_partial_data(&channel);
}

TEST(channel, partial_unreliable_failed)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes[4];
	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		node->utcp_bunch.ChSequence = i * 2;
		node->utcp_bunch.bReliable = false;
		node->utcp_bunch.bPartial = true;
		node->utcp_bunch.bPartialInitial = i == 0;
		node->utcp_bunch.bPartialFinal = (i + 1 == std::size(nodes));
	}

	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		bool bOutSkipAck;
		int ret = merge_partial_data(&channel, node, &bOutSkipAck);

		if (i == 0)
		{
			ASSERT_EQ(ret, partial_merge_succeed);
			ASSERT_FALSE(bOutSkipAck);
			nodes[i].reset();
		}
		else
		{
			ASSERT_EQ(ret, partial_merge_failed);
			ASSERT_TRUE(bOutSkipAck);
		}
	}
}

TEST(channel, partial_unreliable_none_initial)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes[2];
	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		node->utcp_bunch.ChSequence = i;
		node->utcp_bunch.bReliable = false;
		node->utcp_bunch.bPartial = true;
		node->utcp_bunch.bPartialInitial = 0;
		node->utcp_bunch.bPartialFinal = 0;
	}

	for (int i = 0; i < std::size(nodes); ++i)
	{
		auto node = &nodes[i];
		bool bOutSkipAck;
		ASSERT_EQ(merge_partial_data(&channel, node, &bOutSkipAck), partial_merge_failed);
	}
}

TEST(channel, partial_unreliable_reliable)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes1[2];
	for (int i = 0; i < std::size(nodes1); ++i)
	{
		auto node = &nodes1[i];
		node->utcp_bunch.ChSequence = i;
		node->utcp_bunch.bReliable = false;
		node->utcp_bunch.bPartial = true;
		node->utcp_bunch.bPartialInitial = i == 0;
		node->utcp_bunch.bPartialFinal = 0;
	}

	utcp_bunch_node_raii nodes2[2];
	for (int i = 0; i < std::size(nodes1); ++i)
	{
		auto node = &nodes2[i];
		node->utcp_bunch.ChSequence = i;
		node->utcp_bunch.bReliable = true;
		node->utcp_bunch.bPartial = true;
		node->utcp_bunch.bPartialInitial = i == 0;
		node->utcp_bunch.bPartialFinal = 0;
	}

	for (int i = 0; i < std::size(nodes1); ++i)
	{
		auto node = &nodes1[i];
		bool bOutSkipAck;
		ASSERT_EQ(merge_partial_data(&channel, node, &bOutSkipAck), partial_merge_succeed);
		nodes1[i].reset();
	}

	for (int i = 0; i < std::size(nodes2); ++i)
	{
		auto node = &nodes2[i];
		bool bOutSkipAck;
		ASSERT_EQ(merge_partial_data(&channel, node, &bOutSkipAck), partial_merge_succeed);
		nodes2[i].reset();
	}
}

TEST(channel, partial_reliable_unreliable)
{
	utcp_channel_rtti channel;
	utcp_bunch_node_raii nodes1[2];
	for (int i = 0; i < std::size(nodes1); ++i)
	{
		auto node = &nodes1[i];
		node->utcp_bunch.ChSequence = i;
		node->utcp_bunch.bReliable = false;
		node->utcp_bunch.bPartial = true;
		node->utcp_bunch.bPartialInitial = i == 0;
		node->utcp_bunch.bPartialFinal = 0;
	}

	utcp_bunch_node_raii nodes2[2];
	for (int i = 0; i < std::size(nodes1); ++i)
	{
		auto node = &nodes2[i];
		node->utcp_bunch.ChSequence = i;
		node->utcp_bunch.bReliable = true;
		node->utcp_bunch.bPartial = true;
		node->utcp_bunch.bPartialInitial = i == 0;
		node->utcp_bunch.bPartialFinal = 0;
	}

	for (int i = 0; i < std::size(nodes2); ++i)
	{
		auto node = &nodes2[i];
		bool bOutSkipAck;
		ASSERT_EQ(merge_partial_data(&channel, node, &bOutSkipAck), partial_merge_succeed);
		nodes2[i].reset();
	}

	for (int i = 0; i < std::size(nodes1); ++i)
	{
		auto node = &nodes1[i];
		bool bOutSkipAck;
		ASSERT_EQ(merge_partial_data(&channel, node, &bOutSkipAck), partial_merge_failed);
	}
}
