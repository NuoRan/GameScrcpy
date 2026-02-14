#include "ScriptSandbox.h"
#include "ScriptWatchdog.h"
#include "ScriptEngine.h"
#include "controller.h"
#include "SessionContext.h"
#include "fastmsg.h"
#include "keycodes.h"
#include "ConfigCenter.h"
#include "ScriptTipWidget.h"
#include "selectionregionmanager.h"

#ifdef ENABLE_IMAGE_MATCHING
#include "imagematcher.h"
#endif

#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QRandomGenerator>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 静态成员初始化
int ScriptSandbox::s_maxTouchPoints = 10;

// =============================================================
// ScriptSandbox 实现
// =============================================================

ScriptSandbox::ScriptSandbox(int sandboxId, Controller* controller, SessionContext* ctx,
                             QObject* parent)
    : QObject(parent)
    , m_sandboxId(sandboxId)
    , m_controller(controller)
    , m_sessionContext(ctx)
{
    // 创建工作线程（parent 设为 nullptr，因为 QThread 不能有 parent 当它管理其他对象时）
    m_thread = new QThread();

    // 创建看门狗（保持在主线程，parent 设为 parent 而不是 this，防止跟着 moveToThread）
    m_watchdog = new ScriptWatchdog(30000, parent);
    connect(m_watchdog, &ScriptWatchdog::softTimeout, this, &ScriptSandbox::onSoftTimeout, Qt::DirectConnection);
    connect(m_watchdog, &ScriptWatchdog::hardTimeout, this, &ScriptSandbox::onHardTimeout, Qt::DirectConnection);
}

ScriptSandbox::~ScriptSandbox()
{
    stop();

    if (m_thread) {
        if (m_thread->isRunning()) {
            if (!m_thread->wait(5000)) {
                qCritical() << "[ScriptSandbox" << m_sandboxId << "] Thread didn't stop after 5s, giving up.";
            }
        }
        delete m_thread;
        m_thread = nullptr;
    }

    disconnect();

    delete m_watchdog;
    m_watchdog = nullptr;
}

void ScriptSandbox::setScript(const QString& script)
{
    m_script = script;
    m_isInlineScript = true;
}

void ScriptSandbox::setScriptPath(const QString& path)
{
    m_scriptPath = path;
    m_isInlineScript = false;
}

void ScriptSandbox::setTimeoutMs(int ms)
{
    m_watchdog->setTimeoutMs(ms);
}

void ScriptSandbox::start()
{
    if (m_running.exchange(true)) return;

    m_stopRequested.store(false);

    if (m_thread) {
        delete m_thread;
    }
    m_thread = QThread::create([this]() {
        runScript();
    });

    connect(m_thread, &QThread::finished, this, [this]() {
        m_running.store(false);
        if (m_watchdog) {
            m_watchdog->stop();
        }
        emit finished(m_sandboxId);
    }, Qt::QueuedConnection);

    m_watchdog->start();
    // 使用 NormalPriority 避免在 MMCSS 环境下 InheritPriority 导致 "参数错误"
    m_thread->start(QThread::NormalPriority);
}

void ScriptSandbox::stop()
{
    m_stopRequested.store(true);

    QJSEngine* engine = m_jsEngine.load();
    if (engine) {
        engine->setInterrupted(true);
    }

    m_running.store(false);

    if (m_watchdog) {
        m_watchdog->stop();
    }
}

void ScriptSandbox::forceTerminate()
{
    stop();
    m_sessionContext.store(nullptr);
    disconnect(this, nullptr, nullptr, nullptr);

    if (m_thread && m_thread->isRunning()) {
        m_thread->wait(2000);
    }

    m_running.store(false);
}

void ScriptSandbox::clearSessionContext()
{
    m_sessionContext.store(nullptr);
    stop();
}

void ScriptSandbox::setMaxTouchPoints(int max)
{
    s_maxTouchPoints = qBound(1, max, 50);
}

