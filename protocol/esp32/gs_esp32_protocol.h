#ifndef GS_ESP32_PROTOCOL_H
#define GS_ESP32_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GS_ESP32_PROTOCOL_VERSION 2
#define GS_ESP32_MAGIC0 0xA5
#define GS_ESP32_MAGIC1 0x5A
#define GS_ESP32_HEADER_SIZE 6
#define GS_ESP32_CRC_SIZE 2
#define GS_ESP32_FRAME_OVERHEAD 8
#define GS_ESP32_MAX_PAYLOAD 512
#define GS_ESP32_MAX_FRAME 520
#define GS_ESP32_TAG_SIZE 16
#define GS_ESP32_KEY_SIZE 32
#define GS_ESP32_MAX_DATAGRAM 536
#define GS_ESP32_UDP_PORT 4545
#define GS_ESP32_HANDSHAKE_BAUD 921600
#define GS_ESP32_FAST_BAUD 2000000
#define GS_ESP32_SAFETY_MS 2000
#define GS_ESP32_HELLO_WAIT_MS 300
#define GS_ESP32_BAUD_PROBATION_MS 1000
#define GS_ESP32_MAX_CONTACTS 10
#define GS_ESP32_COORD_MAX 32767
#define GS_ESP32_CONTACT_ID_MAX 127
#define GS_ESP32_KEYS 6
#define GS_ESP32_HAT_CENTERED 8
#define GS_ESP32_MAX_FIRMWARE 16
#define GS_ESP32_MAX_NAME 32
#define GS_ESP32_MAX_SSID 32
#define GS_ESP32_MAX_PASSWORD 64
#define GS_ESP32_MAX_TEXT 255

#define GS_ESP32_MSG_HELLO_REQ 0x01
#define GS_ESP32_MSG_TOUCH 0x02
#define GS_ESP32_MSG_KEYBOARD 0x03
#define GS_ESP32_MSG_CONSUMER 0x04
#define GS_ESP32_MSG_MOUSE 0x05
#define GS_ESP32_MSG_GAMEPAD 0x06
#define GS_ESP32_MSG_RELEASE_ALL 0x07
#define GS_ESP32_MSG_PING 0x08
#define GS_ESP32_MSG_SET_BAUD 0x09
#define GS_ESP32_MSG_WIFI_SET 0x0A
#define GS_ESP32_MSG_WIFI_FORGET 0x0B
#define GS_ESP32_MSG_SET_NAME 0x0C
#define GS_ESP32_MSG_REBOOT 0x0D
#define GS_ESP32_MSG_ENTER_DOWNLOAD 0x0E
#define GS_ESP32_MSG_HELLO 0x81
#define GS_ESP32_MSG_STATUS 0x82
#define GS_ESP32_MSG_PONG 0x83
#define GS_ESP32_MSG_LOG 0x84
#define GS_ESP32_MSG_ERROR 0x85

#define GS_ESP32_FEATURE_USB_HID 0x01u
#define GS_ESP32_FEATURE_USB_HIGH_SPEED 0x02u
#define GS_ESP32_FEATURE_BLE_HID 0x04u
#define GS_ESP32_FEATURE_WIFI 0x08u
#define GS_ESP32_FEATURE_USB_SERIAL_LINK 0x10u

#define GS_ESP32_USB_NONE 0
#define GS_ESP32_USB_DETACHED 1
#define GS_ESP32_USB_READY 2
#define GS_ESP32_USB_SUSPENDED 3

#define GS_ESP32_BLE_NONE 0
#define GS_ESP32_BLE_IDLE 1
#define GS_ESP32_BLE_ADVERTISING 2
#define GS_ESP32_BLE_CONNECTED 3

#define GS_ESP32_WIFI_NONE 0
#define GS_ESP32_WIFI_OFF 1
#define GS_ESP32_WIFI_CONNECTING 2
#define GS_ESP32_WIFI_CONNECTED 3
#define GS_ESP32_WIFI_FAILED 4

#define GS_ESP32_LOG_ERROR 0
#define GS_ESP32_LOG_WARNING 1
#define GS_ESP32_LOG_INFO 2
#define GS_ESP32_LOG_DEBUG 3

#define GS_ESP32_ERROR_MALFORMED 1
#define GS_ESP32_ERROR_UNKNOWN_TYPE 2
#define GS_ESP32_ERROR_UNSUPPORTED 3
#define GS_ESP32_ERROR_STORAGE 4
#define GS_ESP32_ERROR_NOT_READY 5

#define GS_ESP32_HELLO_EXTENSION 1

#define GS_ESP32_PART_OK 0
#define GS_ESP32_PART_UNSUPPORTED 1
#define GS_ESP32_PART_FAILED 2
#define GS_ESP32_PART_PENDING 3

