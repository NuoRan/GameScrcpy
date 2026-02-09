#ifndef TERMINALDIALOG_H
#define TERMINALDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QEvent>

/**
 * @brief 终端调试对话框 / Terminal Debug Dialog
 *
 * 提供命令输入和输出显示，用于调试 adb 命令。
 * Provides command input and output display for debugging adb commands.
 */
class TerminalDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TerminalDialog(QWidget *parent = nullptr);

    QString getCommand() const;
    void appendOutput(const QString &text);
    void clearOutput();

signals:
    void executeCommand(const QString &cmd);
    void stopCommand();

private:
    void setupUI();
    void applyStyle();
    void retranslateUi();

protected:
    void changeEvent(QEvent *event) override;

private:
    QLabel *m_titleLabel;
    QLabel *m_outputLabel;
    QLineEdit *m_commandEdit;
    QTextEdit *m_outputEdit;
    QPushButton *m_executeBtn;
    QPushButton *m_stopBtn;
    QPushButton *m_clearBtn;
};

#endif // TERMINALDIALOG_H
