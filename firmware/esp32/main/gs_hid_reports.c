#include "gs_hid_reports.h"

#include <string.h>

static void gs_store_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)(value >> 8);
}

static int16_t gs_axis(int16_t value)
{
    return value < -32767 ? (int16_t)-32767 : value;
}

size_t gs_report_touch(const gs_esp32_touch *touch, uint8_t out[GS_TOUCH_REPORT_SIZE])
{
    uint8_t count = touch->count > GS_ESP32_MAX_CONTACTS ? GS_ESP32_MAX_CONTACTS : touch->count;
    memset(out, 0, GS_TOUCH_REPORT_SIZE);
    out[0] = count;
    for (uint8_t i = 0; i < count; ++i) {
        const gs_esp32_contact *contact = &touch->contacts[i];
        uint8_t *slot = out + 1 + 6 * i;
        slot[0] = contact->tip ? 1 : 0;
        slot[1] = contact->id;
        gs_store_u16(slot + 2, contact->x > GS_ESP32_COORD_MAX ? GS_ESP32_COORD_MAX : contact->x);
        gs_store_u16(slot + 4, contact->y > GS_ESP32_COORD_MAX ? GS_ESP32_COORD_MAX : contact->y);
    }
    return GS_TOUCH_REPORT_SIZE;
}

size_t gs_report_keyboard(const gs_esp32_keyboard *keyboard, uint8_t out[GS_KEYBOARD_REPORT_SIZE])
{
    out[0] = keyboard->modifiers;
    out[1] = 0;
    memcpy(out + 2, keyboard->keys, GS_ESP32_KEYS);
    return GS_KEYBOARD_REPORT_SIZE;
}

size_t gs_report_consumer(uint16_t usage, uint8_t out[GS_CONSUMER_REPORT_SIZE])
{
    gs_store_u16(out, usage);
    return GS_CONSUMER_REPORT_SIZE;
}

size_t gs_report_mouse(const gs_esp32_mouse *mouse, uint8_t out[GS_MOUSE_REPORT_SIZE])
{
    out[0] = (uint8_t)(mouse->buttons & 0x1Fu);
    gs_store_u16(out + 1, (uint16_t)gs_axis(mouse->dx));
    gs_store_u16(out + 3, (uint16_t)gs_axis(mouse->dy));
    out[5] = (uint8_t)(mouse->wheel < -127 ? -127 : mouse->wheel);
    return GS_MOUSE_REPORT_SIZE;
}

size_t gs_report_gamepad(const gs_esp32_gamepad *gamepad, uint8_t out[GS_GAMEPAD_REPORT_SIZE])
{
    out[0] = (uint8_t)(gamepad->buttons & 0xFFu);
    out[1] = (uint8_t)((gamepad->buttons >> 8) & 0xFFu);
    out[2] = (uint8_t)((gamepad->buttons >> 16) & 0xFFu);
    out[3] = (uint8_t)((gamepad->buttons >> 24) & 0xFFu);
    out[4] = (uint8_t)(gamepad->hat > GS_ESP32_HAT_CENTERED ? GS_ESP32_HAT_CENTERED : gamepad->hat);
    gs_store_u16(out + 5, (uint16_t)gs_axis(gamepad->lx));
    gs_store_u16(out + 7, (uint16_t)gs_axis(gamepad->ly));
    gs_store_u16(out + 9, (uint16_t)gs_axis(gamepad->rx));
    gs_store_u16(out + 11, (uint16_t)gs_axis(gamepad->ry));
    out[13] = gamepad->lt;
    out[14] = gamepad->rt;
    return GS_GAMEPAD_REPORT_SIZE;
}

bool gs_report_lifts(const gs_esp32_touch *touch)
{
    for (uint8_t i = 0; i < touch->count && i < GS_ESP32_MAX_CONTACTS; ++i) {
        if (!touch->contacts[i].tip) {
            return true;
        }
    }
    return false;
}
