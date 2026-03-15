/**
 * @file VideoBottomBar.h
 * @brief 视频窗口右侧操作栏 — 竖直侧边条
 *
 * 嵌入 VideoForm 右侧，提供返回/主页/多任务/全屏/键位映射按钮
 */
#ifndef VIDEOBOTTOMBAR_H
#define VIDEOBOTTOMBAR_H

#include <QWidget>

namespace Fluent { class FluentButton; }

class VideoBottomBar : public QWidget
{
    Q_OBJECT
public:
    explicit VideoBottomBar(QWidget* parent = nullptr);

    void setKeyMapMode(bool active);
    void setFullScreenMode(bool fullScreen);
    void setAudioMuted(bool muted);
    void setAudioVisible(bool visible);
    void setCompanionConnected(bool connected);

signals:
    void goBack();
    void goHome();
    void appSwitch();
    void fullScreen();
    void keyMapToggled(bool active);
    void settingsClicked();
    void audioToggled(bool muted);
    void companionClicked();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void setupUI();

    Fluent::FluentButton* m_backBtn = nullptr;
    Fluent::FluentButton* m_homeBtn = nullptr;
    Fluent::FluentButton* m_appSwitchBtn = nullptr;
    Fluent::FluentButton* m_fullScreenBtn = nullptr;
    Fluent::FluentButton* m_keyMapBtn = nullptr;
    Fluent::FluentButton* m_audioBtn = nullptr;
    Fluent::FluentButton* m_settingsBtn = nullptr;
    Fluent::FluentButton* m_companionBtn = nullptr;
    bool m_keyMapActive = false;
    bool m_audioMuted = false;  // 音频启用时默认播放
};

#endif
