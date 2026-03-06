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
#include <QIcon>
#ifdef Q_OS_LINUX
#include <QFileInfo>
#endif
#include <QSurfaceFormat>
#include <QTranslator>
#include <QDateTime>
#include <QTimer>
#include <QThread>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QDir>
#include <mutex>
#include <vector>
#include <QTextStream>
#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QFontInfo>

#include "config.h"
#include "dialog.h"
#include "MainWindow.h"
#include "ThemeManager.h"
#include "mousetap.h"
#include "ConfigCenter.h"
#include "GameScrcpyCore.h"
#include "StringUtils.h"
#include "ThreadDispatcher.h"

static MainWindow *g_mainWindow = Q_NULLPTR;
static QtMessageHandler g_oldMessageHandler = Q_NULLPTR;
void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg);
void installTranslator(const QString &langOverride = QString());

// ============================================================
// 文件日志系统 / File Logging System
// 高性能异步式文件日志，在exe目录下生成logs文件夹
// 自动按日期滚动日志文件，保留最近7天的日志
// ============================================================

class FileLogger
{
public:
    static FileLogger& instance()
    {
        static FileLogger s_instance;
        return s_instance;
    }

    // 初始化日志目录（在 QApplication 构造之后调用）
    bool initialize()
    {
        // exe 同级目录下创建 logs 文件夹
        m_logDir = QCoreApplication::applicationDirPath() + "/logs";
        QDir dir(m_logDir);
        if (!dir.exists()) {
            if (!dir.mkpath(".")) {
                qWarning() << "[FileLogger] Failed to create log directory:" << m_logDir;
                return false;
            }
        }

        // 清理过期日志（保留最近7天）
        cleanOldLogs(7);

        // 按日期打开日志文件
        bool ok = openLogFile();

        // 启动定期刷新定时器（5秒），确保 DEBUG/INFO 级别日志也能及时写盘
        // 崩溃前的关键日志不会因缓冲而丢失
        if (ok) {
            startPeriodicFlush();
        }

        return ok;
    }

    // 写入一条日志（线程安全，低开销）
    void write(QtMsgType type, const QMessageLogContext &context, const QString &msg)
    {
        if (!m_initialized) return;

        // 检查是否需要滚动到新一天的文件
        QString today = QDate::currentDate().toString("yyyy-MM-dd");
        if (today != m_currentDate) {
            std::lock_guard<std::mutex> locker(m_mutex);
            if (today != m_currentDate) {  // double-check
                openLogFile();
            }
        }

        // 格式化日志行
        QString levelStr;
        switch (type) {
        case QtDebugMsg:    levelStr = QStringLiteral("DEBUG"); break;
        case QtInfoMsg:     levelStr = QStringLiteral("INFO "); break;
        case QtWarningMsg:  levelStr = QStringLiteral("WARN "); break;
        case QtCriticalMsg: levelStr = QStringLiteral("ERROR"); break;
        case QtFatalMsg:    levelStr = QStringLiteral("FATAL"); break;
        }

        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);

        // 包含源文件信息（仅Debug构建时有效）
        QString location;
        if (context.file) {
            // 只取文件名，不含完整路径
            QString fileName = QString::fromUtf8(context.file);
            int lastSlash = fileName.lastIndexOf('/');
            int lastBackslash = fileName.lastIndexOf('\\');
            int pos = qMax(lastSlash, lastBackslash);
            if (pos >= 0) fileName = fileName.mid(pos + 1);
            location = QStringLiteral(" [%1:%2]").arg(fileName).arg(context.line);
        }

        QString line = QStringLiteral("%1 [%2] [T:%3]%4 %5\n")
                            .arg(timestamp, levelStr, threadId, location, msg);

        // 加锁写入文件
        {
            std::lock_guard<std::mutex> locker(m_mutex);
            if (m_file.isOpen()) {
                m_stream << line;
                // 仅在 WARNING 及以上级别立即刷新，减少 IO 开销
                if (type >= QtWarningMsg) {
                    m_stream.flush();
                }
            }
        }
    }

    // 强制刷新缓冲区到磁盘
    void flush()
    {
        std::lock_guard<std::mutex> locker(m_mutex);
        if (m_file.isOpen()) {
            m_stream.flush();
            m_file.flush();
        }
    }

    ~FileLogger()
    {
        if (m_flushTimer) {
            m_flushTimer->stop();
            delete m_flushTimer;
            m_flushTimer = nullptr;
        }
        std::lock_guard<std::mutex> locker(m_mutex);
        if (m_file.isOpen()) {
            m_stream.flush();
            m_file.close();
        }
    }

