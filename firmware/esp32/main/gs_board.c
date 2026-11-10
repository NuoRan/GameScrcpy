#include "gs_board.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include "gs_download.h"
#include "gs_hid.h"
#include "gs_wifi.h"

#define GS_STATUS_PERIOD_MS 50
#define GS_NVS_NAMESPACE "gs"
#define GS_NVS_NAME "name"

static const char *gs_tag = "gs";
static SemaphoreHandle_t gs_hid_lock;
static volatile int64_t gs_last_frame;
static gs_esp32_touch gs_touch;
static bool gs_keys_down;
static bool gs_consumer_down;
static bool gs_buttons_down;
static bool gs_gamepad_moved;
static char gs_name[GS_ESP32_MAX_NAME + 1];
static uint8_t gs_mac[6];
static gs_esp32_status gs_pushed;
static gs_esp32_parts gs_parts;

static void gs_copy_text(char *out, size_t capacity, const char *text)
{
    const size_t length = strnlen(text, capacity - 1);
    memcpy(out, text, length);
    out[length] = '\0';
}

static void gs_send_error(gs_link_kind link, uint16_t code, const char *text)
{
    gs_esp32_error error;
    uint8_t payload[GS_ESP32_MAX_TEXT + 3];
    memset(&error, 0, sizeof(error));
    error.code = code;
    gs_copy_text(error.text, sizeof(error.text), text);
    const size_t size = gs_esp32_encode_error(payload, sizeof(payload), &error);
    if (size > 0) {
        gs_link_send(link, GS_ESP32_MSG_ERROR, payload, size);
    }
}

static void gs_send_pong(gs_link_kind link, uint32_t token)
{
    uint8_t payload[4];
    const size_t size = gs_esp32_encode_token(payload, sizeof(payload), token);
    gs_link_send(link, GS_ESP32_MSG_PONG, payload, size);
}

static void gs_send_hello(gs_link_kind link)
{
    gs_esp32_hello hello;
    esp_chip_info_t chip;
    uint8_t payload[GS_ESP32_MAX_PAYLOAD];
    const esp_app_desc_t *application = esp_app_get_description();
    memset(&hello, 0, sizeof(hello));
    esp_chip_info(&chip);
    hello.protocol = GS_ESP32_PROTOCOL_VERSION;
    gs_copy_text(hello.firmware, sizeof(hello.firmware), application->version);
    hello.chip_id = CONFIG_IDF_FIRMWARE_CHIP_ID;
    hello.chip_revision = chip.revision;
    hello.features = gs_hid_features();
    if (gs_wifi_available()) {
        hello.features |= GS_ESP32_FEATURE_WIFI;
    }
    if (link == GS_LINK_USB_JTAG) {
        hello.features |= GS_ESP32_FEATURE_USB_SERIAL_LINK;
    }
    memcpy(hello.mac, gs_mac, sizeof(hello.mac));
    hello.wifi = gs_wifi_state();
    gs_wifi_ipv4(hello.ipv4);
    gs_copy_text(hello.name, sizeof(hello.name), gs_name);
    hello.extension = GS_ESP32_HELLO_EXTENSION;
    hello.parts = gs_parts;
    if (gs_parts.usb_problem == GS_ESP32_PART_OK || gs_parts.ble_problem == GS_ESP32_PART_OK) {
        gs_esp32_status live;
        memset(&live, 0, sizeof(live));
        gs_hid_phone(&live);
        hello.parts.usb = live.usb;
        hello.parts.ble = live.ble;
    }
    const size_t size = gs_esp32_encode_hello(payload, sizeof(payload), &hello);
    if (size > 0) {
        gs_link_send(link, GS_ESP32_MSG_HELLO, payload, size);
    }
}

static void gs_parts_defaults(void)
{
    const uint32_t features = gs_hid_features();
    gs_parts.usb = GS_ESP32_USB_NONE;
    gs_parts.ble = GS_ESP32_BLE_NONE;
    gs_parts.usb_problem =
        (features & GS_ESP32_FEATURE_USB_HID) != 0 ? GS_ESP32_PART_PENDING : GS_ESP32_PART_UNSUPPORTED;
    gs_parts.ble_problem =
        (features & GS_ESP32_FEATURE_BLE_HID) != 0 ? GS_ESP32_PART_PENDING : GS_ESP32_PART_UNSUPPORTED;
    gs_parts.wifi_problem = gs_wifi_available() ? GS_ESP32_PART_PENDING : GS_ESP32_PART_UNSUPPORTED;
}

static const char *gs_part_text(uint8_t problem)
{
    switch (problem) {
    case GS_ESP32_PART_OK:
        return "ready";
    case GS_ESP32_PART_FAILED:
        return "did not start";
    case GS_ESP32_PART_UNSUPPORTED:
        return "not on this board";
    default:
        return "starting";
    }
}

