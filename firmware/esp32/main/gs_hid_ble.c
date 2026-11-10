#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

#include "gs_hid.h"
#include "gs_hid_descriptors.h"
#include "gs_hid_reports.h"

#define GS_BLE_VID 0x303A
#define GS_BLE_PID 0x40A5
#define GS_BLE_VERSION 0x0100
#define GS_BLE_APPEARANCE 0x03C0
#define GS_BLE_MTU 247
#define GS_BLE_ADVERTISED_NAME 29
#define GS_BLE_RETRIES 10

void ble_store_config_init(void);

static const char *gs_tag = "gs_ble";
static esp_hidd_dev_t *gs_device;
static portMUX_TYPE gs_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint16_t gs_connection = BLE_HS_CONN_HANDLE_NONE;
static volatile bool gs_advertising;
static volatile bool gs_synced;
static volatile bool gs_ready;
static uint8_t gs_address_type;
static char gs_name[GS_ESP32_MAX_NAME + 1];
static char gs_peer[GS_ESP32_MAX_NAME + 1];
static char gs_serial[13];

static esp_hid_raw_report_map_t gs_report_maps[] = {
    {.data = gs_combined_descriptor, .len = sizeof(gs_combined_descriptor)},
};

static esp_hid_device_config_t gs_config = {
    .vendor_id = GS_BLE_VID,
    .product_id = GS_BLE_PID,
    .version = GS_BLE_VERSION,
    .device_name = gs_name,
    .manufacturer_name = "GameScrcpy",
    .serial_number = gs_serial,
    .report_maps = gs_report_maps,
    .report_maps_len = 1,
};

static int gs_gap_event(struct ble_gap_event *event, void *argument);

static void gs_copy(char *out, size_t capacity, const char *text)
{
    size_t length = 0;
    while (length + 1 < capacity && text[length] != '\0') {
        ++length;
    }
    memcpy(out, text, length);
    out[length] = '\0';
}

static void gs_set_peer(const char *text)
{
    portENTER_CRITICAL(&gs_lock);
    gs_copy(gs_peer, sizeof(gs_peer), text);
    portEXIT_CRITICAL(&gs_lock);
}

static void gs_advertise(void)
{
    static const ble_uuid16_t hid_service = BLE_UUID16_INIT(0x1812);
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields response;
    struct ble_gap_adv_params parameters;
    size_t name_length;
    if (!gs_synced || !gs_ready || gs_connection != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) {
        return;
    }
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = GS_BLE_APPEARANCE;
    fields.appearance_is_present = 1;
    fields.uuids16 = &hid_service;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) {
        ESP_LOGW(gs_tag, "could not set the advertising data");
        return;
    }
    memset(&response, 0, sizeof(response));
    name_length = strlen(gs_name);
    response.name = (uint8_t *)gs_name;
    response.name_len = (uint8_t)(name_length > GS_BLE_ADVERTISED_NAME ? GS_BLE_ADVERTISED_NAME : name_length);
    response.name_is_complete = name_length <= GS_BLE_ADVERTISED_NAME;
    ble_gap_adv_rsp_set_fields(&response);
    memset(&parameters, 0, sizeof(parameters));
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    parameters.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
    parameters.itvl_max = BLE_GAP_ADV_ITVL_MS(50);
    if (ble_gap_adv_start(gs_address_type, NULL, BLE_HS_FOREVER, &parameters, gs_gap_event, NULL) == 0) {
        gs_advertising = true;
    }
}

static int gs_peer_name_read(uint16_t connection, const struct ble_gatt_error *error, struct ble_gatt_attr *attribute,
                             void *argument)
{
    (void)connection;
    (void)argument;
    if (error->status == 0 && attribute != NULL) {
        char name[GS_ESP32_MAX_NAME + 1];
        uint16_t length = 0;
        if (ble_hs_mbuf_to_flat(attribute->om, name, GS_ESP32_MAX_NAME, &length) == 0 || length > 0) {
            name[length > GS_ESP32_MAX_NAME ? GS_ESP32_MAX_NAME : length] = '\0';
            if (name[0] != '\0') {
                gs_set_peer(name);
            }
        }
    }
    return 0;
}

static void gs_remember_peer(uint16_t connection)
{
    struct ble_gap_conn_desc description;
    char address[18];
    if (ble_gap_conn_find(connection, &description) != 0) {
        return;
    }
    const uint8_t *value = description.peer_id_addr.val;
    snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X", value[5], value[4], value[3], value[2],
             value[1], value[0]);
    gs_set_peer(address);
}

static int gs_gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    struct ble_gap_conn_desc description;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        gs_advertising = false;
        if (event->connect.status == 0) {
            struct ble_gap_upd_params update = {
                .itvl_min = 6,
                .itvl_max = 12,
                .latency = 0,
                .supervision_timeout = 400,
                .min_ce_len = 0,
                .max_ce_len = 0,
            };
            gs_connection = event->connect.conn_handle;
            gs_remember_peer(gs_connection);
            ble_gap_update_params(gs_connection, &update);
            ble_gattc_exchange_mtu(gs_connection, NULL, NULL);
            ESP_LOGI(gs_tag, "phone connected");
        } else {
            gs_advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        gs_connection = BLE_HS_CONN_HANDLE_NONE;
        gs_set_peer("");
        ESP_LOGI(gs_tag, "phone disconnected");
        gs_advertise();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        gs_advertising = false;
        gs_advertise();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            ble_gattc_read_by_uuid(event->enc_change.conn_handle, 1, 0xFFFF,
                                   BLE_UUID16_DECLARE(BLE_SVC_GAP_CHR_UUID16_DEVICE_NAME), gs_peer_name_read, NULL);
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &description) == 0) {
            ble_store_util_delete_peer(&description.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    default:
        return 0;
    }
}

