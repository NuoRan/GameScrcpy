#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

#include "gs_hid.h"
#include "gs_hid_descriptors.h"
#include "gs_hid_reports.h"

#define GS_USB_VID 0x303A
#define GS_USB_PID 0x40A5
#define GS_USB_RELEASE 0x0100
#define GS_USB_POWER_MA 100
#define GS_USB_INTERVAL 1
#define GS_USB_ENDPOINT_SIZE 64
#define GS_USB_QUEUE_LENGTH 32
#define GS_USB_SEND_WAIT_US 50000

enum {
    GS_ITF_TOUCH = 0,
    GS_ITF_KEYBOARD = 1,
    GS_ITF_MOUSE = 2,
    GS_ITF_GAMEPAD = 3,
    GS_ITF_COUNT = 4,
};

#define GS_EP_TOUCH 0x81
#define GS_EP_KEYBOARD 0x82
#define GS_EP_MOUSE 0x83
#define GS_EP_GAMEPAD 0x84
#define GS_CONFIG_LENGTH (TUD_CONFIG_DESC_LEN + GS_ITF_COUNT * TUD_HID_DESC_LEN)

typedef struct {
    uint8_t instance;
    uint8_t report_id;
    uint8_t length;
    uint8_t data[GS_LARGEST_REPORT];
} gs_usb_report;

static const char *gs_tag = "gs_usb";
static QueueHandle_t gs_queue;
static TaskHandle_t gs_sender;
static char gs_serial[13];

static const tusb_desc_device_t gs_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = GS_USB_VID,
    .idProduct = GS_USB_PID,
    .bcdDevice = GS_USB_RELEASE,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const char *gs_strings[] = {
    (const char[]){0x09, 0x04},
    "GameScrcpy",
    "GameScrcpy Touch",
    gs_serial,
    "GameScrcpy Touch Screen",
    "GameScrcpy Keyboard",
    "GameScrcpy Mouse",
    "GameScrcpy Gamepad",
};

static const uint8_t gs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, GS_ITF_COUNT, 0, GS_CONFIG_LENGTH, 0, GS_USB_POWER_MA),
    TUD_HID_DESCRIPTOR(GS_ITF_TOUCH, 4, HID_ITF_PROTOCOL_NONE, sizeof(gs_touch_descriptor), GS_EP_TOUCH,
                       GS_USB_ENDPOINT_SIZE, GS_USB_INTERVAL),
    TUD_HID_DESCRIPTOR(GS_ITF_KEYBOARD, 5, HID_ITF_PROTOCOL_NONE, sizeof(gs_keyboard_descriptor), GS_EP_KEYBOARD,
                       GS_USB_ENDPOINT_SIZE, GS_USB_INTERVAL),
    TUD_HID_DESCRIPTOR(GS_ITF_MOUSE, 6, HID_ITF_PROTOCOL_NONE, sizeof(gs_mouse_descriptor), GS_EP_MOUSE,
                       GS_USB_ENDPOINT_SIZE, GS_USB_INTERVAL),
    TUD_HID_DESCRIPTOR(GS_ITF_GAMEPAD, 7, HID_ITF_PROTOCOL_NONE, sizeof(gs_gamepad_descriptor), GS_EP_GAMEPAD,
                       GS_USB_ENDPOINT_SIZE, GS_USB_INTERVAL),
};

#if TUD_OPT_HIGH_SPEED
static const tusb_desc_device_qualifier_t gs_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0x00,
};
#endif

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    switch (instance) {
    case GS_ITF_TOUCH:
        return gs_touch_descriptor;
    case GS_ITF_KEYBOARD:
        return gs_keyboard_descriptor;
    case GS_ITF_MOUSE:
        return gs_mouse_descriptor;
    default:
        return gs_gamepad_descriptor;
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    if (instance == GS_ITF_TOUCH && report_id == GS_REPORT_TOUCH_LIMITS && report_type == HID_REPORT_TYPE_FEATURE &&
        reqlen >= 1) {
        buffer[0] = GS_ESP32_MAX_CONTACTS;
        return 1;
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)instance;
    (void)report;
    (void)len;
    if (gs_sender != NULL) {
        xTaskNotifyGive(gs_sender);
    }
}

static void gs_send_task(void *argument)
{
    (void)argument;
    gs_usb_report report;
    for (;;) {
        if (xQueueReceive(gs_queue, &report, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        const int64_t deadline = esp_timer_get_time() + GS_USB_SEND_WAIT_US;
        while (tud_mounted() && !tud_hid_n_ready(report.instance) && esp_timer_get_time() < deadline) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2));
        }
        if (tud_mounted() && tud_hid_n_ready(report.instance)) {
            tud_hid_n_report(report.instance, report.report_id, report.data, report.length);
        }
    }
}

