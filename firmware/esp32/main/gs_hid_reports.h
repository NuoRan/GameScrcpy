#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gs_esp32_protocol.h"
#include "gs_hid_descriptors.h"

size_t gs_report_touch(const gs_esp32_touch *touch, uint8_t out[GS_TOUCH_REPORT_SIZE]);
size_t gs_report_keyboard(const gs_esp32_keyboard *keyboard, uint8_t out[GS_KEYBOARD_REPORT_SIZE]);
size_t gs_report_consumer(uint16_t usage, uint8_t out[GS_CONSUMER_REPORT_SIZE]);
size_t gs_report_mouse(const gs_esp32_mouse *mouse, uint8_t out[GS_MOUSE_REPORT_SIZE]);
size_t gs_report_gamepad(const gs_esp32_gamepad *gamepad, uint8_t out[GS_GAMEPAD_REPORT_SIZE]);
bool gs_report_lifts(const gs_esp32_touch *touch);
