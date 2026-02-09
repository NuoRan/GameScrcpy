#include "videoform.h"
#include "ui_videoform.h"
#include "toolform.h"
#include "qyuvopenglwidget.h"
#include "iconhelper.h"
#include "config.h"
#include "mousetap.h"
#include "keepratiowidget.h"
#include "KeyMapItems.h"
#include "KeyMapOverlay.h"
#include "ConfigCenter.h"
#include "ScriptTipWidget.h"
#include <QFileInfo>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QDateTime>
#include <QMap>
#include <QEventLoop>
#include <QMetaEnum>
#include <QMessageBox>
#include <QByteArray>
#include <QThread>
#include <iostream>
#include <QStyleOption>

#if defined(Q_OS_WIN32)
#include <Windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

// 新架构：直接使用 DeviceSession（不再使用 IDevice/DeviceObserver）
#include "service/DeviceSession.h"
#include "infra/FrameData.h"
#include "GameScrcpyCore.h"  // IDeviceManage

// =======================================================
// VideoForm 实现
// =======================================================

// ---------------------------------------------------------
// 构造与析构
// ---------------------------------------------------------
VideoForm::VideoForm(bool framelessWindow, bool skin, bool showToolbar, QWidget *parent) : QWidget(parent), ui(new Ui::videoForm), m_skin(skin) {
    ui->setupUi(this);
    initUI();
    updateShowSize(size());
    this->show_toolbar = showToolbar;

    // 如果启用皮肤，根据宽高比加载样式
    if (m_skin) {
        updateStyleSheet(size().height() > size().width());
    } else {
        // 无皮肤模式：应用深色样式
        applyDarkStyle();
    }
    // 设置无边框窗口
    if (framelessWindow) {
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    }
}

// 应用深色样式（与主界面一致）
void VideoForm::applyDarkStyle() {
    setStyleSheet(R"(
        QWidget#videoForm {
            background-color: #09090b;
            border: 1px solid #27272a;
            border-radius: 8px;
        }
    )");
    layout()->setContentsMargins(2, 2, 2, 2);
}

// 设置 Windows 深色标题栏
void VideoForm::setDarkTitleBar() {
#if defined(Q_OS_WIN32)
    HWND hwnd = reinterpret_cast<HWND>(winId());
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
#endif
}

VideoForm::~VideoForm() {
    // 断开信号槽连接
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
    }
    delete ui;
}

// ---------------------------------------------------------
// 会话绑定（UI 解耦核心）
// 通过此方法绑定 DeviceSession，使用纯信号槽交互
// ---------------------------------------------------------
void VideoForm::bindSession(qsc::core::DeviceSession* session) {
    // 断开旧会话的信号
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
        m_session->setFrameGrabCallback(nullptr);
    }

    m_session = session;

    if (m_session) {
        connect(m_session, &qsc::core::DeviceSession::frameAvailable,
                this, &VideoForm::onSessionFrameAvailable, Qt::DirectConnection);

        // 连接 FPS 更新信号
        connect(m_session, &qsc::core::DeviceSession::fpsUpdated,
                this, &VideoForm::onSessionFpsUpdated);

        // 连接光标状态信号
        connect(m_session, &qsc::core::DeviceSession::cursorGrabChanged,
                this, &VideoForm::onSessionCursorGrabChanged);

        // 连接脚本提示信号
        connect(m_session, &qsc::core::DeviceSession::scriptTip,
                this, &VideoForm::onSessionScriptTip);

        // 连接键位覆盖层更新信号
        connect(m_session, &qsc::core::DeviceSession::keyMapOverlayUpdated,
                this, &VideoForm::onSessionKeyMapOverlayUpdated);

        // 设置帧获取回调（用于脚本图像识别）
        // 注意：回调的生命周期由 ScriptEngine 的互斥锁机制管理
        // 在 closeEvent 中会先清除回调，再停止脚本
        m_session->setFrameGrabCallback([this]() -> QImage {
            return grabCurrentFrame();
        });
    }
}

