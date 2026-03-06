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

#include <string>
#include <vector>
#include <cstdint>
#include "GameTypes.h"

/**
 * @brief 用户启动配置结构体 / User Boot Configuration
 *
 * 包含各种开关状态、码率、路径等启动参数
 * Contains various switch states, bitrate, paths, and other startup parameters.
 */
struct UserBootConfig
{
    std::string recordPath;
    uint32_t bitRate = 2000000;
    int maxSizeIndex = 0;
    int recordFormatIndex = 0;
    int lockOrientationIndex = 0;
    int maxFps = 60;
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

class IniConfig;

// ---------------------------------------------------------
// 配置管理类 (单例) / Configuration Manager (Singleton)
// 管理 config.ini (只读配置) 和 userdata.ini (用户偏好)
// Manages config.ini (read-only) and userdata.ini (user preferences)
// ---------------------------------------------------------
class Config
{
public:
    static Config &getInstance();

    // 读取全局配置 (config.ini) / Read global config (config.ini)
    std::string getLanguage();
    void setLanguage(const std::string &lang);
    std::string getTitle();
    int getMaxFps();
    int getDesktopOpenGL();
    int getSkin();
    int getRenderExpiredFrames();
    std::string getServerPath();
    std::string getAdbPath();
    std::string getLogLevel();
    std::string getCodecOptions();
    std::string getCodecName();
    std::vector<std::string> getConnectedGroups();

    // 读写用户配置 (userdata.ini) - 通用 / Read/write user config (userdata.ini) - general
    void setUserBootConfig(const UserBootConfig &config);
    UserBootConfig getUserBootConfig();
    void setTrayMessageShown(bool shown);
    bool getTrayMessageShown();

    // 使用协议接受状态 / License agreement accepted state
    void setAgreementAccepted(bool accepted);
    bool getAgreementAccepted();

    // 分场景引导状态 / Per-scene onboarding state
    void setOnboardingCompleted(const std::string &scene, bool completed);
    bool getOnboardingCompleted(const std::string &scene);
    void resetAllOnboarding();  // 重置所有场景的引导状态

    // 场景常量
    static constexpr const char* OB_MAIN_WINDOW     = "mainWindow";
    static constexpr const char* OB_VIDEO_FORM      = "videoForm";
    static constexpr const char* OB_EDIT_MODE       = "editMode";
    static constexpr const char* OB_SELECTION_TOOL   = "selectionTool";
    static constexpr const char* OB_SCRIPT_EDITOR   = "scriptEditor";

    // 读写用户配置 - 设备专属 / Read/write user config - per-device
    void setNickName(const std::string &serial, const std::string &name);
    std::string getNickName(const std::string &serial);
    void setRect(const std::string &serial, const Rect &rc);
    Rect getRect(const std::string &serial);

    // 读写键位映射配置 / Read/write key mapping config
    void setKeyMap(const std::string &serial, const std::string &keyMapFile);
    std::string getKeyMap(const std::string &serial);

    void deleteGroup(const std::string &serial);

    // IP 历史记录 / IP history
    void saveIpHistory(const std::string &ip);
    std::vector<std::string> getIpHistory();
    void clearIpHistory();

    // 端口历史记录 / Port history
    void savePortHistory(const std::string &port);
    std::vector<std::string> getPortHistory();
    void clearPortHistory();

private:
    Config();
    const std::string &getConfigPath();

private:
    static std::string s_configPath;
    IniConfig *m_settings = nullptr; // 对应 config.ini
    IniConfig *m_userData = nullptr; // 对应 userdata.ini
};

#endif // CONFIG_H
