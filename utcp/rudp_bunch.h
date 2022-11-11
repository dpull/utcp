#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

bool rudp_bunch_read(struct rudp_bunch* rudp_bunch, struct bitbuf* bitbuf);
bool rudp_bunch_write(struct rudp_bunch* rudp_bunch, struct bitbuf* bitbuf, struct rudp_fd* fd);