int ScriptSandbox::maxTouchPoints()
{
    return s_maxTouchPoints;
}

QString ScriptSandbox::resolveModulePath(const QString& modulePath)
{
    QFileInfo fileInfo(modulePath);
    if (fileInfo.isAbsolute()) {
        return modulePath;
    }

    QString basePath = m_scriptBasePath;
    if (basePath.isEmpty()) {
        basePath = QDir::currentPath() + "/keymap/scripts";
    }

    QString fullPath = QDir(basePath).absoluteFilePath(modulePath);

    if (!fullPath.endsWith(".js") && !fullPath.endsWith(".mjs")) {
        QString withJs = fullPath + ".js";
        if (QFileInfo::exists(withJs)) {
            return withJs;
        }
    }

    return fullPath;
}

void ScriptSandbox::onSoftTimeout()
{
    qWarning() << "[ScriptSandbox" << m_sandboxId << "] Soft timeout, attempting graceful interrupt...";
    QJSEngine* engine = m_jsEngine.load();
    if (engine) {
        engine->setInterrupted(true);
    }
}

void ScriptSandbox::onHardTimeout()
{
    qWarning() << "[ScriptSandbox" << m_sandboxId << "] Hard timeout, detaching sandbox...";

    // 再次尝试中断（可能上次没生效）
    QJSEngine* engine = m_jsEngine.load();
    if (engine) {
        engine->setInterrupted(true);
    }

    // 清除外部引用，切断脚本与外界的联系
    m_sessionContext.store(nullptr);
    disconnect(this, nullptr, nullptr, nullptr);
    m_running.store(false);

    // 不调用 terminate()！让线程自然结束
    // setInterrupted(true) 最终会让 JS 代码停止
}

void ScriptSandbox::doRun()
{
    runScript();
}

void ScriptSandbox::runScript()
{
    // 在工作线程中创建 JS 引擎
    QJSEngine engine;
    m_jsEngine.store(&engine);
    engine.installExtensions(QJSEngine::ConsoleExtension);

    // 创建沙箱专用 API（在工作线程中创建）
    SandboxScriptApi* api = new SandboxScriptApi(this);
    api->setJSEngine(&engine);
    api->setScriptBasePath(m_scriptBasePath);
    api->setKeyId(m_keyId);
    api->setAnchorPos(m_anchorPos);
    api->setIsPress(m_isPress);
    api->setSessionContext(m_sessionContext.load());

    // 连接信号（从工作线程的 API 转发到主线程的 Sandbox，使用 QueuedConnection）
    connect(api, &SandboxScriptApi::touchRequested, this, &ScriptSandbox::touchRequested, Qt::QueuedConnection);
    connect(api, &SandboxScriptApi::keyRequested, this, &ScriptSandbox::keyRequested, Qt::QueuedConnection);
    connect(api, &SandboxScriptApi::tipRequested, this, &ScriptSandbox::tipRequested, Qt::QueuedConnection);
    connect(api, &SandboxScriptApi::shotmodeRequested, this, &ScriptSandbox::shotmodeRequested, Qt::QueuedConnection);
    connect(api, &SandboxScriptApi::radialParamRequested, this, &ScriptSandbox::radialParamRequested, Qt::QueuedConnection);
    connect(api, &SandboxScriptApi::resetviewRequested, this, &ScriptSandbox::resetviewRequested, Qt::QueuedConnection);
    connect(api, &SandboxScriptApi::resetWheelRequested, this, &ScriptSandbox::resetWheelRequested, Qt::QueuedConnection);
    connect(api, &SandboxScriptApi::simulateKeyRequested, this, &ScriptSandbox::simulateKeyRequested, Qt::QueuedConnection);
    connect(api, &SandboxScriptApi::keyUIPosRequested, this, &ScriptSandbox::keyUIPosRequested, Qt::QueuedConnection);

    // 注册 mapi 对象
    QJSValue apiObj = engine.newQObject(api);
    engine.globalObject().setProperty("mapi", apiObj);

    // 找图 API:
    //   mapi.findImage("图片名", x1, y1, x2, y2, threshold)         -- 按坐标区域
    //   mapi.findImageByRegion("图片名", regionId, threshold)  -- 按选区编号

    // 注册 logerror 函数
    engine.globalObject().setProperty("logerror",
        engine.evaluate("(function(err) { console.error(err); })"));

    QJSValue result;

    if (m_isInlineScript) {
        result = engine.evaluate(m_script);
    } else {
        QString fullPath = resolveModulePath(m_scriptPath);
        result = engine.importModule(fullPath);
    }

    if (result.isError()) {
        QString errorMsg = QString("Sandbox %1 script error: %2")
            .arg(m_sandboxId)
            .arg(result.toString());
        if (!engine.isInterrupted()) {
            qWarning() << errorMsg;
            emit scriptError(errorMsg);
        }
    }

    // 清理
    m_jsEngine.store(nullptr);

    SessionContext* ctx = m_sessionContext.load();
    if (!m_isPress && ctx) {
        QString keyIdStr = QString::number(api->keyId());
        if (ctx->radialParamKeyId() == keyIdStr) {
            ctx->setRadialParamKeyId(QString());
            emit radialParamRequested(1.0, 1.0, 1.0, 1.0);
        }
    }

    api->disconnect();
    delete api;
}

