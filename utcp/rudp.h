// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	PACKET_ID_INDEX_NONE = -1
};

struct packet_id_range
{
	int32_t First;
	int32_t Last;
};

enum callback_type
{
	callback_newconn,
	callback_reconn,
	callback_send,
	callback_recv_bunches,
};
typedef int (*callback_fn)(struct rudp_fd* fd, void* userdata, enum callback_type callback_type, const void* data, int len);

void rudp_env_init(void);
void rudp_env_add_time(int64_t delta_time_ms);
void rudp_env_setcallback(callback_fn callback);
void rudp_env_set_debug_cookie(const uint8_t debug_cookie[20]);

struct rudp_fd* rudp_create();
void rudp_destory(struct rudp_fd* fd);

void rudp_init(struct rudp_fd* fd, void* userdata, int is_client);
int rudp_incoming(struct rudp_fd* fd, char* buffer, int len);
int rudp_accept_incoming(struct rudp_fd* fd, const char* address, const char* buffer, int len);
int rudp_update(struct rudp_fd* fd);

struct packet_id_range rudp_send(struct rudp_fd* fd, const struct rudp_bunch* bunches[], int bunches_count);

void rudp_sequence_init(struct rudp_fd* fd, int32_t IncomingSequence, int32_t OutgoingSequence);
int rudp_flush(struct rudp_fd* fd);

#ifdef __cplusplus
}
#endif