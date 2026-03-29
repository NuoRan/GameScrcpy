#include <QCoreApplication>
#include <filesystem>
#include <algorithm>
#include <set>
#include <cctype>

#include "config.h"
#include "IniConfig.h"
#include "StringUtils.h"

// ---------------------------------------------------------
// 配置文件键名与默认值定义
// ---------------------------------------------------------

#define GROUP_COMMON "common"

// 通用配置
#define COMMON_LANGUAGE_KEY "Language"
#define COMMON_LANGUAGE_DEF "Auto"

#define COMMON_TITLE_KEY "WindowTitle"
#define COMMON_TITLE_DEF QCoreApplication::applicationName()

#define COMMON_SERVER_PATH_KEY "ServerPath"
#define COMMON_SERVER_PATH_DEF "/data/local/tmp/scrcpy-server.jar"

#define COMMON_MAX_FPS_KEY "MaxFps"
#define COMMON_MAX_FPS_DEF 0

#define COMMON_DESKTOP_OPENGL_KEY "UseDesktopOpenGL"
#define COMMON_DESKTOP_OPENGL_DEF -1

#define COMMON_SKIN_KEY "UseSkin"
#define COMMON_SKIN_DEF 1

#define COMMON_RENDER_EXPIRED_FRAMES_KEY "RenderExpiredFrames"
#define COMMON_RENDER_EXPIRED_FRAMES_DEF 0

#define COMMON_ADB_PATH_KEY "AdbPath"
#define COMMON_ADB_PATH_DEF ""

#define COMMON_LOG_LEVEL_KEY "LogLevel"
#define COMMON_LOG_LEVEL_DEF "info"

#define COMMON_CODEC_OPTIONS_KEY "CodecOptions"
#define COMMON_CODEC_OPTIONS_DEF ""

#define COMMON_CODEC_NAME_KEY "CodecName"
#define COMMON_CODEC_NAME_DEF ""

// 用户启动配置
#define COMMON_RECORD_KEY "RecordPath"
#define COMMON_RECORD_DEF ""

#define COMMON_BITRATE_KEY "BitRate"
#define COMMON_BITRATE_DEF 2000000

#define COMMON_MAX_SIZE_INDEX_KEY "MaxSizeIndex"
#define COMMON_MAX_SIZE_INDEX_DEF 2

#define COMMON_RECORD_FORMAT_INDEX_KEY "RecordFormatIndex"
#define COMMON_RECORD_FORMAT_INDEX_DEF 0

#define COMMON_LOCK_ORIENTATION_INDEX_KEY "LockDirectionIndex"
#define COMMON_LOCK_ORIENTATION_INDEX_DEF 0

#define COMMON_MAX_FPS_USER_KEY "MaxFpsUser"
#define COMMON_MAX_FPS_USER_DEF 60

#define COMMON_RECORD_SCREEN_KEY "RecordScreen"
#define COMMON_RECORD_SCREEN_DEF false

#define COMMON_RECORD_BACKGROUD_KEY "RecordBackGround"
#define COMMON_RECORD_BACKGROUD_DEF false

#define COMMON_REVERSE_CONNECT_KEY "ReverseConnect"
#define COMMON_REVERSE_CONNECT_DEF true

#define COMMON_SHOW_FPS_KEY "ShowFPS"
#define COMMON_SHOW_FPS_DEF false

#define COMMON_WINDOW_ON_TOP_KEY "WindowOnTop"
#define COMMON_WINDOW_ON_TOP_DEF false

#define COMMON_AUTO_OFF_SCREEN_KEY "AutoOffScreen"
#define COMMON_AUTO_OFF_SCREEN_DEF false

#define COMMON_FRAMELESS_WINDOW_KEY "FramelessWindow"
#define COMMON_FRAMELESS_WINDOW_DEF false

#define COMMON_KEEP_ALIVE_KEY "KeepAlive"
#define COMMON_KEEP_ALIVE_DEF false

#define COMMON_SIMPLE_MODE_KEY "SimpleMode"
#define COMMON_SIMPLE_MODE_DEF false

#define COMMON_AUTO_UPDATE_DEVICE_KEY "AutoUpdateDevice"
#define COMMON_AUTO_UPDATE_DEVICE_DEF true