private:
    FileLogger() = default;
    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;

    void startPeriodicFlush()
    {
        if (m_flushTimer) return;  // 已经启动
        m_flushTimer = new QTimer();
        m_flushTimer->setInterval(1000);  // 每1秒刷新一次（确保崩溃前日志不丢失）
        QObject::connect(m_flushTimer, &QTimer::timeout, [this]() {
            flush();
        });
        m_flushTimer->start();
    }

    bool openLogFile()
    {
        if (m_file.isOpen()) {
            m_stream.flush();
            m_file.close();
        }

        m_currentDate = QDate::currentDate().toString("yyyy-MM-dd");
        QString fileName = QStringLiteral("%1/GameScrcpy_%2.log").arg(m_logDir, m_currentDate);

        m_file.setFileName(fileName);
        if (!m_file.open(QIODevice::Append | QIODevice::Text)) {
            qWarning() << "[FileLogger] Failed to open log file:" << fileName;
            m_initialized = false;
            return false;
        }

        m_stream.setDevice(&m_file);
        m_initialized = true;

        // 写入启动头
        m_stream << QStringLiteral("\n========================================\n");
        m_stream << QStringLiteral("GameScrcpy started at %1\n")
                        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"));
        m_stream << QStringLiteral("Version: %1\n").arg(QCoreApplication::applicationVersion());
        m_stream << QStringLiteral("========================================\n");
        m_stream.flush();

        return true;
    }

    void cleanOldLogs(int keepDays)
    {
        QDir dir(m_logDir);
        QStringList filters;
        filters << "GameScrcpy_*.log";
        QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);

        QDateTime cutoff = QDateTime::currentDateTime().addDays(-keepDays);
        for (const QFileInfo &fi : files) {
            if (fi.lastModified() < cutoff) {
                QFile::remove(fi.absoluteFilePath());
            }
        }
    }

    std::mutex m_mutex;
    QFile m_file;
    QTextStream m_stream;
    QString m_logDir;
    QString m_currentDate;
    bool m_initialized = false;
    QTimer* m_flushTimer = nullptr;
};

/**
 * @brief 首次运行使用协议弹窗
 * @return true=用户接受, false=用户拒绝
 */
