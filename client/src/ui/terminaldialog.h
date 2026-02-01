#ifndef TERMINALDIALOG_H
#define TERMINALDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>

// 终端调试对话框
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

private:
    QLineEdit *m_commandEdit;
    QTextEdit *m_outputEdit;
    QPushButton *m_executeBtn;
    QPushButton *m_stopBtn;
    QPushButton *m_clearBtn;
};

#endif // TERMINALDIALOG_H