#define COMMON_TRAY_MESSAGE_SHOWN_KEY "TrayMessageShown"
#define COMMON_TRAY_MESSAGE_SHOWN_DEF false

#define COMMON_SHOW_TOOLBAR_KEY "showToolbar"
#define COMMON_SHOW_TOOLBAR_DEF true

#define COMMON_VIDEO_CODEC_INDEX_KEY "VideoCodecIndex"
#define COMMON_VIDEO_CODEC_INDEX_DEF 0

// 设备专属配置（窗口位置、昵称、键位）
#define SERIAL_WINDOW_RECT_KEY_X "WindowRectX"
#define SERIAL_WINDOW_RECT_KEY_Y "WindowRectY"
#define SERIAL_WINDOW_RECT_KEY_W "WindowRectW"
#define SERIAL_WINDOW_RECT_KEY_H "WindowRectH"
#define SERIAL_WINDOW_RECT_KEY_DEF -1
#define SERIAL_NICK_NAME_KEY "NickName"
#define SERIAL_NICK_NAME_DEF "Phone"

// 历史记录
#define IP_HISTORY_KEY "IpHistory"
#define IP_HISTORY_DEF ""
#define IP_HISTORY_MAX 10

#define PORT_HISTORY_KEY "PortHistory"
#define PORT_HISTORY_DEF ""
#define PORT_HISTORY_MAX 10

#define SERIAL_KEYMAP_KEY "KeyMap"
#define SERIAL_KEYMAP_DEF "default.json"

std::string Config::s_configPath;

// ---------------------------------------------------------
// 构造函数与单例获取
// 初始化 IniConfig 读取 INI 文件
// ---------------------------------------------------------
Config::Config()
{
    std::string cfgPath = getConfigPath() + "/config.ini";
    std::string usrPath = getConfigPath() + "/userdata.ini";
    m_settings = new IniConfig(cfgPath);
    m_userData = new IniConfig(usrPath);
}

Config &Config::getInstance()
{
    static Config config;
    return config;
}

// ---------------------------------------------------------
// 获取配置文件存储路径
// 兼容 Windows, Linux 和 macOS (App Bundle)
// ---------------------------------------------------------
const std::string &Config::getConfigPath()
{
    if (s_configPath.empty()) {
        // Use _dupenv_s for MSVC safety
        char *envVal = nullptr;
        size_t envLen = 0;
        if (_dupenv_s(&envVal, &envLen, "KZSCRCPY_CONFIG_PATH") == 0 && envVal) {
            s_configPath = envVal;
            free(envVal);
        }
        namespace fs = std::filesystem;
        if (s_configPath.empty() || !fs::is_directory(strutil::toWide(s_configPath))) {
            // 使用可执行文件目录下的 config 子目录
            std::string exeConfig = strutil::appDirPath() + "/config";
            if (fs::is_directory(strutil::toWide(exeConfig))) {
                s_configPath = exeConfig;
            } else {
                // 最终回退: 创建 exe 同级 config 目录
                fs::create_directories(fs::path(strutil::toWide(exeConfig)));
                s_configPath = exeConfig;
            }
        }
    }
    return s_configPath;
}

// ---------------------------------------------------------
// 用户启动配置读写
// 包含码率、分辨率、全屏、置顶等设置
// ---------------------------------------------------------
void Config::setUserBootConfig(const UserBootConfig &config)
{
    m_userData->setString("common/RecordPath", config.recordPath);
    m_userData->setUInt("common/BitRate", config.bitRate);
    m_userData->setInt("common/MaxSizeIndex", config.maxSizeIndex);
    m_userData->setInt("common/RecordFormatIndex", config.recordFormatIndex);
    m_userData->setBool("common/FramelessWindow", config.framelessWindow);
    m_userData->setInt("common/LockDirectionIndex", config.lockOrientationIndex);
    m_userData->setInt("common/MaxFpsUser", config.maxFps);
    m_userData->setBool("common/RecordScreen", config.recordScreen);
    m_userData->setBool("common/RecordBackGround", config.recordBackground);
    m_userData->setBool("common/ReverseConnect", config.reverseConnect);
    m_userData->setBool("common/ShowFPS", config.showFPS);
    m_userData->setBool("common/WindowOnTop", config.windowOnTop);
    m_userData->setBool("common/AutoOffScreen", config.autoOffScreen);
    m_userData->setBool("common/KeepAlive", config.keepAlive);
    m_userData->setBool("common/SimpleMode", config.simpleMode);
    m_userData->setBool("common/AutoUpdateDevice", config.autoUpdateDevice);
    m_userData->setBool("common/showToolbar", config.showToolbar);
    m_userData->setInt("common/VideoCodecIndex", config.videoCodecIndex);
    m_userData->sync();
}