// =============================================================
// SandboxScriptApi 实现（与 WorkerScriptApi 完全一致）
// =============================================================

SandboxScriptApi::SandboxScriptApi(ScriptSandbox* sandbox, QObject* parent)
    : QObject(parent)
    , m_sandbox(sandbox)
{
}

void SandboxScriptApi::normalizePos(double x, double y, quint16& outX, quint16& outY)
{
    double tx = qBound(0.0, x, 1.0);
    double ty = qBound(0.0, y, 1.0);
    outX = static_cast<quint16>(tx * 65535.0);
    outY = static_cast<quint16>(ty * 65535.0);
}

QPointF SandboxScriptApi::applyRandomOffset(double x, double y)
{
    int offsetLevel = qsc::ConfigCenter::instance().randomOffset();
    if (offsetLevel <= 0) {
        return QPointF(x, y);
    }

    double maxOffset = offsetLevel * 0.0003;

    double offsetX = (QRandomGenerator::global()->generateDouble() - 0.5) * 2.0 * maxOffset;
    double offsetY = (QRandomGenerator::global()->generateDouble() - 0.5) * 2.0 * maxOffset;

    double resultX = qBound(0.001, x + offsetX, 0.999);
    double resultY = qBound(0.001, y + offsetY, 0.999);

    return QPointF(resultX, resultY);
}

QVector<QPointF> SandboxScriptApi::generateSmoothPath(double sx, double sy, double ex, double ey, int steps)
{
    QVector<QPointF> path;
    if (steps < 1) steps = 1;

    double dx = ex - sx;
    double dy = ey - sy;
    double distance = std::sqrt(dx * dx + dy * dy);

    if (distance < 0.0001) {
        path.append(QPointF(ex, ey));
        return path;
    }

    int curveLevel = qsc::ConfigCenter::instance().slideCurve();

    double perpX = -dy / distance;
    double perpY = dx / distance;

    double mainDirection = (QRandomGenerator::global()->bounded(2) == 0) ? 1.0 : -1.0;
    double mainAmplitude = (curveLevel / 100.0) * 0.15 * distance;

    double secondFreq = 1.5 + QRandomGenerator::global()->generateDouble();
    double secondDirection = (QRandomGenerator::global()->bounded(2) == 0) ? 1.0 : -1.0;
    double secondAmplitude = (curveLevel / 100.0) * 0.06 * distance;

    double microFreq = 3.0 + QRandomGenerator::global()->generateDouble() * 2.0;
    double microDirection = (QRandomGenerator::global()->bounded(2) == 0) ? 1.0 : -1.0;
    double microAmplitude = (curveLevel / 100.0) * 0.02 * distance;

    double mainPhase = QRandomGenerator::global()->generateDouble() * 0.2;
    double secondPhase = QRandomGenerator::global()->generateDouble() * M_PI;
    double microPhase = QRandomGenerator::global()->generateDouble() * M_PI * 2;

    for (int i = 1; i <= steps; i++) {
        double t = static_cast<double>(i) / steps;

        double baseX = sx + dx * t;
        double baseY = sy + dy * t;

        double mainOffset = std::sin(M_PI * (t + mainPhase)) * mainAmplitude * mainDirection;
        mainOffset *= std::sin(M_PI * t);

        double secondOffset = std::sin(secondFreq * M_PI * t + secondPhase) * secondAmplitude * secondDirection;
        secondOffset *= std::sin(M_PI * t);

        double microOffset = std::sin(microFreq * M_PI * t + microPhase) * microAmplitude * microDirection;
        microOffset *= std::sin(M_PI * t);

        double totalOffset = mainOffset + secondOffset + microOffset;

        double finalX = baseX + perpX * totalOffset;
        double finalY = baseY + perpY * totalOffset;

        finalX = qBound(0.001, finalX, 0.999);
        finalY = qBound(0.001, finalY, 0.999);

        path.append(QPointF(finalX, finalY));
    }

    return path;
}

