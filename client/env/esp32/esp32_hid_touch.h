/**
 * @file esp32_hid_touch.h
 * @brief ESP32-S3 HID Touch 固件协议定义 (C 翻译)
 *
 * 原始代码: QineTouch v3.2 (hid_touch.ino)
 * 翻译自 Arduino/ESP32 → 标准 C 头文件，供 PC 端参考协议细节。
 *
 * === 串口协议 ===
 * PC → ESP32:
 *   [0xF4] [TipSwitch] [ContactID] [X_LE32] [Y_LE32] [ContactCount]
 *   共 12 字节 (1 header + 11 body)
 *
 * ESP32 → Phone (USB HID):
 *   直接转发 11 字节 body 作为 HID Touch Screen Report
 *
 * === HID Report 格式 (11 字节) ===
 *   Byte 0:    TipSwitch (0=UP, 1=DOWN/MOVE)
 *   Byte 1:    Contact Identifier (0-15)
 *   Byte 2-5:  X (uint32 LE, 0-32767)
 *   Byte 6-9:  Y (uint32 LE, 0-32767)
 *   Byte 10:   Contact Count (当前帧活跃触点总数)
 *
 * === 配置 ===
 *   波特率: 921600
 *   数据位: 8, 停止位: 1, 无校验, 无流控
 */
#ifndef ESP32_HID_TOUCH_H
#define ESP32_HID_TOUCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 协议常量 ---- */
#define ESP32_MAGIC_HEADER    0xF4
#define ESP32_SERIAL_BAUD     921600
#define ESP32_PACKET_SIZE     12   /* header(1) + body(11) */
#define ESP32_BODY_SIZE       11   /* = HID report size */
#define ESP32_HID_COORD_MAX   32767

/* ---- HID 描述符 (Touch Screen, 32-bit X/Y, 单指报告 + ContactCount) ---- */
static const uint8_t esp32_hid_report_descriptor[] = {
    0x05, 0x0D,                         /* Usage Page (Digitizer)        */
    0x09, 0x04,                         /* Usage (Touch Screen)          */
    0xA1, 0x01,                         /* Collection (Application)      */
    /* Finger 1 */
    0x09, 0x22,                         /*   Usage (Finger)              */
    0xA1, 0x02,                         /*   Collection (Logical)        */
    0x09, 0x42,                         /*     Usage (Tip Switch)        */
    0x15, 0x00,                         /*     Logical Minimum (0)       */
    0x25, 0x01,                         /*     Logical Maximum (1)       */
    0x75, 0x01,                         /*     Report Size (1)           */
    0x95, 0x01,                         /*     Report Count (1)          */
    0x81, 0x02,                         /*     Input (Data,Var,Abs)      */
    0x95, 0x07,                         /*     Report Count (7) padding  */
    0x81, 0x03,                         /*     Input (Const,Var,Abs)     */
    0x09, 0x51,                         /*     Usage (Contact Identifier)*/
    0x75, 0x08,                         /*     Report Size (8)           */
    0x95, 0x01,                         /*     Report Count (1)          */
    0x81, 0x02,                         /*     Input (Data,Var,Abs)      */
    0x05, 0x01,                         /*     Usage Page (Generic Desktop) */
    /* X */
    0x09, 0x30,                         /*     Usage (X)                 */
    0x15, 0x00,                         /*     Logical Minimum (0)       */
    0x27, 0xFF, 0x7F, 0x00, 0x00,       /*     Logical Maximum (32767)   */
    0x75, 0x20,                         /*     Report Size (32)          */
    0x95, 0x01,                         /*     Report Count (1)          */
    0x81, 0x02,                         /*     Input (Data,Var,Abs)      */
    /* Y */
    0x09, 0x31,                         /*     Usage (Y)                 */
    0x15, 0x00,                         /*     Logical Minimum (0)       */
    0x27, 0xFF, 0x7F, 0x00, 0x00,       /*     Logical Maximum (32767)   */
    0x75, 0x20,                         /*     Report Size (32)          */
    0x95, 0x01,                         /*     Report Count (1)          */
    0x81, 0x02,                         /*     Input (Data,Var,Abs)      */
    0xC0,                               /*   End Collection              */
    /* Contact Count */
    0x05, 0x0D,                         /*   Usage Page (Digitizer)      */
    0x09, 0x54,                         /*   Usage (Contact Count)       */
    0x25, 0x10,                         /*   Logical Maximum (16)        */
    0x75, 0x08,                         /*   Report Size (8)             */
    0x95, 0x01,                         /*   Report Count (1)            */
    0x81, 0x02,                         /*   Input (Data,Var,Abs)        */
    0xC0                                /* End Collection                */
};

#define ESP32_HID_REPORT_DESC_SIZE  sizeof(esp32_hid_report_descriptor)

/**
 * @brief 构建发送给 ESP32 的串口数据包
 *
 * @param buf       输出缓冲区，至少 12 字节
 * @param tipSwitch 0 = UP, 1 = DOWN/MOVE
 * @param contactId 触点标识 (0-15)
 * @param x         触点 X，范围 0-32767
 * @param y         触点 Y，范围 0-32767
 * @param count     当前帧活跃触点总数
 */
static inline void esp32_build_packet(uint8_t buf[12],
                                       uint8_t tipSwitch,
                                       uint8_t contactId,
                                       uint32_t x,
                                       uint32_t y,
                                       uint8_t count)
{
    buf[0]  = ESP32_MAGIC_HEADER;
    buf[1]  = tipSwitch & 0x01;
    buf[2]  = contactId;
    /* X - little endian uint32 */
    buf[3]  = (uint8_t)(x);
    buf[4]  = (uint8_t)(x >> 8);
    buf[5]  = (uint8_t)(x >> 16);
    buf[6]  = (uint8_t)(x >> 24);
    /* Y - little endian uint32 */
    buf[7]  = (uint8_t)(y);
    buf[8]  = (uint8_t)(y >> 8);
    buf[9]  = (uint8_t)(y >> 16);
    buf[10] = (uint8_t)(y >> 24);
    buf[11] = count;
}

/**
 * @brief 归一化坐标 (0.0-1.0) → HID 坐标 (0-32767)
 */
static inline uint32_t esp32_normalize_to_hid(double normalized)
{
    if (normalized < 0.0) normalized = 0.0;
    if (normalized > 1.0) normalized = 1.0;
    return (uint32_t)(normalized * ESP32_HID_COORD_MAX);
}

#ifdef __cplusplus
}
#endif

#endif /* ESP32_HID_TOUCH_H */
