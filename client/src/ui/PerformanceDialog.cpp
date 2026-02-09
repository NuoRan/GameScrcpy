#include "PerformanceDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QDateTime>
#include <QEvent>

namespace qsc {

PerformanceDialog::PerformanceDialog(QWidget* parent)
    : QDialog(parent)
{
    setMinimumSize(320, 400);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

    setupUI();
    applyStyle();
    retranslateUi();

    // 连接性能监控信号
    connect(&PerformanceMonitor::instance(), &PerformanceMonitor::metricsUpdated,
            this, &PerformanceDialog::updateMetrics);

    // 启用性能监控
    PerformanceMonitor::instance().setEnabled(true);
}

PerformanceDialog::~PerformanceDialog()
{
    PerformanceMonitor::instance().setEnabled(false);
}

void PerformanceDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // === 视频管线组 ===
    m_videoGroup = new QGroupBox(this);
    auto* videoLayout = new QGridLayout(m_videoGroup);
    videoLayout->setSpacing(8);

    m_fpsLabel = new QLabel("0", this);
    m_fpsLabel->setObjectName("fpsValue");
    m_decodeLatencyLabel = new QLabel("0.0 ms", this);
    m_renderLatencyLabel = new QLabel("0.0 ms", this);
    m_framesLabel = new QLabel("0", this);
    m_droppedLabel = new QLabel("0", this);

    m_fpsNameLabel = new QLabel(this);
    m_decodeNameLabel = new QLabel(this);
    m_renderNameLabel = new QLabel(this);
    m_framesNameLabel = new QLabel(this);
    m_droppedNameLabel = new QLabel(this);

    videoLayout->addWidget(m_fpsNameLabel, 0, 0);
    videoLayout->addWidget(m_fpsLabel, 0, 1);
    videoLayout->addWidget(m_decodeNameLabel, 1, 0);
    videoLayout->addWidget(m_decodeLatencyLabel, 1, 1);
    videoLayout->addWidget(m_renderNameLabel, 2, 0);
    videoLayout->addWidget(m_renderLatencyLabel, 2, 1);
    videoLayout->addWidget(m_framesNameLabel, 3, 0);
    videoLayout->addWidget(m_framesLabel, 3, 1);
    videoLayout->addWidget(m_droppedNameLabel, 4, 0);
    videoLayout->addWidget(m_droppedLabel, 4, 1);

    mainLayout->addWidget(m_videoGroup);

    // === 网络组 ===
    m_networkGroup = new QGroupBox(this);
    auto* networkLayout = new QGridLayout(m_networkGroup);
    networkLayout->setSpacing(8);

    m_networkLatencyLabel = new QLabel("0.0 ms", this);
    m_bytesSentLabel = new QLabel("0 KB", this);
    m_bytesReceivedLabel = new QLabel("0 KB", this);
    m_pendingLabel = new QLabel("0 bytes", this);

    m_netLatencyNameLabel = new QLabel(this);
    m_sentNameLabel = new QLabel(this);
    m_recvNameLabel = new QLabel(this);
    m_pendingNameLabel = new QLabel(this);

    networkLayout->addWidget(m_netLatencyNameLabel, 0, 0);
    networkLayout->addWidget(m_networkLatencyLabel, 0, 1);
    networkLayout->addWidget(m_sentNameLabel, 1, 0);
    networkLayout->addWidget(m_bytesSentLabel, 1, 1);
    networkLayout->addWidget(m_recvNameLabel, 2, 0);
    networkLayout->addWidget(m_bytesReceivedLabel, 2, 1);
    networkLayout->addWidget(m_pendingNameLabel, 3, 0);
    networkLayout->addWidget(m_pendingLabel, 3, 1);

    mainLayout->addWidget(m_networkGroup);

    // === 输入组 ===
    m_inputGroup = new QGroupBox(this);
    auto* inputLayout = new QGridLayout(m_inputGroup);
    inputLayout->setSpacing(8);

