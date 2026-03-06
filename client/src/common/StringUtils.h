#ifndef STRINGUTILS_H
#define STRINGUTILS_H

/**
 * @file StringUtils.h
 * @brief UTF-8 std::string ↔ QString / std::wstring 双向转换工具
 *
 * 所有非 UI 代码使用 std::string (UTF-8)，UI 边界处使用这些工具转换。
 * All non-UI code uses std::string (UTF-8), convert at UI boundary using these utilities.
 */

#include <string>
#include <cstdarg>
#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

// Qt 转换仅在有 Qt 时可用
#ifdef QT_CORE_LIB
#include <QString>
#include <QStringList>
#include <vector>
#endif

namespace strutil {

// ============================================================
// UTF-8 std::string ↔ std::wstring (Win32 API 交互)
// ============================================================

#ifdef _WIN32
/**
 * @brief UTF-8 std::string → std::wstring (UTF-16)
 */
inline std::wstring toWide(const std::string& utf8)
{
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), len);
    return result;
}

/**
 * @brief std::wstring (UTF-16) → UTF-8 std::string
 */
inline std::string fromWide(const std::wstring& wide)
{
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), len, nullptr, nullptr);
    return result;
}
#endif

// ============================================================
// 可执行文件目录 (替代 QCoreApplication::applicationDirPath)
// ============================================================

#ifdef _WIN32
/**
 * @brief 获取当前可执行文件所在目录 (UTF-8)
 * Replaces QCoreApplication::applicationDirPath()
 */
inline std::string appDirPath()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fromWide(std::filesystem::path(buf).parent_path().wstring());
}
#endif

// ============================================================
// UTF-8 std::string ↔ QString (Qt UI 边界)
// ============================================================

#ifdef QT_CORE_LIB
/**
 * @brief std::string (UTF-8) → QString
 */
inline QString toQ(const std::string& s)
{
    return QString::fromUtf8(s.c_str(), static_cast<int>(s.size()));
}

/**
 * @brief QString → std::string (UTF-8)
 */
inline std::string fromQ(const QString& s)
{
    auto u8 = s.toUtf8();
    return std::string(u8.data(), static_cast<size_t>(u8.size()));
}

/**
 * @brief std::vector<std::string> → QStringList
 */
inline QStringList toQList(const std::vector<std::string>& v)
{
    QStringList result;
    result.reserve(static_cast<int>(v.size()));
    for (const auto& s : v) result << toQ(s);
    return result;
}
#endif

// ============================================================
// printf-style 格式化
// ============================================================

/**
 * @brief printf-style 字符串格式化
 * @param fmt 格式字符串
 * @return 格式化后的 std::string
 */
inline std::string format(const char* fmt, ...)
{
    // 先用小缓冲区尝试
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n < 0) return {};
    if (static_cast<size_t>(n) < sizeof(buf)) {
        return std::string(buf, static_cast<size_t>(n));
    }

    // 需要更大的缓冲区
    std::string result(static_cast<size_t>(n) + 1, '\0');
    va_start(args, fmt);
    vsnprintf(result.data(), result.size(), fmt, args);
    va_end(args);
    result.resize(static_cast<size_t>(n));
    return result;
}

// ============================================================
// 常用字符串操作
// ============================================================

/**
 * @brief 检查字符串是否以指定前缀开头
 */
inline bool startsWith(const std::string& str, const std::string& prefix)
{
    if (prefix.size() > str.size()) return false;
    return str.compare(0, prefix.size(), prefix) == 0;
}

/**
 * @brief 检查字符串是否以指定后缀结尾
 */
inline bool endsWith(const std::string& str, const std::string& suffix)
{
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/**
 * @brief 去除字符串两端空白
 */
inline std::string trim(const std::string& str)
{
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

/**
 * @brief 字符串替换（所有匹配项）
 */
inline std::string replaceAll(const std::string& str, const std::string& from, const std::string& to)
{
    if (from.empty()) return str;
    std::string result = str;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.size(), to);
        pos += to.size();
    }
    return result;
}

} // namespace strutil

#endif // STRINGUTILS_H
