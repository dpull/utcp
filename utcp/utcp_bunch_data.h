// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "utcp_bunch_data_def.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

void init_utcp_bunch_data(struct utcp_bunch_data* utcp_bunch_data);

int merge_partial_data(struct utcp_bunch_data* utcp_bunch_data, struct utcp_bunch_node* utcp_bunch_node, bool* bOutSkipAck);
void clear_partial_data(struct utcp_bunch_data* utcp_bunch_data);
int get_partial_bunch(struct utcp_bunch_data* utcp_bunch_data, struct utcp_bunch* bunches[], int bunches_size);

void add_ougoing_data(struct utcp_bunch_data* utcp_bunch_data, struct utcp_bunch_node* utcp_bunch_node);
int remove_ougoing_data(struct utcp_bunch_data* utcp_bunch_data, int32_t packet_id, struct utcp_bunch_node* bunch_node[], int bunch_node_size);