void SandboxScriptApi::click(double x, double y)
{
    if (!m_isPress) return;
    if (isInterrupted()) return;

    quint32 seqId = FastTouchSeq::next();
    quint16 nx, ny;
    double px = (x < 0) ? m_anchorPos.x() : x;
    double py = (y < 0) ? m_anchorPos.y() : y;

    QPointF randomPos = applyRandomOffset(px, py);
    normalizePos(randomPos.x(), randomPos.y(), nx, ny);

    emit touchRequested(seqId, FTA_DOWN, nx, ny);
    emit touchRequested(seqId, FTA_UP, nx, ny);
}

void SandboxScriptApi::holdpress(double x, double y)
{
    if (isInterrupted()) return;

    // 安全获取 SessionContext 指针（防止竞争条件）
    SessionContext* ctx = m_sessionContext.load();
    if (!ctx) {
        // SessionContext 已销毁，停止脚本
        if (m_sandbox) m_sandbox->stop();
        return;
    }

    double px = (x < 0) ? m_anchorPos.x() : x;
    double py = (y < 0) ? m_anchorPos.y() : y;

    QPointF randomPos = applyRandomOffset(px, py);
    quint16 nx, ny;
    normalizePos(randomPos.x(), randomPos.y(), nx, ny);

    if (m_isPress) {
        int currentCount = ctx->touchSeqCount(m_keyId);
        int maxPoints = ScriptSandbox::maxTouchPoints();
        if (currentCount >= maxPoints) {
            qWarning() << "[SandboxScriptApi] Max touch points reached for keyId:" << m_keyId
                       << "(limit:" << maxPoints << ")";
            return;
        }

        quint32 seqId = FastTouchSeq::next();
        ctx->addTouchSeq(m_keyId, seqId);
        emit touchRequested(seqId, FTA_DOWN, nx, ny);
    } else {
        if (ctx->hasTouchSeqs(m_keyId)) {
            QList<quint32> seqIds = ctx->takeTouchSeqs(m_keyId);
            for (quint32 seqId : seqIds) {
                emit touchRequested(seqId, FTA_UP, nx, ny);
            }
        }
    }
}

void SandboxScriptApi::releaseAll()
{
    // 安全获取 SessionContext 指针
    SessionContext* ctx = m_sessionContext.load();
    if (!ctx) return;

    if (ctx->hasTouchSeqs(m_keyId)) {
        QList<quint32> seqIds = ctx->takeTouchSeqs(m_keyId);
        for (quint32 seqId : seqIds) {
            emit touchRequested(seqId, FTA_UP, 32767, 32767);
        }
    }
}

