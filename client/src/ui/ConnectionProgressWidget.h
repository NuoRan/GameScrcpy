/**
 * ConnectionProgressWidget - 连接进度指示器 / Connection Progress Indicator
 *
 * 功能 / Features:
 * - 显示连接状态的动画进度 / Animated connection status progress
 * - 显示各阶段的详细状态信息 / Detailed phase status information
 * - 支持取消操作 / Cancel operation support
 * - 超时自动处理 / Automatic timeout handling
 */

#ifndef CONNECTIONPROGRESSWIDGET_H
#define CONNECTIONPROGRESSWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QPropertyAnimation>
#include <QElapsedTimer>

// 连接阶段枚举
enum class ConnectionPhase {
    Idle,               // 空闲
    Checking,           // 检查设备
    Pushing,            // 推送服务端
    Starting,           // 启动服务
    Connecting,         // 建立连接
    Negotiating,        // 协商参数
    Streaming,          // 开始流传输
    Connected,          // 已连接
    Failed,             // 失败
    Timeout,            // 超时
    Cancelled           // 已取消
};

class ConnectionProgressWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int pulseValue READ pulseValue WRITE setPulseValue)

public:
    explicit ConnectionProgressWidget(QWidget *parent = nullptr);
    ~ConnectionProgressWidget();

    // 开始连接过程
    void startConnection(const QString& deviceSerial);

    // 更新当前阶段
    void setPhase(ConnectionPhase phase, const QString& message = QString());

    // 设置进度 (0-100)
    void setProgress(int value);

    // 设置超时时间（毫秒）
    void setTimeout(int ms);

    // 获取当前阶段
    ConnectionPhase currentPhase() const { return m_currentPhase; }

    // 获取已用时间
    qint64 elapsedTime() const;

    // 重置状态
    void reset();

    // 脉冲动画值
    int pulseValue() const { return m_pulseValue; }
    void setPulseValue(int value);

signals:
    void cancelled();
    void timeout();
    void phaseChanged(ConnectionPhase phase);
    void connectionComplete();
    void connectionFailed(const QString& reason);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onCancelClicked();
    void onTimeoutCheck();
    void updateAnimation();

private:
    void setupUI();
    void updatePhaseDisplay();
    QString phaseToString(ConnectionPhase phase) const;
    QString phaseToIcon(ConnectionPhase phase) const;
    QColor phaseToColor(ConnectionPhase phase) const;
    void startPulseAnimation();
    void stopPulseAnimation();

private:
    // UI 组件
    QLabel* m_iconLabel;
    QLabel* m_titleLabel;
    QLabel* m_messageLabel;
    QLabel* m_timeLabel;
    QProgressBar* m_progressBar;
    QPushButton* m_cancelButton;

    // 阶段指示器
    QList<QLabel*> m_phaseIndicators;

    // 状态
    ConnectionPhase m_currentPhase = ConnectionPhase::Idle;
    QString m_deviceSerial;
    int m_timeoutMs = 30000;  // 默认30秒超时

    // 计时器
    QTimer* m_timeoutTimer;
    QTimer* m_updateTimer;
    QElapsedTimer m_elapsedTimer;

    // 动画
    QPropertyAnimation* m_pulseAnimation;
    int m_pulseValue = 0;
};

#endif // CONNECTIONPROGRESSWIDGET_H
