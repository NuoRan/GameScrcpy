#ifndef TERMINALPAGE_H
#define TERMINALPAGE_H

#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QEvent>

namespace Fluent { class FluentButton; class FluentCard; }

/**
 * @brief 终端页 — 替代旧 TerminalDialog
 *
 * 内嵌 ADB 命令执行环境与输出显示。
 */
class TerminalPage : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalPage(QWidget *parent = nullptr);

    void appendOutput(const QString &text);
    void clearOutput();

signals:
    void executeCommand(const QString &cmd);
    void stopCommand();

protected:
    void changeEvent(QEvent *event) override;

private:
    void setupUI();
    void retranslateUi();

    QLabel     *m_titleLabel  = nullptr;
    QLabel     *m_outputLabel = nullptr;
    QLineEdit  *m_commandEdit = nullptr;
    QTextEdit  *m_outputEdit  = nullptr;
    Fluent::FluentButton *m_executeBtn = nullptr;
    Fluent::FluentButton *m_stopBtn    = nullptr;
    QPushButton *m_clearBtn   = nullptr;
};

#endif // TERMINALPAGE_H
