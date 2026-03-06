/**
 * @file VideoSettingsPopup.h
 * @brief 视频流实时设置弹窗 — 与主界面设置页同风格
 *
 * 显示为全尺寸遮罩 + 卡片弹窗 (类似 FluentDialog)。
 * 包含:
 *   1. 视频参数 (码率 / 帧率 / 分辨率) — 全部可实时调整
 *   2. 显示选项 (键位显示 / 键位透明度 / 提示透明度)
 *   3. 设备控制 (息屏模式 / 暂停视频流)
 *
 * 码率/帧率/分辨率 从 Config (userdata.ini) 读写，与主界面 SettingsPage 共享同一配置源。
 * 显示选项从 ConfigCenter 读写。
 */
#ifndef VIDEOSETTINGSPOPUP_H
#define VIDEOSETTINGSPOPUP_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>

namespace Fluent {
class FluentSlider;
class FluentToggle;
class FluentCard;
class SettingRow;
}

class VideoSettingsPopup : public QDialog
{
    Q_OBJECT
public:
    explicit VideoSettingsPopup(QWidget* parent = nullptr);

    /// 打开前从 Config 同步当前值 (与主界面设置共用配置源)
    void syncFromConfig();

signals:
    // 仅在值变化时发出 (供 VideoForm 做实时协议发送)
    void videoParamsChanged(quint32 bitrate, quint16 maxFps, quint16 maxSize);
    void overlayVisibleChanged(bool visible);
    void overlayOpacityChanged(int opacity);     // 0-100
    void tipOpacityChanged(int opacity);         // 0-100
    void screenOffChanged(bool off);
    void videoStreamingChanged(bool streaming);  // true=播放, false=暂停

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    void setupUI();
    void applyStyle();
    void flushAndSaveVideoParams();  // 将当前值写配置并发信号

    QWidget* m_card = nullptr;

    // 视频参数
    QLineEdit* m_bitRateEdit = nullptr;
    QComboBox* m_fpsBox      = nullptr;
    QComboBox* m_maxSizeBox  = nullptr;

    // 显示选项
    Fluent::FluentToggle* m_overlayToggle = nullptr;
    Fluent::FluentSlider* m_overlayOpacitySlider = nullptr;
    Fluent::FluentSlider* m_tipOpacitySlider = nullptr;

    // 设备控制
    Fluent::FluentToggle* m_screenOffToggle  = nullptr;
    Fluent::FluentToggle* m_streamingToggle  = nullptr;

    // 跟踪值 (仅变化时发信号)
    quint32 m_lastBitRate = 0;
    int     m_lastMaxFps  = 60;
    int     m_lastMaxSize = 0;
    bool    m_lastOverlayVisible = true;
    int     m_lastOverlayOpacity = 80;
    int     m_lastTipOpacity = 100;
    bool    m_lastScreenOff = false;
    bool    m_lastStreaming  = true;
};

#endif // VIDEOSETTINGSPOPUP_H
