#pragma once
/**
 * @file GameTypes.h
 * @brief 纯 C++ 几何/工具类型，替代 QSize / QPointF / QRectF / QScopeGuard
 *
 * 设计原则:
 *   - 零 Qt 依赖，纯 POD / 值语义
 *   - 提供与 Qt 类型之间的无缝转换辅助（在 UI 边界使用）
 *   - 所有成员默认初始化为 0
 */

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

// ============================================================================
//  Size  (替代 QSize)
// ============================================================================
struct Size {
    int width  = 0;
    int height = 0;

    Size() = default;
    Size(int w, int h) : width(w), height(h) {}

    bool isEmpty()   const { return width <= 0 || height <= 0; }
    bool isValid()   const { return width >= 0 && height >= 0; }
    bool isNull()    const { return width == 0 && height == 0; }

    Size transposed() const { return { height, width }; }

    bool operator==(const Size& o) const { return width == o.width && height == o.height; }
    bool operator!=(const Size& o) const { return !(*this == o); }
};

// ============================================================================
//  PointF  (替代 QPointF)
// ============================================================================
struct PointF {
    double x = 0.0;
    double y = 0.0;

    PointF() = default;
    PointF(double px, double py) : x(px), y(py) {}

    bool isNull() const { return x == 0.0 && y == 0.0; }

    PointF operator+(const PointF& o) const { return { x + o.x, y + o.y }; }
    PointF operator-(const PointF& o) const { return { x - o.x, y - o.y }; }
    PointF operator*(double f)        const { return { x * f, y * f }; }
    PointF operator/(double f)        const { return { x / f, y / f }; }

    PointF& operator+=(const PointF& o) { x += o.x; y += o.y; return *this; }
    PointF& operator-=(const PointF& o) { x -= o.x; y -= o.y; return *this; }
    PointF& operator*=(double f) { x *= f; y *= f; return *this; }
    PointF& operator/=(double f) { x /= f; y /= f; return *this; }

    bool operator==(const PointF& o) const { return x == o.x && y == o.y; }
    bool operator!=(const PointF& o) const { return !(*this == o); }
};

// ============================================================================
//  RectF  (替代 QRectF)
// ============================================================================
struct RectF {
    double x      = 0.0;
    double y      = 0.0;
    double width  = 0.0;
    double height = 0.0;

    RectF() = default;
    RectF(double rx, double ry, double rw, double rh)
        : x(rx), y(ry), width(rw), height(rh) {}

    bool isEmpty()  const { return width <= 0.0 || height <= 0.0; }
    bool isValid()  const { return width > 0.0 && height > 0.0; }
    bool isNull()   const { return width == 0.0 && height == 0.0; }

    double left()   const { return x; }
    double top()    const { return y; }
    double right()  const { return x + width; }
    double bottom() const { return y + height; }

    PointF topLeft()     const { return { x, y }; }
    PointF bottomRight() const { return { x + width, y + height }; }
    PointF center()      const { return { x + width / 2.0, y + height / 2.0 }; }

    bool contains(const PointF& p) const {
        return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height;
    }

    bool operator==(const RectF& o) const {
        return x == o.x && y == o.y && width == o.width && height == o.height;
    }
    bool operator!=(const RectF& o) const { return !(*this == o); }
};

// ============================================================================
//  Rect  (替代 QRect — 整数版)
// ============================================================================
struct Rect {
    int x      = 0;
    int y      = 0;
    int width  = 0;
    int height = 0;

    Rect() = default;
    Rect(int rx, int ry, int rw, int rh) : x(rx), y(ry), width(rw), height(rh) {}

    bool isEmpty()  const { return width <= 0 || height <= 0; }
    bool isValid()  const { return width > 0 && height > 0; }
    bool isNull()   const { return width == 0 && height == 0; }

    int left()   const { return x; }
    int top()    const { return y; }
    int right()  const { return x + width; }
    int bottom() const { return y + height; }

    bool operator==(const Rect& o) const {
        return x == o.x && y == o.y && width == o.width && height == o.height;
    }
    bool operator!=(const Rect& o) const { return !(*this == o); }
};

// ============================================================================
//  ScopeGuard  (替代 QScopeGuard / qScopeGuard)
// ============================================================================
template <typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F&& fn) : m_fn(std::move(fn)), m_active(true) {}
    explicit ScopeGuard(const F& fn) : m_fn(fn), m_active(true) {}

    ~ScopeGuard() { if (m_active) m_fn(); }

    ScopeGuard(ScopeGuard&& other) noexcept
        : m_fn(std::move(other.m_fn)), m_active(other.m_active) {
        other.dismiss();
    }

    void dismiss() { m_active = false; }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

private:
    F    m_fn;
    bool m_active;
};

template <typename F>
ScopeGuard<typename std::decay<F>::type> makeScopeGuard(F&& fn) {
    return ScopeGuard<typename std::decay<F>::type>(std::forward<F>(fn));
}

// ============================================================================
//  Qt 边界转换辅助 (仅在包含 Qt 头文件时可用)
// ============================================================================
#ifdef QT_CORE_LIB
#include <QSize>
#include <QPointF>
#include <QRectF>
#include <QRect>