static void gs_on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &gs_address_type);
    gs_synced = true;
    gs_advertise();
}

static void gs_on_reset(int reason)
{
    gs_synced = false;
    ESP_LOGW(gs_tag, "Bluetooth host reset (%d)", reason);
}

static void gs_host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void gs_hidd_event(void *handler_argument, esp_event_base_t base, int32_t id, void *data)
{
    (void)handler_argument;
    (void)base;
    (void)data;
    switch ((esp_hidd_event_t)id) {
    case ESP_HIDD_START_EVENT:
        gs_ready = true;
        gs_advertise();
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        gs_advertise();
        break;
    default:
        break;
    }
}

bool gs_hid_start(const char *name, const uint8_t mac[6])
{
    uint8_t limit = GS_ESP32_MAX_CONTACTS;
    esp_err_t result;
    gs_copy(gs_name, sizeof(gs_name), name);
    snprintf(gs_serial, sizeof(gs_serial), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    result = nimble_port_init();
    if (result != ESP_OK) {
        ESP_LOGE(gs_tag, "Bluetooth could not start (%s); the board keeps running without it", esp_err_to_name(result));
        return false;
    }
    ble_hs_cfg.reset_cb = gs_on_reset;
    ble_hs_cfg.sync_cb = gs_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_att_set_preferred_mtu(GS_BLE_MTU);
    result = esp_hidd_dev_init(&gs_config, ESP_HID_TRANSPORT_BLE, gs_hidd_event, &gs_device);
    if (result != ESP_OK) {
        ESP_LOGE(gs_tag, "the Bluetooth HID device could not start (%s); the board keeps running without it",
                 esp_err_to_name(result));
        return false;
    }
    ble_svc_gap_device_name_set(gs_name);
    esp_hidd_dev_feature_set(gs_device, 0, GS_REPORT_TOUCH_LIMITS, &limit, 1);
    ble_store_config_init();
    gs_ready = true;
    nimble_port_freertos_init(gs_host_task);
    ESP_LOGI(gs_tag, "Bluetooth touch screen, keyboard, mouse and gamepad ready");
    return true;
}

static bool gs_send(uint8_t report_id, uint8_t *report, size_t length, bool important)
{
    const int attempts = important ? GS_BLE_RETRIES : 1;
    if (gs_device == NULL || gs_connection == BLE_HS_CONN_HANDLE_NONE) {
        return false;
    }
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (esp_hidd_dev_input_set(gs_device, 0, report_id, report, length) == ESP_OK) {
            return true;
        }
        vTaskDelay(1);
    }
    return false;
}

bool gs_hid_touch(const gs_esp32_touch *touch)
{
    uint8_t report[GS_TOUCH_REPORT_SIZE];
    const size_t length = gs_report_touch(touch, report);
    return gs_send(GS_REPORT_TOUCH, report, length, gs_report_lifts(touch));
}

bool gs_hid_keyboard(const gs_esp32_keyboard *keyboard)
{
    uint8_t report[GS_KEYBOARD_REPORT_SIZE];
    const size_t length = gs_report_keyboard(keyboard, report);
    return gs_send(GS_REPORT_KEYBOARD, report, length, true);
}

bool gs_hid_consumer(uint16_t usage)
{
    uint8_t report[GS_CONSUMER_REPORT_SIZE];
    const size_t length = gs_report_consumer(usage, report);
    return gs_send(GS_REPORT_CONSUMER, report, length, true);
}

bool gs_hid_mouse(const gs_esp32_mouse *mouse)
{
    uint8_t report[GS_MOUSE_REPORT_SIZE];
    const size_t length = gs_report_mouse(mouse, report);
    return gs_send(GS_REPORT_MOUSE, report, length, mouse->dx == 0 && mouse->dy == 0);
}

bool gs_hid_gamepad(const gs_esp32_gamepad *gamepad)
{
    uint8_t report[GS_GAMEPAD_REPORT_SIZE];
    const size_t length = gs_report_gamepad(gamepad, report);
    return gs_send(GS_REPORT_GAMEPAD, report, length, true);
}

void gs_hid_phone(gs_esp32_status *status)
{
    status->usb = GS_ESP32_USB_NONE;
    if (gs_connection != BLE_HS_CONN_HANDLE_NONE) {
        status->ble = GS_ESP32_BLE_CONNECTED;
    } else if (gs_advertising) {
        status->ble = GS_ESP32_BLE_ADVERTISING;
    } else {
        status->ble = GS_ESP32_BLE_IDLE;
    }
    portENTER_CRITICAL(&gs_lock);
    memcpy(status->peer, gs_peer, sizeof(status->peer));
    portEXIT_CRITICAL(&gs_lock);
    if (status->ble != GS_ESP32_BLE_CONNECTED) {
        status->peer[0] = '\0';
    }
}

uint32_t gs_hid_features(void)
{
    return GS_ESP32_FEATURE_BLE_HID;
}

void gs_hid_rename(const char *name)
{
    gs_copy(gs_name, sizeof(gs_name), name);
    ble_svc_gap_device_name_set(gs_name);
    if (gs_connection == BLE_HS_CONN_HANDLE_NONE && ble_gap_adv_active()) {
        ble_gap_adv_stop();
        gs_advertising = false;
        gs_advertise();
    }
}