static void gs_read_status(gs_esp32_status *status)
{
    memset(status, 0, sizeof(*status));
    gs_hid_phone(status);
    status->wifi = gs_wifi_state();
    gs_wifi_ipv4(status->ipv4);
}

static bool gs_same_status(const gs_esp32_status *a, const gs_esp32_status *b)
{
    return a->usb == b->usb && a->ble == b->ble && a->wifi == b->wifi && memcmp(a->ipv4, b->ipv4, 4) == 0 &&
           strcmp(a->peer, b->peer) == 0;
}

static void gs_push_status(const gs_esp32_status *status)
{
    uint8_t payload[GS_ESP32_MAX_PAYLOAD];
    const size_t size = gs_esp32_encode_status(payload, sizeof(payload), status);
    if (size > 0) {
        gs_link_push(GS_ESP32_MSG_STATUS, payload, size);
    }
}

static bool gs_holding(void)
{
    return gs_touch.count > 0 || gs_keys_down || gs_consumer_down || gs_buttons_down || gs_gamepad_moved;
}

void gs_board_release(void)
{
    xSemaphoreTake(gs_hid_lock, portMAX_DELAY);
    if (gs_touch.count > 0) {
        for (uint8_t i = 0; i < gs_touch.count; ++i) {
            gs_touch.contacts[i].tip = 0;
        }
        gs_hid_touch(&gs_touch);
        memset(&gs_touch, 0, sizeof(gs_touch));
    }
    if (gs_keys_down) {
        gs_esp32_keyboard none;
        memset(&none, 0, sizeof(none));
        gs_hid_keyboard(&none);
        gs_keys_down = false;
    }
    if (gs_consumer_down) {
        gs_hid_consumer(0);
        gs_consumer_down = false;
    }
    if (gs_buttons_down) {
        gs_esp32_mouse none;
        memset(&none, 0, sizeof(none));
        gs_hid_mouse(&none);
        gs_buttons_down = false;
    }
    if (gs_gamepad_moved) {
        gs_esp32_gamepad centered;
        memset(&centered, 0, sizeof(centered));
        centered.hat = GS_ESP32_HAT_CENTERED;
        gs_hid_gamepad(&centered);
        gs_gamepad_moved = false;
    }
    xSemaphoreGive(gs_hid_lock);
}

static void gs_apply_touch(const gs_esp32_touch *touch)
{
    xSemaphoreTake(gs_hid_lock, portMAX_DELAY);
    gs_hid_touch(touch);
    memset(&gs_touch, 0, sizeof(gs_touch));
    for (uint8_t i = 0; i < touch->count; ++i) {
        if (touch->contacts[i].tip) {
            gs_touch.contacts[gs_touch.count] = touch->contacts[i];
            gs_touch.count++;
        }
    }
    xSemaphoreGive(gs_hid_lock);
}

static void gs_apply_keyboard(const gs_esp32_keyboard *keyboard)
{
    bool down = keyboard->modifiers != 0;
    for (int i = 0; i < GS_ESP32_KEYS; ++i) {
        down = down || keyboard->keys[i] != 0;
    }
    xSemaphoreTake(gs_hid_lock, portMAX_DELAY);
    gs_hid_keyboard(keyboard);
    gs_keys_down = down;
    xSemaphoreGive(gs_hid_lock);
}

static void gs_apply_consumer(uint16_t usage)
{
    xSemaphoreTake(gs_hid_lock, portMAX_DELAY);
    gs_hid_consumer(usage);
    gs_consumer_down = usage != 0;
    xSemaphoreGive(gs_hid_lock);
}

static void gs_apply_mouse(const gs_esp32_mouse *mouse)
{
    xSemaphoreTake(gs_hid_lock, portMAX_DELAY);
    gs_hid_mouse(mouse);
    gs_buttons_down = mouse->buttons != 0;
    xSemaphoreGive(gs_hid_lock);
}

static void gs_apply_gamepad(const gs_esp32_gamepad *gamepad)
{
    const bool centered = gamepad->buttons == 0 && gamepad->hat == GS_ESP32_HAT_CENTERED && gamepad->lx == 0 &&
                          gamepad->ly == 0 && gamepad->rx == 0 && gamepad->ry == 0 && gamepad->lt == 0 &&
                          gamepad->rt == 0;
    xSemaphoreTake(gs_hid_lock, portMAX_DELAY);
    gs_hid_gamepad(gamepad);
    gs_gamepad_moved = !centered;
    xSemaphoreGive(gs_hid_lock);
}

