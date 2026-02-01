/**
 * ConnectionProgressWidget 实现
 */

#include "ConnectionProgressWidget.h"
#include <QPainter>
#include <QDebug>

ConnectionProgressWidget::ConnectionProgressWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();

    // 初始化计时器
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &ConnectionProgressWidget::onTimeoutCheck);

    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &ConnectionProgressWidget::updateAnimation);

    // 初始化脉冲动画
    m_pulseAnimation = new QPropertyAnimation(this, "pulseValue");
    m_pulseAnimation->setDuration(1000);
    m_pulseAnimation->setStartValue(0);
    m_pulseAnimation->setEndValue(100);
    m_pulseAnimation->setLoopCount(-1);  // 无限循环
    m_pulseAnimation->setEasingCurve(QEasingCurve::InOutSine);
}

ConnectionProgressWidget::~ConnectionProgressWidget()
{
    m_timeoutTimer->stop();
    m_updateTimer->stop();
    m_pulseAnimation->stop();
}

void ConnectionProgressWidget::setupUI()
{
    setMinimumSize(350, 200);
    setMaximumSize(450, 280);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // 顶部：图标和标题
    QHBoxLayout* topLayout = new QHBoxLayout();

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(48, 48);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setStyleSheet("font-size: 32px;");
    topLayout->addWidget(m_iconLabel);

    QVBoxLayout* titleLayout = new QVBoxLayout();
    m_titleLabel = new QLabel(tr("Connecting..."), this);
    m_titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #2196F3;");
    titleLayout->addWidget(m_titleLabel);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setStyleSheet("font-size: 12px; color: #757575;");
    m_messageLabel->setWordWrap(true);
    titleLayout->addWidget(m_messageLabel);

    topLayout->addLayout(titleLayout, 1);
    mainLayout->addLayout(topLayout);

    // 阶段指示器
    QHBoxLayout* phaseLayout = new QHBoxLayout();
    phaseLayout->setSpacing(4);

    QStringList phases = {tr("Check"), tr("Push"), tr("Start"), tr("Connect"), tr("Stream")};
    for (const QString& phase : phases) {
        QLabel* indicator = new QLabel(phase, this);
        indicator->setAlignment(Qt::AlignCenter);
        indicator->setFixedHeight(24);
        indicator->setStyleSheet(
            "background-color: #E0E0E0; "
            "border-radius: 12px; "
            "padding: 2px 8px; "
            "font-size: 10px; "
            "color: #757575;"
        );
        m_phaseIndicators.append(indicator);
        phaseLayout->addWidget(indicator);
    }
    mainLayout->addLayout(phaseLayout);

    // 进度条
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);
    m_progressBar->setStyleSheet(
        "QProgressBar {"
        "  background-color: #E0E0E0;"
        "  border-radius: 4px;"
        "}"
        "QProgressBar::chunk {"
        "  background-color: #2196F3;"
        "  border-radius: 4px;"
        "}"
    );
    mainLayout->addWidget(m_progressBar);

    // 时间显示
    m_timeLabel = new QLabel(this);
    m_timeLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel->setStyleSheet("font-size: 11px; color: #9E9E9E;");
    mainLayout->addWidget(m_timeLabel);

    // 取消按钮
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setFixedWidth(100);
    m_cancelButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #F5F5F5;"
        "  border: 1px solid #E0E0E0;"
        "  border-radius: 4px;"
        "  padding: 6px 16px;"
        "  color: #616161;"
        "}"
        "QPushButton:hover {"
        "  background-color: #EEEEEE;"
        "  border-color: #BDBDBD;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #E0E0E0;"
        "}"
    );
    connect(m_cancelButton, &QPushButton::clicked, this, &ConnectionProgressWidget::onCancelClicked);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    // 设置窗口样式
    setStyleSheet(
        "ConnectionProgressWidget {"
        "  background-color: #FFFFFF;"
        "  border-radius: 8px;"
        "}"
    );

    setAttribute(Qt::WA_StyledBackground, true);
}

void ConnectionProgressWidget::startConnection(const QString& deviceSerial)
{
    m_deviceSerial = deviceSerial;
    reset();

    setPhase(ConnectionPhase::Checking, tr("Checking device %1...").arg(deviceSerial));

    m_elapsedTimer.start();
    m_timeoutTimer->start(m_timeoutMs);
    m_updateTimer->start(100);

    startPulseAnimation();
    show();
}