static bool showAgreementDialog()
{
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("使用协议 / User Agreement"));
    dlg.setMinimumSize(560, 480);
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // 样式自动继承 qApp->setStyleSheet() 设置的全局样式

    auto *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);

    auto *titleLabel = new QLabel(QStringLiteral(
        "<h2>GameScrcpy 使用协议</h2>"
        "<p style='color:gray;'>User License Agreement</p>"));
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto *textEdit = new QTextEdit(&dlg);
    textEdit->setReadOnly(true);
    // 设置文档默认颜色与主题一致（QSS 不影响 HTML 渲染的内部颜色）
    textEdit->document()->setDefaultStyleSheet(
        "body { color: #DCDCDC; }"
        "h3 { color: #00BB9E; }"
        "a { color: #00BB9E; }"
    );
    textEdit->setHtml(QStringLiteral(
        "<p>GameScrcpy 是一个基于 Apache License 2.0 协议发布的开源项目。"
        "在使用本软件前，请阅读以下内容：</p>"

        "<h3>开源许可</h3>"
        "<p>本软件基于 <b>Apache License, Version 2.0</b> 开源。"
        "您可以自由地使用、复制、修改和分发本软件，包括用于商业目的，"
        "但须保留原始版权声明和许可证文本。完整许可证请参阅项目根目录下的 LICENSE 文件。</p>"

        "<h3>免责声明</h3>"
        "<p>本软件按「现状」（AS IS）提供，不提供任何形式的明示或暗示担保，"
        "包括但不限于对适销性、特定用途适用性和非侵权性的担保。</p>"
        "<p>在任何情况下，作者或版权持有人均不对因本软件或使用本软件而产生的"
        "任何索赔、损害或其他责任承担责任。</p>"

        "<h3>使用规范</h3>"
        "<p>您不得将本软件用于任何违反所在地区法律法规的用途。"
        "因不当使用本软件而产生的一切法律后果由使用者自行承担。</p>"

        "<hr>"
        "<p style='color:gray; font-size:small;'>"
        "Copyright 2019-2026 Rankun. Licensed under the Apache License, Version 2.0.</p>"
    ));
    layout->addWidget(textEdit);

    auto *checkBox = new QCheckBox(QStringLiteral("我已阅读并同意上述使用协议 / I have read and agree to the above agreement"), &dlg);
    layout->addWidget(checkBox);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto *acceptBtn = new QPushButton(QStringLiteral("接受 / Accept"), &dlg);
    auto *rejectBtn = new QPushButton(QStringLiteral("拒绝 / Reject"), &dlg);
    acceptBtn->setEnabled(false);
    acceptBtn->setMinimumWidth(120);
    rejectBtn->setMinimumWidth(120);
    btnLayout->addWidget(acceptBtn);
    btnLayout->addWidget(rejectBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    QObject::connect(checkBox, &QCheckBox::toggled, acceptBtn, &QPushButton::setEnabled);
    QObject::connect(acceptBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(rejectBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    return dlg.exec() == QDialog::Accepted;
}

static QtMsgType g_msgType = QtInfoMsg;
QtMsgType convertLogLevel(const QString &logLevel);

// ============================================================
// 崩溃捕获系统 / Crash Capture System
// 捕获所有类型的崩溃并写入崩溃日志
// ============================================================
#ifdef Q_OS_WIN32
#include <windows.h>
#include <csignal>
#include <cstdlib>

// VEH 保存的真实异常上下文（per-thread, 避免被 first-chance 异常覆盖后丢失）
// VEH 可能被 first-chance 异常触发（__try/__except 最终会捕获），所以不直接写 dump
// 只保存上下文，等 signal handler（只在进程即将死亡时运行）负责写 dump
struct VehSavedContext {
    EXCEPTION_RECORD exRecord;
    CONTEXT          ctx;
    volatile LONG    valid;  // 1=有有效数据
};
static __declspec(thread) VehSavedContext g_vehCtx = {};


// 获取异常代码的可读名称
static const char* getExceptionCodeName(DWORD code) {
    switch (code) {
    case 0xC0000005: return "ACCESS_VIOLATION";
    case 0xC0000008: return "INVALID_HANDLE";
    case 0xC000000D: return "INVALID_PARAMETER";
    case 0xC0000017: return "NO_MEMORY";
    case 0xC000001D: return "ILLEGAL_INSTRUCTION";
    case 0xC0000025: return "NONCONTINUABLE_EXCEPTION";
    case 0xC00000FD: return "STACK_OVERFLOW";
    case 0xC0000094: return "INTEGER_DIVIDE_BY_ZERO";
    case 0xC0000096: return "PRIVILEGED_INSTRUCTION";
    case 0xC000008C: return "ARRAY_BOUNDS_EXCEEDED";
    case 0xC0000090: return "FLOAT_INVALID_OPERATION";
    case 0xC0000091: return "FLOAT_OVERFLOW";
    case 0xC000008D: return "FLOAT_DENORMAL_OPERAND";
    case 0xC000008E: return "FLOAT_DIVIDE_BY_ZERO";
    case 0xE06D7363: return "CPP_EXCEPTION (std::exception)";
    case 0x40010005: return "CONTROL_C_EXIT";
    default: return "UNKNOWN";
    }
}

// MiniDump 已禁用 — 崩溃信息仅写入日志文件
static void writeMiniDump(EXCEPTION_POINTERS *) {}

// 辅助函数：已禁用
static void generateDumpViaSEH(DWORD) {}

// 使用低级 Win32 API 直接追加崩溃日志（不依赖 Qt）
static void writeCrashLogDirect(const char *message)
{
    wchar_t logPath[MAX_PATH];
    GetModuleFileNameW(NULL, logPath, MAX_PATH);
    wchar_t *lastSlash = wcsrchr(logPath, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t logFile[MAX_PATH];
    swprintf_s(logFile, L"%slogs\\GameScrcpy_%04d-%02d-%02d.log", logPath, st.wYear, st.wMonth, st.wDay);

    HANDLE hFile = CreateFileW(logFile, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        char timeBuf[64];
        sprintf_s(timeBuf, "%02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        char fullMsg[2048];
        sprintf_s(fullMsg, "%s [FATAL] [T:%04x] %s\n", timeBuf, GetCurrentThreadId(), message);

        DWORD written;
        WriteFile(hFile, fullMsg, (DWORD)strlen(fullMsg), &written, NULL);
        FlushFileBuffers(hFile);
        CloseHandle(hFile);
    }
}

// SEH 全局异常处理器
LONG WINAPI MyUnhandledExceptionFilter(EXCEPTION_POINTERS *ExceptionInfo)
{
    // 先用 Qt 日志（如果还能用的话）
    qCritical() << "[CRASH] ====== UNHANDLED EXCEPTION ======";
    qCritical() << "[CRASH] Exception code:" << Qt::hex << ExceptionInfo->ExceptionRecord->ExceptionCode
                << "(" << getExceptionCodeName(ExceptionInfo->ExceptionRecord->ExceptionCode) << ")";
    qCritical() << "[CRASH] Address:" << ExceptionInfo->ExceptionRecord->ExceptionAddress;

    if (ExceptionInfo->ExceptionRecord->ExceptionCode == 0xc0000005 &&
        ExceptionInfo->ExceptionRecord->NumberParameters >= 2) {
        qCritical() << "[CRASH] Access violation:"
                    << (ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" :
                        ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 1 ? "WRITE" : "DEP")
                    << "at address:" << Qt::hex << ExceptionInfo->ExceptionRecord->ExceptionInformation[1];
    }

    CONTEXT* ctx = ExceptionInfo->ContextRecord;
    if (ctx) {
        qCritical() << "[CRASH] Registers: RIP=" << Qt::hex << ctx->Rip
                    << "RSP=" << ctx->Rsp << "RBP=" << ctx->Rbp
                    << "RAX=" << ctx->Rax << "RBX=" << ctx->Rbx
                    << "RCX=" << ctx->Rcx << "RDX=" << ctx->Rdx;
    }

    // 刷新 Qt 日志
    FileLogger::instance().flush();

    // 再用低级 API 直接写（防止 Qt 已挂）
    char crashMsg[1024];
    sprintf_s(crashMsg, "[CRASH] Exception 0x%08lX (%s) at %p",
              ExceptionInfo->ExceptionRecord->ExceptionCode,
              getExceptionCodeName(ExceptionInfo->ExceptionRecord->ExceptionCode),
              ExceptionInfo->ExceptionRecord->ExceptionAddress);
    writeCrashLogDirect(crashMsg);

    if (ExceptionInfo->ExceptionRecord->ExceptionCode == 0xc0000005 &&
        ExceptionInfo->ExceptionRecord->NumberParameters >= 2) {
        sprintf_s(crashMsg, "[CRASH] Access violation: %s at 0x%llX",
                  ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" : "WRITE",
                  (unsigned long long)ExceptionInfo->ExceptionRecord->ExceptionInformation[1]);
        writeCrashLogDirect(crashMsg);
    }

    if (ctx) {
        sprintf_s(crashMsg, "[CRASH] RIP=0x%llX RSP=0x%llX RBP=0x%llX RAX=0x%llX RCX=0x%llX",
                  (unsigned long long)ctx->Rip, (unsigned long long)ctx->Rsp,
                  (unsigned long long)ctx->Rbp, (unsigned long long)ctx->Rax,
                  (unsigned long long)ctx->Rcx);
        writeCrashLogDirect(crashMsg);
    }

    writeCrashLogDirect("[CRASH] ====== END OF CRASH REPORT ======");

    return EXCEPTION_EXECUTE_HANDLER;
}

// ---------------------------------------------------------------
// Qt 模块地址范围 (供崩溃诊断使用)
// ---------------------------------------------------------------

struct QtModuleRange {
    DWORD64 base = 0, end = 0;
    const char* name = nullptr;
};
static QtModuleRange s_qtModules[4]; // Qt6Core, Qt6Gui, Qt6Widgets, sentinel
static int s_qtModuleCount = 0;

static bool isInQtModule(DWORD64 rip) {
    for (int i = 0; i < s_qtModuleCount; i++) {
        if (rip >= s_qtModules[i].base && rip < s_qtModules[i].end)
            return true;
    }
    return false;
}

static const char* getQtModuleName(DWORD64 rip) {
    for (int i = 0; i < s_qtModuleCount; i++) {
        if (rip >= s_qtModules[i].base && rip < s_qtModules[i].end)
            return s_qtModules[i].name;
    }
    return nullptr;
}

// ---------------------------------------------------------------
// Vectored Exception Handler — 仅保存异常上下文用于诊断
//
// v10: 完全移除 NULL stub 恢复机制
// 原因: v9 测试证明将 NULL 寄存器替换为 zero-stub 会导致
//       Qt 内部状态级联错误，使单次可恢复崩溃变为多次致命崩溃。
//       现在只记录异常上下文供 signal handler 写高质量 MiniDump。
// ---------------------------------------------------------------
static LONG CALLBACK MyVectoredExceptionHandler(EXCEPTION_POINTERS *ExceptionInfo)
{
    DWORD code = ExceptionInfo->ExceptionRecord->ExceptionCode;

    // 仅保存致命异常上下文（供 signal handler 写 minidump 时使用）
    if (code == 0xC0000005 || // ACCESS_VIOLATION
        code == 0xC00000FD || // STACK_OVERFLOW
        code == 0xC000001D || // ILLEGAL_INSTRUCTION
        code == 0xC0000094 || // INTEGER_DIVIDE_BY_ZERO
        code == 0xC0000096)   // PRIVILEGED_INSTRUCTION
    {
        // 保存异常上下文的副本（每次覆盖: 最新的才是最终崩溃的）
        memcpy(&g_vehCtx.exRecord, ExceptionInfo->ExceptionRecord, sizeof(EXCEPTION_RECORD));
        if (ExceptionInfo->ContextRecord) {
            memcpy(&g_vehCtx.ctx, ExceptionInfo->ContextRecord, sizeof(CONTEXT));
        }
        InterlockedExchange(&g_vehCtx.valid, 1);

        // 输出简短诊断信息到 stderr（用于 console 调试）
        if (code == 0xC0000005 && ExceptionInfo->ExceptionRecord->NumberParameters >= 2) {
            DWORD64 rip = ExceptionInfo->ContextRecord ? ExceptionInfo->ContextRecord->Rip : 0;
            ULONG_PTR accessType = ExceptionInfo->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR targetAddr = ExceptionInfo->ExceptionRecord->ExceptionInformation[1];
            const char* qtMod = getQtModuleName(rip);
            char msg[256];
            sprintf_s(msg, "[VEH] AV: %s at 0x%llX, RIP=0x%llX%s%s\n",
                      accessType == 0 ? "READ" : accessType == 1 ? "WRITE" : "DEP",
                      (unsigned long long)targetAddr,
                      (unsigned long long)rip,
                      qtMod ? " in " : "",
                      qtMod ? qtMod : "");
            OutputDebugStringA(msg);
            fprintf(stderr, "%s", msg);
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

// C++ std::terminate 处理器（捕获未处理的 C++ 异常、纯虚函数调用等）
static void myTerminateHandler()
{
    writeCrashLogDirect("[CRASH] ====== std::terminate() called ======");

    // 尝试获取当前异常信息
    std::exception_ptr eptr = std::current_exception();
    if (eptr) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception &e) {
            char msg[512];
            sprintf_s(msg, "[CRASH] Unhandled C++ exception: %s", e.what());
            writeCrashLogDirect(msg);
            qCritical() << msg;
        } catch (...) {
            writeCrashLogDirect("[CRASH] Unhandled C++ exception (unknown type)");
            qCritical() << "[CRASH] Unhandled C++ exception (unknown type)";
        }
    } else {
        writeCrashLogDirect("[CRASH] std::terminate without active exception (possible pure virtual call or double exception)");
        qCritical() << "[CRASH] std::terminate without active exception";
    }

    FileLogger::instance().flush();

    // 尝试生成 MiniDump（通过辅助函数触发异常来获取上下文）

    writeCrashLogDirect("[CRASH] ====== END OF TERMINATE REPORT ======");
    _exit(3);  // 强制退出，不走正常析构
}

// v15: QuickJS abort line number (defined in quickjs.c)
extern "C" volatile int qjs_last_abort_line;

// SIGABRT / SIGSEGV 信号处理器
static void mySignalHandler(int sig)
{
    const char *sigName = "UNKNOWN";
    switch (sig) {
    case SIGABRT: sigName = "SIGABRT (abort)"; break;
    case SIGSEGV: sigName = "SIGSEGV (segfault)"; break;
    case SIGFPE:  sigName = "SIGFPE (floating point)"; break;
    case SIGILL:  sigName = "SIGILL (illegal instruction)"; break;
    case SIGTERM: sigName = "SIGTERM (terminate)"; break;
    }

    char msg[512];
    sprintf_s(msg, "[CRASH] Signal received: %s (signal %d) on thread 0x%lx", sigName, sig, GetCurrentThreadId());
    writeCrashLogDirect(msg);
    qCritical() << msg;

    // v15: Print QuickJS abort line if available
    {
        if (qjs_last_abort_line > 0) {
            sprintf_s(msg, "[CRASH] QuickJS abort triggered at quickjs.c:%d", qjs_last_abort_line);
            writeCrashLogDirect(msg);
            qCritical() << msg;
        }
    }

    // v13: 捕获调用栈地址（SIGABRT 没有 VEH 上下文，需要手动回溯）
    {
        void* stackFrames[32];
        USHORT frameCount = CaptureStackBackTrace(0, 32, stackFrames, NULL);
        sprintf_s(msg, "[CRASH] Stack trace (%d frames):", (int)frameCount);
        writeCrashLogDirect(msg);
        qCritical() << msg;
        for (USHORT i = 0; i < frameCount; ++i) {
            HMODULE hMod = NULL;
            char modName[MAX_PATH] = "<unknown>";
            DWORD64 modBase = 0;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)stackFrames[i], &hMod) && hMod) {
                GetModuleFileNameA(hMod, modName, MAX_PATH);
                char* slash = strrchr(modName, '\\');
                if (slash) memmove(modName, slash + 1, strlen(slash + 1) + 1);
                modBase = (DWORD64)(uintptr_t)hMod;
            }
            DWORD64 offset = (DWORD64)(uintptr_t)stackFrames[i] - modBase;
            sprintf_s(msg, "[CRASH]   [%2d] 0x%llX (%s+0x%llX)",
                      (int)i, (unsigned long long)(uintptr_t)stackFrames[i],
                      modName, (unsigned long long)offset);
            writeCrashLogDirect(msg);
            qCritical() << msg;
        }
    }

    FileLogger::instance().flush();

    // 使用 VEH 保存的真实异常上下文写 dump（如果有的话）
    if (g_vehCtx.valid) {
        // VEH 保存了真实的异常信息——用它来生成 dump
        EXCEPTION_POINTERS ep;
        ep.ExceptionRecord = &g_vehCtx.exRecord;
        ep.ContextRecord   = &g_vehCtx.ctx;

        DWORD code = g_vehCtx.exRecord.ExceptionCode;
        DWORD64 rip = g_vehCtx.ctx.Rip;

        // 查找崩溃所在模块名
        char moduleName[MAX_PATH] = "<unknown>";
        {
            HMODULE hMod = NULL;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)(uintptr_t)rip, &hMod) && hMod) {
                GetModuleFileNameA(hMod, moduleName, MAX_PATH);
                // 只保留文件名
                char* slash = strrchr(moduleName, '\\');
                if (slash) memmove(moduleName, slash + 1, strlen(slash + 1) + 1);
            }
        }

        // ==== 输出到 CONSOLE (qCritical) — 用户可见 ====
        sprintf_s(msg, "[CRASH] Exception 0x%08lX at RIP=0x%llX (%s)",
                  code, (unsigned long long)rip, moduleName);
        qCritical() << msg;

        if (code == 0xC0000005 && g_vehCtx.exRecord.NumberParameters >= 2) {
            sprintf_s(msg, "[CRASH] ACCESS_VIOLATION: %s at 0x%llX",
                      g_vehCtx.exRecord.ExceptionInformation[0] == 0 ? "READ" : "WRITE",
                      (unsigned long long)g_vehCtx.exRecord.ExceptionInformation[1]);
            qCritical() << msg;
        }

        sprintf_s(msg, "[CRASH] Registers: RAX=0x%llX RCX=0x%llX RDX=0x%llX RBX=0x%llX",
                  (unsigned long long)g_vehCtx.ctx.Rax, (unsigned long long)g_vehCtx.ctx.Rcx,
                  (unsigned long long)g_vehCtx.ctx.Rdx, (unsigned long long)g_vehCtx.ctx.Rbx);
        qCritical() << msg;
        sprintf_s(msg, "[CRASH] Registers: RSI=0x%llX RDI=0x%llX RSP=0x%llX RBP=0x%llX",
                  (unsigned long long)g_vehCtx.ctx.Rsi, (unsigned long long)g_vehCtx.ctx.Rdi,
                  (unsigned long long)g_vehCtx.ctx.Rsp, (unsigned long long)g_vehCtx.ctx.Rbp);
        qCritical() << msg;

        // Qt 模块范围判断
        bool inQt = isInQtModule(rip);
        sprintf_s(msg, "[CRASH] Module: %s | inQt=%s",
                  moduleName,
                  inQt ? "YES" : "NO");
        qCritical() << msg;

        // ==== 也写入日志文件 ====
        sprintf_s(msg, "[CRASH] Exception 0x%08lX at RIP=0x%llX (%s)",
                  code, (unsigned long long)rip, moduleName);
        writeCrashLogDirect(msg);

        if (code == 0xC0000005 && g_vehCtx.exRecord.NumberParameters >= 2) {
            sprintf_s(msg, "[CRASH] ACCESS_VIOLATION: %s at 0x%llX",
                      g_vehCtx.exRecord.ExceptionInformation[0] == 0 ? "READ" : "WRITE",
                      (unsigned long long)g_vehCtx.exRecord.ExceptionInformation[1]);
            writeCrashLogDirect(msg);
        }
        sprintf_s(msg, "[CRASH] Registers: RIP=0x%llX RSP=0x%llX RAX=0x%llX RCX=0x%llX",
                  (unsigned long long)rip, (unsigned long long)g_vehCtx.ctx.Rsp,
                  (unsigned long long)g_vehCtx.ctx.Rax, (unsigned long long)g_vehCtx.ctx.Rcx);
        writeCrashLogDirect(msg);

        writeMiniDump(&ep);
    } else {
        qCritical() << "[CRASH] No VEH context available (crash was not ACCESS_VIOLATION)";
    }

    writeCrashLogDirect("[CRASH] ====== END OF SIGNAL REPORT ======");
    FileLogger::instance().flush();
    _exit(128 + sig);
}

