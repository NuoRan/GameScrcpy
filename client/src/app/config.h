#ifndef CONFIG_H
#define CONFIG_H

#include <QObject>
#include <QPointer>
#include <QRect>

// ---------------------------------------------------------
// 用户启动配置结构体
// 包含各种开关状态、码率、路径等
// ---------------------------------------------------------
struct UserBootConfig
{
    QString recordPath = "";
    quint32 bitRate = 2000000;
    int maxSizeIndex = 0;
    int recordFormatIndex = 0;
    int lockOrientationIndex = 0;
    bool recordScreen     = false;
    bool recordBackground = false;
    bool reverseConnect   = true;
    bool showFPS          = false;
    bool windowOnTop      = false;
    bool autoOffScreen    = false;
    bool framelessWindow  = false;
    bool keepAlive        = false;
    bool simpleMode       = false;
    bool autoUpdateDevice = true;
    bool showToolbar      = true;
};

class QSettings;

// ---------------------------------------------------------
// 配置管理类 (单例)
// 管理 config.ini (只读配置) 和 userdata.ini (用户偏好)
// ---------------------------------------------------------
class Config : public QObject
{
    Q_OBJECT
public:
    static Config &getInstance();

    // 读取全局配置 (config.ini)
    QString getLanguage();
    QString getTitle();
    int getMaxFps();
    int getDesktopOpenGL();
    int getSkin();
    int getRenderExpiredFrames();
    QString getPushFilePath();
    QString getServerPath();
    QString getAdbPath();
    QString getLogLevel();
    QString getCodecOptions();
    QString getCodecName();
    QStringList getConnectedGroups();

    // 读写用户配置 (userdata.ini) - 通用
    void setUserBootConfig(const UserBootConfig &config);
    UserBootConfig getUserBootConfig();
    void setTrayMessageShown(bool shown);
    bool getTrayMessageShown();

    // 读写用户配置 - 设备专属
    void setNickName(const QString &serial, const QString &name);
    QString getNickName(const QString &serial);
    void setRect(const QString &serial, const QRect &rc);
    QRect getRect(const QString &serial);

    // 读写键位映射配置
    void setKeyMap(const QString &serial, const QString &keyMapFile);
    QString getKeyMap(const QString &serial);

    void deleteGroup(const QString &serial);

    // IP 历史记录
    void saveIpHistory(const QString &ip);
    QStringList getIpHistory();
    void clearIpHistory();

    // 端口历史记录
    void savePortHistory(const QString &port);
    QStringList getPortHistory();
    void clearPortHistory();

private:
    explicit Config(QObject *parent = nullptr);
    const QString &getConfigPath();

private:
    static QString s_configPath;
    QPointer<QSettings> m_settings; // 对应 config.ini
    QPointer<QSettings> m_userData; // 对应 userdata.ini
};

#endif // CONFIG_H