#define GS_ESP32_CHIP_ESP32 0
#define GS_ESP32_CHIP_ESP32S2 2
#define GS_ESP32_CHIP_ESP32C3 5
#define GS_ESP32_CHIP_ESP32S3 9
#define GS_ESP32_CHIP_ESP32C6 13
#define GS_ESP32_CHIP_ESP32P4 18

#define GS_ESP32_LEGACY_MAGIC 0xF4
#define GS_ESP32_LEGACY_PACKET 12

typedef struct {
    uint8_t type;
    uint8_t seq;
    uint16_t length;
    const uint8_t *payload;
} gs_esp32_frame;

typedef struct {
    uint8_t id;
    uint8_t tip;
    uint16_t x;
    uint16_t y;
} gs_esp32_contact;

typedef struct {
    uint8_t count;
    gs_esp32_contact contacts[GS_ESP32_MAX_CONTACTS];
} gs_esp32_touch;

typedef struct {
    uint8_t modifiers;
    uint8_t keys[GS_ESP32_KEYS];
} gs_esp32_keyboard;

typedef struct {
    uint8_t buttons;
    int16_t dx;
    int16_t dy;
    int8_t wheel;
} gs_esp32_mouse;

typedef struct {
    uint32_t buttons;
    uint8_t hat;
    int16_t lx;
    int16_t ly;
    int16_t rx;
    int16_t ry;
    uint8_t lt;
    uint8_t rt;
} gs_esp32_gamepad;

typedef struct {
    uint32_t baud;
    uint32_t token;
} gs_esp32_set_baud;

typedef struct {
    char ssid[GS_ESP32_MAX_SSID + 1];
    char password[GS_ESP32_MAX_PASSWORD + 1];
    uint8_t key[GS_ESP32_KEY_SIZE];
} gs_esp32_wifi_set;

typedef struct {
    uint8_t usb;
    uint8_t usb_problem;
    uint8_t ble;
    uint8_t ble_problem;
    uint8_t wifi_problem;
} gs_esp32_parts;

typedef struct {
    uint8_t protocol;
    char firmware[GS_ESP32_MAX_FIRMWARE + 1];
    uint16_t chip_id;
    uint16_t chip_revision;
    uint32_t features;
    uint8_t mac[6];
    uint8_t wifi;
    uint8_t ipv4[4];
    char name[GS_ESP32_MAX_NAME + 1];
    uint8_t extension;
    gs_esp32_parts parts;
} gs_esp32_hello;

typedef struct {
    uint8_t usb;
    uint8_t ble;
    char peer[GS_ESP32_MAX_NAME + 1];
    uint8_t wifi;
    uint8_t ipv4[4];
} gs_esp32_status;

typedef struct {
    uint8_t level;
    char text[GS_ESP32_MAX_TEXT + 1];
} gs_esp32_log;

typedef struct {
    uint16_t code;
    char text[GS_ESP32_MAX_TEXT + 1];
} gs_esp32_error;

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t used;
    int failed;
} gs_esp32_writer;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t at;
    int failed;
} gs_esp32_reader;

typedef struct {
    uint8_t buffer[GS_ESP32_MAX_FRAME];
    size_t used;
    size_t ready;
    uint32_t dropped;
    uint32_t bad_frames;
} gs_esp32_parser;

typedef struct {
    uint32_t state[8];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} gs_esp32_sha256;

static inline uint16_t gs_esp32_crc16_update(uint16_t crc, const uint8_t *data, size_t size)
{
    size_t i;
    int bit;
    for (i = 0; i < size; ++i) {
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)data[i] << 8));
        for (bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000u) != 0) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static inline uint16_t gs_esp32_crc16(const uint8_t *data, size_t size)
{
    return gs_esp32_crc16_update(0xFFFFu, data, size);
}

static inline uint16_t gs_esp32_load_u16(const uint8_t *in)
{
    return (uint16_t)(in[0] | (uint16_t)(in[1] << 8));
}

static inline void gs_esp32_store_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)(value >> 8);
}

static inline gs_esp32_writer gs_esp32_writer_make(uint8_t *data, size_t capacity)
{
    gs_esp32_writer writer;
    writer.data = data;
    writer.capacity = capacity;
    writer.used = 0;
    writer.failed = 0;
    return writer;
}

static inline void gs_esp32_put_u8(gs_esp32_writer *writer, uint8_t value)
{
    if (writer->failed || writer->used >= writer->capacity) {
        writer->failed = 1;
        return;
    }
    writer->data[writer->used++] = value;
}