void SandboxScriptApi::release()
{
    quint32 seqId = FastTouchSeq::next();
    emit touchRequested(seqId, FTA_UP, 32767, 32767);
}

void SandboxScriptApi::slide(double sx, double sy, double ex, double ey, int delayMs, int num)
{
    if (!m_isPress) return;
    if (isInterrupted()) return;
    if (num <= 0) num = 10;

    quint32 seqId = FastTouchSeq::next();
    quint16 nx, ny;

    QPointF startPos = applyRandomOffset(sx, sy);
    QPointF endPos = applyRandomOffset(ex, ey);

    QVector<QPointF> path = generateSmoothPath(startPos.x(), startPos.y(), endPos.x(), endPos.y(), num);

    normalizePos(startPos.x(), startPos.y(), nx, ny);
    emit touchRequested(seqId, FTA_DOWN, nx, ny);

    int stepTime = (path.size() > 0) ? (delayMs / path.size()) : delayMs;
    if (stepTime < 2) stepTime = 2;

    for (int i = 0; i < path.size() && !isInterrupted(); ++i) {
        sleep(stepTime);
        normalizePos(path[i].x(), path[i].y(), nx, ny);
        emit touchRequested(seqId, FTA_MOVE, nx, ny);
    }

    normalizePos(endPos.x(), endPos.y(), nx, ny);
    emit touchRequested(seqId, FTA_UP, nx, ny);
}

void SandboxScriptApi::pinch(double centerX, double centerY, double scale, int durationMs, int steps)
{
    if (!m_isPress) return;
    if (isInterrupted()) return;
    if (steps <= 0) steps = 10;
    if (scale <= 0) scale = 1.0;

    QPointF center = applyRandomOffset(centerX, centerY);

    double baseDistance = 0.1;
    double startDistance, endDistance;

    if (scale > 1.0) {
        startDistance = baseDistance;
        endDistance = baseDistance * scale;
    } else {
        startDistance = baseDistance / scale;
        endDistance = baseDistance;
    }

    quint32 seqId1 = FastTouchSeq::next();
    quint32 seqId2 = FastTouchSeq::next();

    quint16 nx1, ny1, nx2, ny2;

    double startX1 = center.x() - startDistance / 2;
    double startX2 = center.x() + startDistance / 2;
    double startY1 = center.y();
    double startY2 = center.y();

    QPointF pos1 = applyRandomOffset(startX1, startY1);
    QPointF pos2 = applyRandomOffset(startX2, startY2);

    normalizePos(pos1.x(), pos1.y(), nx1, ny1);
    normalizePos(pos2.x(), pos2.y(), nx2, ny2);
    emit touchRequested(seqId1, FTA_DOWN, nx1, ny1);
    emit touchRequested(seqId2, FTA_DOWN, nx2, ny2);

    int stepTime = durationMs / steps;
    if (stepTime < 2) stepTime = 2;
    double distanceStep = (endDistance - startDistance) / steps;

    for (int i = 1; i <= steps && !isInterrupted(); ++i) {
        sleep(stepTime);

        double currentDistance = startDistance + distanceStep * i;
        double x1 = center.x() - currentDistance / 2;
        double x2 = center.x() + currentDistance / 2;

        normalizePos(qBound(0.001, x1, 0.999), startY1, nx1, ny1);
        normalizePos(qBound(0.001, x2, 0.999), startY2, nx2, ny2);

        emit touchRequested(seqId1, FTA_MOVE, nx1, ny1);
        emit touchRequested(seqId2, FTA_MOVE, nx2, ny2);
    }

    double endX1 = center.x() - endDistance / 2;
    double endX2 = center.x() + endDistance / 2;
    QPointF endPos1 = applyRandomOffset(endX1, startY1);
    QPointF endPos2 = applyRandomOffset(endX2, startY2);
    normalizePos(endPos1.x(), endPos1.y(), nx1, ny1);
    normalizePos(endPos2.x(), endPos2.y(), nx2, ny2);
    emit touchRequested(seqId1, FTA_UP, nx1, ny1);
    emit touchRequested(seqId2, FTA_UP, nx2, ny2);
}