UserBootConfig Config::getUserBootConfig()
{
    UserBootConfig config;
    config.recordPath = m_userData->getString("common/RecordPath", COMMON_RECORD_DEF);
    config.bitRate = m_userData->getUInt("common/BitRate", COMMON_BITRATE_DEF);
    config.maxSizeIndex = m_userData->getInt("common/MaxSizeIndex", COMMON_MAX_SIZE_INDEX_DEF);
    config.recordFormatIndex = m_userData->getInt("common/RecordFormatIndex", COMMON_RECORD_FORMAT_INDEX_DEF);
    config.lockOrientationIndex = m_userData->getInt("common/LockDirectionIndex", COMMON_LOCK_ORIENTATION_INDEX_DEF);
    config.maxFps = m_userData->getInt("common/MaxFpsUser", COMMON_MAX_FPS_USER_DEF);
    config.framelessWindow = m_userData->getBool("common/FramelessWindow", COMMON_FRAMELESS_WINDOW_DEF);
    config.recordScreen = m_userData->getBool("common/RecordScreen", COMMON_RECORD_SCREEN_DEF);
    config.recordBackground = m_userData->getBool("common/RecordBackGround", COMMON_RECORD_BACKGROUD_DEF);
    config.reverseConnect = m_userData->getBool("common/ReverseConnect", COMMON_REVERSE_CONNECT_DEF);
    config.showFPS = m_userData->getBool("common/ShowFPS", COMMON_SHOW_FPS_DEF);
    config.windowOnTop = m_userData->getBool("common/WindowOnTop", COMMON_WINDOW_ON_TOP_DEF);
    config.autoOffScreen = m_userData->getBool("common/AutoOffScreen", COMMON_AUTO_OFF_SCREEN_DEF);
    config.keepAlive = m_userData->getBool("common/KeepAlive", COMMON_KEEP_ALIVE_DEF);
    config.simpleMode = m_userData->getBool("common/SimpleMode", COMMON_SIMPLE_MODE_DEF);
    config.autoUpdateDevice = m_userData->getBool("common/AutoUpdateDevice", COMMON_AUTO_UPDATE_DEVICE_DEF);
    config.showToolbar = m_userData->getBool("common/showToolbar", COMMON_SHOW_TOOLBAR_DEF);
    config.videoCodecIndex = m_userData->getInt("common/VideoCodecIndex", COMMON_VIDEO_CODEC_INDEX_DEF);
    return config;
}

// ---------------------------------------------------------
// 托盘消息状态
// ---------------------------------------------------------
void Config::setTrayMessageShown(bool shown)
{
    m_userData->setBool("common/TrayMessageShown", shown);
    m_userData->sync();
}

bool Config::getTrayMessageShown()
{
    return m_userData->getBool("common/TrayMessageShown", COMMON_TRAY_MESSAGE_SHOWN_DEF);
}

// ---------------------------------------------------------
// 使用协议接受状态
// ---------------------------------------------------------
void Config::setAgreementAccepted(bool accepted)
{
    m_userData->setBool("common/agreementAccepted", accepted);
    m_userData->sync();
}

bool Config::getAgreementAccepted()
{
    return m_userData->getBool("common/agreementAccepted", false);
}

// ---------------------------------------------------------
// 分场景引导状态
// ---------------------------------------------------------
void Config::setOnboardingCompleted(const std::string &scene, bool completed)
{
    std::string key = "onboarding/" + scene;
    m_userData->setBool(key, completed);
    m_userData->sync();
}

bool Config::getOnboardingCompleted(const std::string &scene)
{
    std::string key = "onboarding/" + scene;
    return m_userData->getBool(key, false);
}

void Config::resetAllOnboarding()
{
    // keysInSection 返回不带 section 前缀的短键名，
    // 但 remove() 需要 "section/key" 格式，所以手动拼接前缀。
    auto allKeys = m_userData->keysInSection("onboarding");
    for (const auto &k : allKeys) {
        m_userData->remove("onboarding/" + k);
    }
    m_userData->sync();
}

