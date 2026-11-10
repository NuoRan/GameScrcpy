#include "gs_wifi.h"

#include <string.h>

#include "soc/soc_caps.h"

#if SOC_WIFI_SUPPORTED

#include <stdio.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"

#include "gs_link.h"

#define GS_WIFI_NAMESPACE "gs"
#define GS_WIFI_SSID "ssid"
#define GS_WIFI_PASSWORD "pass"
#define GS_WIFI_KEY "key"
#define GS_WIFI_RETRY_US 2000000
#define GS_WIFI_SERVICE "_gs-esp32"
#define GS_WIFI_PROTOCOL "_udp"

static const char *gs_tag = "gs_wifi";
static portMUX_TYPE gs_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t gs_state = GS_ESP32_WIFI_OFF;
static uint8_t gs_address[4];
static uint8_t gs_key[GS_ESP32_KEY_SIZE];
static volatile bool gs_configured;
static bool gs_started;
static int gs_socket = -1;
static struct sockaddr_in gs_peer;
static volatile bool gs_has_peer;
static esp_timer_handle_t gs_retry;
static char gs_hostname[32];
static char gs_id[13];
static char gs_name[GS_ESP32_MAX_NAME + 1];

static void gs_copy_text(char *out, size_t capacity, const char *text)
{
    const size_t length = strnlen(text, capacity - 1);
    memcpy(out, text, length);
    out[length] = '\0';
}

static void gs_set_address(const uint8_t address[4])
{
    portENTER_CRITICAL(&gs_lock);
    memcpy(gs_address, address, 4);
    portEXIT_CRITICAL(&gs_lock);
}

static void gs_publish(void)
{
    mdns_txt_item_t text[] = {
        {"id", gs_id},
        {"name", gs_name},
        {"chip", CONFIG_IDF_TARGET},
    };
    mdns_service_remove(GS_WIFI_SERVICE, GS_WIFI_PROTOCOL);
    mdns_instance_name_set(gs_name);
    mdns_service_add(gs_name, GS_WIFI_SERVICE, GS_WIFI_PROTOCOL, GS_ESP32_UDP_PORT, text,
                     sizeof(text) / sizeof(text[0]));
}

static void gs_on_event(void *argument, esp_event_base_t base, int32_t id, void *data)
{
    static const uint8_t none[4] = {0, 0, 0, 0};
    (void)argument;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        gs_state = GS_ESP32_WIFI_CONNECTING;
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)data;
        const bool refused = event->reason == WIFI_REASON_AUTH_FAIL || event->reason == WIFI_REASON_NO_AP_FOUND ||
                             event->reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                             event->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT;
        gs_set_address(none);
        if (gs_configured) {
            gs_state = refused ? GS_ESP32_WIFI_FAILED : GS_ESP32_WIFI_CONNECTING;
            esp_timer_start_once(gs_retry, GS_WIFI_RETRY_US);
        } else {
            gs_state = GS_ESP32_WIFI_OFF;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        uint8_t address[4];
        memcpy(address, &event->ip_info.ip.addr, 4);
        gs_set_address(address);
        gs_state = GS_ESP32_WIFI_CONNECTED;
        ESP_LOGI(gs_tag, "Wi-Fi connected as %u.%u.%u.%u", address[0], address[1], address[2], address[3]);
    }
}

static void gs_on_retry(void *argument)
{
    (void)argument;
    if (gs_configured) {
        esp_wifi_connect();
    }
}

static void gs_connect(const char *ssid, const char *password)
{
    wifi_config_t config;
    memset(&config, 0, sizeof(config));
    memcpy(config.sta.ssid, ssid, strnlen(ssid, sizeof(config.sta.ssid)));
    memcpy(config.sta.password, password, strnlen(password, sizeof(config.sta.password)));
    config.sta.threshold.authmode = password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    gs_configured = true;
    gs_state = GS_ESP32_WIFI_CONNECTING;
    if (gs_started) {
        esp_wifi_disconnect();
        esp_wifi_set_config(WIFI_IF_STA, &config);
        esp_wifi_connect();
        return;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &config);
    if (esp_wifi_start() == ESP_OK) {
        gs_started = true;
#if CONFIG_BT_ENABLED
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
#else
        esp_wifi_set_ps(WIFI_PS_NONE);
#endif
    }
}

