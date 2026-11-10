#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gs_esp32_protocol.h"

typedef enum {
    GS_LINK_UART = 0,
    GS_LINK_USB_JTAG = 1,
    GS_LINK_UDP = 2,
} gs_link_kind;

bool gs_link_start(void);
void gs_link_send(gs_link_kind link, uint8_t type, const uint8_t *payload, size_t size);
void gs_link_push(uint8_t type, const uint8_t *payload, size_t size);
gs_link_kind gs_link_active(void);
void gs_link_received(gs_link_kind link, const gs_esp32_frame *frame);
bool gs_link_baud_allowed(uint32_t baud);
void gs_link_set_baud(gs_link_kind link, uint32_t baud);
void gs_link_flush(void);