// ---------------------------------------------------------
// DeviceSession 信号槽实现
// ---------------------------------------------------------
void VideoForm::onSessionFrameAvailable() {
    // 从 FrameQueue 消费帧
    // 注意：此方法在 Demuxer 线程执行（DirectConnection）

    if (!m_session || m_closing) return;

    // 【跳帧优化】drain 队列，只保留最新帧，避免积压导致延迟累积
    qsc::core::FrameData* frame = nullptr;
    qsc::core::FrameData* latest = nullptr;
    while ((frame = m_session->consumeFrame()) != nullptr) {
        if (latest) {
            m_session->releaseFrame(latest);  // 释放旧帧
        }
        latest = frame;
    }
    frame = latest;
    if (!frame || !frame->isValid()) {
        if (frame) m_session->releaseFrame(frame);
        return;
    }

    const int w = frame->width;
    const int h = frame->height;

    // 调试：打印分辨率变化
    static int lastW = 0, lastH = 0;
    if (w != lastW || h != lastH) {
        qInfo("[VideoForm] Frame size changed: %dx%d -> %dx%d", lastW, lastH, w, h);
        lastW = w;
        lastH = h;
    }

    // 【零拷贝直推】使用 submitFrameDirect，直接传指针给渲染器
    // 只经过一次 QueuedConnection（在 submitFrameDirect 内部），消除双重投递延迟
    // 渲染完成后通过回调归还帧，生命周期由 FramePool 引用计数管理
    m_session->retainFrame(frame);  // 增加引用计数，确保跨线程安全

    // 首帧处理和窗口尺寸更新需要在 GUI 线程
    if (!m_firstFrameReceived || QSize(w, h) != m_videoWidget->frameSize()) {
        QMetaObject::invokeMethod(this, [this, w, h]() {
            if (m_videoWidget->isHidden()) m_videoWidget->show();
            updateShowSize(QSize(w, h));
            m_videoWidget->setFrameSize(QSize(w, h));
            if (!m_firstFrameReceived) {
                m_firstFrameReceived = true;
                if (!m_currentKeyMapFile.isEmpty() && m_session) {
                    m_session->runAutoStartScripts();
                }
            }
        }, Qt::QueuedConnection);
    }

    m_videoWidget->submitFrameDirect(
        frame->dataY, frame->dataU, frame->dataV,
        w, h,
        frame->linesizeY, frame->linesizeU, frame->linesizeV,
        [session = m_session, frame]() {
            // paintGL 完成后归还帧（在 GUI 线程执行）
            // 捕获 session 指针副本，避免依赖 VideoForm::m_session 生命周期
            if (session) {
                session->releaseFrame(frame);  // retain 的引用
                session->releaseFrame(frame);  // consumeFrame 的引用
            }
        }
    );
}

void VideoForm::onSessionFpsUpdated(quint32 fps) {
    if (m_fpsLabel) {
        m_fpsLabel->setText(QString("FPS:%1").arg(fps));
    }
}

void VideoForm::onSessionCursorGrabChanged(bool grabbed) {
    // 启用/禁用鼠标捕获
    QRect rc = getGrabCursorRect();
    MouseTap::getInstance()->enableMouseEventTap(rc, grabbed);

    // 同步设置 tip 弹窗的游戏模式
    // 游戏模式下弹窗对鼠标透明，不会干扰视角控制
    ScriptTipWidget::instance()->setGameMode(grabbed);
}

void VideoForm::onSessionScriptTip(const QString& msg, int keyId, int durationMs) {
    ScriptTipWidget* tipWidget = ScriptTipWidget::instance();
    tipWidget->setParentVideoWidget(this);
    tipWidget->addMessage(msg, durationMs, keyId);
}

void VideoForm::onSessionKeyMapOverlayUpdated() {
    if (m_keyMapOverlay) {
        m_keyMapOverlay->update();
    }
}

// ---------------------------------------------------------
// 初始化UI
// 设置OpenGL渲染控件、FPS显示标签以及键位编辑视图
// ---------------------------------------------------------
void VideoForm::initUI() {
    // 加载手机皮肤资源
    if (m_skin) {
        QPixmap phone;
        if (phone.load(":/res/phone.png")) {
            m_widthHeightRatio = 1.0f * phone.width() / phone.height();
        }
#ifndef Q_OS_OSX
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
#endif
    }

    // 初始化视频渲染控件 (YUV OpenGL)
    m_videoWidget = new QYUVOpenGLWidget();
    m_videoWidget->hide();

    // 设置保持比例容器
    ui->keepRatioWidget->setWidget(m_videoWidget);
    ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);

    // FPS 显示标签
    m_fpsLabel = new QLabel(m_videoWidget);
    QFont ft;
    ft.setPointSize(15);
    ft.setWeight(QFont::Light);
    ft.setBold(true);
    m_fpsLabel->setFont(ft);
    m_fpsLabel->move(5, 15);
    m_fpsLabel->setMinimumWidth(100);
    m_fpsLabel->setStyleSheet("QLabel{color:#00FF00;}");

    // 开启鼠标追踪
    setMouseTracking(true);
    m_videoWidget->setMouseTracking(true);
    ui->keepRatioWidget->setMouseTracking(true);

    // 初始化键位编辑覆盖层
    m_keyMapEditView = new KeyMapEditView();
    m_keyMapEditView->attachTo(m_videoWidget);

    // 初始化键位提示层
    m_keyMapOverlay = new KeyMapOverlay(m_videoWidget);
    m_keyMapOverlay->setOpacity(qsc::ConfigCenter::instance().keyMapOverlayOpacity() / 100.0);
    // 根据保存的设置决定是否显示
    if (qsc::ConfigCenter::instance().keyMapOverlayVisible()) {
        // 延迟显示，等 loadKeyMap 完成后再更新
        QTimer::singleShot(500, this, [this]() {
            setKeyMapOverlayVisible(true);
        });
    } else {
        m_keyMapOverlay->hide();
    }
}

