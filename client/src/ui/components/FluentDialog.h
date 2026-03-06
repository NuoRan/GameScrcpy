/**
 * @file FluentDialog.h
 * @brief Fluent Focus 对话框 — 替代 QMessageBox / QInputDialog
 *
 * 特性:
 * - 半透明遮罩 + 卡片式弹窗
 * - 进入/退出动画
 * - info / error / confirm / input 快捷静态方法
 */

#ifndef FLUENTDIALOG_H
#define FLUENTDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace Fluent {

class FluentDialog : public QDialog
{
    Q_OBJECT

public:
    enum DialogType { Info, Error, Warning, Confirm, Input };

    explicit FluentDialog(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setMessage(const QString& message);
    void setDialogType(DialogType type);

    /// 自定义内容区 (添加在 message 下方)
    void setContentWidget(QWidget* w);

    // 按钮
    void setPrimaryButtonText(const QString& text);
    void setSecondaryButtonText(const QString& text);  // 为空则不显示
    void setDangerMode(bool danger);  // 主按钮变红

    /// 获取输入框文本 (仅 Input 模式)
    QString inputText() const;
    void setInputPlaceholder(const QString& placeholder);
    void setInputText(const QString& text);

    // ─── 快捷静态方法 ─────────────────────────────────
    static void info(QWidget* parent, const QString& title, const QString& message);
    static void error(QWidget* parent, const QString& title, const QString& message);
    static void warning(QWidget* parent, const QString& title, const QString& message);
    static bool confirm(QWidget* parent, const QString& title, const QString& message);
    static QString input(QWidget* parent, const QString& title, const QString& placeholder, const QString& defaultText = {});

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupUI();
    void applyStyle();

    DialogType m_type = Info;
    QWidget* m_card = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_messageLabel = nullptr;
    QLineEdit* m_inputEdit = nullptr;
    QWidget* m_contentWidget = nullptr;
    QPushButton* m_primaryBtn = nullptr;
    QPushButton* m_secondaryBtn = nullptr;
    bool m_dangerMode = false;
};

} // namespace Fluent

#endif // FLUENTDIALOG_H
