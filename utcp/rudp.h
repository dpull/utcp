// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "rudp_def.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rudp_config* rudp_get_config();
void rudp_add_time(int64_t delta_time_ns);

void rudp_init(struct rudp_fd* fd, void* userdata, int is_client);

int rudp_connectionless_incoming(struct rudp_fd* fd, const char* address, const char* buffer, int len);
void rudp_sequence_init(struct rudp_fd* fd, int32_t IncomingSequence, int32_t OutgoingSequence);

int rudp_incoming(struct rudp_fd* fd, char* buffer, int len);
int rudp_update(struct rudp_fd* fd);

struct packet_id_range rudp_send(struct rudp_fd* fd, struct rudp_bunch* bunches[], int bunches_count);
int rudp_flush(struct rudp_fd* fd);

#ifdef __cplusplus
}
#endif