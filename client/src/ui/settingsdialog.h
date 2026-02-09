#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QEvent>

// ---------------------------------------------------------
// 设置对话框 / Settings Dialog
// 配置视频参数、显示选项、无线连接等
// Configure video params, display options, wireless connection, etc.
// ---------------------------------------------------------
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    // 获取设置值
    QString getSerial() const;
    quint32 getBitRate() const;
    quint16 getMaxSize() const;
    int getMaxSizeIndex() const;
    int getMaxFps() const;
    int getMaxTouchPoints() const;
    bool isReverseConnect() const;
    bool showToolbar() const;
    bool isFrameless() const;
    bool showFPS() const;

    // 无线连接相关
    QString getDeviceIP() const;
    QString getDevicePort() const;

    // 设置值
    void setSerialList(const QStringList &serials);
    void setCurrentSerial(const QString &serial);
    void setBitRate(quint32 bitRate);
    void setMaxSizeIndex(int index);
    void setMaxFps(int fps);
    void setMaxTouchPoints(int points);
    void setReverseConnect(bool checked);
    void setShowToolbar(bool checked);
    void setFrameless(bool checked);
    void setShowFPS(bool checked);
    void setDeviceIP(const QString &ip);
    void setDevicePort(const QString &port);
    void setIpHistory(const QStringList &ips);
    void setPortHistory(const QStringList &ports);

signals:
    void wirelessConnect();
    void wirelessDisconnect();
    void requestDeviceIP();
    void startAdbd();

private:
    void setupUI();
    void applyStyle();
    void retranslateUi();

protected:
    void changeEvent(QEvent *event) override;

private:
    // 需要动态翻译的标签
    QLabel *m_videoTitle;
    QLabel *m_bitrateLabel;
    QLabel *m_fpsLabel;
    QLabel *m_sizeLabel;
    QLabel *m_touchLabel;
    QLabel *m_optionsTitle;
    QLabel *m_wifiTitle;
    QLabel *m_ipLabel;

    // 视频参数
    QLineEdit *m_bitRateEdit;
    QComboBox *m_bitRateUnit;
    QSpinBox *m_fpsSpinBox;
    QComboBox *m_maxSizeBox;
    QSpinBox *m_touchPointsSpinBox;

    // 显示选项
    QCheckBox *m_reverseCheck;
    QCheckBox *m_toolbarCheck;
    QCheckBox *m_framelessCheck;
    QCheckBox *m_fpsCheck;

    // 无线连接
    QComboBox *m_ipEdit;
    QComboBox *m_portEdit;
    QPushButton *m_connectBtn;
    QPushButton *m_disconnectBtn;
    QPushButton *m_getIpBtn;
    QPushButton *m_adbdBtn;
};

#endif // SETTINGSDIALOG_H
