/**
 * @file config.h
 * @brief 配置管理模块 / Configuration Management Module
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 *
 * 功能 / Features:
 * - 管理应用程序配置 (config.ini) / Manage application config (config.ini)
 * - 管理用户偏好设置 (userdata.ini) / Manage user preferences (userdata.ini)
 * - 设备专属配置 (窗口位置、昵称、键位映射) / Per-device config (window position, nickname, key mapping)
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <QObject>
#include <QPointer>
#include <QRect>

/**
 * @brief 用户启动配置结构体 / User Boot Configuration
 *
 * 包含各种开关状态、码率、路径等启动参数
 * Contains various switch states, bitrate, paths, and other startup parameters.
 */
struct UserBootConfig
{
    QString recordPath = "";
    quint32 bitRate = 2000000;
    int maxSizeIndex = 0;
    int recordFormatIndex = 0;
    int lockOrientationIndex = 0;
    int maxFps = 60;
    int maxTouchPoints = 10;  // 最大触摸点数（1-10）
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
    int videoCodecIndex   = 0;     // 0=H.264
};

class QSettings;

// ---------------------------------------------------------
// 配置管理类 (单例) / Configuration Manager (Singleton)
// 管理 config.ini (只读配置) 和 userdata.ini (用户偏好)
// Manages config.ini (read-only) and userdata.ini (user preferences)
// ---------------------------------------------------------
class Config : public QObject
{
    Q_OBJECT
public:
    static Config &getInstance();

    // 读取全局配置 (config.ini) / Read global config (config.ini)
    QString getLanguage();
    void setLanguage(const QString &lang);
    QString getTitle();
    int getMaxFps();
    int getDesktopOpenGL();
    int getSkin();
    int getRenderExpiredFrames();
    QString getServerPath();
    QString getAdbPath();
    QString getLogLevel();
    QString getCodecOptions();
    QString getCodecName();
    QStringList getConnectedGroups();

    // 读写用户配置 (userdata.ini) - 通用 / Read/write user config (userdata.ini) - general
    void setUserBootConfig(const UserBootConfig &config);
    UserBootConfig getUserBootConfig();
    void setTrayMessageShown(bool shown);
    bool getTrayMessageShown();

    // 使用协议接受状态 / License agreement accepted state
    void setAgreementAccepted(bool accepted);
    bool getAgreementAccepted();

    // 读写用户配置 - 设备专属 / Read/write user config - per-device
    void setNickName(const QString &serial, const QString &name);
    QString getNickName(const QString &serial);
    void setRect(const QString &serial, const QRect &rc);
    QRect getRect(const QString &serial);

    // 读写键位映射配置 / Read/write key mapping config
    void setKeyMap(const QString &serial, const QString &keyMapFile);
    QString getKeyMap(const QString &serial);

    void deleteGroup(const QString &serial);

    // IP 历史记录 / IP history
    void saveIpHistory(const QString &ip);
    QStringList getIpHistory();
    void clearIpHistory();

    // 端口历史记录 / Port history
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
