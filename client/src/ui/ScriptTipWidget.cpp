#include "ScriptTipWidget.h"
#include <QPainter>
#include <QDateTime>
#include <QApplication>
#include <QScreen>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// 单例实例
ScriptTipWidget* ScriptTipWidget::s_instance = nullptr;

// ---------------------------------------------------------
// 单例获取
// ---------------------------------------------------------
ScriptTipWidget* ScriptTipWidget::instance(QWidget* parent)
{
    if (!s_instance) {
        s_instance = new ScriptTipWidget(parent);
    }
    return s_instance;
}

void ScriptTipWidget::destroyInstance()
{
    if (s_instance) {
        delete s_instance;
        s_instance = nullptr;
    }
}

// ---------------------------------------------------------
// 构造函数
// ---------------------------------------------------------
ScriptTipWidget::ScriptTipWidget(QWidget* parent)
    : QWidget(parent)
{
    // 设置窗口属性：无边框、不获取焦点、透明背景
    // 不使用 WindowStaysOnTopHint，而是跟随父视频窗口的激活状态
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);  // 不获取焦点

    // 创建垂直布局
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(4);
    setLayout(m_layout);

    // 设置最小/最大宽度
    setMinimumWidth(200);
    setMaximumWidth(400);

    // 恢复保存的位置
    restorePosition();

    // 初始隐藏
    hide();
}

ScriptTipWidget::~ScriptTipWidget()
{
    savePosition();
    clearAll();
}