namespace typeconv {

inline Size    fromQ(const QSize& s)   { return { s.width(), s.height() }; }
inline QSize   toQ(const Size& s)      { return QSize(s.width, s.height); }

inline PointF  fromQ(const QPointF& p) { return { p.x(), p.y() }; }
inline QPointF toQ(const PointF& p)    { return QPointF(p.x, p.y); }

inline RectF   fromQ(const QRectF& r)  { return { r.x(), r.y(), r.width(), r.height() }; }
inline QRectF  toQ(const RectF& r)     { return QRectF(r.x, r.y, r.width, r.height); }

inline Rect    fromQ(const QRect& r)   { return { r.x(), r.y(), r.width(), r.height() }; }
inline QRect   toQ(const Rect& r)      { return QRect(r.x, r.y, r.width, r.height); }

} // namespace typeconv
#endif

// ============================================================================
//  Script value type (替代 QVariant for script variables)
// ============================================================================
#include <variant>
#include <string>

using ScriptValue = std::variant<std::monostate, bool, int, double, std::string>;

// ============================================================================
//  Script result structs (替代 QVariantMap)
// ============================================================================
struct PosResult {
    double x = 0.0;
    double y = 0.0;
};

struct KeyPosResult {
    double x = 0.0;
    double y = 0.0;
    bool valid = false;
};

struct ButtonPosResult {
    double x = 0.0;
    double y = 0.0;
    bool valid = false;
    std::string name;
};

struct FindImageResult {
    bool found = false;
    double x = 0.0;
    double y = 0.0;
    double confidence = 0.0;
};

// ============================================================================
//  scriptval — ScriptValue 便捷转换
// ============================================================================
namespace scriptval {
inline bool toBool(const ScriptValue& v, bool def = false) {
    if (auto* p = std::get_if<bool>(&v)) return *p;
    return def;
}
inline int toInt(const ScriptValue& v, int def = 0) {
    if (auto* p = std::get_if<int>(&v)) return *p;
    if (auto* d = std::get_if<double>(&v)) return static_cast<int>(*d);
    return def;
}
inline double toDouble(const ScriptValue& v, double def = 0.0) {
    if (auto* d = std::get_if<double>(&v)) return *d;
    if (auto* p = std::get_if<int>(&v)) return static_cast<double>(*p);
    return def;
}
inline std::string toString(const ScriptValue& v, const std::string& def = "") {
    if (auto* p = std::get_if<std::string>(&v)) return *p;
    return def;
}
inline bool isNull(const ScriptValue& v) {
    return std::holds_alternative<std::monostate>(v);
}
} // namespace scriptval
// ============================================================================
//  ConfigValue — configuration value type (replaces QVariant for ConfigCenter)
// ============================================================================
using ConfigValue = std::variant<std::monostate, bool, int, uint32_t, double, std::string, Rect>;

namespace configval {
inline bool toBool(const ConfigValue& v, bool def = false) {
    if (auto* p = std::get_if<bool>(&v)) return *p;
    if (auto* p = std::get_if<int>(&v)) return *p != 0;
    return def;
}
inline int toInt(const ConfigValue& v, int def = 0) {
    if (auto* p = std::get_if<int>(&v)) return *p;
    if (auto* p = std::get_if<uint32_t>(&v)) return static_cast<int>(*p);
    if (auto* p = std::get_if<double>(&v)) return static_cast<int>(*p);
    if (auto* p = std::get_if<bool>(&v)) return *p ? 1 : 0;
    return def;
}
inline uint32_t toUInt(const ConfigValue& v, uint32_t def = 0) {
    if (auto* p = std::get_if<uint32_t>(&v)) return *p;
    if (auto* p = std::get_if<int>(&v)) return static_cast<uint32_t>(*p);
    if (auto* p = std::get_if<double>(&v)) return static_cast<uint32_t>(*p);
    return def;
}
inline double toDouble(const ConfigValue& v, double def = 0.0) {
    if (auto* p = std::get_if<double>(&v)) return *p;
    if (auto* p = std::get_if<int>(&v)) return static_cast<double>(*p);
    if (auto* p = std::get_if<uint32_t>(&v)) return static_cast<double>(*p);
    return def;
}
inline std::string toString(const ConfigValue& v, const std::string& def = "") {
    if (auto* p = std::get_if<std::string>(&v)) return *p;
    if (auto* p = std::get_if<bool>(&v)) return *p ? "true" : "false";
    if (auto* p = std::get_if<int>(&v)) return std::to_string(*p);
    if (auto* p = std::get_if<uint32_t>(&v)) return std::to_string(*p);
    if (auto* p = std::get_if<double>(&v)) return std::to_string(*p);
    return def;
}
inline Rect toRect(const ConfigValue& v, const Rect& def = Rect()) {
    if (auto* p = std::get_if<Rect>(&v)) return *p;
    return def;
}
inline bool isNull(const ConfigValue& v) {
    return std::holds_alternative<std::monostate>(v);
}
} // namespace configval
