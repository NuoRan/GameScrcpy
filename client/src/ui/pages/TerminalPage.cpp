#include "TerminalPage.h"
#include "FluentCard.h"
#include "FluentButton.h"
#include "ThemeManager.h"
#include "DesignTokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>

using namespace Fluent;

TerminalPage::TerminalPage(QWidget *parent) : QWidget(parent)
{
    setupUI();
    retranslateUi();
}

void TerminalPage::setupUI()
{
    auto &tm = ThemeManager::instance();

    auto *main = new QVBoxLayout(this);
    main->setContentsMargins(32, 28, 32, 20);
    main->setSpacing(16);

    // 标题
    m_titleLabel = new QLabel;
    m_titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: 700; color: %1; background: transparent;")
        .arg(tm.textPrimary()));
    main->addWidget(m_titleLabel);

    // 输入行
    auto *inputCard = new FluentCard;
    auto *inputLayout = new QHBoxLayout(inputCard);
    inputLayout->setContentsMargins(16, 12, 16, 12);
    inputLayout->setSpacing(8);

    auto *prefix = new QLabel("adb");
    prefix->setStyleSheet(QStringLiteral(
        "font-family: 'Consolas','Courier New',monospace; font-size: 13px; color: %1; background: transparent;")
        .arg(tm.textSecondary()));

    m_commandEdit = new QLineEdit;
    m_commandEdit->setMinimumHeight(36);
    m_commandEdit->setPlaceholderText("shell ls /sdcard");
    m_commandEdit->setStyleSheet(QStringLiteral(
        "font-family: 'Consolas','Courier New',monospace; font-size: 13px;"));

    m_executeBtn = new FluentButton(QString(), FluentButton::Primary);
    m_executeBtn->setMinimumSize(60, 36);

    m_stopBtn = new FluentButton(QString(), FluentButton::Danger);
    m_stopBtn->setMinimumSize(60, 36);

    inputLayout->addWidget(prefix);
    inputLayout->addWidget(m_commandEdit, 1);
    inputLayout->addWidget(m_executeBtn);
    inputLayout->addWidget(m_stopBtn);
    main->addWidget(inputCard);

    // 输出区
    auto *outputCard = new FluentCard;
    outputCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto *outLayout = new QVBoxLayout(outputCard);
    outLayout->setContentsMargins(16, 12, 16, 12);
    outLayout->setSpacing(8);

    auto *outHeader = new QHBoxLayout;
    m_outputLabel = new QLabel;
    m_outputLabel->setStyleSheet(QStringLiteral(
        "font-size: 13px; font-weight: 600; color: %1; background: transparent;")
        .arg(tm.textPrimary()));

    m_clearBtn = new QPushButton;
    m_clearBtn->setMinimumHeight(28);

    outHeader->addWidget(m_outputLabel);
    outHeader->addStretch();
    outHeader->addWidget(m_clearBtn);
    outLayout->addLayout(outHeader);

    m_outputEdit = new QTextEdit;
    m_outputEdit->setReadOnly(true);
    m_outputEdit->setStyleSheet(QStringLiteral(
        "QTextEdit { background: transparent; border: none; "
        "font-family: 'Consolas','Courier New',monospace; font-size: 12px; color: %1; }")
        .arg(tm.textSecondary()));
    outLayout->addWidget(m_outputEdit);

    main->addWidget(outputCard, 1);

    // 信号
    connect(m_executeBtn, &QPushButton::clicked, this, [this]() {
        const QString cmd = m_commandEdit->text().trimmed();
        if (!cmd.isEmpty()) {
            emit executeCommand(cmd);
        }
    });
    connect(m_commandEdit, &QLineEdit::returnPressed, this, [this]() {
        m_executeBtn->click();
    });
    connect(m_stopBtn, &QPushButton::clicked, this, &TerminalPage::stopCommand);
    connect(m_clearBtn, &QPushButton::clicked, this, &TerminalPage::clearOutput);
}

void TerminalPage::appendOutput(const QString &text)
{
    m_outputEdit->append(text);
    // 自动滚到底部
    auto *bar = m_outputEdit->verticalScrollBar();
    if (bar) bar->setValue(bar->maximum());
}

void TerminalPage::clearOutput()
{
    m_outputEdit->clear();
}

void TerminalPage::retranslateUi()
{
    m_titleLabel->setText(tr("ADB 终端"));
    m_outputLabel->setText(tr("输出"));
    m_executeBtn->setText(tr("执行"));
    m_stopBtn->setText(tr("停止"));
    m_clearBtn->setText(tr("清空"));
    m_commandEdit->setPlaceholderText("shell ls /sdcard");
}

void TerminalPage::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}
