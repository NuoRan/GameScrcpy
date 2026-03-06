#ifndef CONFIGCENTER_H
#define CONFIGCENTER_H

#include <mutex>
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <functional>
#include "GameTypes.h"

class IniConfig;

namespace qsc {

// 配置变更回调函数类型 / Configuration change callback function type
using ConfigChangeListener = std::function<void(const std::string& key, const ConfigValue& oldValue, const ConfigValue& newValue)>;

/**
 * @brief 配置中心 - 单例模式 / Configuration Center - Singleton Pattern
 *
 * 统一管理全局配置和用户配置，支持：
 * Centralized management of global and user config, supporting:
 * - 分层配置（默认值 -> 全局配置 -> 用户配置 -> 运行时覆盖）/ Layered config (default -> global -> user -> runtime override)
 * - 配置变更监听 / Config change listeners
 * - 依赖注入（用于测试）/ Dependency injection (for testing)
 */
class ConfigCenter
{
public:
    static ConfigCenter& instance();

    // 依赖注入支持
    static void injectInstance(ConfigCenter* instance);
    static void resetInstance();

    virtual ~ConfigCenter();

    // 初始化
    bool initialize(const std::string& configPath = std::string(), const std::string& userDataPath = std::string());
    bool isInitialized() const;

    // 通用配置访问
    ConfigValue get(const std::string& key, const ConfigValue& defaultValue = ConfigValue()) const;
    void set(const std::string& key, const ConfigValue& value, bool persistent = true);
    void setOverride(const std::string& key, const ConfigValue& value);
    void removeOverride(const std::string& key);
    bool contains(const std::string& key) const;
    void remove(const std::string& key);

    // 模板方法 — uses configval helpers / std::get_if
    template<typename T>
    T get(const std::string& key, const T& defaultValue = T()) const {
        ConfigValue v = get(key, ConfigValue(defaultValue));
        if (auto* p = std::get_if<T>(&v)) return *p;
        return defaultValue;
    }

    // Specializations for type-converting access
    template<> bool get<bool>(const std::string& key, const bool& defaultValue) const {
        return configval::toBool(get(key, ConfigValue(defaultValue)), defaultValue);
    }
    template<> int get<int>(const std::string& key, const int& defaultValue) const {
        return configval::toInt(get(key, ConfigValue(defaultValue)), defaultValue);
    }
    template<> uint32_t get<uint32_t>(const std::string& key, const uint32_t& defaultValue) const {
        return configval::toUInt(get(key, ConfigValue(defaultValue)), defaultValue);
    }
    template<> double get<double>(const std::string& key, const double& defaultValue) const {
        return configval::toDouble(get(key, ConfigValue(defaultValue)), defaultValue);
    }

    // String accessor helper (std::string specialization)
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;

    // --- 全局配置快捷方法 ---
    std::string language() const;
    std::string title() const;
    int maxFps() const;
    int desktopOpenGL() const;
    bool useSkin() const;
    bool renderExpiredFrames() const;
    std::string serverPath() const;
    std::string adbPath() const;
    std::string logLevel() const;
    std::string codecOptions() const;
    std::string codecName() const;

    // --- 用户配置快捷方法 ---
    std::string recordPath() const;
    void setRecordPath(const std::string& path);

    uint32_t bitRate() const;
    void setBitRate(uint32_t rate);

    int maxSizeIndex() const;
    void setMaxSizeIndex(int index);

    bool reverseConnect() const;
    void setReverseConnect(bool enable);

    bool showFPS() const;
    void setShowFPS(bool show);

    bool windowOnTop() const;
    void setWindowOnTop(bool onTop);

    bool autoOffScreen() const;
    void setAutoOffScreen(bool enable);

    bool framelessWindow() const;
    void setFramelessWindow(bool enable);

    bool keepAlive() const;
    void setKeepAlive(bool enable);

    bool simpleMode() const;
    void setSimpleMode(bool enable);

    bool showToolbar() const;
    void setShowToolbar(bool show);

    // 随机偏移范围 (0~100，对应 0~50像素)
    int randomOffset() const;
    void setRandomOffset(int value);

    // 轮盘平滑度 (0~100，0=无平滑，100=高平滑)
    int steerWheelSmooth() const;
    void setSteerWheelSmooth(int value);

    // 轮盘拟人曲线幅度 (0~100，0=无曲线，100=最大幅度)
    int steerWheelCurve() const;
    void setSteerWheelCurve(int value);

    // 滑动轨迹曲线幅度 (0~100，0=直线，100=最大弧度)
    int slideCurve() const;
    void setSlideCurve(int value);

    // 键位提示层透明度 (0~100，0=全透明，100=不透明)
    int keyMapOverlayOpacity() const;
    void setKeyMapOverlayOpacity(int value);

    // 键位提示层是否显示
    bool keyMapOverlayVisible() const;
    void setKeyMapOverlayVisible(bool visible);

    // 脚本弹窗透明度 (0~100，0=全透明，100=不透明)
    int scriptTipOpacity() const;
    void setScriptTipOpacity(int value);

    // 视频传输状态 (true=正在传输, false=暂停)
    bool videoStreaming() const;
    void setVideoStreaming(bool streaming);

    // 息屏状态
    bool screenOff() const;
    void setScreenOff(bool off);

    // --- 设备专属配置 ---
    std::string nickName(const std::string& serial) const;
    void setNickName(const std::string& serial, const std::string& name);

    Rect windowRect(const std::string& serial) const;
    void setWindowRect(const std::string& serial, const Rect& rect);

    std::string keyMap(const std::string& serial) const;
    void setKeyMap(const std::string& serial, const std::string& keyMapFile);

    // --- 配置变更监听 ---
    int addChangeListener(const std::string& key, ConfigChangeListener listener);
    void removeChangeListener(int listenerId);

    // --- 配置导入导出 ---
    std::map<std::string, ConfigValue> exportUserConfig() const;
    void importUserConfig(const std::map<std::string, ConfigValue>& config);
    void resetToDefaults();

protected:
    ConfigCenter();

private:
    void registerDefaults();
    std::string deviceKey(const std::string& serial, const std::string& key) const;
    void notifyChange(const std::string& key, const ConfigValue& oldValue, const ConfigValue& newValue);

private:
    static ConfigCenter* s_instance;
    static ConfigCenter* s_injectedInstance;

    IniConfig* m_globalConfig = nullptr;
    IniConfig* m_userConfig = nullptr;
    mutable std::recursive_mutex m_mutex;
    bool m_initialized = false;

    std::map<std::string, ConfigValue> m_defaults;
    std::map<std::string, ConfigValue> m_overrides;

    struct ListenerEntry {
        int id;
        std::string pattern;
        ConfigChangeListener listener;
    };
    std::vector<ListenerEntry> m_listeners;
    int m_nextListenerId = 1;
};

} // namespace qsc

#endif // CONFIGCENTER_H
