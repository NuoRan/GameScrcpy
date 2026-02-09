#ifndef SCRIPTTIPWIDGET_H
#define SCRIPTTIPWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QList>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QSettings>
#include <QPointer>
#include <QPushButton>

// ---------------------------------------------------------
// 单条消息项 / Single Message Item
// ---------------------------------------------------------
struct TipMessage {
    QString text;           // 消息文本 / Message text
    int durationMs;         // 显示时长 (毫秒) / Display duration (ms)
    QWidget* container;     // 容器 widget / Container widget
    QLabel* label;          // 内容标签 / Content label
    QLabel* countdownLabel; // 倒计时标签 / Countdown label
    QPushButton* closeBtn;  // 关闭按钮 / Close button
    QTimer* timer;          // 关闭定时器 / Close timer
    QTimer* countdownTimer; // 倒计时更新定时器 / Countdown update timer
    qint64 createTime;      // 创建时间戳 / Creation timestamp
    int keyId;              // 发送消息的按键 ID / Key ID that sent the message
};

// ---------------------------------------------------------
// ScriptTipWidget - 脚本弹窗提示控件
// 特性：
// - 不影响操作（不会获取焦点）
// - 支持多条消息叠加显示
// - 每条消息独立计时销毁
// - 支持拖拽移动位置
// - 透明度可调节
// ---------------------------------------------------------
class ScriptTipWidget : public QWidget
{
    Q_OBJECT
public:
    // 单例模式 - 全局唯一弹窗
    static ScriptTipWidget* instance(QWidget* parent = nullptr);
    static void destroyInstance();

    // 添加一条消息
    // @param message: 消息内容
    // @param durationMs: 显示时长（毫秒），默认 3000ms，0 表示永久显示
    // @param keyId: 按键 ID，相同 keyId 的消息会更新而非新增
    void addMessage(const QString& message, int durationMs = 3000, int keyId = -1);

    // 清空所有消息
    void clearAll();

    // 设置透明度 (0-100)
    void setOpacityLevel(int level);
    int opacityLevel() const { return m_opacityLevel; }

    // 保存/恢复位置
    void savePosition();
    void restorePosition();

    // 设置父窗口（用于相对定位）
    void setParentVideoWidget(QWidget* videoWidget);

    // 设置游戏模式（控制鼠标穿透）
    // 游戏模式下，弹窗对鼠标事件透明，不会干扰视角控制
    void setGameMode(bool enabled);
    bool isGameMode() const { return m_gameMode; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;  // 监听父窗口事件

private:
    explicit ScriptTipWidget(QWidget* parent = nullptr);
    ~ScriptTipWidget();

    void removeMessage(TipMessage* msg);
    void updateLayout();
    void applyOpacity();

private:
    static ScriptTipWidget* s_instance;

    QVBoxLayout* m_layout = nullptr;
    QList<TipMessage*> m_messages;
    int m_opacityLevel = 70;  // 0-100

    // 拖拽相关
    bool m_dragging = false;
    QPoint m_dragStartPos;
    QPoint m_dragStartWidgetPos;

    // 父视频窗口（用于计算相对位置，使用 QPointer 防止悬空指针）
    QPointer<QWidget> m_parentVideo;

    // 游戏模式标志（鼠标穿透）
    bool m_gameMode = false;

    // 最大消息数（防止过多）
    static const int MAX_MESSAGES = 20;
};

#endif // SCRIPTTIPWIDGET_H
