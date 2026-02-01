#include "ConfigCenter.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QDebug>

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

ConfigCenter::ConfigCenter(QObject* parent)
    : QObject(parent)
{
    registerDefaults();
}

ConfigCenter::~ConfigCenter()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void ConfigCenter::registerDefaults()
{
    // 全局配置默认值
    m_defaults["common/language"] = "auto";
    m_defaults["common/title"] = "QtScrcpy";
    m_defaults["common/maxFps"] = 60;
    m_defaults["common/desktopOpenGL"] = -1;
    m_defaults["common/skin"] = 1;
    m_defaults["common/renderExpiredFrames"] = 0;
    m_defaults["common/pushFilePath"] = "/sdcard/";
    m_defaults["common/serverPath"] = "";
    m_defaults["common/adbPath"] = "";
    m_defaults["common/logLevel"] = "*:W";
    m_defaults["common/codecOptions"] = "";
    m_defaults["common/codecName"] = "";

    // 用户配置默认值
    m_defaults["user/recordPath"] = "";
    m_defaults["user/bitRate"] = 4000000;  // 降低默认码率，WiFi更稳定
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
}

bool ConfigCenter::initialize(const QString& configPath, const QString& userDataPath)
{
    QMutexLocker locker(&m_mutex);

    if (m_initialized) {
        return true;
    }

    // 确定配置文件路径
    QString globalPath = configPath;
    if (globalPath.isEmpty()) {
        globalPath = QCoreApplication::applicationDirPath() + "/config/config.ini";
    }

    QString userPath = userDataPath;
    if (userPath.isEmpty()) {
        userPath = QCoreApplication::applicationDirPath() + "/config/userdata.ini";
    }

    // 确保目录存在
    QFileInfo globalInfo(globalPath);
    QFileInfo userInfo(userPath);
    QDir().mkpath(globalInfo.absolutePath());
    QDir().mkpath(userInfo.absolutePath());

    // 创建 QSettings 对象
    m_globalConfig = new QSettings(globalPath, QSettings::IniFormat, this);
    m_userConfig = new QSettings(userPath, QSettings::IniFormat, this);

    m_initialized = true;
    qInfo() << "ConfigCenter initialized with:" << globalPath << "and" << userPath;

    return true;
}

bool ConfigCenter::isInitialized() const
{
    return m_initialized;
}

QVariant ConfigCenter::get(const QString& key, const QVariant& defaultValue) const
{
    QMutexLocker locker(&m_mutex);

    // 1. 检查运行时覆盖
    if (m_overrides.contains(key)) {
        return m_overrides.value(key);
    }

    // 2. 检查用户配置
    if (m_userConfig && m_userConfig->contains(key)) {
        return m_userConfig->value(key);
    }

    // 3. 检查全局配置
    if (m_globalConfig && m_globalConfig->contains(key)) {
        return m_globalConfig->value(key);
    }

    // 4. 检查默认值
    if (m_defaults.contains(key)) {
        return m_defaults.value(key);
    }

    return defaultValue;
}

void ConfigCenter::set(const QString& key, const QVariant& value, bool persistent)
{
    QVariant oldValue;
    {
        QMutexLocker locker(&m_mutex);
        oldValue = get(key);

        if (persistent && m_userConfig) {
            m_userConfig->setValue(key, value);
            m_userConfig->sync();
        } else {
            m_overrides[key] = value;
        }
    }

    if (oldValue != value) {
        notifyChange(key, oldValue, value);
    }
}

void ConfigCenter::setOverride(const QString& key, const QVariant& value)
{
    QVariant oldValue;
    {
        QMutexLocker locker(&m_mutex);
        oldValue = get(key);
        m_overrides[key] = value;
    }

    if (oldValue != value) {
        notifyChange(key, oldValue, value);
    }
}

void ConfigCenter::removeOverride(const QString& key)
{
    QMutexLocker locker(&m_mutex);
    m_overrides.remove(key);
}

bool ConfigCenter::contains(const QString& key) const
{
    QMutexLocker locker(&m_mutex);
    return m_overrides.contains(key) ||
           (m_userConfig && m_userConfig->contains(key)) ||
           (m_globalConfig && m_globalConfig->contains(key)) ||
           m_defaults.contains(key);
}

void ConfigCenter::remove(const QString& key)
{
    QMutexLocker locker(&m_mutex);
    m_overrides.remove(key);
    if (m_userConfig) {
        m_userConfig->remove(key);
        m_userConfig->sync();
    }
}