// ---------------------------------------------------------
// 触摸与按键事件发送
// 将本地坐标/事件转换为 Android 设备指令
// ---------------------------------------------------------
void VideoForm::sendTouchDown(int id, float x, float y) {
    if (!m_session) return;
    Q_UNUSED(id);

    QPoint l((int)(x * m_videoWidget->width()), (int)(y * m_videoWidget->height()));
    QMouseEvent e(QEvent::MouseButtonPress, l, l, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    m_session->mouseEvent(&e, m_frameSize, m_videoWidget->size());
}

void VideoForm::sendTouchUp(int id, float x, float y) {
    if (!m_session) return;
    Q_UNUSED(id);

    QPoint l((int)(x * m_videoWidget->width()), (int)(y * m_videoWidget->height()));
    QMouseEvent e(QEvent::MouseButtonRelease, l, l, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    m_session->mouseEvent(&e, m_frameSize, m_videoWidget->size());
}

void VideoForm::sendTouchMove(int id, float x, float y) {
    if (!m_session) return;
    Q_UNUSED(id);

    QPoint l((int)(x * m_videoWidget->width()), (int)(y * m_videoWidget->height()));
    QMouseEvent e(QEvent::MouseMove, l, l, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    m_session->mouseEvent(&e, m_frameSize, m_videoWidget->size());
}

void VideoForm::sendKeyClick(int qtKey) {
    if (!m_session) return;

    QKeyEvent e1(QEvent::KeyPress, qtKey, Qt::NoModifier);
    m_session->keyEvent(&e1, m_frameSize, m_videoWidget->size());
    QKeyEvent e2(QEvent::KeyRelease, qtKey, Qt::NoModifier);
    m_session->keyEvent(&e2, m_frameSize, m_videoWidget->size());
}

// ---------------------------------------------------------
// 获取当前视频帧 (用于图像识别)
// ---------------------------------------------------------
QImage VideoForm::grabCurrentFrame() {
    if (m_videoWidget) {
        return m_videoWidget->grabCurrentFrame();
    }
    return QImage();
}

// ---------------------------------------------------------
// 键位映射加载逻辑
// 读取JSON文件，更新内存配置，并下发脚本到 Core 库
// runAutoStart: 是否执行自动启动脚本（初始加载时为 false，等视频流准备好后再执行）
// ---------------------------------------------------------
void VideoForm::loadKeyMap(const QString& filename, bool runAutoStart) {
    if (filename.isEmpty()) return;

    // 0. 清除脚本设置的 UI 位置覆盖，恢复到键位配置的原始位置
    KeyMapOverlay::clearAllOverrides();

    if (m_keyMapEditView && m_keyMapEditView->scene()) {
        m_keyMapEditView->scene()->clear();
    }

    QFile file("keymap/" + filename);
    if (!file.open(QIODevice::ReadOnly)) return;

    QByteArray data = file.readAll();
    file.close();

    // 1. 保存当前文件名
    m_currentKeyMapFile = filename;

    // 2. 判断是否在编辑模式
    bool isInEditMode = m_keyMapEditView && m_keyMapEditView->isVisible();

    // 3. 将脚本更新到底层设备实例
    // 只有不在编辑模式且 runAutoStart=true 时才执行自动启动脚本
    if (m_session) {
        m_session->updateScript(data, runAutoStart && !isInEditMode);
    }

    // 3. 记录配置，下次启动自动加载
    Config::getInstance().setKeyMap(m_serial, filename);

    // 4. 同步工具栏UI状态
    if (m_toolForm) {
        m_toolForm->setCurrentKeyMap(filename);
    }

    // 5. 解析 JSON 并在 UI 上绘制可视化键位
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    m_currentConfigBase = root;

    KeyMapFactoryImpl factory;
    QSize sz = m_videoWidget->size().isEmpty() ? QSize(100,100) : m_videoWidget->size();

    if (root.contains("keyMapNodes")) {
        QJsonArray nodes = root["keyMapNodes"].toArray();
        for (const auto& nodeRef : nodes) {
            QJsonObject node = nodeRef.toObject();

            QString typeStr = node["type"].toString();
            KeyMapType type = KeyMapHelper::getTypeFromString(typeStr);

            if (type == KMT_STEER_WHEEL || type == KMT_SCRIPT || type == KMT_CAMERA_MOVE || type == KMT_FREE_LOOK) {
                KeyMapItemBase* item = factory.createItem(type);
                if (item) {
                    item->fromJson(node);
                    double x = 0, y = 0;
                    if (node.contains("pos")) {
                        QJsonObject p = node["pos"].toObject();
                        x = p["x"].toDouble();
                        y = p["y"].toDouble();
                    } else if (node.contains("centerPos")) {
                        QJsonObject p = node["centerPos"].toObject();
                        x = p["x"].toDouble();
                        y = p["y"].toDouble();
                    }

                    m_keyMapEditView->scene()->addItem(item);
                    item->setNormalizedPos(QPointF(x, y), sz);

                    if (auto* w = dynamic_cast<KeyMapItemSteerWheel*>(item)) w->updateSubItemsPos();
                }
            }
        }
    }

    // 6. 更新键位提示层
    if (m_keyMapOverlay && m_keyMapOverlay->isVisible()) {
        updateKeyMapOverlay();
    }
}

// ---------------------------------------------------------
// 键位映射保存逻辑
// 检测按键冲突，生成 JSON 并写入文件
// ---------------------------------------------------------
void VideoForm::saveKeyMap() {
    if (m_currentKeyMapFile.isEmpty()) m_currentKeyMapFile = "default.json";
    if (!m_keyMapEditView || !m_keyMapEditView->scene()) return;

    QList<QGraphicsItem*> items = m_keyMapEditView->scene()->items();

    // 1. 重置冲突状态
    for (auto i : items) {
        if(auto* b = dynamic_cast<KeyMapItemBase*>(i)) {
            b->setConflicted(false);
            if(auto* w = dynamic_cast<KeyMapItemSteerWheel*>(b)) {
                for(int j=0; j<4; j++) w->setSubItemConflicted(j, false);
            }
        }
    }

    // 2. 检测按键冲突
    QMap<QString, int> keyCount;
    QMap<QString, QList<QPair<KeyMapItemBase*, int>>> owners;

    for (auto g : items) {
        if (auto* item = dynamic_cast<KeyMapItemBase*>(g)) {
            if (auto* w = dynamic_cast<KeyMapItemSteerWheel*>(item)) {
                QString keys[] = {w->getUpKey(), w->getDownKey(), w->getLeftKey(), w->getRightKey()};
                for(int i=0; i<4; i++) {
                    if(!keys[i].isEmpty()) {
                        keyCount[keys[i]]++;
                        owners[keys[i]].append({item, i});
                    }
                }
            } else if (auto* s = dynamic_cast<KeyMapItemScript*>(item)) {
                QString k = s->getKey();
                if (!k.isEmpty()) {
                    keyCount[k]++;
                    owners[k].append({item, -1});
                }
            } else if (auto* c = dynamic_cast<KeyMapItemCamera*>(item)) {
                QString k = c->getKey();
                if (!k.isEmpty()) {
                    keyCount[k]++;
                    owners[k].append({item, -1});
                }
            } else if (auto* fl = dynamic_cast<KeyMapItemFreeLook*>(item)) {
                QString k = fl->getKey();
                if (!k.isEmpty()) {
                    keyCount[k]++;
                    owners[k].append({item, -1});
                }
            }
        }
    }

    bool conflict = false;
    for (auto it = keyCount.begin(); it != keyCount.end(); ++it) {
        if (it.value() > 1) {
            conflict = true;
            for (auto o : owners[it.key()]) {
                if (o.second == -1) o.first->setConflicted(true);
                else dynamic_cast<KeyMapItemSteerWheel*>(o.first)->setSubItemConflicted(o.second, true);
            }
        }
    }

    if (conflict) {
        QMessageBox::warning(this, "警告", "按键设置冲突，请修改红色标记的按键！");
        return;
    }

    // 3. 检测空热键
    bool hasEmptyKey = false;
    for (auto g : items) {
        if (auto* item = dynamic_cast<KeyMapItemBase*>(g)) {
            if (auto* w = dynamic_cast<KeyMapItemSteerWheel*>(item)) {
                if (w->getUpKey().isEmpty() || w->getDownKey().isEmpty() ||
                    w->getLeftKey().isEmpty() || w->getRightKey().isEmpty()) {
                    hasEmptyKey = true;
                    w->setConflicted(true);  // 用红色标记
                }
            } else if (auto* s = dynamic_cast<KeyMapItemScript*>(item)) {
                if (s->getKey().isEmpty()) {
                    hasEmptyKey = true;
                    s->setConflicted(true);
                }
            } else if (auto* c = dynamic_cast<KeyMapItemCamera*>(item)) {
                if (c->getKey().isEmpty()) {
                    hasEmptyKey = true;
                    c->setConflicted(true);
                }
            } else if (auto* fl = dynamic_cast<KeyMapItemFreeLook*>(item)) {
                if (fl->getKey().isEmpty()) {
                    hasEmptyKey = true;
                    fl->setConflicted(true);
                }
            }
        }
    }

    if (hasEmptyKey) {
        QMessageBox::warning(this, "警告", "存在未设置热键的组件，请为红色标记的组件设置热键！");
        return;
    }

    // 3. 序列化并保存
    QJsonObject root = m_currentConfigBase;
    QJsonArray nodes;
    QJsonObject mouseMoveMap;

    for (auto g : items) {
        if (auto* item = dynamic_cast<KeyMapItemBase*>(g)) {
            QJsonObject d = item->toJson();
            nodes.append(d);
        }
    }

    root["keyMapNodes"] = nodes;
    root["mouseMoveMap"] = mouseMoveMap;

    QFile file("keymap/" + m_currentKeyMapFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();

        // 保存后重新加载以应用更改
        loadKeyMap(m_currentKeyMapFile);
    }
}

// ---------------------------------------------------------
// 鼠标交互事件处理
// 负责处理窗口拖拽、点击操作映射到手机
// ---------------------------------------------------------
void VideoForm::mousePressEvent(QMouseEvent *e) {
    if (m_keyMapEditView && m_keyMapEditView->isVisible()) return;

    // 记录按下状态（仅用于窗口拖拽判断，不阻止事件）
    m_pressedButtons |= e->button();

    // 快速判断是否在视频区域内
    QRect videoRect = m_videoWidget->geometry();
    if (videoRect.contains(e->pos())) {
        if(!m_session) return;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF localPos = m_videoWidget->mapFrom(this, e->pos());
        QMouseEvent ne(e->type(), localPos, e->globalPos(), e->button(), e->buttons(), e->modifiers());
#else
        QPointF localPos = m_videoWidget->mapFrom(this, e->position().toPoint());
        QMouseEvent ne(e->type(), localPos, e->globalPosition(), e->button(), e->buttons(), e->modifiers());
#endif
        m_session->mouseEvent(&ne, m_frameSize, m_videoWidget->size());
    } else {
        // 在视频区域外：处理窗口拖拽
        if (e->button()==Qt::LeftButton) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
            m_dragPosition = e->globalPos() - frameGeometry().topLeft();
#else
            m_dragPosition = e->globalPosition().toPoint() - frameGeometry().topLeft();
#endif
            e->accept();
        }
    }
}

void VideoForm::mouseReleaseEvent(QMouseEvent *e) {
    if (m_keyMapEditView && m_keyMapEditView->isVisible()) return;

    // 清除按下状态（仅用于窗口拖拽判断，不阻止事件）
    m_pressedButtons &= ~e->button();

    if(m_dragPosition.isNull()){
        if(!m_session) return;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF localPos = m_videoWidget->mapFrom(this, e->pos());
        QMouseEvent ne(e->type(), localPos, e->globalPos(), e->button(), e->buttons(), e->modifiers());
#else
        QPointF localPos = m_videoWidget->mapFrom(this, e->position().toPoint());
        QMouseEvent ne(e->type(), localPos, e->globalPosition(), e->button(), e->buttons(), e->modifiers());
#endif
        m_session->mouseEvent(&ne, m_frameSize, m_videoWidget->size());
    } else {
        m_dragPosition = QPoint(0,0);
    }
}

void VideoForm::mouseMoveEvent(QMouseEvent *e) {
    if (m_keyMapEditView && m_keyMapEditView->isVisible()) return;

    QRect videoRect = m_videoWidget->geometry();
    if(videoRect.contains(e->pos())){
        if(!m_session) return;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF localPos = m_videoWidget->mapFrom(this, e->pos());
        QMouseEvent ne(e->type(), localPos, e->globalPos(), e->button(), e->buttons(), e->modifiers());
#else
        QPointF localPos = m_videoWidget->mapFrom(this, e->position().toPoint());
        QMouseEvent ne(e->type(), localPos, e->globalPosition(), e->button(), e->buttons(), e->modifiers());
#endif
        m_session->mouseEvent(&ne, m_frameSize, m_videoWidget->size());
    } else if(!m_dragPosition.isNull() && (e->buttons() & Qt::LeftButton)){
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        move(e->globalPos() - m_dragPosition);
#else
        move(e->globalPosition().toPoint() - m_dragPosition);
#endif
        e->accept();
    }
}

void VideoForm::mouseDoubleClickEvent(QMouseEvent *e) {
    if (m_keyMapEditView && m_keyMapEditView->isVisible()) return;

    // 双击非视频区域去除黑边
    if(e->button()==Qt::LeftButton && !m_videoWidget->geometry().contains(e->pos())) {
        if(!isMaximized()) removeBlackRect();
        return;  // 已处理，不再转发
    }
    // 右键双击熄屏/亮屏
    if(e->button()==Qt::RightButton && m_session && !m_session->isCurrentCustomKeymap()) {
        m_session->postPower();  // 双击视为亮屏/熄屏
        return;  // 已处理，不再转发
    }
    // 这导致快速连击时大约一半的点击被"吃掉"，严重影响响应速度
    // 解决方案：将双击事件当作 Press 事件处理
    if(m_videoWidget->geometry().contains(e->pos())){
        if(!m_session) return;
        // 转换事件类型：MouseButtonDblClick -> MouseButtonPress
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF global = e->globalPos();
#else
        QPointF global = e->globalPosition();
#endif
        QMouseEvent pressEvent(QEvent::MouseButtonPress, m_videoWidget->mapFrom(this, e->pos()), global, e->button(), e->buttons(), e->modifiers());
        m_session->mouseEvent(&pressEvent, m_frameSize, m_videoWidget->size());
    }
}

void VideoForm::wheelEvent(QWheelEvent *e) {
    if (m_keyMapEditView && m_keyMapEditView->isVisible()) return;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if(m_videoWidget->geometry().contains(e->position().toPoint())) {
        if(!m_session) return;
        QWheelEvent we(m_videoWidget->mapFrom(this, e->position().toPoint()), e->globalPosition(), e->pixelDelta(), e->angleDelta(), e->buttons(), e->modifiers(), e->phase(), e->inverted());
        m_session->wheelEvent(&we, m_frameSize, m_videoWidget->size());
    }
#else
    if(m_videoWidget->geometry().contains(e->pos())) {
        if(!m_session) return;
        QWheelEvent we(m_videoWidget->mapFrom(this, e->pos()), e->globalPosF(), e->pixelDelta(), e->angleDelta(), e->delta(), e->orientation(), e->buttons(), e->modifiers(), e->phase(), e->source(), e->inverted());
        m_session->wheelEvent(&we, m_frameSize, m_videoWidget->size());
    }
#endif
}

// ---------------------------------------------------------
// 键盘事件处理
// 转发按键到手机，处理全屏退出
// ---------------------------------------------------------
void VideoForm::keyPressEvent(QKeyEvent *e) {
    if (!m_session) return;

    if (Qt::Key_Escape == e->key() && !e->isAutoRepeat() && isFullScreen()) {
        switchFullScreen();
    }

    m_session->keyEvent(e, m_frameSize, m_videoWidget->size());
}

void VideoForm::keyReleaseEvent(QKeyEvent *e) {
    if (!m_session) return;
    m_session->keyEvent(e, m_frameSize, m_videoWidget->size());
}

// ---------------------------------------------------------
// 辅助功能函数
// ---------------------------------------------------------

// 切换键位编辑模式
void VideoForm::onKeyMapEditModeToggled(bool active) {
    if (m_keyMapEditView) {
        active ? m_keyMapEditView->show() : m_keyMapEditView->hide();
    }

    // 退出编辑时强制获取焦点
    if (!active) {
        this->setFocus();
        this->activateWindow();
    }
}

// 获取光标抓取区域
QRect VideoForm::getGrabCursorRect() {
#if defined(Q_OS_WIN32)
    QRect rc = QRect(ui->keepRatioWidget->mapToGlobal(m_videoWidget->pos()), m_videoWidget->size());
    rc.setTopLeft(rc.topLeft() * m_videoWidget->devicePixelRatioF());
    rc.setBottomRight(rc.bottomRight() * m_videoWidget->devicePixelRatioF());
#else
    QRect rc = m_videoWidget->geometry();
#endif
    return rc.adjusted(10, 10, -20, -20);
}

const QSize &VideoForm::frameSize() { return m_frameSize; }

void VideoForm::resizeSquare() {
    resize(ui->keepRatioWidget->goodSize());
}

void VideoForm::removeBlackRect() {
    resize(ui->keepRatioWidget->goodSize());
}

void VideoForm::showFPS(bool show) {
    if (m_fpsLabel) m_fpsLabel->setVisible(show);
}

// 更新渲染画面
void VideoForm::updateRender(int w, int h, uint8_t* y, uint8_t* u, uint8_t* v, int ly, int lu, int lv) {
    if (m_videoWidget->isHidden()) m_videoWidget->show();
    updateShowSize(QSize(w, h));
    m_videoWidget->setFrameSize(QSize(w, h));
    m_videoWidget->updateTextures(y, u, v, ly, lu, lv);

    // 收到第一帧后执行自动启动脚本
    if (!m_firstFrameReceived) {
        m_firstFrameReceived = true;
        // 重新加载当前键位并执行自动启动脚本
        if (!m_currentKeyMapFile.isEmpty() && m_session) {
            m_session->runAutoStartScripts();
        }
    }
}

// 设置设备序列号并加载上次的键位配置
void VideoForm::setSerial(const QString &s) {
    m_serial = s;
    m_firstFrameReceived = false;  // 重置状态，等待视频流准备好

    QString savedKeyMap = Config::getInstance().getKeyMap(m_serial);
    if (!savedKeyMap.isEmpty()) {
        // 加载键位但不执行自动启动脚本，等视频流准备好后再执行
        loadKeyMap(savedKeyMap, false);
    }
    // 注：帧获取回调、脚本tip信号、键位覆盖层更新信号的连接已移至 bindDevice()
}

// 显示/隐藏工具栏
void VideoForm::showToolForm(bool s) {
    if (!m_toolForm) {
        m_toolForm = new ToolForm(this, ToolForm::AP_OUTSIDE_RIGHT);
        m_toolForm->setSerial(m_serial);
        connect(m_toolForm, &ToolForm::keyMapEditModeToggled, this, &VideoForm::onKeyMapEditModeToggled);
        connect(m_toolForm, &ToolForm::keyMapChanged, this, [this](const QString& keyMapPath) {
            loadKeyMap(keyMapPath, true);  // 用户切换按键映射时，执行自动启动脚本
        });
        connect(m_toolForm, &ToolForm::keyMapSaveRequested, this, &VideoForm::saveKeyMap);
        connect(m_toolForm, &ToolForm::keyMapOverlayToggled, this, &VideoForm::setKeyMapOverlayVisible);
        connect(m_toolForm, &ToolForm::keyMapOverlayOpacityChanged, this, [this](int opacity) {
            if (m_keyMapOverlay) {
                m_keyMapOverlay->setOpacity(opacity / 100.0);
            }
        });
        connect(m_toolForm, &ToolForm::scriptTipOpacityChanged, this, [this](int opacity) {
            ScriptTipWidget::instance()->setOpacityLevel(opacity);
        });

        if (!m_currentKeyMapFile.isEmpty()) {
            m_toolForm->setCurrentKeyMap(m_currentKeyMapFile);
        }
    }
    m_toolForm->move(pos().x() + width(), pos().y() + 30);
    m_toolForm->setVisible(s);
}

void VideoForm::moveCenter() {
    move(QApplication::primaryScreen()->availableGeometry().center() - rect().center());
}

QRect VideoForm::getScreenRect() {
    return QApplication::primaryScreen()->availableGeometry();
}

void VideoForm::updateStyleSheet(bool v) {
    setStyleSheet(v ? R"(#videoForm{border-image:url(:/image/videoform/phone-v.png) 150px 65px 85px 65px;border-width:150px 65px 85px 65px;})"
                    : R"(#videoForm{border-image:url(:/image/videoform/phone-h.png) 65px 85px 65px 150px;border-width:65px 85px 65px 150px;})");
    layout()->setContentsMargins(getMargins(v));
}

QMargins VideoForm::getMargins(bool v) {
    return v ? QMargins(10, 68, 12, 62) : QMargins(68, 12, 62, 10);
}

void VideoForm::updateShowSize(const QSize &s) {
    if (m_frameSize != s) {
        m_frameSize = s;
        m_widthHeightRatio = 1.0f * s.width() / s.height();
        ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);

        bool v = m_widthHeightRatio < 1.0f;

        // 如果已有用户设置的窗口位置，不强制 resize（保持用户设置的大小）
        if (m_hasUserGeometry) {
            // 只更新样式（横竖屏切换）和键位提示层
            if (m_skin) updateStyleSheet(v);
        } else {
            QSize ss = s;
            if (m_skin) {
                ss.setWidth(ss.width() + getMargins(v).left() + getMargins(v).right());
                ss.setHeight(ss.height() + getMargins(v).top() + getMargins(v).bottom());
            }
            if (ss != size()) resize(ss);
        }

        // 无论如何都需要更新键位提示层（处理旋转）
        // 使用延时确保 videoWidget 大小已更新
        QTimer::singleShot(100, this, [this]() {
            if (m_keyMapOverlay && m_keyMapOverlay->isVisible()) {
                m_keyMapOverlay->resize(m_videoWidget->size());
                updateKeyMapOverlay();
            }
        });
    }
}

// 切换全屏模式
void VideoForm::switchFullScreen() {
    if (isFullScreen()) {
        // 退出全屏：恢复 Fit 模式
        ui->keepRatioWidget->setScaleMode(KeepRatioWidget::FitMode);
        if (m_widthHeightRatio > 1.0f) ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);
        showNormal();
        resize(m_normalSize);
        move(m_fullScreenBeforePos);
        if (m_skin) updateStyleSheet(m_frameSize.height() > m_frameSize.width());
        showToolForm(this->show_toolbar);
#ifdef Q_OS_WIN32
        ::SetThreadExecutionState(ES_CONTINUOUS);
#endif
    } else {
        // 进入全屏：使用 Fit 模式（保持比例，可能有黑边）
        m_normalSize = size();
        m_fullScreenBeforePos = pos();
        showToolForm(false);
        if (m_skin) layout()->setContentsMargins(0, 0, 0, 0);
        ui->keepRatioWidget->setScaleMode(KeepRatioWidget::FitMode);
        showFullScreen();
#ifdef Q_OS_WIN32
        ::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif
    }
}

