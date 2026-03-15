/**
 * @file HidReportDescriptor.h
 * @brief HID 触摸屏报告描述符 / HID Touch Screen Report Descriptor
 *
 * 定义 USB HID 触摸屏和键盘的报告描述符，用于 AOA HID 注册。
 */
#ifndef HID_REPORT_DESCRIPTOR_H
#define HID_REPORT_DESCRIPTOR_H

#include <cstdint>

// AOA HID 设备 ID
static constexpr uint16_t HID_ACCESSORY_ID_TOUCH    = 1;
static constexpr uint16_t HID_ACCESSORY_ID_KEYBOARD = 2;

// 触摸屏参数
static constexpr uint16_t HID_TOUCH_COORD_MAX   = 32767;
static constexpr uint8_t  HID_TOUCH_MAX_CONTACTS = 16;
static constexpr uint8_t  HID_TOUCH_REPORT_SIZE  = 7;  // 每个触摸报告字节数

/**
 * USB HID Touch Screen Report Descriptor
 *
 * 报告格式 (7 bytes):
 *   [0] Tip Switch (1 bit) + padding (7 bits)
 *   [1] Contact ID (8 bits, 0-15)
 *   [2-3] X (16 bits LE, 0-32767)
 *   [4-5] Y (16 bits LE, 0-32767)
 *   [6] Contact Count (8 bits, 0-16)
 */
static const uint8_t HID_TOUCH_SCREEN_REPORT_DESC[] = {
    0x05, 0x0D,       // Usage Page (Digitizers)
    0x09, 0x04,       // Usage (Touch Screen)
    0xA1, 0x01,       // Collection (Application)
    0x09, 0x22,       //   Usage (Finger)
    0xA1, 0x02,       //   Collection (Logical)

    // Tip Switch
    0x09, 0x42,       //     Usage (Tip Switch)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x75, 0x01,       //     Report Size (1)
    0x95, 0x01,       //     Report Count (1)
    0x81, 0x02,       //     Input (Data, Variable, Absolute)

    // 7 bits padding
    0x75, 0x07,       //     Report Size (7)
    0x95, 0x01,       //     Report Count (1)
    0x81, 0x03,       //     Input (Constant)

    // Contact ID
    0x09, 0x51,       //     Usage (Contact Identifier)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x0F,       //     Logical Maximum (15)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x01,       //     Report Count (1)
    0x81, 0x02,       //     Input (Data, Variable, Absolute)

    // X coordinate
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x09, 0x30,       //     Usage (X)
    0x15, 0x00,       //     Logical Minimum (0)
    0x26, 0xFF, 0x7F, //     Logical Maximum (32767)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x01,       //     Report Count (1)
    0x81, 0x02,       //     Input (Data, Variable, Absolute)

    // Y coordinate
    0x09, 0x31,       //     Usage (Y)
    0x15, 0x00,       //     Logical Minimum (0)
    0x26, 0xFF, 0x7F, //     Logical Maximum (32767)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x01,       //     Report Count (1)
    0x81, 0x02,       //     Input (Data, Variable, Absolute)

    // Contact Count
    0x05, 0x0D,       //     Usage Page (Digitizers)
    0x09, 0x54,       //     Usage (Contact Count)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x10,       //     Logical Maximum (16)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x01,       //     Report Count (1)
    0x81, 0x02,       //     Input (Data, Variable, Absolute)

    0xC0,             //   End Collection (Logical)

    // Contact Count Maximum (Feature Report)
    0x09, 0x55,       //   Usage (Contact Count Maximum)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x10,       //   Logical Maximum (16)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x01,       //   Report Count (1)
    0xB1, 0x02,       //   Feature (Data, Variable, Absolute)

    0xC0              // End Collection (Application)
};

static constexpr uint16_t HID_TOUCH_REPORT_DESC_SIZE = sizeof(HID_TOUCH_SCREEN_REPORT_DESC);

/**
 * USB HID Keyboard Report Descriptor (Boot Protocol)
 * 用于纯 AOA 模式下发送 Android 按键
 */
static const uint8_t HID_KEYBOARD_REPORT_DESC[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)

    // Modifier keys (8 bits)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,       //   Usage Minimum (Left Control)
    0x29, 0xE7,       //   Usage Maximum (Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)

    // Reserved byte
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x01,       //   Input (Constant)

    // Key codes (6 slots)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,       //   Usage Minimum (Reserved)
    0x29, 0x65,       //   Usage Maximum (Keyboard Application)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x65,       //   Logical Maximum (101)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x06,       //   Report Count (6)
    0x81, 0x00,       //   Input (Data, Array)

    0xC0              // End Collection
};

static constexpr uint16_t HID_KEYBOARD_REPORT_DESC_SIZE = sizeof(HID_KEYBOARD_REPORT_DESC);
static constexpr uint8_t  HID_KEYBOARD_REPORT_SIZE = 8;  // modifier(1) + reserved(1) + keys(6)

#endif // HID_REPORT_DESCRIPTOR_H
