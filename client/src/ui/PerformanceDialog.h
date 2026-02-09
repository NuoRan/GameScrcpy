#ifndef PERFORMANCEDIALOG_H
#define PERFORMANCEDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QGroupBox>
#include <QPushButton>
#include <QEvent>
#include "PerformanceMonitor.h"

namespace qsc {

/**
 * @brief 性能监控对话框 / Performance Monitor Dialog
 */
class PerformanceDialog : public QDialog {
    Q_OBJECT

public:
    explicit PerformanceDialog(QWidget* parent = nullptr);
    ~PerformanceDialog() override;

private slots:
    void updateMetrics(const PerformanceMetrics& m);

private:
    void setupUI();
    void applyStyle();
    void retranslateUi();

protected:
    void changeEvent(QEvent *event) override;

private:
    // 视频标签
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_decodeLatencyLabel = nullptr;
    QLabel* m_renderLatencyLabel = nullptr;
    QLabel* m_framesLabel = nullptr;
    QLabel* m_droppedLabel = nullptr;

    // 网络标签
    QLabel* m_networkLatencyLabel = nullptr;
    QLabel* m_bytesSentLabel = nullptr;
    QLabel* m_bytesReceivedLabel = nullptr;
    QLabel* m_pendingLabel = nullptr;

    // 输入标签
    QLabel* m_inputLatencyLabel = nullptr;
    QLabel* m_inputProcessedLabel = nullptr;
    QLabel* m_inputDroppedLabel = nullptr;

    // 帧池进度条
    QProgressBar* m_framePoolBar = nullptr;

    // 可翻译的组框和按钮 / Translatable groups and buttons
    QGroupBox* m_videoGroup = nullptr;
    QGroupBox* m_networkGroup = nullptr;
    QGroupBox* m_inputGroup = nullptr;
    QGroupBox* m_poolGroup = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
    // 视频标签名
    QLabel* m_fpsNameLabel = nullptr;
    QLabel* m_decodeNameLabel = nullptr;
    QLabel* m_renderNameLabel = nullptr;
    QLabel* m_framesNameLabel = nullptr;
    QLabel* m_droppedNameLabel = nullptr;
    // 网络标签名
    QLabel* m_netLatencyNameLabel = nullptr;
    QLabel* m_sentNameLabel = nullptr;
    QLabel* m_recvNameLabel = nullptr;
    QLabel* m_pendingNameLabel = nullptr;
    // 输入标签名
    QLabel* m_inputRateNameLabel = nullptr;
    QLabel* m_inputProcNameLabel = nullptr;
    QLabel* m_inputDropNameLabel = nullptr;
};

} // namespace qsc

#endif // PERFORMANCEDIALOG_H