void ConnectionProgressWidget::setPhase(ConnectionPhase phase, const QString& message)
{
    m_currentPhase = phase;

    if (!message.isEmpty()) {
        m_messageLabel->setText(message);
    } else {
        m_messageLabel->setText(phaseToString(phase));
    }

    updatePhaseDisplay();
    emit phaseChanged(phase);

    // 处理终态
    switch (phase) {
        case ConnectionPhase::Connected:
            stopPulseAnimation();
            m_progressBar->setValue(100);
            m_timeoutTimer->stop();
            m_cancelButton->setEnabled(false);
            emit connectionComplete();
            break;

        case ConnectionPhase::Failed:
        case ConnectionPhase::Timeout:
        case ConnectionPhase::Cancelled:
            stopPulseAnimation();
            m_timeoutTimer->stop();
            m_cancelButton->setEnabled(false);
            if (phase == ConnectionPhase::Failed) {
                emit connectionFailed(message);
            }
            break;

        default:
            break;
    }

    update();
}

void ConnectionProgressWidget::setProgress(int value)
{
    m_progressBar->setValue(qBound(0, value, 100));
}

void ConnectionProgressWidget::setTimeout(int ms)
{
    m_timeoutMs = ms;
}

qint64 ConnectionProgressWidget::elapsedTime() const
{
    return m_elapsedTimer.elapsed();
}

void ConnectionProgressWidget::reset()
{
    m_currentPhase = ConnectionPhase::Idle;
    m_progressBar->setValue(0);
    m_messageLabel->clear();
    m_timeLabel->clear();
    m_cancelButton->setEnabled(true);

    // 重置阶段指示器
    for (QLabel* indicator : m_phaseIndicators) {
        indicator->setStyleSheet(
            "background-color: #E0E0E0; "
            "border-radius: 12px; "
            "padding: 2px 8px; "
            "font-size: 10px; "
            "color: #757575;"
        );
    }

    m_iconLabel->setText("🔌");
    m_titleLabel->setText(tr("Connecting..."));
    m_titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #2196F3;");
}

void ConnectionProgressWidget::setPulseValue(int value)
{
    m_pulseValue = value;
    update();
}

void ConnectionProgressWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    // 绘制脉冲效果（可选）
    if (m_currentPhase != ConnectionPhase::Idle &&
        m_currentPhase != ConnectionPhase::Connected &&
        m_currentPhase != ConnectionPhase::Failed &&
        m_currentPhase != ConnectionPhase::Cancelled) {
        // 可以在这里添加额外的视觉效果
    }
}

void ConnectionProgressWidget::onCancelClicked()
{
    m_timeoutTimer->stop();
    m_updateTimer->stop();
    stopPulseAnimation();

    setPhase(ConnectionPhase::Cancelled, tr("Connection cancelled by user"));
    emit cancelled();
}

void ConnectionProgressWidget::onTimeoutCheck()
{
    setPhase(ConnectionPhase::Timeout, tr("Connection timeout after %1 seconds").arg(m_timeoutMs / 1000));
    emit timeout();
}

void ConnectionProgressWidget::updateAnimation()
{
    // 更新时间显示
    qint64 elapsed = m_elapsedTimer.elapsed();
    int seconds = elapsed / 1000;
    int remaining = (m_timeoutMs - elapsed) / 1000;

    m_timeLabel->setText(tr("Elapsed: %1s / Timeout: %2s").arg(seconds).arg(remaining));

    // 根据阶段更新进度
    int baseProgress = 0;
    switch (m_currentPhase) {
        case ConnectionPhase::Checking:    baseProgress = 10; break;
        case ConnectionPhase::Pushing:     baseProgress = 30; break;
        case ConnectionPhase::Starting:    baseProgress = 50; break;
        case ConnectionPhase::Connecting:  baseProgress = 70; break;
        case ConnectionPhase::Negotiating: baseProgress = 85; break;
        case ConnectionPhase::Streaming:   baseProgress = 95; break;
        case ConnectionPhase::Connected:   baseProgress = 100; break;
        default: break;
    }

    // 添加脉冲效果
    int pulse = (m_pulseValue * 5) / 100;  // 最多增加5%
    setProgress(qMin(99, baseProgress + pulse));
}

