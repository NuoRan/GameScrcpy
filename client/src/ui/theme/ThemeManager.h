/**
 * @file ThemeManager.h
 * @brief 主题管理器 — 单例，管理深色/浅色/跟随系统主题 + 强调色
 *
 * 职责：
 * - 运行时主题切换 (深色/浅色/跟随系统)
 * - 强调色切换 (6种预设)
 * - 编译 QSS 模板 ({{token}} → 实际颜色)
 * - 通知所有组件主题变更
 */

#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QColor>
#include <QString>

namespace Fluent {

enum class Theme { Dark, Light, System };
enum class AccentColor { Indigo, Blue, Violet, Rose, Emerald, Amber };

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    static ThemeManager& instance();

    // ─── 主题控制 ─────────────────────────────────────
    void setTheme(Theme theme);
    Theme currentTheme() const { return m_theme; }
    bool isDarkMode() const;

    // ─── 强调色 ───────────────────────────────────────
    void setAccentColor(AccentColor color);
    AccentColor currentAccentColor() const { return m_accent; }

    // ─── Token 访问 (根据当前主题返回颜色字符串) ──────
    const char* base() const;
    const char* card() const;
    const char* surface() const;
    const char* border() const;
    const char* borderSoft() const;
    const char* textPrimary() const;
    const char* textSecondary() const;
    const char* textTertiary() const;
    const char* textDisabled() const;
    const char* navBackground() const;
    const char* navHover() const;
    const char* navActive() const;
    const char* inputBg() const;
    const char* inputBorder() const;
    const char* inputFocusBorder() const;
    const char* scrollThumb() const;
    const char* scrollTrack() const;

    // 强调色 Token
    const char* accentPrimary() const;
    const char* accentHover() const;
    const char* accentActive() const;

    // QColor 版本
    QColor baseColor() const       { return QColor(base()); }
    QColor cardColor() const       { return QColor(card()); }
    QColor surfaceColor() const    { return QColor(surface()); }
    QColor borderColor() const     { return QColor(border()); }
    QColor accentColor() const     { return QColor(accentPrimary()); }
    QColor textPrimaryColor() const { return QColor(textPrimary()); }

    // ─── QSS 编译 ────────────────────────────────────
    /// 将 {{base}}, {{card}}, {{accent}} 等占位符替换为实际颜色值
    QString compileQss(const QString& templateQss) const;

    /// 加载并编译 QSS 模板文件
    QString loadAndCompileQss(const QString& resourcePath) const;

    /// 获取当前完整编译后的全局 QSS
    QString compiledGlobalStyleSheet() const;

    /// 应用主题到 qApp
    void applyToApplication();

signals:
    void themeChanged(bool isDark);
    void accentColorChanged();

public:
    struct AccentPalette {
        const char* primary;
        const char* hover;
        const char* active;
        const char* subtle;
    };
    static const AccentPalette& accentPalette(AccentColor c);

private:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override = default;

    bool systemIsDark() const;
    void resolveEffectiveTheme();

private:
    Theme m_theme = Theme::Dark;
    AccentColor m_accent = AccentColor::Indigo;
    bool m_effectiveDark = true;
};

} // namespace Fluent

#endif // THEMEMANAGER_H