// ---------------------------------------------------------
// 设备特定配置（位置、昵称）
// ---------------------------------------------------------

// 辅助函数：将 serial 转换为安全的组名（替换特殊字符）
static std::string safeGroupName(const std::string &serial) {
    std::string safe = serial;
    std::replace(safe.begin(), safe.end(), ':', '_');  // WiFi 设备 serial 包含冒号
    std::replace(safe.begin(), safe.end(), '/', '_');
    return safe;
}

void Config::setRect(const std::string &serial, const Rect &rc)
{
    std::string group = safeGroupName(serial);
    m_userData->setInt(group + "/WindowRectX", rc.x);
    m_userData->setInt(group + "/WindowRectY", rc.y);
    m_userData->setInt(group + "/WindowRectW", rc.width);
    m_userData->setInt(group + "/WindowRectH", rc.height);
    m_userData->sync();
}

Rect Config::getRect(const std::string &serial)
{
    Rect rc;
    std::string group = safeGroupName(serial);
    rc.x = m_userData->getInt(group + "/WindowRectX", SERIAL_WINDOW_RECT_KEY_DEF);
    rc.y = m_userData->getInt(group + "/WindowRectY", SERIAL_WINDOW_RECT_KEY_DEF);
    rc.width = m_userData->getInt(group + "/WindowRectW", SERIAL_WINDOW_RECT_KEY_DEF);
    rc.height = m_userData->getInt(group + "/WindowRectH", SERIAL_WINDOW_RECT_KEY_DEF);
    return rc;
}

void Config::setNickName(const std::string &serial, const std::string &name)
{
    std::string group = safeGroupName(serial);
    m_userData->setString(group + "/NickName", name);
    m_userData->sync();
}

std::string Config::getNickName(const std::string &serial)
{
    std::string group = safeGroupName(serial);
    return m_userData->getString(group + "/NickName", SERIAL_NICK_NAME_DEF);
}

// ---------------------------------------------------------
// 全局通用只读配置 (config.ini)
// ---------------------------------------------------------
int Config::getMaxFps()
{
    return m_settings->getInt("common/MaxFps", COMMON_MAX_FPS_DEF);
}

int Config::getDesktopOpenGL()
{
    return m_settings->getInt("common/UseDesktopOpenGL", COMMON_DESKTOP_OPENGL_DEF);
}

int Config::getSkin()
{
    return 0; // 强制禁用皮肤
}

int Config::getRenderExpiredFrames()
{
    return m_settings->getInt("common/RenderExpiredFrames", COMMON_RENDER_EXPIRED_FRAMES_DEF);
}

std::string Config::getServerPath()
{
    return m_settings->getString("common/ServerPath", COMMON_SERVER_PATH_DEF);
}

std::string Config::getAdbPath()
{
    return m_settings->getString("common/AdbPath", COMMON_ADB_PATH_DEF);
}

std::string Config::getLogLevel()
{
    return m_settings->getString("common/LogLevel", COMMON_LOG_LEVEL_DEF);
}

std::string Config::getCodecOptions()
{
    return m_settings->getString("common/CodecOptions", COMMON_CODEC_OPTIONS_DEF);
}

std::string Config::getCodecName()
{
    return m_settings->getString("common/CodecName", COMMON_CODEC_NAME_DEF);
}

std::vector<std::string> Config::getConnectedGroups()
{
    auto allKeys = m_userData->allKeys();
    std::set<std::string> sections;
    for (const auto &k : allKeys) {
        auto pos = k.find('/');
        if (pos != std::string::npos) {
            std::string section = k.substr(0, pos);
            if (section != "common" && section != "onboarding" && section != "General") {
                sections.insert(section);
            }
        }
    }
    return std::vector<std::string>(sections.begin(), sections.end());
}

void Config::deleteGroup(const std::string &serial)
{
    std::string prefix = serial;
    auto allKeys = m_userData->allKeys();
    for (const auto &k : allKeys) {
        if (k.find(prefix + "/") == 0 || k == prefix) {
            m_userData->remove(k);
        }
    }
    m_userData->sync();
}

