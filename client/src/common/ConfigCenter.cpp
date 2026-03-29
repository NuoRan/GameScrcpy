#include "ConfigCenter.h"
#include "IniConfig.h"
#include "StringUtils.h"
#include <algorithm>
#include <filesystem>

// Helper: convert IniConfig string value to ConfigValue, trying to preserve type
static ConfigValue iniValueToConfigValue(const std::string& val) {
    if (val.empty()) return ConfigValue();
    // Try bool
    if (val == "true") return ConfigValue(true);
    if (val == "false") return ConfigValue(false);
    // Try int
    try {
        size_t pos = 0;
        long long llVal = std::stoll(val, &pos);
        if (pos == val.size()) {
            if (llVal >= 0 && llVal <= static_cast<long long>(std::numeric_limits<uint32_t>::max()) && llVal > std::numeric_limits<int>::max())
                return ConfigValue(static_cast<uint32_t>(llVal));
            if (llVal >= std::numeric_limits<int>::min() && llVal <= std::numeric_limits<int>::max())
                return ConfigValue(static_cast<int>(llVal));
        }
    } catch (...) {}
    // Try double (only if it contains a dot)
    if (val.find('.') != std::string::npos) {
        try {
            size_t pos = 0;
            double dblVal = std::stod(val, &pos);
            if (pos == val.size()) return ConfigValue(dblVal);
        } catch (...) {}
    }
    // String
    return ConfigValue(val);
}

// Helper: convert ConfigValue to string for IniConfig storage
static std::string configValueToIniString(const ConfigValue& val) {
    if (auto* p = std::get_if<bool>(&val)) return *p ? "true" : "false";
    if (auto* p = std::get_if<int>(&val)) return std::to_string(*p);
    if (auto* p = std::get_if<uint32_t>(&val)) return std::to_string(*p);
    if (auto* p = std::get_if<double>(&val)) return std::to_string(*p);
    if (auto* p = std::get_if<Rect>(&val)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "@Rect(%d %d %d %d)", p->x, p->y, p->width, p->height);
        return buf;
    }
    if (auto* p = std::get_if<std::string>(&val)) return *p;
    return ""; // monostate
}

// Helper: check if IniConfig has a key
static bool iniContains(IniConfig* cfg, const std::string& key) {
    if (!cfg) return false;
    return cfg->contains(key);
}

// Helper: get value from IniConfig as ConfigValue
static ConfigValue iniGet(IniConfig* cfg, const std::string& key) {
    if (!cfg) return ConfigValue();
    std::string val = cfg->getString(key);
    return iniValueToConfigValue(val);
}

// Helper: set value in IniConfig from ConfigValue
static void iniSet(IniConfig* cfg, const std::string& key, const ConfigValue& value) {
    if (!cfg) return;
    cfg->setString(key, configValueToIniString(value));
}



#define LOG_TAG "ConfigCenter"
#include "Logger.h"

