#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gs_esp32_protocol.h"

bool gs_wifi_available(void);
bool gs_wifi_start(const char *name, const uint8_t mac[6]);
bool gs_wifi_configure(const char *ssid, const char *password, const uint8_t key[GS_ESP32_KEY_SIZE]);
bool gs_wifi_forget(void);
uint8_t gs_wifi_state(void);
void gs_wifi_ipv4(uint8_t out[4]);
void gs_wifi_rename(const char *name);
void gs_wifi_send(const uint8_t *frame, size_t size);
