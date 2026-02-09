/**
 * @file main.cpp
 * @brief GameScrcpy 应用程序入口 / GameScrcpy Application Entry Point
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 *
 * 主要功能 / Main Features:
 * - 初始化 Qt 应用程序 / Initialize Qt application
 * - 加载样式表和翻译文件 / Load stylesheets and translation files
 * - 初始化鼠标钩子、性能监控等组件 / Initialize mouse hooks, performance monitor, etc.
 * - 创建并显示主对话框 / Create and show main dialog
 */

#include <QApplication>
#include <QDebug>
#include <QFile>
#ifdef Q_OS_LINUX
#include <QFileInfo>
#include <QIcon>
#endif
#include <QSurfaceFormat>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTranslator>
#include <QDateTime>
#include <QTimer>
#include <QThread>

#include "config.h"
#include "dialog.h"
#include "mousetap.h"
#include "ConfigCenter.h"
#include "PerformanceMonitor.h"

static Dialog *g_mainDlg = Q_NULLPTR;
static QtMessageHandler g_oldMessageHandler = Q_NULLPTR;
void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg);
void installTranslator(const QString &langOverride = QString());

static QtMsgType g_msgType = QtInfoMsg;
QtMsgType covertLogLevel(const QString &logLevel);

// Windows 异常处理器 / Windows unhandled exception handler
#ifdef Q_OS_WIN32
#include <windows.h>
LONG WINAPI MyUnhandledExceptionFilter(EXCEPTION_POINTERS *ExceptionInfo)
{
    qCritical() << "[CRASH] Unhandled exception! Code:" << Qt::hex << ExceptionInfo->ExceptionRecord->ExceptionCode;
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN32
    // 安装异常处理器
    SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);
#endif

    // ---------------------------------------------------------
    // 设置环境变量
    // 在Windows下指定ADB、Server、Keymap和配置文件的路径
    // ---------------------------------------------------------
#ifdef Q_OS_WIN32
    qputenv("KZSCRCPY_ADB_PATH", "../env/adb/win/adb.exe");
    qputenv("KZSCRCPY_SERVER_PATH", "../env/scrcpy-server");
    qputenv("KZSCRCPY_KEYMAP_PATH", "../../../keymap");
    qputenv("KZSCRCPY_CONFIG_PATH", "../../../config");
#endif

    g_msgType = covertLogLevel(Config::getInstance().getLogLevel());

    // 设置默认Surface格式
    QSurfaceFormat varFormat = QSurfaceFormat::defaultFormat();
    varFormat.setVersion(2, 0);
    varFormat.setProfile(QSurfaceFormat::NoProfile);
    varFormat.setSwapInterval(1);  // 启用 VSync，保证帧节奏与显示器刷新同步，消除画面抖动
    QSurfaceFormat::setDefaultFormat(varFormat);

    // 安装自定义消息处理器（日志系统）
    g_oldMessageHandler = qInstallMessageHandler(myMessageOutput);
    QApplication a(argc, argv);

    // 【重要】预初始化性能监控器，确保在 GUI 线程中创建
    // 必须在 QApplication 之后、其他组件之前调用
    qsc::PerformanceMonitor::instance();

    // 规范化版本号格式
    QStringList versionList = QCoreApplication::applicationVersion().split(".");
    if (versionList.size() >= 3) {
        QString version = versionList[0] + "." + versionList[1] + "." + versionList[2];
        a.setApplicationVersion(version);
    }

    // 安装翻译文件
    installTranslator();  // 首次调用读取配置

    // 初始化鼠标钩子（用于全局事件捕获）
#if defined(Q_OS_WIN32) || defined(Q_OS_OSX)
    MouseTap::getInstance()->initMouseEventTap();
#endif

    // ---------------------------------------------------------
    // 加载样式表
    // ---------------------------------------------------------
    QFile file(":/qss/psblack.css");
    if (file.open(QFile::ReadOnly)) {
        QString qss = QLatin1String(file.readAll());
        QString paletteColor = qss.mid(20, 7);
        qApp->setPalette(QPalette(QColor(paletteColor)));
        qApp->setStyleSheet(qss);
        file.close();
    }

    qsc::AdbProcess::setAdbPath(Config::getInstance().getAdbPath());

    // 初始化配置中心（使用默认路径）
    qsc::ConfigCenter::instance().initialize();

    // 创建并显示主对话框
    g_mainDlg = new Dialog {};
    g_mainDlg->show();

    int ret = a.exec();
    delete g_mainDlg;

#if defined(Q_OS_WIN32) || defined(Q_OS_OSX)
    MouseTap::getInstance()->quitMouseEventTap();
#endif
    return ret;
}

// ---------------------------------------------------------
// 安装/切换翻译器（支持运行时切换）
// Install/switch translator (supports runtime switching)
// 传入语言代码（如 "zh_CN" "en_US"），空串则读取配置
// ---------------------------------------------------------
void installTranslator(const QString &langOverride)
{
    static QTranslator *translator = nullptr;

    // 先移除旧的翻译器
    if (translator) {
        qApp->removeTranslator(translator);
        delete translator;
        translator = nullptr;
    }

    // 确定目标语言
    QString langCode = langOverride.isEmpty() ? Config::getInstance().getLanguage() : langOverride;

    QLocale locale;
    QLocale::Language language = locale.language();

    if (langCode == "zh_CN") {
        language = QLocale::Chinese;
    } else if (langCode == "en_US") {
        language = QLocale::English;
    } else if (langCode == "ja_JP") {
        language = QLocale::Japanese;
    }

    QString languagePath = ":/i18n/";
    switch (language) {
    case QLocale::Chinese:
        languagePath += "zh_CN.qm";
        break;
    case QLocale::Japanese:
        languagePath += "ja_JP.qm";
        break;
    case QLocale::English:
    default:
        languagePath += "en_US.qm";
        break;
    }

    translator = new QTranslator();
    auto loaded = translator->load(languagePath);
    if (!loaded) {
        qWarning() << "Failed to load translation file:" << languagePath;
    }
    qApp->installTranslator(translator);
}

// 日志级别转换
QtMsgType covertLogLevel(const QString &logLevel)
{
    if ("debug" == logLevel) return QtDebugMsg;
    if ("info" == logLevel) return QtInfoMsg;
    if ("warn" == logLevel) return QtWarningMsg;
    if ("error" == logLevel) return QtCriticalMsg;
#ifdef QT_NO_DEBUG
    return QtInfoMsg;
#else
    return QtDebugMsg;
#endif
}

// ---------------------------------------------------------
// 自定义消息输出处理
// 格式化日志并输出到控制台及UI界面
// ---------------------------------------------------------
void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString outputMsg;

    outputMsg = msg;
    if (g_oldMessageHandler) {
        g_oldMessageHandler(type, context, outputMsg);
    }

    // 过滤并显示日志到主窗口
    float fLogLevel = g_msgType;
    if (QtInfoMsg == g_msgType) fLogLevel = QtDebugMsg + 0.5f;
    float fLogLevel2 = type;
    if (QtInfoMsg == type) fLogLevel2 = QtDebugMsg + 0.5f;

    if (fLogLevel <= fLogLevel2) {
        if (g_mainDlg && g_mainDlg->isVisible() && !g_mainDlg->filterLog(outputMsg)) {
            g_mainDlg->outLog(outputMsg);
        }
    }

    if (QtFatalMsg == type) {
        //abort();
    }
}
