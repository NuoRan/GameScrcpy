/**
 * @file HelpDialog.h
 * @brief 综合帮助中心 — 全面细致的使用文档
 */
#ifndef HELPDIALOG_H
#define HELPDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QLabel>
#include <QScrollArea>
#include <QPushButton>
#include <QSplitter>
#include <QPixmap>
#include <QPainter>

#ifdef Q_OS_WIN
#include "winutils.h"
#endif

#include "ThemeManager.h"
#include "DesignTokens.h"

class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr("帮助中心"));
        setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
        resize(900, 640);
        setMinimumSize(720, 480);

#ifdef Q_OS_WIN
        WinUtils::setDarkBorderToWindow(reinterpret_cast<HWND>(winId()),
            Fluent::ThemeManager::instance().isDarkMode());
#endif

        auto& tm = Fluent::ThemeManager::instance();

        setStyleSheet(QString(
            "HelpDialog { background-color: %1; }"
            "QLabel { background: transparent; }"
            "QScrollBar:vertical { width: 5px; background: transparent; }"
            "QScrollBar::handle:vertical { background: %2; border-radius: 2px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        ).arg(tm.base(), tm.scrollThumb()));

        QHBoxLayout* mainLayout = new QHBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        // 左侧导航
        QWidget* sidebar = new QWidget(this);
        sidebar->setFixedWidth(220);
        sidebar->setStyleSheet(QString("background-color: %1; border-right: 1px solid %2;")
            .arg(tm.card(), tm.border()));

        QVBoxLayout* sideLayout = new QVBoxLayout(sidebar);
        sideLayout->setContentsMargins(10, 14, 10, 14);
        sideLayout->setSpacing(4);

        QLabel* sideTitle = new QLabel(tr("帮助中心"), this);
        sideTitle->setStyleSheet(QString(
            "font-family: 'Microsoft YaHei UI', 'Segoe UI', sans-serif;"
            "font-size: 15px; font-weight: 700; color: %1; padding: 8px 8px 14px 8px;")
            .arg(tm.textPrimary()));
        sideLayout->addWidget(sideTitle);

        m_navList = new QListWidget(this);
        m_navList->setStyleSheet(QString(
            "QListWidget { background: transparent; border: none; outline: none; }"
            "QListWidget::item { padding: 9px 14px; border-radius: 6px; color: %1;"
            "  font-family: 'Microsoft YaHei UI', 'Segoe UI', sans-serif; font-size: 13px; }"
            "QListWidget::item:hover { background: %2; }"
            "QListWidget::item:selected { background: %3; color: #ffffff; }")
            .arg(tm.textSecondary(), tm.border(), tm.accentPrimary()));
        sideLayout->addWidget(m_navList, 1);
        mainLayout->addWidget(sidebar);

        // 右侧内容区
        m_contentStack = new QStackedWidget(this);
        m_contentStack->setStyleSheet(QString("background-color: %1;").arg(tm.base()));
        mainLayout->addWidget(m_contentStack, 1);

        // 构建页面
        addSection(tr("快速入门"),       buildQuickStart());
        addSection(tr("连接设备"),       buildConnection());
        addSection(tr("投屏窗口"),       buildVideoWindow());
        addSection(tr("键鼠映射"),       buildKeyMap());
        addSection(tr("脚本编辑器"),     buildScriptEditor());
        addSection(tr("mapi API"),       buildApi());
        addSection(tr("图像识别"),       buildImageMatch());
        addSection(tr("自定义选区"),     buildSelection());
        addSection(tr("Companion App"), buildCompanionApp());
        addSection(tr("触控后端"),       buildTouchBackend());
        addSection(tr("设置参数"),       buildSettings());
        addSection(tr("终端 / ADB"),     buildTerminal());
        addSection(tr("快捷键"),         buildShortcuts());
        addSection(tr("常见问题"),       buildFAQ());

        m_navList->setCurrentRow(0);
        connect(m_navList, &QListWidget::currentRowChanged,
                m_contentStack, &QStackedWidget::setCurrentIndex);
    }

private:
    QListWidget* m_navList = nullptr;
    QStackedWidget* m_contentStack = nullptr;

    void addSection(const QString& t, QWidget* p) {
        m_navList->addItem(t);
        m_contentStack->addWidget(p);
    }

    QWidget* page(const QString& html) {
        auto& tm = Fluent::ThemeManager::instance();
        QScrollArea* s = new QScrollArea;
        s->setWidgetResizable(true);
        s->setFrameShape(QFrame::NoFrame);
        s->setStyleSheet("QScrollArea{background:transparent;border:none;}");

        QTextBrowser* b = new QTextBrowser;
        b->setOpenExternalLinks(true);
        b->setReadOnly(true);
        QFont docFont(QStringLiteral("Microsoft YaHei UI"), 10);
        docFont.setStyleStrategy(QFont::PreferAntialias);
        b->setFont(docFont);
        b->setStyleSheet(QString(
            "QTextBrowser{background:%3;color:%1;border:none;"
            "padding:28px 36px;selection-background-color:%2;}"
            "QTextBrowser code{font-family:'Cascadia Code','Consolas',monospace;}"
            "QTextBrowser pre{font-family:'Cascadia Code','Consolas',monospace;}")
            .arg(tm.textPrimary(), tm.accentPrimary(), tm.card()));

        QString css = QString(
            "<style>"
            "body{color:%1;font-family:'Microsoft YaHei UI','\\u5FAE\\u8F6F\\u96C5\\u9ED1','Segoe UI',sans-serif;font-size:13px;line-height:1.7;}"
            "h1{color:%1;font-size:20px;font-weight:700;margin-bottom:14px;border-bottom:2px solid %2;padding-bottom:10px;}"
            "h2{color:%1;font-size:16px;font-weight:600;margin-top:24px;margin-bottom:10px;border-bottom:1px solid %3;padding-bottom:6px;}"
            "h3{color:%4;font-size:14px;font-weight:600;margin-top:18px;margin-bottom:6px;}"
            "h4{color:%4;font-size:13px;font-weight:600;margin-top:14px;margin-bottom:4px;}"
            "code{background:%3;color:%5;padding:2px 6px;border-radius:3px;font-family:'Cascadia Code','Consolas',monospace;font-size:12px;}"
            "pre{background:#111113;color:%1;padding:14px 16px;border-radius:8px;border:1px solid %2;"
            "font-family:'Cascadia Code','Consolas',monospace;font-size:12px;line-height:1.6;margin:10px 0;white-space:pre-wrap;}"
            "table{border-collapse:collapse;width:100%%;margin:10px 0;}"
            "th{background:%3;color:%1;padding:10px 14px;text-align:left;border:1px solid %2;font-weight:600;}"
            "td{padding:8px 14px;border:1px solid %2;vertical-align:top;}"
            "ul,ol{margin-left:18px;}"
            "li{margin-bottom:5px;}"
            ".tip{background:rgba(%6,%7,%8,0.08);border-left:3px solid %5;padding:10px 14px;border-radius:0 6px 6px 0;margin:12px 0;}"
            ".warn{background:rgba(234,179,8,0.08);border-left:3px solid #eab308;padding:10px 14px;border-radius:0 6px 6px 0;margin:12px 0;}"
            ".card{background:%3;border:1px solid %2;border-radius:8px;padding:14px 18px;margin:10px 0;}"
            "hr{border:none;border-top:1px solid %2;margin:20px 0;}"
            "</style>"
        ).arg(tm.textPrimary(), tm.border(), tm.card(), tm.textSecondary(), tm.accentPrimary(),
              QString::number(QColor(tm.accentPrimary()).red()),
              QString::number(QColor(tm.accentPrimary()).green()),
              QString::number(QColor(tm.accentPrimary()).blue()));

        b->setHtml(css + html);
        s->setWidget(b);
        return s;
    }

    // === 各帮助页面 build 方法声明 ===
    QWidget* buildQuickStart();
    QWidget* buildConnection();
    QWidget* buildVideoWindow();
    QWidget* buildKeyMap();
    QWidget* buildScriptEditor();
    QWidget* buildApi();
    QWidget* buildImageMatch();
    QWidget* buildSelection();
    QWidget* buildCompanionApp();
    QWidget* buildTouchBackend();
    QWidget* buildSettings();
    QWidget* buildTerminal();
    QWidget* buildShortcuts();
    QWidget* buildFAQ();
};

#endif // HELPDIALOG_H