// --- 全局配置快捷方法 ---
QString ConfigCenter::language() const { return get<QString>("common/language", "auto"); }
QString ConfigCenter::title() const { return get<QString>("common/title", "QtScrcpy"); }
int ConfigCenter::maxFps() const { return get<int>("common/maxFps", 60); }
int ConfigCenter::desktopOpenGL() const { return get<int>("common/desktopOpenGL", -1); }
bool ConfigCenter::useSkin() const { return get<int>("common/skin", 1) != 0; }
bool ConfigCenter::renderExpiredFrames() const { return get<int>("common/renderExpiredFrames", 0) != 0; }
QString ConfigCenter::pushFilePath() const { return get<QString>("common/pushFilePath", "/sdcard/"); }
QString ConfigCenter::serverPath() const { return get<QString>("common/serverPath", ""); }
QString ConfigCenter::adbPath() const { return get<QString>("common/adbPath", ""); }
QString ConfigCenter::logLevel() const { return get<QString>("common/logLevel", "*:W"); }
QString ConfigCenter::codecOptions() const { return get<QString>("common/codecOptions", ""); }
QString ConfigCenter::codecName() const { return get<QString>("common/codecName", ""); }

// --- 用户配置快捷方法 ---
QString ConfigCenter::recordPath() const { return get<QString>("user/recordPath", ""); }
void ConfigCenter::setRecordPath(const QString& path) { set("user/recordPath", path); }

quint32 ConfigCenter::bitRate() const { return get<quint32>("user/bitRate", 8000000); }
void ConfigCenter::setBitRate(quint32 rate) { set("user/bitRate", rate); }

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

// --- 设备专属配置 ---
QString ConfigCenter::deviceKey(const QString& serial, const QString& key) const
{
    QString safeSerial = serial;
    safeSerial.replace(":", "_").replace(".", "_");
    return QString("device/%1/%2").arg(safeSerial, key);
}

QString ConfigCenter::nickName(const QString& serial) const
{
    return get<QString>(deviceKey(serial, "nickName"), "");
}

void ConfigCenter::setNickName(const QString& serial, const QString& name)
{
    set(deviceKey(serial, "nickName"), name);
}

QRect ConfigCenter::windowRect(const QString& serial) const
{
    QString key = deviceKey(serial, "rect");
    QMutexLocker locker(&m_mutex);
    if (m_userConfig && m_userConfig->contains(key)) {
        return m_userConfig->value(key).toRect();
    }
    return QRect();
}

void ConfigCenter::setWindowRect(const QString& serial, const QRect& rect)
{
    set(deviceKey(serial, "rect"), rect);
}

QString ConfigCenter::keyMap(const QString& serial) const
{
    return get<QString>(deviceKey(serial, "keyMap"), "");
}

void ConfigCenter::setKeyMap(const QString& serial, const QString& keyMapFile)
{
    set(deviceKey(serial, "keyMap"), keyMapFile);
}

// --- 配置变更监听 ---
int ConfigCenter::addChangeListener(const QString& key, ConfigChangeListener listener)
{
    QMutexLocker locker(&m_mutex);
    int id = m_nextListenerId++;
    m_listeners.append({id, key, listener});
    return id;
}

void ConfigCenter::removeChangeListener(int listenerId)
{
    QMutexLocker locker(&m_mutex);
    for (int i = 0; i < m_listeners.size(); ++i) {
        if (m_listeners[i].id == listenerId) {
            m_listeners.removeAt(i);
            break;
        }
    }
}

void ConfigCenter::notifyChange(const QString& key, const QVariant& oldValue, const QVariant& newValue)
{
    emit configChanged(key, oldValue, newValue);

    QList<ConfigChangeListener> toNotify;
    {
        QMutexLocker locker(&m_mutex);
        for (const auto& entry : m_listeners) {
            if (entry.pattern == key || entry.pattern == "*") {
                toNotify.append(entry.listener);
            } else if (entry.pattern.endsWith("*")) {
                QString prefix = entry.pattern.left(entry.pattern.length() - 1);
                if (key.startsWith(prefix)) {
                    toNotify.append(entry.listener);
                }
            }
        }
    }

    for (const auto& listener : toNotify) {
        listener(key, oldValue, newValue);
    }
}

// --- 配置导入导出 ---
QVariantMap ConfigCenter::exportUserConfig() const
{
    QMutexLocker locker(&m_mutex);
    QVariantMap result;
    if (m_userConfig) {
        for (const auto& key : m_userConfig->allKeys()) {
            result[key] = m_userConfig->value(key);
        }
    }
    return result;
}

void ConfigCenter::importUserConfig(const QVariantMap& config)
{
    for (auto it = config.begin(); it != config.end(); ++it) {
        set(it.key(), it.value());
    }
}

void ConfigCenter::resetToDefaults()
{
    QMutexLocker locker(&m_mutex);
    m_overrides.clear();
    if (m_userConfig) {
        m_userConfig->clear();
        m_userConfig->sync();
    }
}

} // namespace qsc