namespace qsc {

ConfigCenter* ConfigCenter::s_instance = nullptr;
ConfigCenter* ConfigCenter::s_injectedInstance = nullptr;

ConfigCenter& ConfigCenter::instance()
{
    if (s_injectedInstance) {
        return *s_injectedInstance;
    }
    if (!s_instance) {
        s_instance = new ConfigCenter();
    }
    return *s_instance;
}

void ConfigCenter::injectInstance(ConfigCenter* instance)
{
    s_injectedInstance = instance;
}

void ConfigCenter::resetInstance()
{
    s_injectedInstance = nullptr;
}

ConfigCenter::ConfigCenter()
{
    registerDefaults();
}

ConfigCenter::~ConfigCenter()
{
    delete m_globalConfig;
    delete m_userConfig;
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void ConfigCenter::registerDefaults()
{
    using S = std::string;
    // 全局配置默认值
    m_defaults["common/language"] = S("auto");
    m_defaults["common/title"] = S("GameScrcpy");
    m_defaults["common/maxFps"] = 60;
    m_defaults["common/desktopOpenGL"] = -1;
    m_defaults["common/skin"] = 1;
    m_defaults["common/renderExpiredFrames"] = 0;
    m_defaults["common/serverPath"] = S("");
    m_defaults["common/adbPath"] = S("");
    m_defaults["common/logLevel"] = S("*:W");
    m_defaults["common/codecOptions"] = S("");
    m_defaults["common/codecName"] = S("");

    // 用户配置默认值
    m_defaults["user/recordPath"] = S("");
    m_defaults["user/bitRate"] = uint32_t(4000000);  // 降低默认码率，WiFi更稳定
    m_defaults["user/maxSizeIndex"] = 0;
    m_defaults["user/recordFormatIndex"] = 0;
    m_defaults["user/lockOrientationIndex"] = 0;
    m_defaults["user/recordScreen"] = false;
    m_defaults["user/recordBackground"] = false;
    m_defaults["user/reverseConnect"] = true;
    m_defaults["user/showFPS"] = false;
    m_defaults["user/windowOnTop"] = false;
    m_defaults["user/autoOffScreen"] = false;
    m_defaults["user/framelessWindow"] = false;
    m_defaults["user/keepAlive"] = false;
    m_defaults["user/simpleMode"] = false;
    m_defaults["user/autoUpdateDevice"] = true;
    m_defaults["user/showToolbar"] = true;
    m_defaults["user/randomOffset"] = 0;  // 随机偏移范围 0~100
    m_defaults["user/steerWheelSmooth"] = 0;  // 轮盘平滑度 0~100
    m_defaults["user/steerWheelCurve"] = 0;   // 轮盘拟人曲线 0~100
    m_defaults["user/slideCurve"] = 30;        // 滑动轨迹曲线 0~100
    m_defaults["user/keyMapOverlayOpacity"] = 60;  // 键位提示层透明度 0~100
    m_defaults["user/keyMapOverlayVisible"] = false;  // 键位提示层是否显示
    m_defaults["user/scriptTipOpacity"] = 70;  // 脚本弹窗透明度 0~100
    m_defaults["user/sharpenStrength"] = 0;     // 锐化强度 0~100
    m_defaults["user/aoaResWidth"] = 1080;
    m_defaults["user/aoaResHeight"] = 2400;
    m_defaults["user/esp32FallbackRotation"] = 270;
    m_defaults["user/touchMode"] = 0;
    m_defaults["user/themeIndex"] = 0;
    m_defaults["user/accentIndex"] = 0;
    m_defaults["user/videoChannelEnabled"] = true;
    m_defaults["user/audioChannelEnabled"] = false;
    m_defaults["user/controlChannelEnabled"] = true;
    m_defaults["user/auxChannelEnabled"] = true;
}

bool ConfigCenter::initialize(const std::string& configPath, const std::string& userDataPath)
{
    std::lock_guard<std::recursive_mutex> locker(m_mutex);

    if (m_initialized) {
        return true;
    }

    // 确定配置文件路径
    std::string globalPath = configPath;
    if (globalPath.empty()) {
        globalPath = strutil::appDirPath() + "/config/config.ini";
    }

    std::string userPath = userDataPath;
    if (userPath.empty()) {
        userPath = strutil::appDirPath() + "/config/userdata.ini";
    }

    // 确保目录存在
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(strutil::toWide(globalPath)).parent_path());
    fs::create_directories(fs::path(strutil::toWide(userPath)).parent_path());

    // 创建 IniConfig 对象
    m_globalConfig = new IniConfig(strutil::toWide(globalPath));
    m_userConfig = new IniConfig(strutil::toWide(userPath));

    m_initialized = true;

    return true;
}

bool ConfigCenter::isInitialized() const
{
    return m_initialized;
}

ConfigValue ConfigCenter::get(const std::string& key, const ConfigValue& defaultValue) const
{
    std::lock_guard<std::recursive_mutex> locker(m_mutex);

    // 1. 检查运行时覆盖
    if (m_overrides.count(key)) {
        return m_overrides.at(key);
    }

    // 2. 检查用户配置
    if (iniContains(m_userConfig, key)) {
        return iniGet(m_userConfig, key);
    }

    // 3. 检查全局配置
    if (iniContains(m_globalConfig, key)) {
        return iniGet(m_globalConfig, key);
    }

    // 4. 检查默认值
    if (m_defaults.count(key)) {
        return m_defaults.at(key);
    }

    return defaultValue;
}

void ConfigCenter::set(const std::string& key, const ConfigValue& value, bool persistent)
{
    ConfigValue oldValue;
    {
        std::lock_guard<std::recursive_mutex> locker(m_mutex);
        oldValue = get(key);

        if (persistent && m_userConfig) {
            iniSet(m_userConfig, key, value);
            m_userConfig->sync();
        } else {
            m_overrides[key] = value;
        }
    }

    if (oldValue != value) {
        notifyChange(key, oldValue, value);
    }
}

void ConfigCenter::setOverride(const std::string& key, const ConfigValue& value)
{
    ConfigValue oldValue;
    {
        std::lock_guard<std::recursive_mutex> locker(m_mutex);
        oldValue = get(key);
        m_overrides[key] = value;
    }

    if (oldValue != value) {
        notifyChange(key, oldValue, value);
    }
}

void ConfigCenter::removeOverride(const std::string& key)
{
    std::lock_guard<std::recursive_mutex> locker(m_mutex);
    m_overrides.erase(key);
}

bool ConfigCenter::contains(const std::string& key) const
{
    std::lock_guard<std::recursive_mutex> locker(m_mutex);
    return m_overrides.count(key) ||
           iniContains(m_userConfig, key) ||
           iniContains(m_globalConfig, key) ||
           m_defaults.count(key);
}

void ConfigCenter::remove(const std::string& key)
{
    std::lock_guard<std::recursive_mutex> locker(m_mutex);
    m_overrides.erase(key);
    if (m_userConfig) {
        m_userConfig->remove(key);
        m_userConfig->sync();
    }
}

// --- 全局配置快捷方法 ---
std::string ConfigCenter::language() const { return getString("common/language", "auto"); }
std::string ConfigCenter::title() const { return getString("common/title", "GameScrcpy"); }
int ConfigCenter::maxFps() const { return get<int>("common/maxFps", 60); }
int ConfigCenter::desktopOpenGL() const { return get<int>("common/desktopOpenGL", -1); }
bool ConfigCenter::useSkin() const { return get<int>("common/skin", 1) != 0; }
bool ConfigCenter::renderExpiredFrames() const { return get<int>("common/renderExpiredFrames", 0) != 0; }
std::string ConfigCenter::serverPath() const { return getString("common/serverPath", ""); }
std::string ConfigCenter::adbPath() const { return getString("common/adbPath", ""); }
std::string ConfigCenter::logLevel() const { return getString("common/logLevel", "*:W"); }
std::string ConfigCenter::codecOptions() const { return getString("common/codecOptions", ""); }
std::string ConfigCenter::codecName() const { return getString("common/codecName", ""); }

// getString implementation
std::string ConfigCenter::getString(const std::string& key, const std::string& defaultValue) const
{
    return configval::toString(get(key, ConfigValue(std::string(defaultValue))), defaultValue);
}

// --- 用户配置快捷方法 ---
std::string ConfigCenter::recordPath() const { return getString("user/recordPath", ""); }
void ConfigCenter::setRecordPath(const std::string& path) { set("user/recordPath", ConfigValue(std::string(path))); }

uint32_t ConfigCenter::bitRate() const { return get<uint32_t>("user/bitRate", 4000000); }
void ConfigCenter::setBitRate(uint32_t rate) { set("user/bitRate", rate); }

int ConfigCenter::maxSizeIndex() const { return get<int>("user/maxSizeIndex", 0); }
void ConfigCenter::setMaxSizeIndex(int index) { set("user/maxSizeIndex", index); }

bool ConfigCenter::reverseConnect() const { return get<bool>("user/reverseConnect", true); }
void ConfigCenter::setReverseConnect(bool enable) { set("user/reverseConnect", enable); }

bool ConfigCenter::showFPS() const { return get<bool>("user/showFPS", false); }
void ConfigCenter::setShowFPS(bool show) { set("user/showFPS", show); }

bool ConfigCenter::windowOnTop() const { return get<bool>("user/windowOnTop", false); }
void ConfigCenter::setWindowOnTop(bool onTop) { set("user/windowOnTop", onTop); }

bool ConfigCenter::autoOffScreen() const { return get<bool>("user/autoOffScreen", false); }
void ConfigCenter::setAutoOffScreen(bool enable) { set("user/autoOffScreen", enable); }

bool ConfigCenter::framelessWindow() const { return get<bool>("user/framelessWindow", false); }
void ConfigCenter::setFramelessWindow(bool enable) { set("user/framelessWindow", enable); }

bool ConfigCenter::keepAlive() const { return get<bool>("user/keepAlive", false); }
void ConfigCenter::setKeepAlive(bool enable) { set("user/keepAlive", enable); }

bool ConfigCenter::simpleMode() const { return get<bool>("user/simpleMode", false); }
void ConfigCenter::setSimpleMode(bool enable) { set("user/simpleMode", enable); }

bool ConfigCenter::showToolbar() const { return get<bool>("user/showToolbar", true); }
void ConfigCenter::setShowToolbar(bool show) { set("user/showToolbar", show); }

int ConfigCenter::randomOffset() const { return get<int>("user/randomOffset", 0); }
void ConfigCenter::setRandomOffset(int value) { set("user/randomOffset", std::clamp(value, 0, 100)); }

int ConfigCenter::steerWheelSmooth() const { return get<int>("user/steerWheelSmooth", 0); }
void ConfigCenter::setSteerWheelSmooth(int value) { set("user/steerWheelSmooth", std::clamp(value, 0, 100)); }

int ConfigCenter::steerWheelCurve() const { return get<int>("user/steerWheelCurve", 0); }
void ConfigCenter::setSteerWheelCurve(int value) { set("user/steerWheelCurve", std::clamp(value, 0, 100)); }

int ConfigCenter::slideCurve() const { return get<int>("user/slideCurve", 30); }
void ConfigCenter::setSlideCurve(int value) { set("user/slideCurve", std::clamp(value, 0, 100)); }

int ConfigCenter::keyMapOverlayOpacity() const { return get<int>("user/keyMapOverlayOpacity", 60); }
void ConfigCenter::setKeyMapOverlayOpacity(int value) { set("user/keyMapOverlayOpacity", std::clamp(value, 0, 100)); }

bool ConfigCenter::keyMapOverlayVisible() const { return get<bool>("user/keyMapOverlayVisible", false); }
void ConfigCenter::setKeyMapOverlayVisible(bool visible) { set("user/keyMapOverlayVisible", visible); }

int ConfigCenter::scriptTipOpacity() const { return get<int>("user/scriptTipOpacity", 70); }
void ConfigCenter::setScriptTipOpacity(int value) { set("user/scriptTipOpacity", std::clamp(value, 0, 100)); }

bool ConfigCenter::videoStreaming() const { return get<bool>("user/videoStreaming", true); }
void ConfigCenter::setVideoStreaming(bool streaming) { set("user/videoStreaming", streaming); }

bool ConfigCenter::screenOff() const { return get<bool>("user/screenOff", false); }
void ConfigCenter::setScreenOff(bool off) { set("user/screenOff", off); }

int ConfigCenter::sharpenStrength() const { return get<int>("user/sharpenStrength", 0); }
void ConfigCenter::setSharpenStrength(int value) { set("user/sharpenStrength", std::clamp(value, 0, 100)); }

// --- 设备专属配置 ---
std::string ConfigCenter::deviceKey(const std::string& serial, const std::string& key) const
{
    std::string safeSerial = strutil::replaceAll(serial, ":", "_");
    safeSerial = strutil::replaceAll(safeSerial, ".", "_");
    return "device/" + safeSerial + "/" + key;
}

std::string ConfigCenter::nickName(const std::string& serial) const
{
    return getString(deviceKey(serial, "nickName"), "");
}

void ConfigCenter::setNickName(const std::string& serial, const std::string& name)
{
    set(deviceKey(serial, "nickName"), ConfigValue(std::string(name)));
}

Rect ConfigCenter::windowRect(const std::string& serial) const
{
    std::string key = deviceKey(serial, "rect");
    std::lock_guard<std::recursive_mutex> locker(m_mutex);
    if (iniContains(m_userConfig, key)) {
        auto r = m_userConfig->getRect(key);
        if (r.isValid()) return Rect(r.x, r.y, r.w, r.h);
    }
    return Rect();
}

void ConfigCenter::setWindowRect(const std::string& serial, const Rect& rect)
{
    set(deviceKey(serial, "rect"), ConfigValue(rect));
}

std::string ConfigCenter::keyMap(const std::string& serial) const
{
    return getString(deviceKey(serial, "keyMap"), "");
}

void ConfigCenter::setKeyMap(const std::string& serial, const std::string& keyMapFile)
{
    set(deviceKey(serial, "keyMap"), ConfigValue(std::string(keyMapFile)));
}

// --- 配置变更监听 ---
int ConfigCenter::addChangeListener(const std::string& key, ConfigChangeListener listener)
{
    std::lock_guard<std::recursive_mutex> locker(m_mutex);
    int id = m_nextListenerId++;
    m_listeners.push_back({id, key, listener});
    return id;
}

void ConfigCenter::removeChangeListener(int listenerId)
{
    std::lock_guard<std::recursive_mutex> locker(m_mutex);
    for (int i = 0; i < static_cast<int>(m_listeners.size()); ++i) {
        if (m_listeners[i].id == listenerId) {
            m_listeners.erase(m_listeners.begin() + i);
            break;
        }
    }
}

void ConfigCenter::notifyChange(const std::string& key, const ConfigValue& oldValue, const ConfigValue& newValue)
{
    std::vector<ConfigChangeListener> toNotify;
    {
        std::lock_guard<std::recursive_mutex> locker(m_mutex);
        for (const auto& entry : m_listeners) {
            if (entry.pattern == key || entry.pattern == "*") {
                toNotify.push_back(entry.listener);
            } else if (!entry.pattern.empty() && entry.pattern.back() == '*') {
                std::string prefix = entry.pattern.substr(0, entry.pattern.size() - 1);
                if (key.compare(0, prefix.size(), prefix) == 0) {
                    toNotify.push_back(entry.listener);
                }
            }
        }
    }

    for (const auto& listener : toNotify) {
        listener(key, oldValue, newValue);
    }
}

// --- 配置导入导出 ---
std::map<std::string, ConfigValue> ConfigCenter::exportUserConfig() const
{
    std::lock_guard<std::recursive_mutex> locker(m_mutex);
    std::map<std::string, ConfigValue> result;
    if (m_userConfig) {
        for (const auto& key : m_userConfig->allKeys()) {
            result[key] = iniGet(m_userConfig, key);
        }
    }
    return result;
}

void ConfigCenter::importUserConfig(const std::map<std::string, ConfigValue>& config)
{
    for (auto it = config.begin(); it != config.end(); ++it) {
        set(it->first, it->second);
    }
}

void ConfigCenter::resetToDefaults()
{
    std::lock_guard<std::recursive_mutex> locker(m_mutex);
    m_overrides.clear();
    if (m_userConfig) {
        m_userConfig->clear();
        m_userConfig->sync();
    }
}

} // namespace qsc
