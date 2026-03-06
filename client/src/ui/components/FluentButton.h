/**
 * @file FluentButton.h
 * @brief Fluent Focus 按钮 — Primary / Secondary / Ghost / Danger 四种变体
 */

#ifndef FLUENTBUTTON_H
#define FLUENTBUTTON_H

#include <QPushButton>

namespace Fluent {

class FluentButton : public QPushButton
{
    Q_OBJECT

public:
    enum Variant { Secondary, Primary, Ghost, Danger };
    Q_ENUM(Variant)

    explicit FluentButton(QWidget* parent = nullptr);
    FluentButton(const QString& text, QWidget* parent = nullptr);
    FluentButton(const QString& text, Variant variant, QWidget* parent = nullptr);

    void setVariant(Variant v);
    Variant variant() const { return m_variant; }

    /// 设置图标 + 文字 (fontawesome unicode 或 SVG path)
    void setIconText(const QString& iconChar, const QString& text);

private:
    void applyVariantStyle();
    void connectTheme();

    Variant m_variant = Secondary;
};

} // namespace Fluent

#endif // FLUENTBUTTON_H
