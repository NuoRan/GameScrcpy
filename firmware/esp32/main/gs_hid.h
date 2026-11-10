#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gs_esp32_protocol.h"

bool gs_hid_start(const char *name, const uint8_t mac[6]);
bool gs_hid_touch(const gs_esp32_touch *touch);
bool gs_hid_keyboard(const gs_esp32_keyboard *keyboard);
bool gs_hid_consumer(uint16_t usage);
bool gs_hid_mouse(const gs_esp32_mouse *mouse);
bool gs_hid_gamepad(const gs_esp32_gamepad *gamepad);
void gs_hid_phone(gs_esp32_status *status);
uint32_t gs_hid_features(void);
void gs_hid_rename(const char *name);