bool VideoForm::isHost() {
    return m_toolForm ? m_toolForm->isHost() : false;
}

// 旧的 DeviceObserver 接口方法已删除，使用 onSessionXxx 信号槽替代

void VideoForm::staysOnTop(bool top) {
    bool needShow = false;
    if (isVisible()) needShow = true;
    setWindowFlag(Qt::WindowStaysOnTopHint, top);
    if (m_toolForm) m_toolForm->setWindowFlag(Qt::WindowStaysOnTopHint, top);
    if (needShow) show();
}

void VideoForm::paintEvent(QPaintEvent *e) {
    Q_UNUSED(e);
    QStyleOption o;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    o.initFrom(this);
#else
    o.init(this);
#endif
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &o, &p, this);
}

void VideoForm::showEvent(QShowEvent *e) {
    Q_UNUSED(e);
    // 窗口首次显示后，允许保存窗口位置
    if (m_initializing) {
        m_initializing = false;
        // 窗口首次显示时设置深色标题栏（确保 HWND 有效）
        setDarkTitleBar();
    }
    if (!isFullScreen() && show_toolbar) {
        QTimer::singleShot(500, this, [this](){ showToolForm(show_toolbar); });
    }
}

void VideoForm::resizeEvent(QResizeEvent *e) {
    Q_UNUSED(e);
    saveWindowGeometry();
    // 同步更新键位提示层大小
    if (m_keyMapOverlay && m_keyMapOverlay->isVisible()) {
        m_keyMapOverlay->resize(m_videoWidget->size());
        updateKeyMapOverlay();
    }
}

