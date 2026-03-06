/**
 * @file FluentComboBox.h
 * @brief Fluent Focus 下拉框 — elevation-2 阴影下拉、选中项高亮、展开动画
 */
#ifndef FLUENTCOMBOBOX_H
#define FLUENTCOMBOBOX_H

#include <QComboBox>
#include <QPropertyAnimation>

namespace Fluent {

class FluentComboBox : public QComboBox
{
    Q_OBJECT
    Q_PROPERTY(qreal dropOpacity READ dropOpacity WRITE setDropOpacity)
public:
    explicit FluentComboBox(QWidget* parent = nullptr);

    void setSearchable(bool enabled);
    bool isSearchable() const { return m_searchable; }

    QSize sizeHint() const override { return {180, 32}; }

protected:
    void showPopup() override;
    void hidePopup() override;
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    qreal dropOpacity() const { return m_dropOpacity; }
    void  setDropOpacity(qreal v);
    void  applyStyle();

    bool  m_searchable = false;
    qreal m_dropOpacity = 0.0;
    QPropertyAnimation* m_anim = nullptr;
};

} // namespace Fluent

#endif // FLUENTCOMBOBOX_H
