// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "rudp_def.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

bool rudp_bunch_read(struct rudp_bunch* rudp_bunch, struct bitbuf* bitbuf);
bool rudp_bunch_write_header(const struct rudp_bunch* rudp_bunch, struct bitbuf* bitbuf);
