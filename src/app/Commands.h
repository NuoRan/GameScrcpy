#pragma once

#include "CommandTable.h"
#include "DeviceCommands.h"

#include <QKeyCombination>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

class QJSEngine;
class QKeyEvent;
class QQmlEngine;

namespace gs::app {

using ShortcutMap = std::map<std::string, std::string, std::less<>>;

struct ShortcutInfo {
    QString id;
    QString title;
    bool device = false;
    QKeyCombination keys;
    QKeyCombination defaults;
    bool custom = false;
    bool conflicted = false;

    friend bool operator==(const ShortcutInfo&, const ShortcutInfo&) = default;
};

class Commands : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString currentDevice READ currentDevice NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool customized READ customized NOTIFY shortcutsChanged FINAL)

public:
    static constexpr std::size_t kMaxRecent = 8;

    explicit Commands(DeviceCommandHost& host, QObject* parent = nullptr);
    ~Commands() override;

    static Commands* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static Commands* instance() noexcept { return s_instance; }

    [[nodiscard]] const CommandTable& table() const noexcept { return m_table; }
    [[nodiscard]] QString currentDevice() const;
    [[nodiscard]] QString resolve(const Command& command, const QString& target) const;
    [[nodiscard]] static QStringList keyCaps(QKeyCombination combination);
    [[nodiscard]] static QString keyText(QKeyCombination combination);
    [[nodiscard]] static QKeyCombination parseKeys(const QString& portable);
    [[nodiscard]] static QString portableKeys(QKeyCombination combination);
    [[nodiscard]] static QKeyCombination keysOf(const QKeyEvent& event);
    [[nodiscard]] static bool plain(QKeyCombination combination);

    void applyUserShortcuts(const ShortcutMap& shortcuts);
    void setShortcutStore(std::function<void(const ShortcutMap&)> store);
    [[nodiscard]] QKeyCombination shortcut(QStringView id) const;
    [[nodiscard]] QString match(QKeyCombination keys, CommandScope window) const;
    [[nodiscard]] std::vector<ShortcutInfo> shortcutList() const;
    [[nodiscard]] bool customized() const { return !m_user.empty(); }

    void applyRecentCommands(const std::vector<std::string>& ids);
    void setRecentStore(std::function<void(const std::vector<std::string>&)> store);
    [[nodiscard]] const QStringList& recentCommands() const noexcept { return m_recent; }
    void remember(const QString& id);

    Q_INVOKABLE bool trigger(const QString& id, const QString& target = {});
    Q_INVOKABLE bool isEnabled(const QString& id, const QString& target = {}) const;
    Q_INVOKABLE bool isChecked(const QString& id, const QString& target = {}) const;
    Q_INVOKABLE QString title(const QString& id, const QString& target = {}) const;
    Q_INVOKABLE QString icon(const QString& id) const;
    Q_INVOKABLE QString shortcutText(const QString& id) const;
    Q_INVOKABLE QStringList shortcutCaps(const QString& id) const;
    Q_INVOKABLE QVariantMap device(const QString& serial) const;

    Q_INVOKABLE QStringList keyCapsOf(const QString& keys) const;
    Q_INVOKABLE bool plainKeys(const QString& keys) const;
    Q_INVOKABLE QStringList conflicts(const QString& id, const QString& keys) const;
    Q_INVOKABLE bool setShortcut(const QString& id, const QString& keys);
    Q_INVOKABLE void resetShortcut(const QString& id);
    Q_INVOKABLE void resetAllShortcuts();

public slots:
    void refresh();
    void scheduleRefresh();
    void retranslate();

signals:
    void stateChanged();
    void shortcutsChanged();
    void recentChanged();
    void uiRequested(const QString& id, const QString& target);

private:
    struct Binding {
        QKeyCombination keys;
        bool user = false;
        bool active = false;
    };

    [[nodiscard]] const Binding* binding(QStringView id) const;
    void setOverride(const Command& command, QKeyCombination keys);
    void rebuildShortcuts();
    void storeShortcuts();

    static inline Commands* s_instance = nullptr;

    DeviceCommandHost& m_host;
    CommandTable m_table;
    QString m_lastDevice;
    bool m_refreshQueued = false;
    ShortcutMap m_user;
    ShortcutMap m_kept;
    std::vector<Binding> m_bindings;
    std::function<void(const ShortcutMap&)> m_store;
    QStringList m_recent;
    std::function<void(const std::vector<std::string>&)> m_recentStore;
};

}
