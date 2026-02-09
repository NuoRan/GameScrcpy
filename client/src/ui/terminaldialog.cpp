#include "terminaldialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

TerminalDialog::TerminalDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    applyStyle();
    retranslateUi();
}

void TerminalDialog::setupUI()
{
    setMinimumSize(560, 380);
    resize(600, 420);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(28, 24, 28, 24);

    // 标题
    m_titleLabel = new QLabel();
    m_titleLabel->setObjectName("dialogTitle");

    // 命令输入行
    QHBoxLayout *cmdLayout = new QHBoxLayout();
    cmdLayout->setSpacing(12);

    QLabel *promptLabel = new QLabel("$");
    promptLabel->setObjectName("promptLabel");
    promptLabel->setFixedWidth(24);
    promptLabel->setAlignment(Qt::AlignCenter);

    m_commandEdit = new QLineEdit();
    m_commandEdit->setMinimumHeight(44);
    m_commandEdit->setText("devices");

    m_executeBtn = new QPushButton();
    m_executeBtn->setObjectName("primaryBtn");
    m_executeBtn->setMinimumSize(80, 44);

    m_stopBtn = new QPushButton();
    m_stopBtn->setMinimumSize(80, 44);

    m_clearBtn = new QPushButton();
    m_clearBtn->setMinimumSize(80, 44);

    cmdLayout->addWidget(promptLabel);
    cmdLayout->addWidget(m_commandEdit, 1);
    cmdLayout->addWidget(m_executeBtn);
    cmdLayout->addWidget(m_stopBtn);
    cmdLayout->addWidget(m_clearBtn);

    // 输出区标签
    m_outputLabel = new QLabel();
    m_outputLabel->setObjectName("sectionLabel");

    // 输出区
    m_outputEdit = new QTextEdit();
    m_outputEdit->setReadOnly(true);

    // 组装
    mainLayout->addWidget(m_titleLabel);
    mainLayout->addSpacing(8);
    mainLayout->addLayout(cmdLayout);
    mainLayout->addSpacing(8);
    mainLayout->addWidget(m_outputLabel);
    mainLayout->addWidget(m_outputEdit, 1);

    // 信号连接
    connect(m_executeBtn, &QPushButton::clicked, this, [this]() {
        emit executeCommand(m_commandEdit->text().trimmed());
    });
    connect(m_commandEdit, &QLineEdit::returnPressed, this, [this]() {
        emit executeCommand(m_commandEdit->text().trimmed());
    });
    connect(m_stopBtn, &QPushButton::clicked, this, &TerminalDialog::stopCommand);
    connect(m_clearBtn, &QPushButton::clicked, this, &TerminalDialog::clearOutput);
}

void TerminalDialog::applyStyle()
{
    setStyleSheet(R"(
        QDialog {
            background-color: #0f0f12;
        }
        QLabel {
            color: #71717a;
            font-size: 13px;
            background: transparent;
        }
        QLabel#dialogTitle {
            color: #fafafa;
            font-size: 18px;
            font-weight: 600;
        }
        QLabel#promptLabel {
            color: #22c55e;
            font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
            font-size: 18px;
            font-weight: 700;
        }
        QLabel#sectionLabel {
            color: #52525b;
            font-size: 12px;
            margin-top: 4px;
        }
        QLineEdit {
            background-color: #18181b;
            border: 1px solid #27272a;
            border-radius: 10px;
            padding: 0 16px;
            color: #fafafa;
            font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
            font-size: 14px;
        }
        QLineEdit:focus {
            border-color: #22c55e;
            background-color: #1c1c1f;
        }
        QTextEdit {
            background-color: #09090b;
            border: 1px solid #27272a;
            border-radius: 10px;
            padding: 12px;
            color: #a1a1aa;
            font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
            font-size: 12px;
            selection-background-color: #22c55e;
        }
        QPushButton {
            background-color: #27272a;
            border: 1px solid #3f3f46;
            border-radius: 8px;
            padding: 0 16px;
            color: #fafafa;
            font-size: 13px;
            font-weight: 500;
        }
        QPushButton:hover {
            background-color: #3f3f46;
        }
        QPushButton#primaryBtn {
            background-color: #22c55e;
            border: none;
            color: white;
            font-weight: 600;
        }
        QPushButton#primaryBtn:hover {
            background-color: #16a34a;
        }
        QScrollBar:vertical {
            background-color: transparent;
            width: 8px;
        }
        QScrollBar::handle:vertical {
            background-color: #3f3f46;
            border-radius: 4px;
            min-height: 30px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
    )");
}

QString TerminalDialog::getCommand() const
{
    return m_commandEdit->text().trimmed();
}

void TerminalDialog::appendOutput(const QString &text)
{
    m_outputEdit->append(text);
}

void TerminalDialog::clearOutput()
{
    m_outputEdit->clear();
}

void TerminalDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QDialog::changeEvent(event);
}

void TerminalDialog::retranslateUi()
{
    setWindowTitle(tr("终端调试"));
    m_titleLabel->setText(tr("ADB 终端"));
    m_commandEdit->setPlaceholderText(tr("输入 ADB 命令，如: devices, shell ls"));
    m_executeBtn->setText(tr("执行"));
    m_stopBtn->setText(tr("终止"));
    m_clearBtn->setText(tr("清空"));
    m_outputLabel->setText(tr("输出"));
    m_outputEdit->setPlaceholderText(tr("命令输出将显示在这里..."));
}
