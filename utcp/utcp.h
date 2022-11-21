// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "utcp_def.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct utcp_config* utcp_get_config();
void utcp_add_time(int64_t delta_time_ns);

void utcp_init(struct utcp_fd* fd, void* userdata, int is_client);

void utcp_connect(struct utcp_fd* fd);

int utcp_connectionless_incoming(struct utcp_fd* fd, const char* address, const uint8_t* buffer, int len);
void utcp_sequence_init(struct utcp_fd* fd, int32_t IncomingSequence, int32_t OutgoingSequence);

int utcp_ordered_incoming(struct utcp_fd* fd, uint8_t* buffer, int len);
int utcp_update(struct utcp_fd* fd);

int32_t utcp_peep_packet_id(struct utcp_fd* fd, uint8_t* buffer, int len);
int32_t utcp_expect_packet_id(struct utcp_fd* fd);

int32_t utcp_send_bunch(struct utcp_fd* fd, struct utcp_bunch* bunch);
struct packet_id_range utcp_send_bunches(struct utcp_fd* fd, struct utcp_bunch* bunches[], int bunches_count);
int utcp_flush(struct utcp_fd* fd);

#ifdef __cplusplus
}
#endif