// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "rudp_def.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

bool rudp_bunch_read(struct rudp_bunch* rudp_bunch, struct bitbuf* bitbuf, struct rudp_fd* fd);
bool rudp_bunch_write(struct rudp_bunch* rudp_bunch, struct bitbuf* bitbuf, struct rudp_fd* fd);