// 无效参数处理器（CRT 级别）
static void myInvalidParameterHandler(const wchar_t *expression, const wchar_t *function,
                                       const wchar_t *file, unsigned int line, uintptr_t)
{
    char msg[512];
    if (function) {
        sprintf_s(msg, "[CRASH] Invalid parameter in %ls: %ls (file: %ls, line: %u)",
                  function, expression ? expression : L"(null)", file ? file : L"(null)", line);
    } else {
        sprintf_s(msg, "[CRASH] Invalid parameter detected (no details available)");
    }
    writeCrashLogDirect(msg);
    qCritical() << msg;
    FileLogger::instance().flush();
}

// 安装所有崩溃处理器
static void installCrashHandlers()
{
    // 0. Vectored Exception Handler (最高优先级，在 CRT 和 SEH 之前运行)
    // 用来捕获真实的异常上下文，写入正确的 MiniDump
    AddVectoredExceptionHandler(1, MyVectoredExceptionHandler);

    // 1. SEH 异常处理 (ACCESS_VIOLATION, STACK_OVERFLOW 等)
    SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);

    // 2. C++ 异常终止处理 (std::terminate, 纯虚函数调用, 未捕获异常)
    std::set_terminate(myTerminateHandler);

    // 3. CRT 信号处理 (SIGABRT, SIGSEGV 等)
    // 注意：signal(SIGSEGV) 会导致 CRT 拦截 ACCESS_VIOLATION，
    // 但我们的 VEH 会在 CRT 之前捕获真实异常并写入 dump
    signal(SIGABRT, mySignalHandler);
    signal(SIGSEGV, mySignalHandler);
    signal(SIGFPE,  mySignalHandler);
    signal(SIGILL,  mySignalHandler);
    signal(SIGTERM, mySignalHandler);

    // 4. CRT 无效参数处理
    _set_invalid_parameter_handler(myInvalidParameterHandler);

    // 5. 禁用 CRT 弹出错误对话框（让程序直接走我们的处理器）
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // 6. （v10）已完全移除 NULL stub 恢复机制
    // v9 的 VEH 会将命中 NULL+offset 的寄存器替换为 zero-stub 块，
    // 但测试证明这导致 Qt 内部状态级联错误，使情况更糟。
    // VEH 现在仅保存异常上下文（用于 signal handler 写高质量 MiniDump）。
}
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN32
    // 安装全部崩溃处理器（SEH + VEH + terminate + signal + CRT）
    installCrashHandlers();
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

    // --------------------------------------------------
    // 强制使用 FreeType 字体引擎 (防止 DirectWrite 崩溃)
    // v11: 同时通过 env var + argv 双保险注入
    //      并加上 nodirectwrite 选项彻底禁止 DirectWrite
    // --------------------------------------------------
    {
        _putenv_s("QT_QPA_PLATFORM", "windows:fontengine=freetype:nodirectwrite");
        _putenv_s("QT_LOGGING_RULES", "qt.qpa.fonts=false;qt.qpa.windows=false");
    }