void VideoForm::moveEvent(QMoveEvent *e) {
    Q_UNUSED(e);
    saveWindowGeometry();
}

void VideoForm::saveWindowGeometry() {
    if (m_serial.isEmpty() || isFullScreen() || m_restoringGeometry || m_initializing) return;
    Config::getInstance().setRect(m_serial, geometry());
}

void VideoForm::restoreWindowGeometry() {
    if (m_serial.isEmpty()) return;
    QRect rc = Config::getInstance().getRect(m_serial);
    if (rc.isValid()) {
        m_restoringGeometry = true;
        setGeometry(rc);
        m_restoringGeometry = false;
        m_hasUserGeometry = true;  // 标记有用户设置的窗口位置
    }
}

void VideoForm::closeEvent(QCloseEvent *e) {
    Q_UNUSED(e);

    // 防止重复处理
    if (m_closing) {
        return;
    }
    m_closing = true;

    // 【重要】先释放渲染器持有的帧，此时 m_session 还有效
    // 避免析构时回调访问已空的 m_session
    if (m_videoWidget) {
        m_videoWidget->discardPendingFrame();
    }

    if (m_session) {
        m_session->setFrameGrabCallback(nullptr);
        m_session->resetScriptState();
        m_session->resetAllTouchPoints();
        bindSession(nullptr);
    }
    if (!m_serial.isEmpty()) {
        qsc::IDeviceManage::getInstance().disconnectDevice(m_serial);
    }

    // 标记自己需要删除
    deleteLater();
}

