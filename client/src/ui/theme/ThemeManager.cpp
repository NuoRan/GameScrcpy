/**
 * @file ThemeManager.cpp
 * @brief ThemeManager 实现
 */

#include "ThemeManager.h"
#include "DesignTokens.h"

#include <QApplication>
#include <QFile>
#include <QStyleHints>
#include <QPalette>

namespace Fluent {

// ─── 单例 ─────────────────────────────────────────────────

ThemeManager& ThemeManager::instance()
{
    static ThemeManager s_instance;
    return s_instance;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
    resolveEffectiveTheme();
}

// ─── 主题控制 ──────────────────────────────────────────────

void ThemeManager::setTheme(Theme theme)
{
    if (m_theme == theme) return;
    m_theme = theme;
    resolveEffectiveTheme();
    applyToApplication();
    emit themeChanged(m_effectiveDark);
}

bool ThemeManager::isDarkMode() const
{
    return m_effectiveDark;
}

bool ThemeManager::systemIsDark() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto scheme = QApplication::styleHints()->colorScheme();
    return scheme != Qt::ColorScheme::Light;
#else
    // Qt < 6.5: 默认暗色
    return true;
#endif
}

void ThemeManager::resolveEffectiveTheme()
{
    switch (m_theme) {
    case Theme::Dark:   m_effectiveDark = true; break;
    case Theme::Light:  m_effectiveDark = false; break;
    case Theme::System: m_effectiveDark = systemIsDark(); break;
    }
}

// ─── 强调色 ────────────────────────────────────────────────

struct ThemeManager::AccentPalette ThemeManager_Palettes[] = {
    { "#6366f1", "#818cf8", "#4f46e5", "#6366f11f" }, // Indigo
    { "#3b82f6", "#60a5fa", "#2563eb", "#3b82f61f" }, // Blue
    { "#8b5cf6", "#a78bfa", "#7c3aed", "#8b5cf61f" }, // Violet
    { "#f43f5e", "#fb7185", "#e11d48", "#f43f5e1f" }, // Rose
    { "#10b981", "#34d399", "#059669", "#10b9811f" }, // Emerald
    { "#f59e0b", "#fbbf24", "#d97706", "#f59e0b1f" }, // Amber
};

const ThemeManager::AccentPalette& ThemeManager::accentPalette(AccentColor c)
{
    return ThemeManager_Palettes[static_cast<int>(c)];
}

void ThemeManager::setAccentColor(AccentColor color)
{
    if (m_accent == color) return;
    m_accent = color;
    applyToApplication();
    emit accentColorChanged();
}

// ─── Token 访问 ────────────────────────────────────────────

#define DARK_OR_LIGHT(darkVal, lightVal) (m_effectiveDark ? (darkVal) : (lightVal))

const char* ThemeManager::base() const           { return DARK_OR_LIGHT(Dark::Base, Light::Base); }
const char* ThemeManager::card() const           { return DARK_OR_LIGHT(Dark::Card, Light::Card); }
const char* ThemeManager::surface() const        { return DARK_OR_LIGHT(Dark::Surface, Light::Surface); }
const char* ThemeManager::border() const         { return DARK_OR_LIGHT(Dark::Border, Light::Border); }
const char* ThemeManager::borderSoft() const     { return DARK_OR_LIGHT(Dark::BorderSoft, Light::BorderSoft); }
const char* ThemeManager::textPrimary() const    { return DARK_OR_LIGHT(Dark::TextPrimary, Light::TextPrimary); }
const char* ThemeManager::textSecondary() const  { return DARK_OR_LIGHT(Dark::TextSecondary, Light::TextSecondary); }
const char* ThemeManager::textTertiary() const   { return DARK_OR_LIGHT(Dark::TextTertiary, Light::TextTertiary); }
const char* ThemeManager::textDisabled() const   { return DARK_OR_LIGHT(Dark::TextDisabled, Light::TextDisabled); }
const char* ThemeManager::navBackground() const  { return DARK_OR_LIGHT(Dark::NavBackground, Light::NavBackground); }
const char* ThemeManager::navHover() const       { return DARK_OR_LIGHT(Dark::NavHover, Light::NavHover); }
const char* ThemeManager::navActive() const      { return DARK_OR_LIGHT(Dark::NavActive, Light::NavActive); }
const char* ThemeManager::inputBg() const        { return DARK_OR_LIGHT(Dark::InputBg, Light::InputBg); }
const char* ThemeManager::inputBorder() const    { return DARK_OR_LIGHT(Dark::InputBorder, Light::InputBorder); }
const char* ThemeManager::inputFocusBorder() const { return DARK_OR_LIGHT(Dark::InputFocusBorder, Light::InputFocusBorder); }
const char* ThemeManager::scrollThumb() const    { return DARK_OR_LIGHT(Dark::ScrollThumb, Light::ScrollThumb); }
const char* ThemeManager::scrollTrack() const    { return DARK_OR_LIGHT(Dark::ScrollTrack, Light::ScrollTrack); }