static bool gs_store_name(const char *name)
{
    nvs_handle_t handle;
    if (nvs_open(GS_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const bool stored = nvs_set_str(handle, GS_NVS_NAME, name) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return stored;
}

static void gs_rename(gs_link_kind link, const char *name)
{
    if (!gs_store_name(name)) {
        gs_send_error(link, GS_ESP32_ERROR_STORAGE, "could not store the name");
        return;
    }
    gs_copy_text(gs_name, sizeof(gs_name), name);
    gs_hid_rename(gs_name);
    gs_wifi_rename(gs_name);
    gs_send_hello(link);
}

static void gs_handle_wifi(gs_link_kind link, const gs_esp32_frame *frame)
{
    gs_esp32_wifi_set wifi;
    if (link == GS_LINK_UDP) {
        gs_send_error(link, GS_ESP32_ERROR_UNSUPPORTED, "Wi-Fi is set over the serial port");
        return;
    }
    if (!gs_wifi_available()) {
        gs_send_error(link, GS_ESP32_ERROR_UNSUPPORTED, "this chip has no Wi-Fi");
        return;
    }
    if (!gs_esp32_decode_wifi_set(frame->payload, frame->length, &wifi)) {
        gs_send_error(link, GS_ESP32_ERROR_MALFORMED, "malformed WIFI_SET");
        return;
    }
    if (!gs_wifi_configure(wifi.ssid, wifi.password, wifi.key)) {
        gs_send_error(link, GS_ESP32_ERROR_STORAGE, "could not store the Wi-Fi settings");
        return;
    }
    memset(&wifi, 0, sizeof(wifi));
    gs_send_hello(link);
}

void gs_board_handle(gs_link_kind link, const gs_esp32_frame *frame)
{
    gs_last_frame = esp_timer_get_time();
    switch (frame->type) {
    case GS_ESP32_MSG_HELLO_REQ:
        gs_send_hello(link);
        return;
    case GS_ESP32_MSG_TOUCH: {
        gs_esp32_touch touch;
        if (gs_esp32_decode_touch(frame->payload, frame->length, &touch)) {
            gs_apply_touch(&touch);
        } else {
            gs_send_error(link, GS_ESP32_ERROR_MALFORMED, "malformed TOUCH");
        }
        return;
    }
    case GS_ESP32_MSG_KEYBOARD: {
        gs_esp32_keyboard keyboard;
        if (gs_esp32_decode_keyboard(frame->payload, frame->length, &keyboard)) {
            gs_apply_keyboard(&keyboard);
        } else {
            gs_send_error(link, GS_ESP32_ERROR_MALFORMED, "malformed KEYBOARD");
        }
        return;
    }
    case GS_ESP32_MSG_CONSUMER: {
        uint16_t usage = 0;
        if (gs_esp32_decode_consumer(frame->payload, frame->length, &usage)) {
            gs_apply_consumer(usage);
        } else {
            gs_send_error(link, GS_ESP32_ERROR_MALFORMED, "malformed CONSUMER");
        }
        return;
    }
    case GS_ESP32_MSG_MOUSE: {
        gs_esp32_mouse mouse;
        if (gs_esp32_decode_mouse(frame->payload, frame->length, &mouse)) {
            gs_apply_mouse(&mouse);
        } else {
            gs_send_error(link, GS_ESP32_ERROR_MALFORMED, "malformed MOUSE");
        }
        return;
    }
    case GS_ESP32_MSG_GAMEPAD: {
        gs_esp32_gamepad gamepad;
        if (gs_esp32_decode_gamepad(frame->payload, frame->length, &gamepad)) {
            gs_apply_gamepad(&gamepad);
        } else {
            gs_send_error(link, GS_ESP32_ERROR_MALFORMED, "malformed GAMEPAD");
        }
        return;
    }
    case GS_ESP32_MSG_RELEASE_ALL:
        gs_board_release();
        return;
    case GS_ESP32_MSG_PING: {
        uint32_t token = 0;
        if (gs_esp32_decode_token(frame->payload, frame->length, &token)) {
            gs_send_pong(link, token);
        }
        return;
    }
    case GS_ESP32_MSG_SET_BAUD: {
        gs_esp32_set_baud request;
        if (!gs_esp32_decode_set_baud(frame->payload, frame->length, &request)) {
            gs_send_error(link, GS_ESP32_ERROR_MALFORMED, "malformed SET_BAUD");
            return;
        }
        if (!gs_link_baud_allowed(request.baud)) {
            gs_send_error(link, GS_ESP32_ERROR_UNSUPPORTED, "unsupported baud rate");
            return;
        }
        gs_send_pong(link, request.token);
        gs_link_set_baud(link, request.baud);
        return;
    }
    case GS_ESP32_MSG_WIFI_SET:
        gs_handle_wifi(link, frame);
        return;
    case GS_ESP32_MSG_WIFI_FORGET:
        if (gs_wifi_forget()) {
            gs_send_hello(link);
        } else {
            gs_send_error(link, GS_ESP32_ERROR_STORAGE, "could not forget the Wi-Fi settings");
        }
        return;
    case GS_ESP32_MSG_SET_NAME: {
        char name[GS_ESP32_MAX_NAME + 1];
        if (gs_esp32_decode_name(frame->payload, frame->length, name)) {
            gs_rename(link, name);
        } else {
            gs_send_error(link, GS_ESP32_ERROR_MALFORMED, "malformed SET_NAME");
        }
        return;
    }
    case GS_ESP32_MSG_REBOOT:
        gs_board_release();
        gs_link_flush();
        esp_restart();
        return;
    case GS_ESP32_MSG_ENTER_DOWNLOAD:
        if (!gs_download_supported()) {
            gs_send_error(link, GS_ESP32_ERROR_UNSUPPORTED, "use the reset lines to enter download mode");
            return;
        }
        gs_board_release();
        gs_link_flush();
        gs_download_enter();
        return;
    default:
        gs_send_error(link, GS_ESP32_ERROR_UNKNOWN_TYPE, "unknown message");
        return;
    }
}

static void gs_status_task(void *argument)
{
    (void)argument;
    gs_esp32_status status;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(GS_STATUS_PERIOD_MS));
        if (gs_holding() && esp_timer_get_time() - gs_last_frame > (int64_t)GS_ESP32_SAFETY_MS * 1000) {
            gs_board_release();
            ESP_LOGW(gs_tag, "nothing from the computer for 2 s: released every touch, key and button");
        }
        gs_read_status(&status);
        if (!gs_same_status(&status, &gs_pushed)) {
            gs_pushed = status;
            gs_push_status(&status);
        }
    }
}

