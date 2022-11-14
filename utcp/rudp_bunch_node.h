// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "3rd/dl_list.h"
#include "rudp_def.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/*
------------------------------------------------------------------------------
|Ethernet  | IPv4         |UDP    | Data                   |Ethernet checksum|
------------------------------------------------------------------------------
14 bytes    20 bytes     8 bytes    x bytes                4 bytes
			\ (w/o options)                               /
			 \___________________________________________/
								  |
								 MTU
The ethernet data payload is 1500 bytes. IPv4 requires a minimum of 20 bytes for its header.
Alternatively IPv6 requires a minimum of 40 bytes. UDP requires 8 bytes for its header.
That leaves 1472 bytes (ipv4) or 1452 (ipv6) for your data.
*/
#define UDP_MTU_SIZE (1452)

struct rudp_bunch_node
{
	struct dl_list_node dl_list_node;

	union {
		int32_t sequence;
		int32_t packet_id;
	};

	union {
		struct rudp_bunch rudp_bunch;
		struct 
		{
			uint16_t rudp_bunch_data_len;
			uint8_t rudp_bunch_data[UDP_MTU_SIZE];
		};
	};
};

struct rudp_bunch_node* alloc_rudp_bunch_node(struct rudp_fd* fd);
void free_rudp_bunch_node(struct rudp_fd* fd, struct rudp_bunch_node* rudp_bunch_node);

// sequence = rudp_bunch.ChSequence
void enqueue_incoming_data(struct rudp_fd* fd, struct rudp_bunch_node* rudp_bunch_node);
struct rudp_bunch_node* dequeue_incoming_data(struct rudp_fd* fd);

void add_outcoming_data(struct rudp_fd* fd, struct rudp_bunch_node* rudp_bunch_node);
struct rudp_bunch_node* remove_outcoming_data(struct rudp_fd* fd, int32_t packet_id);

bool push_partial_data(struct rudp_fd* fd, struct rudp_bunch_node* rudp_bunch_node);
