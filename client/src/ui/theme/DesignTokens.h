/**
 * @file DesignTokens.h
 * @brief Fluent Focus 设计令牌 — 颜色、间距、圆角、阴影的集中定义
 *
 * 所有 UI 组件必须引用此处常量，禁止硬编码颜色值。
 * 深色/浅色 Token 在运行时由 ThemeManager 选择。
 */

#ifndef DESIGNTOKENS_H
#define DESIGNTOKENS_H

#include <QColor>
#include <QString>

namespace Fluent {

// ─── 深色主题 Token ───────────────────────────────────────
namespace Dark {
    // 背景层级 (Zinc 灰度)
    inline constexpr const char* Base       = "#09090b";   // zinc-950
    inline constexpr const char* Card       = "#18181b";   // zinc-900
    inline constexpr const char* Surface    = "#27272a";   // zinc-800
    inline constexpr const char* Border     = "#3f3f46";   // zinc-700
    inline constexpr const char* BorderSoft = "#27272a";   // zinc-800

    // 文字层级
    inline constexpr const char* TextPrimary   = "#fafafa";  // zinc-50
    inline constexpr const char* TextSecondary = "#a1a1aa";  // zinc-400
    inline constexpr const char* TextTertiary  = "#71717a";  // zinc-500
    inline constexpr const char* TextDisabled  = "#52525b";  // zinc-600

    // 导航栏
    inline constexpr const char* NavBackground = "#0f0f12";  // 比 Base 稍亮
    inline constexpr const char* NavHover      = "#1e1e22";
    inline constexpr const char* NavActive     = "#27272a";
    inline constexpr const char* NavIndicator  = "#6366f1";  // accent

    // 输入框
    inline constexpr const char* InputBg       = "#18181b";
    inline constexpr const char* InputBorder   = "#3f3f46";
    inline constexpr const char* InputFocusBorder = "#6366f1";

    // 滚动条
    inline constexpr const char* ScrollThumb   = "#3f3f46";
    inline constexpr const char* ScrollTrack   = "#18181b";
}

// ─── 浅色主题 Token ───────────────────────────────────────
namespace Light {
    inline constexpr const char* Base       = "#ffffff";
    inline constexpr const char* Card       = "#f4f4f5";   // zinc-100
    inline constexpr const char* Surface    = "#e4e4e7";   // zinc-200
    inline constexpr const char* Border     = "#d4d4d8";   // zinc-300
    inline constexpr const char* BorderSoft = "#e4e4e7";   // zinc-200

    inline constexpr const char* TextPrimary   = "#09090b";  // zinc-950
    inline constexpr const char* TextSecondary = "#52525b";  // zinc-600
    inline constexpr const char* TextTertiary  = "#71717a";  // zinc-500
    inline constexpr const char* TextDisabled  = "#a1a1aa";  // zinc-400

    inline constexpr const char* NavBackground = "#f8f8fa";
    inline constexpr const char* NavHover      = "#ececef";
    inline constexpr const char* NavActive     = "#e4e4e7";
    inline constexpr const char* NavIndicator  = "#6366f1";

    inline constexpr const char* InputBg       = "#ffffff";
    inline constexpr const char* InputBorder   = "#d4d4d8";
    inline constexpr const char* InputFocusBorder = "#6366f1";

    inline constexpr const char* ScrollThumb   = "#d4d4d8";
    inline constexpr const char* ScrollTrack   = "#f4f4f5";
}

// ─── 强调色 (主题无关) ────────────────────────────────────
namespace Accent {
    // Indigo (默认)
    inline constexpr const char* Primary    = "#6366f1";  // indigo-500
    inline constexpr const char* Hover      = "#818cf8";  // indigo-400
    inline constexpr const char* Active     = "#4f46e5";  // indigo-600
    inline constexpr const char* Subtle     = "#6366f11f"; // 12% 不透明度

    // 语义色
    inline constexpr const char* Success    = "#22c55e";
    inline constexpr const char* Warning    = "#f59e0b";
    inline constexpr const char* Error      = "#ef4444";
    inline constexpr const char* Info       = "#3b82f6";

    inline constexpr const char* SuccessSubtle = "#22c55e1f";
    inline constexpr const char* WarningSubtle = "#f59e0b1f";
    inline constexpr const char* ErrorSubtle   = "#ef44441f";
}

// ─── 间距 Token ──────────────────────────────────────────
namespace Spacing {
    inline constexpr int XXS = 2;
    inline constexpr int XS  = 4;
    inline constexpr int S   = 8;
    inline constexpr int M   = 12;
    inline constexpr int L   = 16;
    inline constexpr int XL  = 24;
    inline constexpr int XXL = 32;
    inline constexpr int XXXL = 48;
}

// ─── 圆角 Token ──────────────────────────────────────────
namespace Radius {
    inline constexpr int Small  = 4;
    inline constexpr int Medium = 8;
    inline constexpr int Large  = 12;
    inline constexpr int XLarge = 16;
    inline constexpr int Round  = 9999;
}

// ─── 字体 Token ──────────────────────────────────────────
namespace Font {
    inline constexpr int Caption   = 11;
    inline constexpr int Body      = 13;
    inline constexpr int BodyLarge = 14;
    inline constexpr int Subtitle  = 16;
    inline constexpr int Title     = 20;
    inline constexpr int TitleLarge = 24;
    inline constexpr int Display   = 32;
}

// ─── 阴影 Token (QSS drop-shadow 不支持，用于 QGraphicsEffect) ─
namespace Shadow {
    inline constexpr int CardBlur    = 8;
    inline constexpr int CardOffsetY = 2;
    inline constexpr int DialogBlur  = 24;
    inline constexpr int DialogOffsetY = 8;
}

// ─── 导航栏尺寸 ──────────────────────────────────────────
namespace Nav {
    inline constexpr int ExpandedWidth  = 200;
    inline constexpr int CollapsedWidth = 56;
    inline constexpr int ItemHeight     = 40;
    inline constexpr int IconSize       = 20;
    inline constexpr int IndicatorWidth = 3;
    inline constexpr int IndicatorHeight = 16;
}

} // namespace Fluent

#endif // DESIGNTOKENS_H
