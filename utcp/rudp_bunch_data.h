// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "rudp_bunch_data_def.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

void init_rudp_bunch_data(struct rudp_bunch_data* rudp_bunch_data);

struct rudp_bunch_node* alloc_rudp_bunch_node(struct rudp_bunch_data* rudp_bunch_data);
void free_rudp_bunch_node(struct rudp_bunch_data* rudp_bunch_data, struct rudp_bunch_node* rudp_bunch_node);

bool enqueue_incoming_data(struct rudp_bunch_data* rudp_bunch_data, struct rudp_bunch_node* rudp_bunch_node);
struct rudp_bunch_node* dequeue_incoming_data(struct rudp_bunch_data* rudp_bunch_data, int sequence);

int merge_partial_data(struct rudp_bunch_data* rudp_bunch_data, struct rudp_bunch_node* rudp_bunch_node, bool* bOutSkipAck);
void clear_partial_data(struct rudp_bunch_data* rudp_bunch_data);
int get_partial_bunch(struct rudp_bunch_data* rudp_bunch_data, struct rudp_bunch* bunches[], int bunches_size);

void add_outcoming_data(struct rudp_bunch_data* rudp_bunch_data, struct rudp_bunch_node* rudp_bunch_node);
int remove_outcoming_data(struct rudp_bunch_data* rudp_bunch_data, int32_t packet_id, struct rudp_bunch_node* bunch_node[], int bunch_node_size);