static inline void gs_esp32_put_u16(gs_esp32_writer *writer, uint16_t value)
{
    gs_esp32_put_u8(writer, (uint8_t)(value & 0xFFu));
    gs_esp32_put_u8(writer, (uint8_t)(value >> 8));
}

static inline void gs_esp32_put_u32(gs_esp32_writer *writer, uint32_t value)
{
    gs_esp32_put_u16(writer, (uint16_t)(value & 0xFFFFu));
    gs_esp32_put_u16(writer, (uint16_t)(value >> 16));
}

static inline void gs_esp32_put_bytes(gs_esp32_writer *writer, const uint8_t *bytes, size_t size)
{
    if (writer->failed || size > writer->capacity - writer->used) {
        writer->failed = 1;
        return;
    }
    if (size > 0) {
        memcpy(writer->data + writer->used, bytes, size);
    }
    writer->used += size;
}

static inline void gs_esp32_put_text(gs_esp32_writer *writer, const char *text, size_t most)
{
    size_t size = 0;
    while (size <= most && text[size] != '\0') {
        ++size;
    }
    if (size > most) {
        writer->failed = 1;
        return;
    }
    gs_esp32_put_u8(writer, (uint8_t)size);
    gs_esp32_put_bytes(writer, (const uint8_t *)text, size);
}

static inline size_t gs_esp32_writer_done(const gs_esp32_writer *writer)
{
    return writer->failed ? 0 : writer->used;
}

static inline gs_esp32_reader gs_esp32_reader_make(const uint8_t *data, size_t size)
{
    gs_esp32_reader reader;
    reader.data = data;
    reader.size = size;
    reader.at = 0;
    reader.failed = 0;
    return reader;
}

static inline uint8_t gs_esp32_take_u8(gs_esp32_reader *reader)
{
    if (reader->failed || reader->at >= reader->size) {
        reader->failed = 1;
        return 0;
    }
    return reader->data[reader->at++];
}

static inline uint16_t gs_esp32_take_u16(gs_esp32_reader *reader)
{
    uint16_t low = gs_esp32_take_u8(reader);
    uint16_t high = gs_esp32_take_u8(reader);
    return (uint16_t)(low | (uint16_t)(high << 8));
}

static inline uint32_t gs_esp32_take_u32(gs_esp32_reader *reader)
{
    uint32_t low = gs_esp32_take_u16(reader);
    uint32_t high = gs_esp32_take_u16(reader);
    return low | (high << 16);
}

static inline void gs_esp32_take_bytes(gs_esp32_reader *reader, uint8_t *out, size_t size)
{
    if (reader->failed || size > reader->size - reader->at) {
        reader->failed = 1;
        if (size > 0) {
            memset(out, 0, size);
        }
        return;
    }
    if (size > 0) {
        memcpy(out, reader->data + reader->at, size);
    }
    reader->at += size;
}

static inline void gs_esp32_take_text(gs_esp32_reader *reader, char *out, size_t most)
{
    size_t size = gs_esp32_take_u8(reader);
    size_t i;
    out[0] = '\0';
    if (reader->failed) {
        return;
    }
    if (size > most || size > reader->size - reader->at) {
        reader->failed = 1;
        return;
    }
    for (i = 0; i < size; ++i) {
        if (reader->data[reader->at + i] == 0) {
            reader->failed = 1;
            return;
        }
    }
    if (size > 0) {
        memcpy(out, reader->data + reader->at, size);
    }
    out[size] = '\0';
    reader->at += size;
}

static inline int gs_esp32_reader_done(const gs_esp32_reader *reader)
{
    return reader->failed ? 0 : 1;
}

static inline size_t gs_esp32_frame_encode(uint8_t *out, size_t capacity, uint8_t type, uint8_t seq,
                                           const uint8_t *payload, size_t size)
{
    uint16_t crc;
    if (size > GS_ESP32_MAX_PAYLOAD || capacity < (size_t)GS_ESP32_FRAME_OVERHEAD + size) {
        return 0;
    }
    if (size > 0) {
        memmove(out + GS_ESP32_HEADER_SIZE, payload, size);
    }
    out[0] = GS_ESP32_MAGIC0;
    out[1] = GS_ESP32_MAGIC1;
    out[2] = type;
    out[3] = seq;
    gs_esp32_store_u16(out + 4, (uint16_t)size);
    crc = gs_esp32_crc16(out + 2, 4 + size);
    gs_esp32_store_u16(out + GS_ESP32_HEADER_SIZE + size, crc);
    return (size_t)GS_ESP32_FRAME_OVERHEAD + size;
}