#undef DARK_OR_LIGHT

const char* ThemeManager::accentPrimary() const { return accentPalette(m_accent).primary; }
const char* ThemeManager::accentHover() const   { return accentPalette(m_accent).hover; }
const char* ThemeManager::accentActive() const  { return accentPalette(m_accent).active; }

// ─── QSS 编译 ──────────────────────────────────────────────

QString ThemeManager::compileQss(const QString& templateQss) const
{
    QString out = templateQss;

    // 主题色替换
    out.replace(QLatin1String("{{base}}"),           QLatin1String(base()));
    out.replace(QLatin1String("{{card}}"),           QLatin1String(card()));
    out.replace(QLatin1String("{{surface}}"),        QLatin1String(surface()));
    out.replace(QLatin1String("{{border}}"),         QLatin1String(border()));
    out.replace(QLatin1String("{{border-soft}}"),    QLatin1String(borderSoft()));
    out.replace(QLatin1String("{{text-primary}}"),   QLatin1String(textPrimary()));
    out.replace(QLatin1String("{{text-secondary}}"), QLatin1String(textSecondary()));
    out.replace(QLatin1String("{{text-tertiary}}"),  QLatin1String(textTertiary()));
    out.replace(QLatin1String("{{text-disabled}}"),  QLatin1String(textDisabled()));
    out.replace(QLatin1String("{{nav-bg}}"),         QLatin1String(navBackground()));
    out.replace(QLatin1String("{{nav-hover}}"),      QLatin1String(navHover()));
    out.replace(QLatin1String("{{nav-active}}"),     QLatin1String(navActive()));
    out.replace(QLatin1String("{{input-bg}}"),       QLatin1String(inputBg()));
    out.replace(QLatin1String("{{input-border}}"),   QLatin1String(inputBorder()));
    out.replace(QLatin1String("{{input-focus-border}}"), QLatin1String(inputFocusBorder()));
    out.replace(QLatin1String("{{scroll-thumb}}"),   QLatin1String(scrollThumb()));
    out.replace(QLatin1String("{{scroll-track}}"),   QLatin1String(scrollTrack()));

    // 强调色替换
    const auto& pal = accentPalette(m_accent);
    out.replace(QLatin1String("{{accent}}"),         QLatin1String(pal.primary));
    out.replace(QLatin1String("{{accent-hover}}"),   QLatin1String(pal.hover));
    out.replace(QLatin1String("{{accent-active}}"),  QLatin1String(pal.active));
    out.replace(QLatin1String("{{accent-subtle}}"),  QLatin1String(pal.subtle));

    // 语义色替换
    out.replace(QLatin1String("{{success}}"),        QLatin1String(Accent::Success));
    out.replace(QLatin1String("{{warning}}"),        QLatin1String(Accent::Warning));
    out.replace(QLatin1String("{{error}}"),          QLatin1String(Accent::Error));
    out.replace(QLatin1String("{{info}}"),           QLatin1String(Accent::Info));

    return out;
}

QString ThemeManager::loadAndCompileQss(const QString& resourcePath) const
{
    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return compileQss(QString::fromUtf8(f.readAll()));
}

QString ThemeManager::compiledGlobalStyleSheet() const
{
    return loadAndCompileQss(QStringLiteral(":/theme/fluent.qss"));
}

void ThemeManager::applyToApplication()
{
    if (!qApp) return;

    // 基础 Palette
    QPalette pal;
    pal.setColor(QPalette::Window, baseColor());
    pal.setColor(QPalette::WindowText, textPrimaryColor());
    pal.setColor(QPalette::Base, cardColor());
    pal.setColor(QPalette::AlternateBase, surfaceColor());
    pal.setColor(QPalette::Text, textPrimaryColor());
    pal.setColor(QPalette::Button, surfaceColor());
    pal.setColor(QPalette::ButtonText, textPrimaryColor());
    pal.setColor(QPalette::Highlight, accentColor());
    pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    pal.setColor(QPalette::PlaceholderText, QColor(textTertiary()));
    qApp->setPalette(pal);

    // 编译并应用全局 QSS
    QString qss = compiledGlobalStyleSheet();
    if (!qss.isEmpty()) {
        qApp->setStyleSheet(qss);
    }
}

} // namespace Fluent