void SandboxScriptApi::key(const QString& keyName, int durationMs)
{
    if (!m_isPress) return;
    if (isInterrupted()) return;

    emit simulateKeyRequested(keyName, true);

    if (durationMs > 0) {
        sleep(durationMs);
    }

    emit simulateKeyRequested(keyName, false);
}

void SandboxScriptApi::sleep(int ms)
{
    if (!m_isPress) return;
    if (ms <= 0) return;

    const int checkInterval = 50;  // 增大检查间隔，减少循环次数
    int remaining = ms;

    while (remaining > 0) {
        if (isInterrupted()) break;

        int sleepTime = qMin(remaining, checkInterval);
        QThread::msleep(sleepTime);
        remaining -= sleepTime;

        // 喂狗，避免超时
        if (m_sandbox && m_sandbox->m_watchdog) {
            m_sandbox->m_watchdog->feed();
        }
    }
}

bool SandboxScriptApi::isInterrupted()
{
    return !m_sandbox || m_sandbox->m_stopRequested.load();
}

void SandboxScriptApi::stop()
{
    if (m_sandbox) {
        m_sandbox->stop();
    }
}

void SandboxScriptApi::toast(const QString& msg, int durationMs)
{
    if (!m_isPress) return;
    if (!m_sandbox || !m_sandbox->isRunning()) return;

    int duration = qMax(1, durationMs);
    int keyId = m_keyId;

    // 直接调用单例，跳过复杂的信号链
    QMetaObject::invokeMethod(qApp, [msg, duration, keyId]() {
        ScriptTipWidget::instance()->addMessage(msg, duration, keyId);
    }, Qt::QueuedConnection);
}

void SandboxScriptApi::setGlobal(const QString& key, const QJSValue& value)
{
    if (!m_isPress || isInterrupted()) return;

    SessionContext* ctx = m_sessionContext.load();
    if (!ctx) return;

    ctx->setVar(key, value.toVariant());
}

QJSValue SandboxScriptApi::getGlobal(const QString& key)
{
    if (isInterrupted()) return QJSValue();

    SessionContext* ctx = m_sessionContext.load();
    if (!ctx) return QJSValue();

    QVariant val = ctx->getVar(key);
    if (val.isValid()) {
        return m_jsEngine->toScriptValue(val);
    }
    return QJSValue();
}

QString SandboxScriptApi::resolveModulePath(const QString& modulePath)
{
    QFileInfo fileInfo(modulePath);
    if (fileInfo.isAbsolute()) {
        return modulePath;
    }

    QString basePath = m_scriptBasePath;
    if (basePath.isEmpty()) {
        basePath = QDir::currentPath() + "/keymap/scripts";
    }

    QString fullPath = QDir(basePath).absoluteFilePath(modulePath);

    if (!fullPath.endsWith(".js") && !fullPath.endsWith(".mjs")) {
        QString withJs = fullPath + ".js";
        if (QFileInfo::exists(withJs)) {
            return withJs;
        }
    }

    return fullPath;
}

QJSValue SandboxScriptApi::loadModule(const QString& modulePath)
{
    if (!m_jsEngine) {
        qWarning() << "[Sandbox loadModule] JS Engine not set!";
        return QJSValue();
    }

    QString fullPath = resolveModulePath(modulePath);

    if (m_moduleCache.contains(fullPath)) {
        return m_moduleCache[fullPath];
    }

    QJSValue module = m_jsEngine->importModule(fullPath);

    if (module.isError()) {
        qWarning() << "[Sandbox loadModule] Failed:" << fullPath << module.toString();
        return module;
    }

    m_moduleCache[fullPath] = module;
    return module;
}

void SandboxScriptApi::log(const QString& msg)
{
    qInfo() << "[Sandbox Script]" << msg;
}

void SandboxScriptApi::shotmode(bool gameMode)
{
    if (!m_isPress) return;
    emit shotmodeRequested(gameMode);
}