#endif

    g_msgType = convertLogLevel(strutil::toQ(Config::getInstance().getLogLevel()));

    // 设置默认Surface格式
    QSurfaceFormat varFormat = QSurfaceFormat::defaultFormat();
    varFormat.setVersion(2, 0);
    varFormat.setProfile(QSurfaceFormat::NoProfile);
    varFormat.setSwapInterval(1);  // 启用 VSync，保证帧节奏与显示器刷新同步，消除画面抖动
    QSurfaceFormat::setDefaultFormat(varFormat);

    // ---------------------------------------------------------
    // 禁用 DirectWrite 字体引擎 — 防止 Qt 6.10.1 崩溃 (QTBUG-XXXXX)
    //
    // Qt 6.10.1 的 DirectWrite text shaping 在 font fallback 路径中
    // 会产生 NULL QFontEngine，导致 [NULL+0x6C] ACCESS_VIOLATION。
    //
    // 最可靠方式：通过命令行注入 -platform windows:fontengine=freetype
    // Qt 解析顺序: argv > QT_QPA_PLATFORM env > 编译时默认值
    // 使用 argv 注入确保在所有情况下都能生效。
    // ---------------------------------------------------------
    static char argPlatform[] = "-platform";
    static char argPlatformVal[] = "windows:fontengine=freetype:nodirectwrite";
    // 构建新的 argv: [exe, -platform, windows:fontengine=freetype:nodirectwrite, 原始args...]
    int newArgc = argc + 2;
    std::vector<char*> newArgv(newArgc + 1);
    newArgv[0] = argv[0];
    newArgv[1] = argPlatform;
    newArgv[2] = argPlatformVal;
    for (int i = 1; i < argc; ++i) newArgv[i + 2] = argv[i];
    newArgv[newArgc] = nullptr;

    // 安装自定义消息处理器（日志系统）
    g_oldMessageHandler = qInstallMessageHandler(myMessageOutput);
    QApplication a(newArgc, newArgv.data());

    // 缓存 Qt 模块地址范围（供 VEH NULL 恢复用）
    {
        const wchar_t* qtDlls[] = { L"Qt6Core.dll", L"Qt6Gui.dll", L"Qt6Widgets.dll" };
        const char*    qtNames[] = { "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll" };
        s_qtModuleCount = 0;
        for (int i = 0; i < 3; i++) {
            HMODULE hMod = GetModuleHandleW(qtDlls[i]);
            if (hMod) {
                auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
                auto nt  = reinterpret_cast<IMAGE_NT_HEADERS*>(
                               reinterpret_cast<BYTE*>(hMod) + dos->e_lfanew);
                s_qtModules[s_qtModuleCount].base = reinterpret_cast<DWORD64>(hMod);
                s_qtModules[s_qtModuleCount].end  = s_qtModules[s_qtModuleCount].base
                                                     + nt->OptionalHeader.SizeOfImage;
                s_qtModules[s_qtModuleCount].name = qtNames[i];
                s_qtModuleCount++;
            }
        }
    }

    // 初始化跨线程消息分发系统
    dispatch::initialize();

    // 初始化文件日志系统（在exe目录下创建logs文件夹）
    FileLogger::instance().initialize();

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
    // 字体安全初始化 — 防止 Qt 6.10.1 font fallback 崩溃
    //
    // 根因: Qt 6.10.1 的 font fallback 路径在为 CJK 字符寻找
    // 后备字体时，可能返回 NULL QFontEngine*，后续代码解引用
    // [NULL+0x6C] 导致 ACCESS_VIOLATION。
    //
    // 对策:
    //   1) 使用 "Microsoft YaHei UI" 作为基线字体
    //      它同时覆盖 Latin + CJK，从根本上避免 font fallback
    //   2) 通过 QFontMetrics 强制提前初始化 font engine 缓存
    //   3) 多权重 (Normal + Bold) + CJK 字符预热
    //   4) QSS 中也已将 YaHei 提至首位
    // ---------------------------------------------------------
    {
        QFont safeFont(QStringLiteral("Microsoft YaHei UI"));
        safeFont.setPointSize(9);
        safeFont.setWeight(QFont::Normal);
        a.setFont(safeFont);

        // 预热 Normal 权重 — 拉丁 + CJK
        QFontMetrics fmNormal(safeFont);
        (void)fmNormal.height();
        (void)fmNormal.averageCharWidth();
        (void)fmNormal.horizontalAdvance(QStringLiteral("ABCabc123"));
        (void)fmNormal.horizontalAdvance(QStringLiteral("\u4e2d\u6587\u6d4b\u8bd5")); // "中文测试"

        // 预热 Bold / SemiBold (Fluent 主题使用 font-weight:600)
        QFont boldFont(safeFont);
        boldFont.setWeight(QFont::DemiBold);
        QFontMetrics fmBold(boldFont);
        (void)fmBold.height();
        (void)fmBold.horizontalAdvance(QStringLiteral("\u8bbe\u7f6e")); // "设置"


    }

    // ---------------------------------------------------------
    // 应用 Fluent 主题 (替代旧 psblack.css)
    // ---------------------------------------------------------
    Fluent::ThemeManager::instance().applyToApplication();

    // 设置全局应用图标
    a.setWindowIcon(QIcon(QStringLiteral(":/image/tray/logo.png")));

    qsc::AdbProcess::setAdbPath(Config::getInstance().getAdbPath());

    // 初始化配置中心（使用默认路径）
    qsc::ConfigCenter::instance().initialize();

    // ---------------------------------------------------------
    // 首次运行：显示使用协议弹窗
    // ---------------------------------------------------------
    if (!Config::getInstance().getAgreementAccepted()) {
        if (!showAgreementDialog()) {
            // 用户拒绝协议，退出程序
            return 0;
        }
        Config::getInstance().setAgreementAccepted(true);
    }

    // 创建并显示新版主窗口 (Fluent NavigationView)
    g_mainWindow = new MainWindow{};
    g_mainWindow->show();

    int ret = a.exec();

    // 正常退出时刷新并关闭日志
    qInfo() << "[Main] Application exiting with code:" << ret;
    FileLogger::instance().flush();

    // 安全关闭：先断开所有设备连接（在 QApplication 仍然活跃时执行）
    // 这确保 deleteLater 对象在后续 processEvents 中被正确销毁
    qsc::IDeviceManage::getInstance().disconnectAllDevice();

    // 处理待删除的 QObject（确保 deleteLater 调度的对象被销毁）
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    delete g_mainWindow;
    g_mainWindow = nullptr;

    // 再次处理残余事件，防止悬挂回调
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

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
    QString langCode = langOverride.isEmpty() ? strutil::toQ(Config::getInstance().getLanguage()) : langOverride;

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