static inline int gs_esp32_frame_decode(const uint8_t *data, size_t size, gs_esp32_frame *frame)
{
    uint16_t length;
    if (size < (size_t)GS_ESP32_FRAME_OVERHEAD || data[0] != GS_ESP32_MAGIC0 || data[1] != GS_ESP32_MAGIC1) {
        return 0;
    }
    length = gs_esp32_load_u16(data + 4);
    if (length > GS_ESP32_MAX_PAYLOAD || size != (size_t)GS_ESP32_FRAME_OVERHEAD + length) {
        return 0;
    }
    if (gs_esp32_crc16(data + 2, (size_t)4 + length) != gs_esp32_load_u16(data + GS_ESP32_HEADER_SIZE + length)) {
        return 0;
    }
    frame->type = data[2];
    frame->seq = data[3];
    frame->length = length;
    frame->payload = data + GS_ESP32_HEADER_SIZE;
    return 1;
}

static inline void gs_esp32_parser_reset(gs_esp32_parser *parser)
{
    parser->used = 0;
    parser->ready = 0;
    parser->dropped = 0;
    parser->bad_frames = 0;
}

static inline void gs_esp32_parser_shift(gs_esp32_parser *parser, size_t count)
{
    if (count >= parser->used) {
        parser->used = 0;
        return;
    }
    memmove(parser->buffer, parser->buffer + count, parser->used - count);
    parser->used -= count;
}

static inline void gs_esp32_parser_consume(gs_esp32_parser *parser)
{
    if (parser->ready > 0) {
        gs_esp32_parser_shift(parser, parser->ready);
        parser->ready = 0;
    }
}

static inline void gs_esp32_parser_skip(gs_esp32_parser *parser)
{
    size_t next = 1;
    while (next < parser->used && parser->buffer[next] != GS_ESP32_MAGIC0) {
        ++next;
    }
    parser->dropped += (uint32_t)next;
    gs_esp32_parser_shift(parser, next);
}

static inline size_t gs_esp32_parser_feed(gs_esp32_parser *parser, const uint8_t *data, size_t size)
{
    size_t room;
    size_t take;
    gs_esp32_parser_consume(parser);
    room = sizeof(parser->buffer) - parser->used;
    take = size < room ? size : room;
    if (take > 0) {
        memcpy(parser->buffer + parser->used, data, take);
    }
    parser->used += take;
    return take;
}

static inline int gs_esp32_parser_next(gs_esp32_parser *parser, gs_esp32_frame *frame)
{
    size_t total;
    uint16_t length;
    gs_esp32_parser_consume(parser);
    for (;;) {
        if (parser->used == 0) {
            return 0;
        }
        if (parser->buffer[0] != GS_ESP32_MAGIC0) {
            gs_esp32_parser_skip(parser);
            continue;
        }
        if (parser->used < 2) {
            return 0;
        }
        if (parser->buffer[1] != GS_ESP32_MAGIC1) {
            gs_esp32_parser_skip(parser);
            continue;
        }
        if (parser->used < (size_t)GS_ESP32_HEADER_SIZE) {
            return 0;
        }
        length = gs_esp32_load_u16(parser->buffer + 4);
        if (length > GS_ESP32_MAX_PAYLOAD) {
            ++parser->bad_frames;
            gs_esp32_parser_skip(parser);
            continue;
        }
        total = (size_t)GS_ESP32_FRAME_OVERHEAD + length;
        if (parser->used < total) {
            return 0;
        }
        if (gs_esp32_crc16(parser->buffer + 2, (size_t)4 + length) !=
            gs_esp32_load_u16(parser->buffer + GS_ESP32_HEADER_SIZE + length)) {
            ++parser->bad_frames;
            gs_esp32_parser_skip(parser);
            continue;
        }
        frame->type = parser->buffer[2];
        frame->seq = parser->buffer[3];
        frame->length = length;
        frame->payload = parser->buffer + GS_ESP32_HEADER_SIZE;
        parser->ready = total;
        return 1;
    }
}

