/**
 * @file OnboardingOverlay.h
 * @brief 首次使用引导覆盖层 — 聚光灯 + 提示卡片 + 分步导航
 *
 * 特性:
 * - 半透明遮罩 + 聚光灯裁剪高亮目标控件
 * - 自动定位提示卡片 (上/下/左/右)
 * - 支持跳过、上一步、下一步
 * - 进入/退出淡入动画
 */

#ifndef ONBOARDINGOVERLAY_H
#define ONBOARDINGOVERLAY_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include <functional>

namespace Fluent {

/**
 * @brief 单步引导描述
 */
struct OnboardingStep {
    QWidget* target = nullptr;        ///< 要高亮的目标控件 (nullptr = 居中展示，无聚光灯)
    QString  title;                   ///< 标题
    QString  description;             ///< 描述文本
    QString  icon;                    ///< 可选 emoji/图标
    std::function<void()> beforeShow; ///< 显示此步前调用 (如切换页面)
};

/**
 * @brief 引导覆盖层
 *
 * 用法:
 * @code
 *   auto* overlay = new OnboardingOverlay(parentWindow);
 *   overlay->setSteps({
 *       {nullptr, "欢迎", "欢迎使用 GameScrcpy！", "🎮"},
 *       {m_usbBtn, "USB 连接", "点击此按钮通过 USB 连接设备"},
 *       ...
 *   });
 *   overlay->start();
 * @endcode
 */
class OnboardingOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit OnboardingOverlay(QWidget* parent = nullptr);

    void setSteps(const QVector<OnboardingStep>& steps);
    void start();

signals:
    void finished();   ///< 引导完成或跳过

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void showStep(int index);
    void layoutCard();
    void applyStyle();
    QRect spotlightRect() const;

    QVector<OnboardingStep> m_steps;
    int m_currentStep = 0;

    // 卡片控件
    QWidget*     m_card = nullptr;
    QLabel*      m_iconLabel = nullptr;
    QLabel*      m_titleLabel = nullptr;
    QLabel*      m_descLabel = nullptr;
    QLabel*      m_stepLabel = nullptr;
    QPushButton* m_skipBtn = nullptr;
    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
};

} // namespace Fluent

#endif // ONBOARDINGOVERLAY_H