void SandboxScriptApi::setRadialParam(double up, double down, double left, double right)
{
    if (!m_isPress) return;

    // 安全获取 SessionContext 指针
    SessionContext* ctx = m_sessionContext.load();
    if (!ctx) return;

    ctx->setRadialParamKeyId(QString::number(m_keyId));
    m_radialParamModified = true;
    emit radialParamRequested(up, down, left, right);
}

void SandboxScriptApi::resetview()
{
    if (!m_isPress) return;
    emit resetviewRequested();
}

void SandboxScriptApi::resetwheel()
{
    if (!m_isPress) return;
    emit resetWheelRequested();
}

QVariantMap SandboxScriptApi::getmousepos()
{
    QVariantMap map;
    map["x"] = 0.0;
    map["y"] = 0.0;

    if (isInterrupted()) return map;

    // 安全获取 SessionContext 指针
    SessionContext* ctx = m_sessionContext.load();
    if (ctx) {
        QPointF pos = ctx->script_getMousePos();
        map["x"] = qRound(pos.x() * 10000.0) / 10000.0;
        map["y"] = qRound(pos.y() * 10000.0) / 10000.0;
    }

    return map;
}

QVariantMap SandboxScriptApi::getkeypos(const QString& keyName)
{
    QVariantMap map;
    map["x"] = 0.0;
    map["y"] = 0.0;
    map["valid"] = false;

    if (isInterrupted()) return map;

    // 安全获取 SessionContext 指针
    SessionContext* ctx = m_sessionContext.load();
    if (ctx) {
        QVariantMap result = ctx->script_getKeyPosByName(keyName);
        map["x"] = result.value("x", 0.0);
        map["y"] = result.value("y", 0.0);
        map["valid"] = result.value("valid", false);
    }

    return map;
}

int SandboxScriptApi::getKeyState(const QString& keyName)
{
    if (isInterrupted()) return 0;

    // 安全获取 SessionContext 指针
    SessionContext* ctx = m_sessionContext.load();
    if (ctx) {
        return ctx->script_getKeyStateByName(keyName);
    }
    return 0;
}

void SandboxScriptApi::setKeyUIPos(const QString& keyName, double x, double y, double xoffset, double yoffset)
{
    if (!m_isPress) return;
    double finalX = x + xoffset;
    double finalY = y + yoffset;
    emit keyUIPosRequested(keyName, finalX, finalY);
}

int SandboxScriptApi::getQtKey(const QString& keyName)
{
    QString k = keyName.toUpper();

    if (k == "SPACE" || k == " ") return Qt::Key_Space;
    if (k == "ENTER" || k == "RETURN") return Qt::Key_Return;
    if (k == "ESC" || k == "ESCAPE") return Qt::Key_Escape;
    if (k == "TAB") return Qt::Key_Tab;
    if (k == "BACKSPACE") return Qt::Key_Backspace;
    if (k == "SHIFT") return Qt::Key_Shift;
    if (k == "CTRL" || k == "CONTROL") return Qt::Key_Control;
    if (k == "ALT") return Qt::Key_Alt;
    if (k == "UP") return Qt::Key_Up;
    if (k == "DOWN") return Qt::Key_Down;
    if (k == "LEFT") return Qt::Key_Left;
    if (k == "RIGHT") return Qt::Key_Right;

    if (k.startsWith("F") && k.length() <= 3) {
        bool ok;
        int num = k.mid(1).toInt(&ok);
        if (ok && num >= 1 && num <= 12) {
            return Qt::Key_F1 + num - 1;
        }
    }

    if (k.length() == 1) {
        QChar c = k.at(0);
        if (c >= 'A' && c <= 'Z') return Qt::Key_A + (c.toLatin1() - 'A');
        if (c >= '0' && c <= '9') return Qt::Key_0 + (c.toLatin1() - '0');

        switch (c.toLatin1()) {
            case '`': return Qt::Key_QuoteLeft;
            case '~': return Qt::Key_AsciiTilde;
            case '-': return Qt::Key_Minus;
            case '=': return Qt::Key_Equal;
            case '[': return Qt::Key_BracketLeft;
            case ']': return Qt::Key_BracketRight;
            case '\\': return Qt::Key_Backslash;
            case ';': return Qt::Key_Semicolon;
            case '\'': return Qt::Key_Apostrophe;
            case ',': return Qt::Key_Comma;
            case '.': return Qt::Key_Period;
            case '/': return Qt::Key_Slash;
        }
    }

    return 0;
}