static inline uint32_t gs_esp32_rotr(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static inline void gs_esp32_sha256_compress(uint32_t state[8], const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    unsigned i;
    for (i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 64; ++i) {
        uint32_t s0 = gs_esp32_rotr(w[i - 15], 7) ^ gs_esp32_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = gs_esp32_rotr(w[i - 2], 17) ^ gs_esp32_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];
    for (i = 0; i < 64; ++i) {
        uint32_t s1 = gs_esp32_rotr(e, 6) ^ gs_esp32_rotr(e, 11) ^ gs_esp32_rotr(e, 25);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + choice + k[i] + w[i];
        uint32_t s0 = gs_esp32_rotr(a, 2) ^ gs_esp32_rotr(a, 13) ^ gs_esp32_rotr(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static inline void gs_esp32_sha256_init(gs_esp32_sha256 *context)
{
    context->state[0] = 0x6a09e667u;
    context->state[1] = 0xbb67ae85u;
    context->state[2] = 0x3c6ef372u;
    context->state[3] = 0xa54ff53au;
    context->state[4] = 0x510e527fu;
    context->state[5] = 0x9b05688cu;
    context->state[6] = 0x1f83d9abu;
    context->state[7] = 0x5be0cd19u;
    context->length = 0;
    context->used = 0;
}

static inline void gs_esp32_sha256_update(gs_esp32_sha256 *context, const uint8_t *data, size_t size)
{
    size_t i;
    for (i = 0; i < size; ++i) {
        context->block[context->used++] = data[i];
        if (context->used == 64) {
            gs_esp32_sha256_compress(context->state, context->block);
            context->used = 0;
        }
    }
    context->length += (uint64_t)size * 8u;
}

static inline void gs_esp32_sha256_final(gs_esp32_sha256 *context, uint8_t digest[32])
{
    uint64_t bits = context->length;
    unsigned i;
    context->block[context->used++] = 0x80;
    if (context->used > 56) {
        while (context->used < 64) {
            context->block[context->used++] = 0;
        }
        gs_esp32_sha256_compress(context->state, context->block);
        context->used = 0;
    }
    while (context->used < 56) {
        context->block[context->used++] = 0;
    }
    for (i = 0; i < 8; ++i) {
        context->block[56 + i] = (uint8_t)(bits >> (56u - 8u * i));
    }
    gs_esp32_sha256_compress(context->state, context->block);
    for (i = 0; i < 8; ++i) {
        digest[i * 4] = (uint8_t)(context->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(context->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(context->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)context->state[i];
    }
}

static inline void gs_esp32_sha256_digest(const uint8_t *data, size_t size, uint8_t digest[32])
{
    gs_esp32_sha256 context;
    gs_esp32_sha256_init(&context);
    gs_esp32_sha256_update(&context, data, size);
    gs_esp32_sha256_final(&context, digest);
}

static inline void gs_esp32_hmac_sha256(const uint8_t *key, size_t key_size, const uint8_t *data, size_t size,
                                        uint8_t mac[32])
{
    uint8_t pad[64];
    uint8_t inner[32];
    uint8_t hashed[32];
    gs_esp32_sha256 context;
    size_t i;
    if (key_size > 64) {
        gs_esp32_sha256_digest(key, key_size, hashed);
        key = hashed;
        key_size = 32;
    }
    for (i = 0; i < 64; ++i) {
        pad[i] = (uint8_t)((i < key_size ? key[i] : 0) ^ 0x36);
    }
    gs_esp32_sha256_init(&context);
    gs_esp32_sha256_update(&context, pad, sizeof(pad));
    gs_esp32_sha256_update(&context, data, size);
    gs_esp32_sha256_final(&context, inner);
    for (i = 0; i < 64; ++i) {
        pad[i] = (uint8_t)((i < key_size ? key[i] : 0) ^ 0x5C);
    }
    gs_esp32_sha256_init(&context);
    gs_esp32_sha256_update(&context, pad, sizeof(pad));
    gs_esp32_sha256_update(&context, inner, sizeof(inner));
    gs_esp32_sha256_final(&context, mac);
}

static inline void gs_esp32_tag(const uint8_t *key, const uint8_t *frame, size_t size, uint8_t *tag)
{
    uint8_t mac[32];
    gs_esp32_hmac_sha256(key, GS_ESP32_KEY_SIZE, frame, size, mac);
    memcpy(tag, mac, GS_ESP32_TAG_SIZE);
}

static inline int gs_esp32_tag_matches(const uint8_t *key, const uint8_t *frame, size_t size, const uint8_t *tag)
{
    uint8_t expected[GS_ESP32_TAG_SIZE];
    uint8_t difference = 0;
    size_t i;
    gs_esp32_tag(key, frame, size, expected);
    for (i = 0; i < (size_t)GS_ESP32_TAG_SIZE; ++i) {
        difference = (uint8_t)(difference | (expected[i] ^ tag[i]));
    }
    return difference == 0 ? 1 : 0;
}

static inline size_t gs_esp32_datagram_seal(uint8_t *datagram, size_t frame_size, size_t capacity, const uint8_t *key)
{
    if (frame_size > (size_t)GS_ESP32_MAX_FRAME || capacity < frame_size + GS_ESP32_TAG_SIZE) {
        return 0;
    }
    gs_esp32_tag(key, datagram, frame_size, datagram + frame_size);
    return frame_size + GS_ESP32_TAG_SIZE;
}

static inline int gs_esp32_datagram_open(const uint8_t *datagram, size_t size, const uint8_t *key, gs_esp32_frame *frame)
{
    size_t frame_size;
    if (size < (size_t)GS_ESP32_FRAME_OVERHEAD + GS_ESP32_TAG_SIZE) {
        return 0;
    }
    frame_size = size - GS_ESP32_TAG_SIZE;
    if (!gs_esp32_tag_matches(key, datagram, frame_size, datagram + frame_size)) {
        return 0;
    }
    return gs_esp32_frame_decode(datagram, frame_size, frame);
}

static inline size_t gs_esp32_encode_hello_req(uint8_t *out, size_t capacity, uint8_t version)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    gs_esp32_put_u8(&writer, version);
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_hello_req(const uint8_t *payload, size_t size, uint8_t *version)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    *version = gs_esp32_take_u8(&reader);
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_touch(uint8_t *out, size_t capacity, const gs_esp32_touch *touch)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    uint8_t i;
    if (touch->count > GS_ESP32_MAX_CONTACTS) {
        return 0;
    }
    gs_esp32_put_u8(&writer, touch->count);
    for (i = 0; i < touch->count; ++i) {
        const gs_esp32_contact *contact = &touch->contacts[i];
        if (contact->id > GS_ESP32_CONTACT_ID_MAX || contact->tip > 1 || contact->x > GS_ESP32_COORD_MAX ||
            contact->y > GS_ESP32_COORD_MAX) {
            return 0;
        }
        gs_esp32_put_u8(&writer, contact->id);
        gs_esp32_put_u8(&writer, contact->tip);
        gs_esp32_put_u16(&writer, contact->x);
        gs_esp32_put_u16(&writer, contact->y);
    }
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_touch(const uint8_t *payload, size_t size, gs_esp32_touch *touch)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    uint8_t i;
    memset(touch, 0, sizeof(*touch));
    touch->count = gs_esp32_take_u8(&reader);
    if (touch->count > GS_ESP32_MAX_CONTACTS) {
        return 0;
    }
    for (i = 0; i < touch->count; ++i) {
        gs_esp32_contact *contact = &touch->contacts[i];
        contact->id = gs_esp32_take_u8(&reader);
        contact->tip = gs_esp32_take_u8(&reader);
        contact->x = gs_esp32_take_u16(&reader);
        contact->y = gs_esp32_take_u16(&reader);
        if (contact->id > GS_ESP32_CONTACT_ID_MAX || contact->tip > 1 || contact->x > GS_ESP32_COORD_MAX ||
            contact->y > GS_ESP32_COORD_MAX) {
            return 0;
        }
    }
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_keyboard(uint8_t *out, size_t capacity, const gs_esp32_keyboard *keyboard)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    gs_esp32_put_u8(&writer, keyboard->modifiers);
    gs_esp32_put_bytes(&writer, keyboard->keys, GS_ESP32_KEYS);
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_keyboard(const uint8_t *payload, size_t size, gs_esp32_keyboard *keyboard)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    keyboard->modifiers = gs_esp32_take_u8(&reader);
    gs_esp32_take_bytes(&reader, keyboard->keys, GS_ESP32_KEYS);
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_consumer(uint8_t *out, size_t capacity, uint16_t usage)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    gs_esp32_put_u16(&writer, usage);
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_consumer(const uint8_t *payload, size_t size, uint16_t *usage)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    *usage = gs_esp32_take_u16(&reader);
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_mouse(uint8_t *out, size_t capacity, const gs_esp32_mouse *mouse)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    gs_esp32_put_u8(&writer, mouse->buttons);
    gs_esp32_put_u16(&writer, (uint16_t)mouse->dx);
    gs_esp32_put_u16(&writer, (uint16_t)mouse->dy);
    gs_esp32_put_u8(&writer, (uint8_t)mouse->wheel);
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_mouse(const uint8_t *payload, size_t size, gs_esp32_mouse *mouse)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    mouse->buttons = gs_esp32_take_u8(&reader);
    mouse->dx = (int16_t)gs_esp32_take_u16(&reader);
    mouse->dy = (int16_t)gs_esp32_take_u16(&reader);
    mouse->wheel = (int8_t)gs_esp32_take_u8(&reader);
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_gamepad(uint8_t *out, size_t capacity, const gs_esp32_gamepad *gamepad)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    if (gamepad->hat > GS_ESP32_HAT_CENTERED) {
        return 0;
    }
    gs_esp32_put_u32(&writer, gamepad->buttons);
    gs_esp32_put_u8(&writer, gamepad->hat);
    gs_esp32_put_u16(&writer, (uint16_t)gamepad->lx);
    gs_esp32_put_u16(&writer, (uint16_t)gamepad->ly);
    gs_esp32_put_u16(&writer, (uint16_t)gamepad->rx);
    gs_esp32_put_u16(&writer, (uint16_t)gamepad->ry);
    gs_esp32_put_u8(&writer, gamepad->lt);
    gs_esp32_put_u8(&writer, gamepad->rt);
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_gamepad(const uint8_t *payload, size_t size, gs_esp32_gamepad *gamepad)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    gamepad->buttons = gs_esp32_take_u32(&reader);
    gamepad->hat = gs_esp32_take_u8(&reader);
    gamepad->lx = (int16_t)gs_esp32_take_u16(&reader);
    gamepad->ly = (int16_t)gs_esp32_take_u16(&reader);
    gamepad->rx = (int16_t)gs_esp32_take_u16(&reader);
    gamepad->ry = (int16_t)gs_esp32_take_u16(&reader);
    gamepad->lt = gs_esp32_take_u8(&reader);
    gamepad->rt = gs_esp32_take_u8(&reader);
    if (gamepad->hat > GS_ESP32_HAT_CENTERED) {
        return 0;
    }
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_token(uint8_t *out, size_t capacity, uint32_t token)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    gs_esp32_put_u32(&writer, token);
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_token(const uint8_t *payload, size_t size, uint32_t *token)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    *token = gs_esp32_take_u32(&reader);
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_set_baud(uint8_t *out, size_t capacity, const gs_esp32_set_baud *request)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    gs_esp32_put_u32(&writer, request->baud);
    gs_esp32_put_u32(&writer, request->token);
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_set_baud(const uint8_t *payload, size_t size, gs_esp32_set_baud *request)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    request->baud = gs_esp32_take_u32(&reader);
    request->token = gs_esp32_take_u32(&reader);
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_wifi_set(uint8_t *out, size_t capacity, const gs_esp32_wifi_set *wifi)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    if (wifi->ssid[0] == '\0') {
        return 0;
    }
    gs_esp32_put_text(&writer, wifi->ssid, GS_ESP32_MAX_SSID);
    gs_esp32_put_text(&writer, wifi->password, GS_ESP32_MAX_PASSWORD);
    gs_esp32_put_bytes(&writer, wifi->key, GS_ESP32_KEY_SIZE);
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_wifi_set(const uint8_t *payload, size_t size, gs_esp32_wifi_set *wifi)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    gs_esp32_take_text(&reader, wifi->ssid, GS_ESP32_MAX_SSID);
    gs_esp32_take_text(&reader, wifi->password, GS_ESP32_MAX_PASSWORD);
    gs_esp32_take_bytes(&reader, wifi->key, GS_ESP32_KEY_SIZE);
    if (wifi->ssid[0] == '\0') {
        return 0;
    }
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_name(uint8_t *out, size_t capacity, const char *name)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    if (name[0] == '\0') {
        return 0;
    }
    gs_esp32_put_text(&writer, name, GS_ESP32_MAX_NAME);
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_name(const uint8_t *payload, size_t size, char name[GS_ESP32_MAX_NAME + 1])
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    gs_esp32_take_text(&reader, name, GS_ESP32_MAX_NAME);
    if (name[0] == '\0') {
        return 0;
    }
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_hello(uint8_t *out, size_t capacity, const gs_esp32_hello *hello)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    gs_esp32_put_u8(&writer, hello->protocol);
    gs_esp32_put_text(&writer, hello->firmware, GS_ESP32_MAX_FIRMWARE);
    gs_esp32_put_u16(&writer, hello->chip_id);
    gs_esp32_put_u16(&writer, hello->chip_revision);
    gs_esp32_put_u32(&writer, hello->features);
    gs_esp32_put_bytes(&writer, hello->mac, sizeof(hello->mac));
    gs_esp32_put_u8(&writer, hello->wifi);
    gs_esp32_put_bytes(&writer, hello->ipv4, sizeof(hello->ipv4));
    gs_esp32_put_text(&writer, hello->name, GS_ESP32_MAX_NAME);
    if (hello->extension != 0) {
        gs_esp32_put_u8(&writer, hello->extension);
        gs_esp32_put_u8(&writer, hello->parts.usb);
        gs_esp32_put_u8(&writer, hello->parts.usb_problem);
        gs_esp32_put_u8(&writer, hello->parts.ble);
        gs_esp32_put_u8(&writer, hello->parts.ble_problem);
        gs_esp32_put_u8(&writer, hello->parts.wifi_problem);
    }
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_hello(const uint8_t *payload, size_t size, gs_esp32_hello *hello)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    uint8_t version;
    hello->protocol = gs_esp32_take_u8(&reader);
    gs_esp32_take_text(&reader, hello->firmware, GS_ESP32_MAX_FIRMWARE);
    hello->chip_id = gs_esp32_take_u16(&reader);
    hello->chip_revision = gs_esp32_take_u16(&reader);
    hello->features = gs_esp32_take_u32(&reader);
    gs_esp32_take_bytes(&reader, hello->mac, sizeof(hello->mac));
    hello->wifi = gs_esp32_take_u8(&reader);
    gs_esp32_take_bytes(&reader, hello->ipv4, sizeof(hello->ipv4));
    gs_esp32_take_text(&reader, hello->name, GS_ESP32_MAX_NAME);
    hello->extension = 0;
    memset(&hello->parts, 0, sizeof(hello->parts));
    if (!reader.failed && reader.size - reader.at >= 6) {
        version = gs_esp32_take_u8(&reader);
        if (version == GS_ESP32_HELLO_EXTENSION) {
            hello->parts.usb = gs_esp32_take_u8(&reader);
            hello->parts.usb_problem = gs_esp32_take_u8(&reader);
            hello->parts.ble = gs_esp32_take_u8(&reader);
            hello->parts.ble_problem = gs_esp32_take_u8(&reader);
            hello->parts.wifi_problem = gs_esp32_take_u8(&reader);
            if (!reader.failed) {
                hello->extension = version;
            } else {
                memset(&hello->parts, 0, sizeof(hello->parts));
            }
        }
    }
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_status(uint8_t *out, size_t capacity, const gs_esp32_status *status)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    gs_esp32_put_u8(&writer, status->usb);
    gs_esp32_put_u8(&writer, status->ble);
    gs_esp32_put_text(&writer, status->peer, GS_ESP32_MAX_NAME);
    gs_esp32_put_u8(&writer, status->wifi);
    gs_esp32_put_bytes(&writer, status->ipv4, sizeof(status->ipv4));
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_status(const uint8_t *payload, size_t size, gs_esp32_status *status)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    status->usb = gs_esp32_take_u8(&reader);
    status->ble = gs_esp32_take_u8(&reader);
    gs_esp32_take_text(&reader, status->peer, GS_ESP32_MAX_NAME);
    status->wifi = gs_esp32_take_u8(&reader);
    gs_esp32_take_bytes(&reader, status->ipv4, sizeof(status->ipv4));
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_log(uint8_t *out, size_t capacity, const gs_esp32_log *message)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    gs_esp32_put_u8(&writer, message->level);
    gs_esp32_put_text(&writer, message->text, GS_ESP32_MAX_TEXT);
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_log(const uint8_t *payload, size_t size, gs_esp32_log *message)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    message->level = gs_esp32_take_u8(&reader);
    gs_esp32_take_text(&reader, message->text, GS_ESP32_MAX_TEXT);
    return gs_esp32_reader_done(&reader);
}

static inline size_t gs_esp32_encode_error(uint8_t *out, size_t capacity, const gs_esp32_error *error)
{
    gs_esp32_writer writer = gs_esp32_writer_make(out, capacity);
    gs_esp32_put_u16(&writer, error->code);
    gs_esp32_put_text(&writer, error->text, GS_ESP32_MAX_TEXT);
    return gs_esp32_writer_done(&writer);
}

static inline int gs_esp32_decode_error(const uint8_t *payload, size_t size, gs_esp32_error *error)
{
    gs_esp32_reader reader = gs_esp32_reader_make(payload, size);
    error->code = gs_esp32_take_u16(&reader);
    gs_esp32_take_text(&reader, error->text, GS_ESP32_MAX_TEXT);
    return gs_esp32_reader_done(&reader);
}

static inline void gs_esp32_legacy_packet(uint8_t *out, uint8_t tip, uint8_t id, uint32_t x, uint32_t y, uint8_t count)
{
    out[0] = GS_ESP32_LEGACY_MAGIC;
    out[1] = (uint8_t)(tip != 0 ? 1 : 0);
    out[2] = id;
    out[3] = (uint8_t)(x & 0xFFu);
    out[4] = (uint8_t)((x >> 8) & 0xFFu);
    out[5] = (uint8_t)((x >> 16) & 0xFFu);
    out[6] = (uint8_t)((x >> 24) & 0xFFu);
    out[7] = (uint8_t)(y & 0xFFu);
    out[8] = (uint8_t)((y >> 8) & 0xFFu);
    out[9] = (uint8_t)((y >> 16) & 0xFFu);
    out[10] = (uint8_t)((y >> 24) & 0xFFu);
    out[11] = count;
}

#ifdef __cplusplus
}
#endif

#endif