static bool gs_load(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    nvs_handle_t handle;
    size_t length = 0;
    bool loaded = false;
    if (nvs_open(GS_WIFI_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    length = ssid_size;
    loaded = nvs_get_str(handle, GS_WIFI_SSID, ssid, &length) == ESP_OK;
    length = password_size;
    loaded = loaded && nvs_get_str(handle, GS_WIFI_PASSWORD, password, &length) == ESP_OK;
    length = sizeof(gs_key);
    loaded = loaded && nvs_get_blob(handle, GS_WIFI_KEY, gs_key, &length) == ESP_OK && length == sizeof(gs_key);
    nvs_close(handle);
    return loaded && ssid[0] != '\0';
}

static void gs_udp_task(void *argument)
{
    (void)argument;
    uint8_t datagram[GS_ESP32_MAX_DATAGRAM + 16];
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(GS_ESP32_UDP_PORT);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    gs_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (gs_socket < 0 || bind(gs_socket, (struct sockaddr *)&local, sizeof(local)) != 0) {
        ESP_LOGE(gs_tag, "could not open UDP port %d", GS_ESP32_UDP_PORT);
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        struct sockaddr_in from;
        socklen_t from_size = sizeof(from);
        gs_esp32_frame frame;
        const int received =
            recvfrom(gs_socket, datagram, sizeof(datagram), 0, (struct sockaddr *)&from, &from_size);
        if (received <= 0 || !gs_configured) {
            continue;
        }
        if (!gs_esp32_datagram_open(datagram, (size_t)received, gs_key, &frame)) {
            continue;
        }
        portENTER_CRITICAL(&gs_lock);
        gs_peer = from;
        gs_has_peer = true;
        portEXIT_CRITICAL(&gs_lock);
        gs_link_received(GS_LINK_UDP, &frame);
    }
}

bool gs_wifi_available(void)
{
    return true;
}

bool gs_wifi_start(const char *name, const uint8_t mac[6])
{
    char ssid[GS_ESP32_MAX_SSID + 1] = {0};
    char password[GS_ESP32_MAX_PASSWORD + 1] = {0};
    esp_err_t result;
    gs_copy_text(gs_name, sizeof(gs_name), name);
    snprintf(gs_id, sizeof(gs_id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(gs_hostname, sizeof(gs_hostname), "gs-esp32-%02x%02x%02x", mac[3], mac[4], mac[5]);
    result = esp_netif_init();
    if (result != ESP_OK) {
        ESP_LOGE(gs_tag, "Wi-Fi could not start (%s); the board keeps running without it", esp_err_to_name(result));
        return false;
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(gs_tag, "Wi-Fi could not start (%s); the board keeps running without it", esp_err_to_name(result));
        return false;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(gs_tag, "Wi-Fi could not start; the board keeps running without it");
        return false;
    }
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init);
    if (result != ESP_OK) {
        ESP_LOGE(gs_tag, "Wi-Fi could not start (%s); the board keeps running without it", esp_err_to_name(result));
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, gs_on_event, NULL, NULL);
    if (result == ESP_OK) {
        result = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, gs_on_event, NULL, NULL);
    }
    if (result != ESP_OK) {
        ESP_LOGE(gs_tag, "Wi-Fi could not follow the network (%s); the board keeps running without it",
                 esp_err_to_name(result));
        return false;
    }
    const esp_timer_create_args_t retry = {
        .callback = gs_on_retry,
        .name = "gs_wifi_retry",
    };
    if (esp_timer_create(&retry, &gs_retry) != ESP_OK) {
        ESP_LOGE(gs_tag, "Wi-Fi could not start; the board keeps running without it");
        return false;
    }
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(gs_hostname);
        gs_publish();
    }
    if (xTaskCreate(gs_udp_task, "gs_udp", 4096, NULL, configMAX_PRIORITIES - 3, NULL) != pdPASS) {
        ESP_LOGE(gs_tag, "Wi-Fi could not start; the board keeps running without it");
        return false;
    }
    if (gs_load(ssid, sizeof(ssid), password, sizeof(password))) {
        gs_connect(ssid, password);
    }
    memset(password, 0, sizeof(password));
    return true;
}

bool gs_wifi_configure(const char *ssid, const char *password, const uint8_t key[GS_ESP32_KEY_SIZE])
{
    nvs_handle_t handle;
    if (nvs_open(GS_WIFI_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const bool stored = nvs_set_str(handle, GS_WIFI_SSID, ssid) == ESP_OK &&
                        nvs_set_str(handle, GS_WIFI_PASSWORD, password) == ESP_OK &&
                        nvs_set_blob(handle, GS_WIFI_KEY, key, GS_ESP32_KEY_SIZE) == ESP_OK &&
                        nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    if (!stored) {
        return false;
    }
    memcpy(gs_key, key, GS_ESP32_KEY_SIZE);
    portENTER_CRITICAL(&gs_lock);
    gs_has_peer = false;
    portEXIT_CRITICAL(&gs_lock);
    gs_connect(ssid, password);
    return true;
}

bool gs_wifi_forget(void)
{
    static const uint8_t none[4] = {0, 0, 0, 0};
    nvs_handle_t handle;
    bool erased = false;
    if (nvs_open(GS_WIFI_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, GS_WIFI_SSID);
        nvs_erase_key(handle, GS_WIFI_PASSWORD);
        nvs_erase_key(handle, GS_WIFI_KEY);
        erased = nvs_commit(handle) == ESP_OK;
        nvs_close(handle);
    }
    gs_configured = false;
    memset(gs_key, 0, sizeof(gs_key));
    portENTER_CRITICAL(&gs_lock);
    gs_has_peer = false;
    portEXIT_CRITICAL(&gs_lock);
    if (gs_started) {
        esp_wifi_disconnect();
    }
    gs_set_address(none);
    gs_state = GS_ESP32_WIFI_OFF;
    return erased;
}

uint8_t gs_wifi_state(void)
{
    return gs_state;
}

void gs_wifi_ipv4(uint8_t out[4])
{
    portENTER_CRITICAL(&gs_lock);
    memcpy(out, gs_address, 4);
    portEXIT_CRITICAL(&gs_lock);
}

void gs_wifi_rename(const char *name)
{
    gs_copy_text(gs_name, sizeof(gs_name), name);
    gs_publish();
}

void gs_wifi_send(const uint8_t *frame, size_t size)
{
    uint8_t datagram[GS_ESP32_MAX_DATAGRAM];
    struct sockaddr_in peer;
    bool known = false;
    if (gs_socket < 0 || !gs_configured || size > GS_ESP32_MAX_FRAME) {
        return;
    }
    portENTER_CRITICAL(&gs_lock);
    known = gs_has_peer;
    peer = gs_peer;
    portEXIT_CRITICAL(&gs_lock);
    if (!known) {
        return;
    }
    memcpy(datagram, frame, size);
    const size_t length = gs_esp32_datagram_seal(datagram, size, sizeof(datagram), gs_key);
    if (length > 0) {
        sendto(gs_socket, datagram, length, 0, (const struct sockaddr *)&peer, sizeof(peer));
    }
}

#else

bool gs_wifi_available(void)
{
    return false;
}

bool gs_wifi_start(const char *name, const uint8_t mac[6])
{
    (void)name;
    (void)mac;
    return false;
}

bool gs_wifi_configure(const char *ssid, const char *password, const uint8_t key[GS_ESP32_KEY_SIZE])
{
    (void)ssid;
    (void)password;
    (void)key;
    return false;
}

bool gs_wifi_forget(void)
{
    return true;
}

uint8_t gs_wifi_state(void)
{
    return GS_ESP32_WIFI_NONE;
}

void gs_wifi_ipv4(uint8_t out[4])
{
    memset(out, 0, 4);
}

void gs_wifi_rename(const char *name)
{
    (void)name;
}

void gs_wifi_send(const uint8_t *frame, size_t size)
{
    (void)frame;
    (void)size;
}

#endif
