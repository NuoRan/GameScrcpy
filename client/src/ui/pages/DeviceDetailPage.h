/**
 * @file DeviceDetailPage.h
 * @brief 设备详情页 — 设备信息 + 投屏控制参数 + 键位配置
 *
 * 双击设备卡片或投屏连接后进入此页面。
 * 整合原 SettingsDialog 的视频参数和 ToolForm 的键位设置到同一页面。
 */
#ifndef DEVICEDETAILPAGE_H
#define DEVICEDETAILPAGE_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include "FluentComboBox.h"
#include <QSpinBox>
#include <QPushButton>

namespace Fluent {
class FluentCard;
class FluentToggle;
class FluentButton;
class FluentSlider;
class FluentBadge;
}

class DeviceDetailPage : public QWidget
{
    Q_OBJECT
public:
    explicit DeviceDetailPage(QWidget* parent = nullptr);

    // 设备信息
    void setSerial(const QString& serial);
    void setDeviceName(const QString& name);
    void setConnectionType(const QString& type);
    void setStreamingState(bool streaming);

    // 获取投屏参数
    quint32 getBitRate() const;
    quint16 getMaxSize() const;
    int     getMaxFps() const;
    QString getVideoCodecName() const;
    bool    isReverseConnect() const;
    bool    showToolbar() const;
    bool    isFrameless() const;
    bool    showFPS() const;

    // 键位
    void    refreshKeyMapList();
    QString currentKeyMapFile() const;
    void    setCurrentKeyMap(const QString& filename);

signals:
    // 投屏控制
    void startStreaming(const QString& serial);
    void stopStreaming(const QString& serial);
    void goBack();

    // 键位管理
    void keyMapChanged(const QString& filename);
    void keyMapSaveRequested();
    void keyMapNewRequested(const QString& filename);

protected:
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUI();
    void retranslateUi();

    QString m_serial;

    // 设备信息区
    QLabel* m_backLabel = nullptr;
    QLabel* m_deviceNameLabel = nullptr;
    QLabel* m_serialLabel = nullptr;
    QLabel* m_connectionLabel = nullptr;
    Fluent::FluentBadge* m_statusBadge = nullptr;

    // 投屏控制
    QLabel* m_streamTitle = nullptr;
    QLineEdit* m_bitRateEdit = nullptr;
    Fluent::FluentComboBox* m_bitRateUnit = nullptr;
    QSpinBox* m_fpsSpinBox = nullptr;
    Fluent::FluentComboBox* m_maxSizeBox = nullptr;
    Fluent::FluentComboBox* m_codecBox = nullptr;
    QLabel* m_bitrateLabel = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_sizeLabel = nullptr;
    QLabel* m_codecLabel = nullptr;

    // 显示选项
    QLabel* m_optionsTitle = nullptr;
    Fluent::FluentToggle* m_reverseToggle = nullptr;
    Fluent::FluentToggle* m_toolbarToggle = nullptr;
    Fluent::FluentToggle* m_framelessToggle = nullptr;
    Fluent::FluentToggle* m_fpsToggle = nullptr;

    // 操作按钮
    Fluent::FluentButton* m_startBtn = nullptr;
    Fluent::FluentButton* m_stopBtn = nullptr;

    // 键位配置区
    QLabel* m_keymapTitle = nullptr;
    Fluent::FluentComboBox* m_keymapCombo = nullptr;
    QPushButton* m_keymapNewBtn = nullptr;
    QPushButton* m_keymapRefreshBtn = nullptr;
    QPushButton* m_keymapFolderBtn = nullptr;
    Fluent::FluentButton* m_keymapSaveBtn = nullptr;

    // 拟人参数
    QLabel* m_humanTitle = nullptr;
    Fluent::FluentSlider* m_randomOffsetSlider = nullptr;
    Fluent::FluentSlider* m_steerSmoothSlider = nullptr;
    Fluent::FluentSlider* m_steerCurveSlider = nullptr;
    Fluent::FluentSlider* m_slideCurveSlider = nullptr;
};

#endif // DEVICEDETAILPAGE_H
