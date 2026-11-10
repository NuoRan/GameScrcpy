#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gs_esp32_protocol.h"
#include "gs_link.h"

void gs_board_init(void);
void gs_board_start(void);
void gs_board_start_parts(void);
const char *gs_board_name(void);
const uint8_t *gs_board_mac(void);
void gs_board_handle(gs_link_kind link, const gs_esp32_frame *frame);
void gs_board_release(void);