void ConnectionProgressWidget::updatePhaseDisplay()
{
    int currentIndex = -1;
    switch (m_currentPhase) {
        case ConnectionPhase::Checking:    currentIndex = 0; break;
        case ConnectionPhase::Pushing:     currentIndex = 1; break;
        case ConnectionPhase::Starting:    currentIndex = 2; break;
        case ConnectionPhase::Connecting:
        case ConnectionPhase::Negotiating: currentIndex = 3; break;
        case ConnectionPhase::Streaming:
        case ConnectionPhase::Connected:   currentIndex = 4; break;
        default: break;
    }

    for (int i = 0; i < m_phaseIndicators.size(); ++i) {
        QLabel* indicator = m_phaseIndicators[i];
        if (i < currentIndex) {
            // 已完成的阶段
            indicator->setStyleSheet(
                "background-color: #4CAF50; "
                "border-radius: 12px; "
                "padding: 2px 8px; "
                "font-size: 10px; "
                "color: white;"
            );
        } else if (i == currentIndex) {
            // 当前阶段
            indicator->setStyleSheet(
                "background-color: #2196F3; "
                "border-radius: 12px; "
                "padding: 2px 8px; "
                "font-size: 10px; "
                "color: white;"
            );
        } else {
            // 未开始的阶段
            indicator->setStyleSheet(
                "background-color: #E0E0E0; "
                "border-radius: 12px; "
                "padding: 2px 8px; "
                "font-size: 10px; "
                "color: #757575;"
            );
        }
    }

    // 更新图标和标题
    m_iconLabel->setText(phaseToIcon(m_currentPhase));

    QColor color = phaseToColor(m_currentPhase);
    m_titleLabel->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1;").arg(color.name()));

    switch (m_currentPhase) {
        case ConnectionPhase::Connected:
            m_titleLabel->setText(tr("Connected!"));
            break;
        case ConnectionPhase::Failed:
            m_titleLabel->setText(tr("Connection Failed"));
            break;
        case ConnectionPhase::Timeout:
            m_titleLabel->setText(tr("Connection Timeout"));
            break;
        case ConnectionPhase::Cancelled:
            m_titleLabel->setText(tr("Cancelled"));
            break;
        default:
            m_titleLabel->setText(tr("Connecting..."));
            break;
    }
}

QString ConnectionProgressWidget::phaseToString(ConnectionPhase phase) const
{
    switch (phase) {
        case ConnectionPhase::Idle:        return tr("Ready");
        case ConnectionPhase::Checking:    return tr("Checking device...");
        case ConnectionPhase::Pushing:     return tr("Pushing server...");
        case ConnectionPhase::Starting:    return tr("Starting server...");
        case ConnectionPhase::Connecting:  return tr("Establishing connection...");
        case ConnectionPhase::Negotiating: return tr("Negotiating parameters...");
        case ConnectionPhase::Streaming:   return tr("Starting stream...");
        case ConnectionPhase::Connected:   return tr("Connected successfully!");
        case ConnectionPhase::Failed:      return tr("Connection failed");
        case ConnectionPhase::Timeout:     return tr("Connection timed out");
        case ConnectionPhase::Cancelled:   return tr("Connection cancelled");
    }
    return QString();
}

QString ConnectionProgressWidget::phaseToIcon(ConnectionPhase phase) const
{
    switch (phase) {
        case ConnectionPhase::Idle:        return "🔌";
        case ConnectionPhase::Checking:    return "🔍";
        case ConnectionPhase::Pushing:     return "📤";
        case ConnectionPhase::Starting:    return "⚡";
        case ConnectionPhase::Connecting:  return "🔗";
        case ConnectionPhase::Negotiating: return "🤝";
        case ConnectionPhase::Streaming:   return "📺";
        case ConnectionPhase::Connected:   return "✅";
        case ConnectionPhase::Failed:      return "❌";
        case ConnectionPhase::Timeout:     return "⏱️";
        case ConnectionPhase::Cancelled:   return "🚫";
    }
    return "🔌";
}

QColor ConnectionProgressWidget::phaseToColor(ConnectionPhase phase) const
{
    switch (phase) {
        case ConnectionPhase::Connected:   return QColor("#4CAF50");  // 绿色
        case ConnectionPhase::Failed:
        case ConnectionPhase::Timeout:     return QColor("#F44336");  // 红色
        case ConnectionPhase::Cancelled:   return QColor("#FF9800");  // 橙色
        default:                           return QColor("#2196F3");  // 蓝色
    }
}

void ConnectionProgressWidget::startPulseAnimation()
{
    if (m_pulseAnimation->state() != QAbstractAnimation::Running) {
        m_pulseAnimation->start();
    }
}

void ConnectionProgressWidget::stopPulseAnimation()
{
    m_pulseAnimation->stop();
    m_pulseValue = 0;
}