QtMsgType convertLogLevel(const QString &logLevel)
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
    // 过滤 Qt6 内部无害警告 (不影响功能)
    if (type == QtWarningMsg) {
        if (msg.contains(QLatin1String("Failed to set thread priority")))
            return;
        // DirectWrite 字体创建警告 — 不再静默过滤，改为记录完整信息以排查崩溃根因
        if (msg.contains(QLatin1String("CreateFontFaceFromHDC"))) {
            fprintf(stderr, "[FONT-WARN] DirectWrite: %s\n", msg.toUtf8().constData());
            fflush(stderr);
            // 继续正常日志流程（不 return）
        }
    }

    QString outputMsg;

    outputMsg = msg;
    if (g_oldMessageHandler) {
        g_oldMessageHandler(type, context, outputMsg);
    }

    // 写入文件日志（所有级别全覆盖）
    FileLogger::instance().write(type, context, outputMsg);

    // 过滤并显示日志到主窗口
    // v12: 必须在主线程调用 outLog，否则会在工作线程上触发
    //      TerminalPage (Consolas) 字体引擎创建，导致线程竞争崩溃
    float fLogLevel = g_msgType;
    if (QtInfoMsg == g_msgType) fLogLevel = QtDebugMsg + 0.5f;
    float fLogLevel2 = type;
    if (QtInfoMsg == type) fLogLevel2 = QtDebugMsg + 0.5f;

    if (fLogLevel <= fLogLevel2) {
        if (g_mainWindow && g_mainWindow->isVisible() && !g_mainWindow->filterLog(outputMsg)) {
            if (QThread::currentThread() == qApp->thread()) {
                g_mainWindow->outLog(outputMsg);
            } else {
                // 跨线程: 投递到主线程执行, 避免在工作线程触发 Qt 字体/UI 操作
                QString logCopy = outputMsg;
                QMetaObject::invokeMethod(g_mainWindow, [logCopy]() {
                    if (g_mainWindow) g_mainWindow->outLog(logCopy);
                }, Qt::QueuedConnection);
            }
        }
    }

    // WARNING 及以上级别立即刷新到磁盘（确保崩溃前的关键日志不丢失）
    if (type >= QtWarningMsg) {
        FileLogger::instance().flush();
    }

    if (QtFatalMsg == type) {
        FileLogger::instance().flush();
    }
}