std::string Config::getLanguage()
{
    std::string userLang = m_userData->getString("common/Language", "");
    if (!userLang.empty()) {
        return userLang;
    }
    return m_settings->getString("common/Language", COMMON_LANGUAGE_DEF);
}

void Config::setLanguage(const std::string &lang)
{
    m_userData->setString("common/Language", lang);
    m_userData->sync();
}

std::string Config::getTitle()
{
    std::string title = m_settings->getString("common/WindowTitle", "");
    if (title.empty()) {
        return strutil::fromQ(QCoreApplication::applicationName());
    }
    return title;
}

namespace {

std::string trimCopy(const std::string& value)
{
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

void appendHistoryNormalized(const std::string& raw, std::vector<std::string>& output)
{
    std::string item = trimCopy(raw);
    if (item.empty()) return;

    const auto hasDelimiter = item.find(',') != std::string::npos
        || item.find(';') != std::string::npos
        || item.find('\n') != std::string::npos
        || item.find('\r') != std::string::npos;

    if (!hasDelimiter) {
        output.push_back(item);
        return;
    }

    std::string token;
    token.reserve(item.size());
    auto flushToken = [&output, &token]() {
        std::string normalized = trimCopy(token);
        if (!normalized.empty()) {
            output.push_back(normalized);
        }
        token.clear();
    };

    for (char ch : item) {
        if (ch == ',' || ch == ';' || ch == '\n' || ch == '\r') {
            flushToken();
        } else {
            token.push_back(ch);
        }
    }
    flushToken();
}

void deduplicatePreserveOrder(std::vector<std::string>& values)
{
    std::set<std::string> seen;
    std::vector<std::string> result;
    result.reserve(values.size());
    for (auto& value : values) {
        if (value.empty()) continue;
        if (seen.insert(value).second) {
            result.push_back(value);
        }
    }
    values.swap(result);
}

} // namespace

// ---------------------------------------------------------
// IP 历史记录管理
// ---------------------------------------------------------
void Config::saveIpHistory(const std::string &ip)
{
    auto ipList = getIpHistory();
    // Remove duplicates
    ipList.erase(std::remove(ipList.begin(), ipList.end(), ip), ipList.end());
    ipList.insert(ipList.begin(), ip); // Prepend

    // Limit history size
    while (static_cast<int>(ipList.size()) > IP_HISTORY_MAX) {
        ipList.pop_back();
    }

    m_userData->setStringList("General/IpHistory", ipList);
    m_userData->sync();
}

std::vector<std::string> Config::getIpHistory()
{
    auto vec = m_userData->getStringList("General/IpHistory");
    std::vector<std::string> result;
    for (const auto &s : vec) {
        appendHistoryNormalized(s, result);
    }
    deduplicatePreserveOrder(result);
    return result;
}

void Config::clearIpHistory()
{
    m_userData->remove("General/IpHistory");
    m_userData->sync();
}

// ---------------------------------------------------------
// 端口历史记录管理
// ---------------------------------------------------------
void Config::savePortHistory(const std::string &port)
{
    auto portList = getPortHistory();
    portList.erase(std::remove(portList.begin(), portList.end(), port), portList.end());
    portList.insert(portList.begin(), port);

    while (static_cast<int>(portList.size()) > PORT_HISTORY_MAX) {
        portList.pop_back();
    }

    m_userData->setStringList("General/PortHistory", portList);
    m_userData->sync();
}

std::vector<std::string> Config::getPortHistory()
{
    auto vec = m_userData->getStringList("General/PortHistory");
    std::vector<std::string> result;
    for (const auto &s : vec) {
        appendHistoryNormalized(s, result);
    }
    deduplicatePreserveOrder(result);
    return result;
}

void Config::clearPortHistory()
{
    m_userData->remove("General/PortHistory");
    m_userData->sync();
}

// ---------------------------------------------------------
// 键位映射文件配置
// ---------------------------------------------------------
void Config::setKeyMap(const std::string &serial, const std::string &keyMapFile)
{
    std::string group = safeGroupName(serial);
    m_userData->setString(group + "/KeyMap", keyMapFile);
    m_userData->sync();
}

std::string Config::getKeyMap(const std::string &serial)
{
    std::string group = safeGroupName(serial);
    return m_userData->getString(group + "/KeyMap", SERIAL_KEYMAP_DEF);
}