int SandboxScriptApi::getAndroidKeyCode(const QString& keyName)
{
    QString k = keyName.toUpper();
    if (k == "W") return AKEYCODE_W;
    if (k == "A") return AKEYCODE_A;
    if (k == "S") return AKEYCODE_S;
    if (k == "D") return AKEYCODE_D;
    if (k == "SPACE") return AKEYCODE_SPACE;
    if (k == "ENTER") return AKEYCODE_ENTER;
    if (k == "ESC") return AKEYCODE_ESCAPE;
    if (k == "BACK") return AKEYCODE_BACK;
    if (k == "HOME") return AKEYCODE_HOME;
    if (k == "MENU") return AKEYCODE_MENU;

    if (k.length() == 1) {
        char c = k.at(0).toLatin1();
        if (c >= '0' && c <= '9') return AKEYCODE_0 + (c - '0');
        if (c >= 'A' && c <= 'Z') return AKEYCODE_A + (c - 'A');
    }
    return AKEYCODE_UNKNOWN;
}

QVariantMap SandboxScriptApi::findImage(const QString& imageName,
                                         double x1, double y1,
                                         double x2, double y2,
                                         double threshold)
{
    QVariantMap result;
    result.insert("found", false);
    result.insert("x", 0.0);
    result.insert("y", 0.0);
    result.insert("confidence", 0.0);

    if (!m_isPress || isInterrupted()) return result;

#ifdef ENABLE_IMAGE_MATCHING
    QImage currentFrame = ScriptEngine::grabCurrentFrame();
    if (currentFrame.isNull()) {
        if (m_sandbox) m_sandbox->stop();
        return result;
    }

    QImage templateImage = ImageMatcher::loadTemplateImage(imageName);
    if (templateImage.isNull()) {
        qWarning() << "[Sandbox findImage] Failed to load template:" << imageName;
        return result;
    }

    QRectF searchRegion(x1, y1, x2 - x1, y2 - y1);

    ImageMatcher matcher;
    ImageMatchResult matchResult = matcher.findTemplate(currentFrame, templateImage, threshold, searchRegion);

    result.insert("found", matchResult.found);
    result.insert("x", qRound(matchResult.x * 10000.0) / 10000.0);
    result.insert("y", qRound(matchResult.y * 10000.0) / 10000.0);
    result.insert("confidence", qRound(matchResult.confidence * 10000.0) / 10000.0);
#else
    Q_UNUSED(imageName);
    Q_UNUSED(x1); Q_UNUSED(y1);
    Q_UNUSED(x2); Q_UNUSED(y2);
    Q_UNUSED(threshold);
    qWarning() << "[Sandbox findImage] Image matching is disabled (OpenCV not available)";
#endif

    return result;
}

QVariantMap SandboxScriptApi::findImageByRegion(const QString& imageName,
                                                 int regionId,
                                                 double threshold)
{
    // 通过选区 ID 查找坐标，再调用现有的 findImage
    SelectionRegion region;
    if (!SelectionRegionManager::instance().findById(regionId, region)) {
        qWarning() << "[Sandbox findImageByRegion] Region not found, id:" << regionId;
        QVariantMap result;
        result.insert("found", false);
        result.insert("x", 0.0);
        result.insert("y", 0.0);
        result.insert("confidence", 0.0);
        return result;
    }
    return findImage(imageName, region.x0, region.y0, region.x1, region.y1, threshold);
}
