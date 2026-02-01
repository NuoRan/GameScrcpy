#ifndef CONFIGCENTER_H
#define CONFIGCENTER_H

#include <QObject>
#include <QSettings>
#include <QMutex>
#include <QVariant>
#include <QMap>
#include <QRect>
#include <QString>
#include <functional>

namespace qsc {

// 配置变更回调函数类型
using ConfigChangeListener = std::function<void(const QString& key, const QVariant& oldValue, const QVariant& newValue)>;

/**
 * @brief 配置中心 - 单例模式
 *
 * 统一管理全局配置和用户配置，支持：
 * - 分层配置（默认值 -> 全局配置 -> 用户配置 -> 运行时覆盖）
 * - 配置变更监听
 * - 依赖注入（用于测试）
 */
class ConfigCenter : public QObject
{
    Q_OBJECT

public:
    static ConfigCenter& instance();

    // 依赖注入支持
    static void injectInstance(ConfigCenter* instance);
    static void resetInstance();

    virtual ~ConfigCenter();

    // 初始化
    bool initialize(const QString& configPath = QString(), const QString& userDataPath = QString());
    bool isInitialized() const;

    // 通用配置访问
    QVariant get(const QString& key, const QVariant& defaultValue = QVariant()) const;
    void set(const QString& key, const QVariant& value, bool persistent = true);
    void setOverride(const QString& key, const QVariant& value);
    void removeOverride(const QString& key);
    bool contains(const QString& key) const;
    void remove(const QString& key);

    // 模板方法
    template<typename T>
    T get(const QString& key, const T& defaultValue = T()) const {
        return get(key, QVariant::fromValue(defaultValue)).template value<T>();
    }

    // --- 全局配置快捷方法 ---
    QString language() const;
    QString title() const;
    int maxFps() const;
    int desktopOpenGL() const;
    bool useSkin() const;
    bool renderExpiredFrames() const;
    QString pushFilePath() const;
    QString serverPath() const;
    QString adbPath() const;
    QString logLevel() const;
    QString codecOptions() const;
    QString codecName() const;

    // --- 用户配置快捷方法 ---
    QString recordPath() const;
    void setRecordPath(const QString& path);

    quint32 bitRate() const;
    void setBitRate(quint32 rate);

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

    // --- 设备专属配置 ---
    QString nickName(const QString& serial) const;
    void setNickName(const QString& serial, const QString& name);

    QRect windowRect(const QString& serial) const;
    void setWindowRect(const QString& serial, const QRect& rect);

    QString keyMap(const QString& serial) const;
    void setKeyMap(const QString& serial, const QString& keyMapFile);

    // --- 配置变更监听 ---
    int addChangeListener(const QString& key, ConfigChangeListener listener);
    void removeChangeListener(int listenerId);

    // --- 配置导入导出 ---
    QVariantMap exportUserConfig() const;
    void importUserConfig(const QVariantMap& config);
    void resetToDefaults();

signals:
    void configChanged(const QString& key, const QVariant& oldValue, const QVariant& newValue);

protected:
    explicit ConfigCenter(QObject* parent = nullptr);

private:
    void registerDefaults();
    QString deviceKey(const QString& serial, const QString& key) const;
    void notifyChange(const QString& key, const QVariant& oldValue, const QVariant& newValue);

private:
    static ConfigCenter* s_instance;
    static ConfigCenter* s_injectedInstance;

    QSettings* m_globalConfig = nullptr;
    QSettings* m_userConfig = nullptr;
    mutable QMutex m_mutex;
    bool m_initialized = false;

    QMap<QString, QVariant> m_defaults;
    QMap<QString, QVariant> m_overrides;

    struct ListenerEntry {
        int id;
        QString pattern;
        ConfigChangeListener listener;
    };
    QList<ListenerEntry> m_listeners;
    int m_nextListenerId = 1;
};

} // namespace qsc

#endif // CONFIGCENTER_H