    m_inputLatencyLabel = new QLabel("0.0 ms", this);
    m_inputProcessedLabel = new QLabel("0", this);
    m_inputDroppedLabel = new QLabel("0", this);

    m_inputRateNameLabel = new QLabel(this);
    m_inputProcNameLabel = new QLabel(this);
    m_inputDropNameLabel = new QLabel(this);

    inputLayout->addWidget(m_inputRateNameLabel, 0, 0);
    inputLayout->addWidget(m_inputLatencyLabel, 0, 1);
    inputLayout->addWidget(m_inputProcNameLabel, 1, 0);
    inputLayout->addWidget(m_inputProcessedLabel, 1, 1);
    inputLayout->addWidget(m_inputDropNameLabel, 2, 0);
    inputLayout->addWidget(m_inputDroppedLabel, 2, 1);

    mainLayout->addWidget(m_inputGroup);

    // === 帧池组 ===
    m_poolGroup = new QGroupBox(this);
    auto* poolLayout = new QVBoxLayout(m_poolGroup);

    m_framePoolBar = new QProgressBar(this);
    m_framePoolBar->setMinimum(0);
    m_framePoolBar->setMaximum(100);
    m_framePoolBar->setTextVisible(true);
    m_framePoolBar->setFormat("%v / %m");

    poolLayout->addWidget(m_framePoolBar);

    mainLayout->addWidget(m_poolGroup);

    // === 按钮 ===
    auto* buttonLayout = new QHBoxLayout();

    m_resetBtn = new QPushButton(this);
    connect(m_resetBtn, &QPushButton::clicked, this, []() {
        PerformanceMonitor::instance().reset();
    });
    buttonLayout->addWidget(m_resetBtn);

    m_closeBtn = new QPushButton(this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
    buttonLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
}

void PerformanceDialog::applyStyle()
{
    setStyleSheet(R"(
        QDialog {
            background-color: #09090b;
            color: #fafafa;
        }
        QGroupBox {
            background-color: #18181b;
            border: 1px solid #27272a;
            border-radius: 8px;
            padding: 12px;
            margin-top: 12px;
            font-weight: 600;
            color: #fafafa;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 4px;
            color: #a1a1aa;
        }
        QLabel {
            color: #a1a1aa;
            font-size: 12px;
        }
        QLabel#fpsValue {
            color: #22c55e;
            font-size: 24px;
            font-weight: 700;
        }
        QProgressBar {
            background-color: #27272a;
            border: none;
            border-radius: 4px;
            height: 20px;
            text-align: center;
            color: #fafafa;
        }
        QProgressBar::chunk {
            background-color: #6366f1;
            border-radius: 4px;
        }
        QPushButton {
            background-color: #27272a;
            color: #fafafa;
            border: 1px solid #3f3f46;
            border-radius: 6px;
            padding: 8px 16px;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: #3f3f46;
            border-color: #6366f1;
        }
    )");
}

