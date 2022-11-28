// Copyright DPULL, Inc. All Rights Reserved.

#pragma once
#include "utcp_def.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// global API
struct utcp_config* utcp_get_config();
void utcp_add_elapsed_time(int64_t delta_time_ns);

// listener API
struct utcp_listener* utcp_listener_create();
void utcp_listener_destroy(struct utcp_listener* fd);

void utcp_listener_init(struct utcp_listener* fd, void* userdata);
void utcp_listener_update_secret(struct utcp_listener* fd, uint8_t special_secret[64] /* = NULL*/);
int utcp_listener_incoming(struct utcp_listener* fd, const char* address, const uint8_t* buffer, int len);
void utcp_listener_accept(struct utcp_listener* listener, struct utcp_connection* conn, bool reconnect);

// connection API
struct utcp_connection* utcp_connection_create();
void utcp_connection_destroy(struct utcp_connection* fd);

void utcp_init(struct utcp_connection* fd, void* userdata);
void utcp_uninit(struct utcp_connection* fd);

void utcp_connect(struct utcp_connection* fd);

bool utcp_incoming(struct utcp_connection* fd, uint8_t* buffer, int len);
int utcp_update(struct utcp_connection* fd);

int32_t utcp_peep_packet_id(struct utcp_connection* fd, uint8_t* buffer, int len);
int32_t utcp_expect_packet_id(struct utcp_connection* fd);

int32_t utcp_send_bunch(struct utcp_connection* fd, struct utcp_bunch* bunch);
int utcp_send_flush(struct utcp_connection* fd);
bool utcp_send_would_block(struct utcp_connection* fd, int count);

void utcp_mark_close(struct utcp_connection* fd, uint8_t close_reason);

#ifdef __cplusplus
}
#endif