void gs_board_init(void)
{
    nvs_handle_t handle;
    gs_hid_lock = xSemaphoreCreateMutex();
    esp_efuse_mac_get_default(gs_mac);
    snprintf(gs_name, sizeof(gs_name), "GameScrcpy %02X%02X", gs_mac[4], gs_mac[5]);
    if (nvs_open(GS_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        char stored[GS_ESP32_MAX_NAME + 1];
        size_t length = sizeof(stored);
        if (nvs_get_str(handle, GS_NVS_NAME, stored, &length) == ESP_OK && stored[0] != '\0') {
            gs_copy_text(gs_name, sizeof(gs_name), stored);
        }
        nvs_close(handle);
    }
    esp_log_level_set("gs", ESP_LOG_INFO);
    esp_log_level_set("gs_usb", ESP_LOG_INFO);
    esp_log_level_set("gs_ble", ESP_LOG_INFO);
    esp_log_level_set("gs_wifi", ESP_LOG_INFO);
    gs_parts_defaults();
}

void gs_board_start(void)
{
    gs_last_frame = esp_timer_get_time();
    xTaskCreate(gs_status_task, "gs_status", 4096, NULL, configMAX_PRIORITIES - 4, NULL);
    gs_send_hello(GS_LINK_UART);
}

void gs_board_start_parts(void)
{
    gs_parts_defaults();
    if (gs_parts.usb_problem == GS_ESP32_PART_PENDING || gs_parts.ble_problem == GS_ESP32_PART_PENDING) {
        const bool started = gs_hid_start(gs_name, gs_mac);
        if (gs_parts.usb_problem == GS_ESP32_PART_PENDING) {
            gs_parts.usb_problem = started ? GS_ESP32_PART_OK : GS_ESP32_PART_FAILED;
        }
        if (gs_parts.ble_problem == GS_ESP32_PART_PENDING) {
            gs_parts.ble_problem = started ? GS_ESP32_PART_OK : GS_ESP32_PART_FAILED;
        }
    }
    if (gs_parts.wifi_problem == GS_ESP32_PART_PENDING) {
        gs_parts.wifi_problem = gs_wifi_start(gs_name, gs_mac) ? GS_ESP32_PART_OK : GS_ESP32_PART_FAILED;
    }
    ESP_LOGI(gs_tag, "USB %s, Bluetooth %s, Wi-Fi %s", gs_part_text(gs_parts.usb_problem),
             gs_part_text(gs_parts.ble_problem), gs_part_text(gs_parts.wifi_problem));
    gs_send_hello(GS_LINK_UART);
    if (gs_link_active() != GS_LINK_UART) {
        gs_send_hello(gs_link_active());
    }
}

const char *gs_board_name(void)
{
    return gs_name;
}

const uint8_t *gs_board_mac(void)
{
    return gs_mac;
}