void PerformanceDialog::updateMetrics(const PerformanceMetrics& m)
{
    // 视频管线
    m_fpsLabel->setText(QString::number(m.fps));

    // 根据 FPS 设置颜色
    if (m.fps >= 55) {
        m_fpsLabel->setStyleSheet("color: #22c55e; font-size: 24px; font-weight: 700;");
    } else if (m.fps >= 30) {
        m_fpsLabel->setStyleSheet("color: #eab308; font-size: 24px; font-weight: 700;");
    } else {
        m_fpsLabel->setStyleSheet("color: #ef4444; font-size: 24px; font-weight: 700;");
    }

    m_decodeLatencyLabel->setText(QString("%1 ms").arg(m.avgDecodeLatencyMs, 0, 'f', 2));
    m_renderLatencyLabel->setText(QString("%1 ms").arg(m.avgRenderLatencyMs, 0, 'f', 2));
    m_framesLabel->setText(QString::number(m.totalFrames));
    m_droppedLabel->setText(QString::number(m.droppedFrames));

    // 丢帧率高时变红
    if (m.totalFrames > 0 && m.droppedFrames * 100 / m.totalFrames > 1) {
        m_droppedLabel->setStyleSheet("color: #ef4444;");
    } else {
        m_droppedLabel->setStyleSheet("color: #a1a1aa;");
    }

    // 网络
    m_networkLatencyLabel->setText(QString("%1 ms").arg(m.networkLatencyMs, 0, 'f', 2));
    m_bytesSentLabel->setText(QString("%1 MB").arg(m.bytesSent / 1048576.0, 0, 'f', 2));
    m_bytesReceivedLabel->setText(QString("%1 MB").arg(m.bytesReceived / 1048576.0, 0, 'f', 2));
    m_pendingLabel->setText(QString("%1 bytes").arg(m.pendingBytes));

    // 待发送字节多时变黄
    if (m.pendingBytes > 1024) {
        m_pendingLabel->setStyleSheet("color: #eab308;");
    } else {
        m_pendingLabel->setStyleSheet("color: #a1a1aa;");
    }

        // 输入（计算每秒事件数）
    static quint64 lastProcessed = 0;
    static qint64 lastTime = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (lastTime > 0 && now > lastTime) {
        double eventsPerSec = (m.inputEventsProcessed - lastProcessed) * 1000.0 / (now - lastTime);
        m_inputLatencyLabel->setText(QString("%1 /").arg(eventsPerSec, 0, 'f', 0) + tr("秒"));
    }
    lastProcessed = m.inputEventsProcessed;
    lastTime = now;

    m_inputProcessedLabel->setText(QString::number(m.inputEventsProcessed));
    m_inputDroppedLabel->setText(QString::number(m.inputEventsDropped));

    // 帧池
    if (m.framePoolTotal > 0) {
        m_framePoolBar->setMaximum(m.framePoolTotal);
        m_framePoolBar->setValue(m.framePoolUsed);
        m_framePoolBar->setFormat(QString("%1 / %2").arg(m.framePoolUsed).arg(m.framePoolTotal));
    }
}

void PerformanceDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QDialog::changeEvent(event);
}

void PerformanceDialog::retranslateUi()
{
    setWindowTitle(tr("性能监控"));

    // 组框标题
    if (m_videoGroup) m_videoGroup->setTitle(tr("视频管线"));
    if (m_networkGroup) m_networkGroup->setTitle(tr("网络"));
    if (m_inputGroup) m_inputGroup->setTitle(tr("输入"));
    if (m_poolGroup) m_poolGroup->setTitle(tr("帧池"));

    // 视频标签名
    if (m_fpsNameLabel) m_fpsNameLabel->setText(tr("FPS:"));
    if (m_decodeNameLabel) m_decodeNameLabel->setText(tr("解码延迟:"));
    if (m_renderNameLabel) m_renderNameLabel->setText(tr("渲染延迟:"));
    if (m_framesNameLabel) m_framesNameLabel->setText(tr("总帧数:"));
    if (m_droppedNameLabel) m_droppedNameLabel->setText(tr("丢帧数:"));

    // 网络标签名
    if (m_netLatencyNameLabel) m_netLatencyNameLabel->setText(tr("延迟:"));
    if (m_sentNameLabel) m_sentNameLabel->setText(tr("发送:"));
    if (m_recvNameLabel) m_recvNameLabel->setText(tr("接收:"));
    if (m_pendingNameLabel) m_pendingNameLabel->setText(tr("待发送:"));

    // 输入标签名
    if (m_inputRateNameLabel) m_inputRateNameLabel->setText(tr("速率:"));
    if (m_inputProcNameLabel) m_inputProcNameLabel->setText(tr("已处理:"));
    if (m_inputDropNameLabel) m_inputDropNameLabel->setText(tr("已丢弃:"));

    // 按钮
    if (m_resetBtn) m_resetBtn->setText(tr("重置统计"));
    if (m_closeBtn) m_closeBtn->setText(tr("关闭"));
}

} // namespace qsc
