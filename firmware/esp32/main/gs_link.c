#include "gs_link.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/uart_pins.h"

#include "gs_board.h"
#include "gs_wifi.h"

#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32P4
#define GS_JTAG_LINK 1
#include "driver/usb_serial_jtag.h"
#else
#define GS_JTAG_LINK 0
#endif

#define GS_UART UART_NUM_0
#define GS_UART_BUFFER 4096
#define GS_UART_EVENTS 16
#define GS_READ_CHUNK 256
#define GS_LINK_TASK_STACK 6144

static const char *gs_tag = "gs_link";
static SemaphoreHandle_t gs_send_lock;
static QueueHandle_t gs_uart_events;
static volatile gs_link_kind gs_active = GS_LINK_UART;
static uint8_t gs_sequence;
static uint32_t gs_uart_baud = GS_ESP32_HANDSHAKE_BAUD;
static uint32_t gs_previous_baud = GS_ESP32_HANDSHAKE_BAUD;
static int64_t gs_probation_until;
static bool gs_probation_passed;
static int64_t gs_uart_heard;

static const uint32_t gs_allowed_bauds[] = {115200, 230400, 460800, 921600, 1500000, 2000000, 3000000};

static void gs_write(gs_link_kind link, const uint8_t *data, size_t size)
{
    switch (link) {
    case GS_LINK_UART:
        uart_write_bytes(GS_UART, data, size);
        break;
#if GS_JTAG_LINK
    case GS_LINK_USB_JTAG:
        usb_serial_jtag_write_bytes(data, size, pdMS_TO_TICKS(20));
        break;
#endif
    case GS_LINK_UDP:
        gs_wifi_send(data, size);
        break;
    default:
        break;
    }
}

void gs_link_send(gs_link_kind link, uint8_t type, const uint8_t *payload, size_t size)
{
    uint8_t frame[GS_ESP32_MAX_FRAME];
    if (gs_send_lock == NULL) {
        return;
    }
    xSemaphoreTake(gs_send_lock, portMAX_DELAY);
    const size_t length = gs_esp32_frame_encode(frame, sizeof(frame), type, gs_sequence, payload, size);
    if (length > 0) {
        gs_sequence++;
        gs_write(link, frame, length);
    }
    xSemaphoreGive(gs_send_lock);
}

void gs_link_push(uint8_t type, const uint8_t *payload, size_t size)
{
    gs_link_send(gs_active, type, payload, size);
}

gs_link_kind gs_link_active(void)
{
    return gs_active;
}

void gs_link_received(gs_link_kind link, const gs_esp32_frame *frame)
{
    if (frame->type >= 0x80) {
        return;
    }
    if (link == GS_LINK_UART) {
        gs_probation_passed = true;
        gs_uart_heard = esp_timer_get_time();
    }
    gs_active = link;
    gs_board_handle(link, frame);
}

static void gs_feed(gs_esp32_parser *parser, gs_link_kind link, const uint8_t *data, size_t size)
{
    size_t at = 0;
    gs_esp32_frame frame;
    while (at < size) {
        at += gs_esp32_parser_feed(parser, data + at, size - at);
        while (gs_esp32_parser_next(parser, &frame)) {
            gs_link_received(link, &frame);
        }
    }
}

bool gs_link_baud_allowed(uint32_t baud)
{
    for (size_t i = 0; i < sizeof(gs_allowed_bauds) / sizeof(gs_allowed_bauds[0]); ++i) {
        if (gs_allowed_bauds[i] == baud) {
            return true;
        }
    }
    return false;
}

void gs_link_set_baud(gs_link_kind link, uint32_t baud)
{
    if (link != GS_LINK_UART || baud == gs_uart_baud) {
        return;
    }
    uart_wait_tx_done(GS_UART, pdMS_TO_TICKS(100));
    gs_previous_baud = gs_uart_baud;
    uart_set_baudrate(GS_UART, baud);
    gs_uart_baud = baud;
    gs_probation_passed = false;
    gs_uart_heard = esp_timer_get_time();
    gs_probation_until = gs_uart_heard + (int64_t)GS_ESP32_BAUD_PROBATION_MS * 1000;
}

static void gs_check_probation(void)
{
    if (gs_probation_until == 0 || esp_timer_get_time() < gs_probation_until) {
        return;
    }
    gs_probation_until = 0;
    if (!gs_probation_passed) {
        uart_set_baudrate(GS_UART, gs_previous_baud);
        gs_uart_baud = gs_previous_baud;
    }
}

static void gs_check_idle(void)
{
    if (gs_uart_baud == GS_ESP32_HANDSHAKE_BAUD || gs_probation_until != 0 ||
        esp_timer_get_time() - gs_uart_heard < (int64_t)GS_ESP32_SAFETY_MS * 1000) {
        return;
    }
    uart_set_baudrate(GS_UART, GS_ESP32_HANDSHAKE_BAUD);
    gs_uart_baud = GS_ESP32_HANDSHAKE_BAUD;
    gs_previous_baud = GS_ESP32_HANDSHAKE_BAUD;
}