static bool gs_enqueue(uint8_t instance, uint8_t report_id, const uint8_t *data, size_t length)
{
    gs_usb_report report;
    if (gs_queue == NULL || length > sizeof(report.data) || !tud_mounted()) {
        return false;
    }
    report.instance = instance;
    report.report_id = report_id;
    report.length = (uint8_t)length;
    memcpy(report.data, data, length);
    return xQueueSend(gs_queue, &report, 0) == pdTRUE;
}

bool gs_hid_start(const char *name, const uint8_t mac[6])
{
    (void)name;
    snprintf(gs_serial, sizeof(gs_serial), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    gs_queue = xQueueCreate(GS_USB_QUEUE_LENGTH, sizeof(gs_usb_report));
    if (gs_queue == NULL) {
        ESP_LOGE(gs_tag, "could not make the USB report queue; the board keeps running without USB");
        return false;
    }
    if (xTaskCreate(gs_send_task, "gs_usb_send", 4096, NULL, configMAX_PRIORITIES - 2, &gs_sender) != pdPASS) {
        ESP_LOGE(gs_tag, "could not start the USB sender; the board keeps running without USB");
        return false;
    }

    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG();
    config.descriptor.device = &gs_device;
    config.descriptor.string = gs_strings;
    config.descriptor.string_count = sizeof(gs_strings) / sizeof(gs_strings[0]);
    config.descriptor.full_speed_config = gs_configuration;
#if TUD_OPT_HIGH_SPEED
    config.descriptor.high_speed_config = gs_configuration;
    config.descriptor.qualifier = &gs_qualifier;
#endif
    if (tinyusb_driver_install(&config) != ESP_OK) {
        ESP_LOGE(gs_tag, "USB could not start; the board keeps running without it");
        return false;
    }
    ESP_LOGI(gs_tag, "USB touch screen, keyboard, mouse and gamepad ready");
    return true;
}

bool gs_hid_touch(const gs_esp32_touch *touch)
{
    uint8_t report[GS_TOUCH_REPORT_SIZE];
    const size_t length = gs_report_touch(touch, report);
    return gs_enqueue(GS_ITF_TOUCH, GS_REPORT_TOUCH, report, length);
}

bool gs_hid_keyboard(const gs_esp32_keyboard *keyboard)
{
    uint8_t report[GS_KEYBOARD_REPORT_SIZE];
    const size_t length = gs_report_keyboard(keyboard, report);
    return gs_enqueue(GS_ITF_KEYBOARD, GS_REPORT_KEYBOARD, report, length);
}

bool gs_hid_consumer(uint16_t usage)
{
    uint8_t report[GS_CONSUMER_REPORT_SIZE];
    const size_t length = gs_report_consumer(usage, report);
    return gs_enqueue(GS_ITF_KEYBOARD, GS_REPORT_CONSUMER, report, length);
}

bool gs_hid_mouse(const gs_esp32_mouse *mouse)
{
    uint8_t report[GS_MOUSE_REPORT_SIZE];
    const size_t length = gs_report_mouse(mouse, report);
    return gs_enqueue(GS_ITF_MOUSE, GS_REPORT_MOUSE, report, length);
}

bool gs_hid_gamepad(const gs_esp32_gamepad *gamepad)
{
    uint8_t report[GS_GAMEPAD_REPORT_SIZE];
    const size_t length = gs_report_gamepad(gamepad, report);
    return gs_enqueue(GS_ITF_GAMEPAD, GS_REPORT_GAMEPAD, report, length);
}

void gs_hid_phone(gs_esp32_status *status)
{
    if (tud_suspended()) {
        status->usb = GS_ESP32_USB_SUSPENDED;
    } else if (tud_mounted()) {
        status->usb = GS_ESP32_USB_READY;
    } else {
        status->usb = GS_ESP32_USB_DETACHED;
    }
    status->ble = GS_ESP32_BLE_NONE;
    status->peer[0] = '\0';
}

uint32_t gs_hid_features(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    return GS_ESP32_FEATURE_USB_HID | GS_ESP32_FEATURE_USB_HIGH_SPEED;
#else
    return GS_ESP32_FEATURE_USB_HID;
#endif
}

void gs_hid_rename(const char *name)
{
    (void)name;
}
