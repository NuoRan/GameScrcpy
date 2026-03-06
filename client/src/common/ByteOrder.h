#pragma once
/**
 * @file ByteOrder.h
 * @brief 字节序转换辅助函数 (替代 QtEndian)
 *
 * 提供大端/小端转换, 利用 MSVC intrinsics 或 GCC/Clang builtins.
 */

#include <cstdint>
#include <cstring>

#ifdef _MSC_VER
#include <stdlib.h> // _byteswap_ulong, _byteswap_uint64
#endif

namespace qsc {

inline uint16_t bswap16(uint16_t x) {
#ifdef _MSC_VER
    return _byteswap_ushort(x);
#else
    return __builtin_bswap16(x);
#endif
}

inline uint32_t bswap32(uint32_t x) {
#ifdef _MSC_VER
    return _byteswap_ulong(x);
#else
    return __builtin_bswap32(x);
#endif
}

inline uint64_t bswap64(uint64_t x) {
#ifdef _MSC_VER
    return _byteswap_uint64(x);
#else
    return __builtin_bswap64(x);
#endif
}

/// 从大端字节流中读取 uint32_t
inline uint32_t readBigEndian32(const void* src) {
    uint32_t val;
    std::memcpy(&val, src, sizeof(val));
    return bswap32(val);
}

/// 从大端字节流中读取 uint64_t
inline uint64_t readBigEndian64(const void* src) {
    uint64_t val;
    std::memcpy(&val, src, sizeof(val));
    return bswap64(val);
}

/// 将 uint32_t 写入大端字节流
inline void writeBigEndian32(void* dst, uint32_t val) {
    val = bswap32(val);
    std::memcpy(dst, &val, sizeof(val));
}

/// 将 uint64_t 写入大端字节流
inline void writeBigEndian64(void* dst, uint64_t val) {
    val = bswap64(val);
    std::memcpy(dst, &val, sizeof(val));
}

} // namespace qsc