// ---------------------------------------------------------
// 添加消息
// ---------------------------------------------------------
void ScriptTipWidget::addMessage(const QString& message, int durationMs, int keyId)
{
    if (message.isEmpty()) return;

    // 自动绑定到当前活动的视频窗口（QPointer 保护，不会访问已销毁对象）
    if (!m_parentVideo || !m_parentVideo->isVisible()) {
        const auto widgets = QApplication::topLevelWidgets();
        for (QWidget* w : widgets) {
            if (w && w->inherits("VideoForm") && w->isVisible()) {
                setParentVideoWidget(w);
                break;
            }
        }
    }

    // 如果有有效的 keyId（非零），查找并更新已存在的消息
    if (keyId != 0) {
        for (TipMessage* existingMsg : m_messages) {
            if (existingMsg->keyId == keyId) {
                // 更新已存在的消息
                existingMsg->text = message;
                existingMsg->label->setText(message);
                existingMsg->durationMs = durationMs;
                existingMsg->createTime = QDateTime::currentMSecsSinceEpoch();

                // 更新倒计时显示
                if (durationMs > 0) {
                    existingMsg->countdownLabel->setText(QString("%1s").arg(durationMs / 1000));
                    existingMsg->countdownLabel->show();
                } else {
                    existingMsg->countdownLabel->hide();
                }

                // 重置关闭定时器
                if (existingMsg->timer) {
                    existingMsg->timer->stop();
                    if (durationMs > 0) {
                        existingMsg->timer->setInterval(durationMs);
                        existingMsg->timer->start();
                    }
                } else if (durationMs > 0) {
                    existingMsg->timer = new QTimer(this);
                    existingMsg->timer->setSingleShot(true);
                    existingMsg->timer->setInterval(durationMs);
                    connect(existingMsg->timer, &QTimer::timeout, this, [this, existingMsg]() {
                        removeMessage(existingMsg);
                    });
                    existingMsg->timer->start();
                }

                // 重置倒计时更新定时器
                if (existingMsg->countdownTimer) {
                    existingMsg->countdownTimer->stop();
                    if (durationMs > 0) {
                        existingMsg->countdownTimer->start();
                    }
                } else if (durationMs > 0) {
                    existingMsg->countdownTimer = new QTimer(this);
                    existingMsg->countdownTimer->setInterval(1000);
                    connect(existingMsg->countdownTimer, &QTimer::timeout, this, [existingMsg]() {
                        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - existingMsg->createTime;
                        int remaining = qMax(0, (existingMsg->durationMs - static_cast<int>(elapsed)) / 1000);
                        existingMsg->countdownLabel->setText(QString("%1s").arg(remaining));
                    });
                    existingMsg->countdownTimer->start();
                }

                updateLayout();
                return;
            }
        }
    }

    // 限制最大消息数
    while (m_messages.size() >= MAX_MESSAGES) {
        removeMessage(m_messages.first());
    }

    // 创建消息项
    TipMessage* msg = new TipMessage();
    msg->text = message;
    msg->durationMs = durationMs;
    msg->createTime = QDateTime::currentMSecsSinceEpoch();
    msg->keyId = keyId;

    // 创建容器
    msg->container = new QWidget(this);
    msg->container->setStyleSheet(
        "QWidget {"
        "  background-color: rgba(39, 39, 42, 220);"
        "  border: 1px solid rgba(99, 102, 241, 150);"
        "  border-radius: 6px;"
        "}"
    );
    // 安装事件过滤器，用于拖动
    msg->container->installEventFilter(this);

    QVBoxLayout* containerLayout = new QVBoxLayout(msg->container);
    containerLayout->setContentsMargins(8, 6, 8, 6);
    containerLayout->setSpacing(4);

    // 顶部行：倒计时（关闭按钮单独处理）
    QHBoxLayout* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(4);

    // 倒计时标签
    msg->countdownLabel = new QLabel(msg->container);
    msg->countdownLabel->setStyleSheet(
        "QLabel { color: rgba(156, 163, 175, 200); font-size: 10px; background: transparent; border: none; }"
    );
    if (durationMs > 0) {
        msg->countdownLabel->setText(QString("%1s").arg(durationMs / 1000));
    }
    topRow->addWidget(msg->countdownLabel);
    topRow->addStretch();

    // 占位，给关闭按钮留空间
    QWidget* spacer = new QWidget(msg->container);
    spacer->setFixedSize(16, 16);
    topRow->addWidget(spacer);

    containerLayout->addLayout(topRow);

    // 内容标签
    msg->label = new QLabel(message, msg->container);
    msg->label->setWordWrap(true);
    msg->label->setStyleSheet(
        "QLabel { color: #fafafa; font-size: 12px; background: transparent; border: none; }"
    );
    containerLayout->addWidget(msg->label);

    // 添加到布局
    m_layout->addWidget(msg->container);

    // 关闭按钮：直接作为 ScriptTipWidget 的子控件（不受容器透明影响）
    msg->closeBtn = new QPushButton("×", this);
    msg->closeBtn->setFixedSize(18, 18);
    msg->closeBtn->setCursor(Qt::PointingHandCursor);
    msg->closeBtn->setStyleSheet(
        "QPushButton {"
        "  color: rgba(156, 163, 175, 220);"
        "  background: rgba(39, 39, 42, 200);"
        "  border: 1px solid rgba(99, 102, 241, 100);"
        "  border-radius: 9px;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  color: #ffffff;"
        "  background: #ef4444;"
        "  border-color: #ef4444;"
        "}"
    );
    connect(msg->closeBtn, &QPushButton::clicked, this, [this, msg]() {
        removeMessage(msg);
    });
    msg->closeBtn->show();

    // 游戏模式下关闭按钮也透明
    if (m_gameMode) {
        msg->closeBtn->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    // 如果有时间限制，设置定时器
    if (durationMs > 0) {
        // 关闭定时器
        msg->timer = new QTimer(this);
        msg->timer->setSingleShot(true);
        msg->timer->setInterval(durationMs);
        connect(msg->timer, &QTimer::timeout, this, [this, msg]() {
            removeMessage(msg);
        });
        msg->timer->start();

        // 倒计时更新定时器（每秒更新）
        msg->countdownTimer = new QTimer(this);
        msg->countdownTimer->setInterval(1000);
        connect(msg->countdownTimer, &QTimer::timeout, this, [msg]() {
            qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - msg->createTime;
            int remaining = qMax(0, (msg->durationMs - static_cast<int>(elapsed)) / 1000);
            msg->countdownLabel->setText(QString("%1s").arg(remaining));
        });
        msg->countdownTimer->start();
    } else {
        msg->timer = nullptr;
        msg->countdownTimer = nullptr;
        msg->countdownLabel->hide();
    }

    m_messages.append(msg);

    // 根据父窗口状态设置置顶
    bool shouldStayOnTop = !m_parentVideo || m_parentVideo->isActiveWindow();
    bool currentlyOnTop = windowFlags() & Qt::WindowStaysOnTopHint;

    if (shouldStayOnTop != currentlyOnTop) {
        if (shouldStayOnTop) {
            setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
        } else {
            setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
        }
    }

    if (!isVisible()) {
        show();
    }

    // 更新布局（在 show 之后，确保容器有正确的几何信息）
    updateLayout();

    // 延迟再更新一次，确保关闭按钮位置正确
    QTimer::singleShot(0, this, [this]() {
        updateLayout();
    });

    raise();
    applyOpacity();
}

// ---------------------------------------------------------
// 移除消息
// ---------------------------------------------------------
void ScriptTipWidget::removeMessage(TipMessage* msg)
{
    if (!msg) return;

    // 从列表移除
    m_messages.removeOne(msg);

    // 停止定时器
    if (msg->timer) {
        msg->timer->stop();
        msg->timer->deleteLater();
    }
    if (msg->countdownTimer) {
        msg->countdownTimer->stop();
        msg->countdownTimer->deleteLater();
    }

    // 删除关闭按钮（它是 ScriptTipWidget 的直接子控件）
    if (msg->closeBtn) {
        msg->closeBtn->deleteLater();
    }

    // 移除容器（会自动删除容器内的子部件）
    if (msg->container) {
        m_layout->removeWidget(msg->container);
        msg->container->deleteLater();
    }

    delete msg;

    // 如果没有消息了，隐藏窗口
    if (m_messages.isEmpty()) {
        hide();
    } else {
        updateLayout();
    }
}

// ---------------------------------------------------------
// 清空所有消息
// ---------------------------------------------------------
void ScriptTipWidget::clearAll()
{
    while (!m_messages.isEmpty()) {
        removeMessage(m_messages.first());
    }
    hide();
}

// ---------------------------------------------------------
// 更新布局
// ---------------------------------------------------------
void ScriptTipWidget::updateLayout()
{
    adjustSize();

    // 确保窗口在屏幕范围内
    if (m_parentVideo) {
        QRect parentRect = m_parentVideo->geometry();
        QPoint globalPos = m_parentVideo->mapToGlobal(QPoint(0, 0));

        // 默认位置：父窗口右上角
        if (pos().isNull() || (x() == 0 && y() == 0)) {
            move(globalPos.x() + parentRect.width() - width() - 20,
                 globalPos.y() + 50);
        }
    }

    // 更新关闭按钮位置（放到每个容器的右上角）
    for (TipMessage* msg : m_messages) {
        if (msg->container && msg->closeBtn) {
            QPoint containerPos = msg->container->pos();
            int btnX = containerPos.x() + msg->container->width() - msg->closeBtn->width() - 4;
            int btnY = containerPos.y() + 4;
            msg->closeBtn->move(btnX, btnY);
            msg->closeBtn->raise();
        }
    }
}

// ---------------------------------------------------------
// 设置透明度
// ---------------------------------------------------------
void ScriptTipWidget::setOpacityLevel(int level)
{
    m_opacityLevel = qBound(0, level, 100);
    applyOpacity();
}

void ScriptTipWidget::applyOpacity()
{
    setWindowOpacity(m_opacityLevel / 100.0);
}

// ---------------------------------------------------------
// 鼠标事件 - 拖拽移动
// ---------------------------------------------------------
void ScriptTipWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragStartWidgetPos = pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void ScriptTipWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        move(m_dragStartWidgetPos + delta);
        event->accept();
    }
}

void ScriptTipWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
        savePosition();
        event->accept();
    }
}

// ---------------------------------------------------------
// 绘制事件 - 绘制半透明背景
// ---------------------------------------------------------
void ScriptTipWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 半透明圆角背景
    painter.setBrush(QColor(24, 24, 27, 180));
    painter.setPen(QPen(QColor(63, 63, 70), 1));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);
}

void ScriptTipWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    applyOpacity();

    // 如果有父视频窗口，跟随其层级
    if (m_parentVideo && m_parentVideo->isActiveWindow()) {
        raise();
    }
}

// ---------------------------------------------------------
// 事件过滤器 - 监听父视频窗口和消息容器事件
// ---------------------------------------------------------
bool ScriptTipWidget::eventFilter(QObject* watched, QEvent* event)
{
    // 处理父视频窗口事件
    if (watched == m_parentVideo) {
        switch (event->type()) {
        case QEvent::WindowActivate:
            if (isVisible()) {
                setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
                show();
            }
            break;
        case QEvent::WindowDeactivate:
            if (isVisible()) {
                setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
                show();
            }
            break;
        case QEvent::Hide:
        case QEvent::Close:
            hide();
            break;
        case QEvent::Show:
            if (!m_messages.isEmpty()) {
                if (m_parentVideo->isActiveWindow()) {
                    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
                }
                show();
            }
            break;
        default:
            break;
        }
        return QWidget::eventFilter(watched, event);
    }

    // 处理消息容器的鼠标事件（用于拖动整个窗口）
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragStartPos = me->globalPosition().toPoint();
            m_dragStartWidgetPos = pos();
            setCursor(Qt::ClosedHandCursor);
            return true;
        }
    } else if (event->type() == QEvent::MouseMove) {
        if (m_dragging) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            QPoint delta = me->globalPosition().toPoint() - m_dragStartPos;
            move(m_dragStartWidgetPos + delta);
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false;
            setCursor(Qt::ArrowCursor);
            savePosition();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------
// 保存/恢复位置
// ---------------------------------------------------------
void ScriptTipWidget::savePosition()
{
    QSettings settings("QtScrcpy", "ScriptTip");
    settings.setValue("pos", pos());
    settings.setValue("opacity", m_opacityLevel);
}

void ScriptTipWidget::restorePosition()
{
    QSettings settings("QtScrcpy", "ScriptTip");
    QPoint savedPos = settings.value("pos", QPoint(100, 100)).toPoint();
    m_opacityLevel = settings.value("opacity", 70).toInt();
    move(savedPos);
}

// ---------------------------------------------------------
// 设置父视频窗口
// ---------------------------------------------------------
void ScriptTipWidget::setParentVideoWidget(QWidget* videoWidget)
{
    // QPointer 会自动检测对象是否被销毁
    if (m_parentVideo && m_parentVideo != videoWidget) {
        m_parentVideo->removeEventFilter(this);
    }

    m_parentVideo = videoWidget;

    if (m_parentVideo) {
        m_parentVideo->installEventFilter(this);
    }
}

// ---------------------------------------------------------
// 设置游戏模式（控制鼠标穿透）
// ---------------------------------------------------------
void ScriptTipWidget::setGameMode(bool enabled)
{
    if (m_gameMode == enabled) return;
    m_gameMode = enabled;

    // 游戏模式下，整个窗口对鼠标透明
    setAttribute(Qt::WA_TransparentForMouseEvents, enabled);

    // 设置所有子控件的穿透状态
    for (TipMessage* msg : m_messages) {
        if (msg->container) {
            msg->container->setAttribute(Qt::WA_TransparentForMouseEvents, enabled);
        }
        if (msg->label) {
            msg->label->setAttribute(Qt::WA_TransparentForMouseEvents, enabled);
        }
        if (msg->countdownLabel) {
            msg->countdownLabel->setAttribute(Qt::WA_TransparentForMouseEvents, enabled);
        }
        if (msg->closeBtn) {
            msg->closeBtn->setAttribute(Qt::WA_TransparentForMouseEvents, enabled);
        }
    }

#ifdef Q_OS_WIN
    // Windows 平台：使用原生 API 实现真正的鼠标穿透
    // WS_EX_TRANSPARENT 让窗口对鼠标点击完全透明
    HWND hwnd = (HWND)winId();
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (enabled) {
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT | WS_EX_LAYERED);
    } else {
        SetWindowLong(hwnd, GWL_EXSTYLE, (exStyle & ~WS_EX_TRANSPARENT) | WS_EX_LAYERED);
    }
#endif

    // 更新光标
    if (enabled) {
        setCursor(Qt::ArrowCursor);  // 游戏模式下不显示拖拽光标
    }
}