void gs_link_flush(void)
{
    uart_wait_tx_done(GS_UART, pdMS_TO_TICKS(200));
#if GS_JTAG_LINK
    vTaskDelay(pdMS_TO_TICKS(20));
#endif
}

static void gs_uart_task(void *argument)
{
    (void)argument;
    static gs_esp32_parser parser;
    uint8_t buffer[GS_READ_CHUNK];
    uart_event_t event;
    gs_esp32_parser_reset(&parser);
    for (;;) {
        if (xQueueReceive(gs_uart_events, &event, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
                uart_flush_input(GS_UART);
                xQueueReset(gs_uart_events);
                gs_esp32_parser_reset(&parser);
            } else if (event.type == UART_DATA) {
                size_t available = 0;
                uart_get_buffered_data_len(GS_UART, &available);
                while (available > 0) {
                    const size_t wanted = available < sizeof(buffer) ? available : sizeof(buffer);
                    const int received = uart_read_bytes(GS_UART, buffer, wanted, 0);
                    if (received <= 0) {
                        break;
                    }
                    gs_feed(&parser, GS_LINK_UART, buffer, (size_t)received);
                    available = (size_t)received >= available ? 0 : available - (size_t)received;
                }
            }
        }
        gs_check_probation();
        gs_check_idle();
    }
}

#if GS_JTAG_LINK
static void gs_jtag_task(void *argument)
{
    (void)argument;
    static gs_esp32_parser parser;
    uint8_t buffer[GS_READ_CHUNK];
    gs_esp32_parser_reset(&parser);
    for (;;) {
        const int received = usb_serial_jtag_read_bytes(buffer, sizeof(buffer), pdMS_TO_TICKS(50));
        if (received > 0) {
            gs_feed(&parser, GS_LINK_USB_JTAG, buffer, (size_t)received);
        }
    }
}
#endif

static int gs_log_vprintf(const char *format, va_list arguments)
{
    char text[GS_ESP32_MAX_TEXT + 1];
    uint8_t payload[GS_ESP32_MAX_TEXT + 2];
    gs_esp32_log message;
    if (gs_send_lock == NULL || xSemaphoreGetMutexHolder(gs_send_lock) == xTaskGetCurrentTaskHandle()) {
        return 0;
    }
    const int length = vsnprintf(text, sizeof(text), format, arguments);
    if (length <= 0) {
        return length;
    }
    size_t size = strnlen(text, sizeof(text) - 1);
    while (size > 0 && (text[size - 1] == '\n' || text[size - 1] == '\r')) {
        --size;
    }
    if (size == 0) {
        return length;
    }
    memset(&message, 0, sizeof(message));
    switch (text[0]) {
    case 'E':
        message.level = GS_ESP32_LOG_ERROR;
        break;
    case 'W':
        message.level = GS_ESP32_LOG_WARNING;
        break;
    case 'I':
        message.level = GS_ESP32_LOG_INFO;
        break;
    default:
        message.level = GS_ESP32_LOG_DEBUG;
        break;
    }
    memcpy(message.text, text, size);
    const size_t encoded = gs_esp32_encode_log(payload, sizeof(payload), &message);
    if (encoded > 0) {
        gs_link_push(GS_ESP32_MSG_LOG, payload, encoded);
    }
    return length;
}

bool gs_link_start(void)
{
    const uart_config_t config = {
        .baud_rate = GS_ESP32_HANDSHAKE_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    bool serial = true;
    gs_send_lock = xSemaphoreCreateMutex();
    const esp_err_t installed =
        uart_driver_install(GS_UART, GS_UART_BUFFER, GS_UART_BUFFER, GS_UART_EVENTS, &gs_uart_events, 0);
    const esp_err_t configured = installed == ESP_OK ? uart_param_config(GS_UART, &config) : installed;
    const esp_err_t pinned =
        configured == ESP_OK
            ? uart_set_pin(GS_UART, U0TXD_GPIO_NUM, U0RXD_GPIO_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)
            : configured;
    if (pinned != ESP_OK) {
        ESP_LOGE(gs_tag, "the serial port could not start; the board keeps running and answers on the other link");
        serial = false;
    }
    if (serial) {
        uart_set_rx_full_threshold(GS_UART, 64);
        uart_set_rx_timeout(GS_UART, 2);
        xTaskCreate(gs_uart_task, "gs_uart", GS_LINK_TASK_STACK, NULL, configMAX_PRIORITIES - 3, NULL);
    }
#if GS_JTAG_LINK
    usb_serial_jtag_driver_config_t jtag = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    jtag.rx_buffer_size = 1024;
    jtag.tx_buffer_size = 1024;
    if (usb_serial_jtag_driver_install(&jtag) == ESP_OK) {
        xTaskCreate(gs_jtag_task, "gs_jtag", GS_LINK_TASK_STACK, NULL, configMAX_PRIORITIES - 3, NULL);
    }
#endif
    esp_log_set_vprintf(gs_log_vprintf);
    return serial;
}