void VideoForm::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);

    if (event->type() == QEvent::ActivationChange) {
        if (!isActiveWindow() && m_session) {
            // 窗口失去焦点，通知底层重置输入状态
            m_session->onWindowFocusLost();
            // 同时释放全部触摸点（防止脚本触摸点泄漏）
            m_session->resetAllTouchPoints();
        }
    }
}

// ---------------------------------------------------------
// 键位提示层控制
// ---------------------------------------------------------
void VideoForm::setKeyMapOverlayVisible(bool visible) {
    if (m_keyMapOverlay) {
        if (visible) {
            updateKeyMapOverlay();
            m_keyMapOverlay->resize(m_videoWidget->size());
            m_keyMapOverlay->show();
            m_keyMapOverlay->raise();
        } else {
            m_keyMapOverlay->hide();
        }
        // 保存显示状态
        qsc::ConfigCenter::instance().setKeyMapOverlayVisible(visible);
    }
}

bool VideoForm::isKeyMapOverlayVisible() const {
    return m_keyMapOverlay && m_keyMapOverlay->isVisible();
}

void VideoForm::updateKeyMapOverlay() {
    if (!m_keyMapOverlay || !m_keyMapEditView || !m_keyMapEditView->scene()) return;

    QList<KeyMapOverlay::KeyInfo> infos;
    QList<QGraphicsItem*> items = m_keyMapEditView->scene()->items();

    for (auto item : items) {
        auto* base = dynamic_cast<KeyMapItemBase*>(item);
        if (!base) continue;

        KeyMapOverlay::KeyInfo info;
        QSize sz = m_videoWidget->size();
        QPointF normPos = base->getNormalizedPos(sz);
        info.pos = normPos;

        // 根据类型设置信息
        if (auto* script = dynamic_cast<KeyMapItemScript*>(base)) {
            info.type = "script";
            info.label = script->getKey();
        } else if (auto* wheel = dynamic_cast<KeyMapItemSteerWheel*>(base)) {
            info.type = "steerWheel";
            info.label = "";
            // 获取轮盘半径
            QJsonObject json = wheel->toJson();
            double leftDist = json["leftOffset"].toDouble(0.1);
            info.size = QSizeF(leftDist * 2, leftDist * 2);
            // 获取 WASD 子按键
            KeyMapOverlay::KeyInfo up, down, left, right;
            up.type = "up"; up.label = wheel->getUpKey();
            down.type = "down"; down.label = wheel->getDownKey();
            left.type = "left"; left.label = wheel->getLeftKey();
            right.type = "right"; right.label = wheel->getRightKey();
            info.subKeys << up << down << left << right;
        } else if (auto* camera = dynamic_cast<KeyMapItemCamera*>(base)) {
            info.type = "camera";
            info.label = "视角";
        } else if (auto* freeLook = dynamic_cast<KeyMapItemFreeLook*>(base)) {
            info.type = "freeLook";
            info.label = freeLook->getKey();
        } else {
            continue;  // 跳过未知类型
        }

        infos.append(info);
    }

    m_keyMapOverlay->setKeyInfos(infos);
}
