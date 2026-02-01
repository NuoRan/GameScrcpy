#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>

// 设置对话框
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

private:
    // 视频参数
    QLineEdit *m_bitRateEdit;
    QComboBox *m_bitRateUnit;
    QComboBox *m_maxSizeBox;

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
