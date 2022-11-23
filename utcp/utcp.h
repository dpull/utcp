// Copyright DPULL, Inc. All Rights Reserved.

#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct utcp_listener;
struct utcp_connection;
struct utcp_bunch;

struct utcp_config* utcp_get_config();
void utcp_add_time(int64_t delta_time_ns);

void utcp_listener_init(struct utcp_listener* fd, void* userdata);
void utcp_listener_update_secret(struct utcp_listener* fd, uint8_t special_secret[64] /* = NULL*/);
int utcp_listener_incoming(struct utcp_listener* fd, const char* address, const uint8_t* buffer, int len);
void utcp_listener_accept(struct utcp_listener* fd, struct utcp_connection* conn, bool reconnect);

void utcp_init(struct utcp_connection* fd, void* userdata, int is_client);
void utcp_uninit(struct utcp_connection* fd);

void utcp_connect(struct utcp_connection* fd);
void utcp_sequence_init(struct utcp_connection* fd, int32_t IncomingSequence, int32_t OutgoingSequence);

int utcp_ordered_incoming(struct utcp_connection* fd, uint8_t* buffer, int len);
int utcp_update(struct utcp_connection* fd);

int32_t utcp_peep_packet_id(struct utcp_connection* fd, uint8_t* buffer, int len);
int32_t utcp_expect_packet_id(struct utcp_connection* fd);

int32_t utcp_send_bunch(struct utcp_connection* fd, struct utcp_bunch* bunch);
int utcp_flush(struct utcp_connection* fd);

#ifdef __cplusplus
}
#endif
