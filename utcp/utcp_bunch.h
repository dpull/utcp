// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include "bit_buffer.h"
#include "utcp_bunch_data_def.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

bool utcp_bunch_read(struct utcp_bunch* utcp_bunch, struct bitbuf* bitbuf);
bool utcp_bunch_write_header(const struct utcp_bunch* utcp_bunch, struct bitbuf* bitbuf);
