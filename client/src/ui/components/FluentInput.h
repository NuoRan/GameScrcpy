/**
 * @file FluentInput.h
 * @brief Fluent Focus 输入框 — 底部焦点指示器、前后缀图标、清除按钮、错误状态
 */
#ifndef FLUENTINPUT_H
#define FLUENTINPUT_H

#include <QLineEdit>
#include <QPropertyAnimation>

class QLabel;
class QToolButton;

namespace Fluent {

class FluentInput : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal focusProgress READ focusProgress WRITE setFocusProgress)
public:
    explicit FluentInput(QWidget* parent = nullptr);

    // 文字
    void setText(const QString& text);
    QString text() const;
    void setPlaceholderText(const QString& text);

    // 装饰
    void setPrefixIcon(const QString& icon);   // Unicode emoji/char
    void setSuffixIcon(const QString& icon);
    void setClearButtonEnabled(bool enabled);

    // 验证
    void setError(const QString& message);
    void clearError();
    bool hasError() const { return m_hasError; }

    // 属性
    void setReadOnly(bool ro);
    QLineEdit* lineEdit() const { return m_edit; }

    QSize sizeHint() const override { return {220, 56}; }

signals:
    void textChanged(const QString& text);
    void editingFinished();
    void returnPressed();

protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    qreal focusProgress() const { return m_focusProgress; }
    void  setFocusProgress(qreal v) { m_focusProgress = v; update(); }
    void  updateLayout();

    QLineEdit*    m_edit = nullptr;
    QLabel*       m_prefixLabel = nullptr;
    QLabel*       m_suffixLabel = nullptr;
    QToolButton*  m_clearBtn = nullptr;
    QLabel*       m_errorLabel = nullptr;

    bool  m_hasError = false;
    bool  m_clearEnabled = false;
    qreal m_focusProgress = 0.0;

    QPropertyAnimation* m_focusAnim = nullptr;
};

} // namespace Fluent

#endif // FLUENTINPUT_H
