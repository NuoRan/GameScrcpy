/**
 * @file KeyConflictIndicator.h
 * @brief 键位冲突可视化指示器 — 红色脉冲边框 + 冲突提示
 *
 * 功能:
 * - 冲突键位: 红色脉冲边框动画
 * - 选中键位: Accent 蓝色描边 + 1.05x 放大 + 阴影
 * - Hover: Surface 背景 + 工具提示
 * - 拖拽中: 半透明 0.7 + 虚线辅助
 */
#ifndef KEYCONFLICTINDICATOR_H
#define KEYCONFLICTINDICATOR_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QSet>
#include <QString>

class KeyConflictIndicator : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal pulsePhase READ pulsePhase WRITE setPulsePhase)
    Q_PROPERTY(qreal selectScale READ selectScale WRITE setSelectScale)
public:
    explicit KeyConflictIndicator(QWidget* parent = nullptr);

    enum State {
        Normal,
        Selected,
        Conflicted,
        Dragging
    };

    void setState(State state);
    State state() const { return m_state; }

    void setConflictKeys(const QSet<QString>& keys);
    QSet<QString> conflictKeys() const { return m_conflictKeys; }

    void setConflictMessage(const QString& msg);
    QString conflictMessage() const { return m_conflictMsg; }

    // 给外部布局用
    QRect indicatorRect() const;

signals:
    void conflictDetected(const QString& message);
    void conflictResolved();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    qreal pulsePhase() const { return m_pulsePhase; }
    void  setPulsePhase(qreal v) { m_pulsePhase = v; update(); }
    qreal selectScale() const { return m_selectScale; }
    void  setSelectScale(qreal v) { m_selectScale = v; update(); }

    void  startPulse();
    void  stopPulse();
    void  animateSelect(bool selected);

    State   m_state = Normal;
    QSet<QString> m_conflictKeys;
    QString m_conflictMsg;

    qreal m_pulsePhase = 0.0;
    qreal m_selectScale = 1.0;

    QPropertyAnimation* m_pulseAnim = nullptr;
    QPropertyAnimation* m_scaleAnim = nullptr;
};

#endif // KEYCONFLICTINDICATOR_H
