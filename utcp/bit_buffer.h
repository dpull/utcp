// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct bitbuf
{
	uint8_t* buffer;
	size_t size;
	size_t num;
};

size_t bitbuf_num_bytes(struct bitbuf* buff);
size_t bitbuf_left_bits(struct bitbuf* buff);
size_t bitbuf_left_bytes(struct bitbuf* buff);

bool bitbuf_write_init(struct bitbuf* buff, uint8_t* buffer, size_t size);
void bitbuf_write_reuse(struct bitbuf* buff, uint8_t* buffer, size_t num_bits, size_t size);
bool bitbuf_write_bit(struct bitbuf* buff, uint8_t value);
bool bitbuf_write_bits(struct bitbuf* buff, const void* data, size_t bits_size);
bool bitbuf_write_bytes(struct bitbuf* buff, const void* data, size_t size);
bool bitbuf_write_int(struct bitbuf* buff, uint32_t value, uint32_t value_max);
bool bitbuf_write_int_packed(struct bitbuf* buff, uint32_t value);
bool bitbuf_write_int_wrapped(struct bitbuf* buff, uint32_t value, uint32_t value_max);

bool bitbuf_read_init(struct bitbuf* buff, const uint8_t* data, size_t len);
bool bitbuf_read_bit(struct bitbuf* buff, uint8_t* value);
bool bitbuf_read_bits(struct bitbuf* buff, void* buffer, size_t bits_size);
bool bitbuf_read_bytes(struct bitbuf* buff, void* buffer, size_t size);
bool bitbuf_read_int(struct bitbuf* buff, uint32_t* value, uint32_t value_max);
bool bitbuf_read_int_packed(struct bitbuf* buff, uint32_t* value);
