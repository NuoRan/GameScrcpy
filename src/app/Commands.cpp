#include "Commands.h"

#include <QCoreApplication>
#include <QJSEngine>
#include <QKeyEvent>
#include <QKeySequence>
#include <QStringList>

#include <algorithm>
#include <numeric>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

constexpr Qt::KeyboardModifiers kShortcutModifiers =
    Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;

bool bound(QKeyCombination keys)
{
    const Qt::Key key = keys.key();
    return key != Qt::Key_unknown && key != Qt::Key(0);
}

bool modifierKey(int key)
{
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
        return true;
    default:
        return false;
    }
}

bool overlaps(CommandScope a, CommandScope b)
{
    return a == CommandScope::Global || b == CommandScope::Global || a == b;
}

QString keyName(Qt::Key key)
{
    switch (key) {
    case Qt::Key_Up:
        return u"↑"_s;
    case Qt::Key_Down:
        return u"↓"_s;
    case Qt::Key_Left:
        return u"←"_s;
    case Qt::Key_Right:
        return u"→"_s;
    case Qt::Key_Space:
        return QCoreApplication::translate("Keys", "Space");
    case Qt::Key_VolumeUp:
        return QCoreApplication::translate("Keys", "Volume Up");
    case Qt::Key_VolumeDown:
        return QCoreApplication::translate("Keys", "Volume Down");
    case Qt::Key_VolumeMute:
        return QCoreApplication::translate("Keys", "Mute");
    case Qt::Key_MediaPlay:
    case Qt::Key_MediaTogglePlayPause:
        return QCoreApplication::translate("Keys", "Play/Pause");
    case Qt::Key_MediaNext:
        return QCoreApplication::translate("Keys", "Next Track");
    case Qt::Key_MediaPrevious:
        return QCoreApplication::translate("Keys", "Previous Track");
    case Qt::Key_MediaStop:
        return QCoreApplication::translate("Keys", "Stop");
    default:
        return QKeySequence(key).toString(QKeySequence::PortableText);
    }
}

std::vector<std::string> standardIds(const QStringList& ids)
{
    std::vector<std::string> standard;
    standard.reserve(static_cast<std::size_t>(ids.size()));
    for (const QString& id : ids) {
        standard.push_back(id.toStdString());
    }
    return standard;
}

}

Commands::Commands(DeviceCommandHost& host, QObject* parent) : QObject(parent), m_host(host)
{
    Q_ASSERT(s_instance == nullptr);
    s_instance = this;
    const UiRequest request = [this](const QString& id, const QString& target) { emit uiRequested(id, target); };
    addDeviceCommands(m_table, m_host, request);
    addHubCommands(m_table, m_host, request);
    rebuildShortcuts();
}

Commands::~Commands()
{
    s_instance = nullptr;
}

Commands* Commands::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString Commands::currentDevice() const
{
    const std::vector<DeviceFacts> devices = m_host.devices();
    const auto listed = [&devices](const QString& serial) {
        return std::ranges::any_of(devices, [&serial](const DeviceFacts& facts) { return facts.serial == serial; });
    };
    if (!m_lastDevice.isEmpty() && listed(m_lastDevice)) {
        return m_lastDevice;
    }
    if (std::ranges::count_if(devices, &DeviceFacts::connected) == 1) {
        return std::ranges::find_if(devices, &DeviceFacts::connected)->serial;
    }
    return devices.size() == 1 ? devices.front().serial : QString();
}

QString Commands::resolve(const Command& command, const QString& target) const
{
    if (command.target != CommandTarget::Device) {
        return {};
    }
    return target.isEmpty() ? currentDevice() : target;
}

QStringList Commands::keyCaps(QKeyCombination combination)
{
    if (!bound(combination)) {
        return {};
    }
    QStringList keys;
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    if (modifiers.testFlag(Qt::ControlModifier)) {
        keys.append(u"Ctrl"_s);
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        keys.append(u"Alt"_s);
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        keys.append(u"Shift"_s);
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        keys.append(u"Win"_s);
    }
    keys.append(keyName(combination.key()));
    return keys;
}

QString Commands::keyText(QKeyCombination combination)
{
    return keyCaps(combination).join(u' ');
}

bool Commands::plain(QKeyCombination combination)
{
    const int key = combination.key();
    const bool function = key >= Qt::Key_F1 && key <= Qt::Key_F35;
    const bool media = key >= Qt::Key_Back && key < Qt::Key_unknown;
    return bound(combination) && !function && !media &&
           !combination.keyboardModifiers().testAnyFlags(Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
}

QKeyCombination Commands::parseKeys(const QString& portable)
{
    const QKeySequence sequence = QKeySequence::fromString(portable, QKeySequence::PortableText);
    if (sequence.count() != 1) {
        return {};
    }
    const QKeyCombination keys = sequence[0];
    if (!bound(keys) || modifierKey(keys.key()) || (keys.keyboardModifiers() & ~kShortcutModifiers).toInt() != 0) {
        return {};
    }
    return keys;
}

QString Commands::portableKeys(QKeyCombination combination)
{
    return bound(combination) ? QKeySequence(combination).toString(QKeySequence::PortableText) : QString();
}

QKeyCombination Commands::keysOf(const QKeyEvent& event)
{
    int key = event.key();
    const quint32 virtualKey = event.nativeVirtualKey();
    if ((virtualKey >= '0' && virtualKey <= '9') || (virtualKey >= 'A' && virtualKey <= 'Z')) {
        key = static_cast<int>(virtualKey);
    }
    if (key == 0 || key == Qt::Key_unknown || modifierKey(key)) {
        return {};
    }
    return QKeyCombination(event.modifiers() & kShortcutModifiers, static_cast<Qt::Key>(key));
}

void Commands::applyUserShortcuts(const ShortcutMap& shortcuts)
{
    m_user.clear();
    m_kept.clear();
    for (const auto& [id, keys] : shortcuts) {
        const Command* command = m_table.find(QString::fromStdString(id));
        if (command == nullptr) {
            qInfo("keeping the shortcut for the unknown command %s unused", id.c_str());
            m_kept.emplace(id, keys);
            continue;
        }
        const QKeyCombination parsed = parseKeys(QString::fromStdString(keys));
        if (!keys.empty() && !bound(parsed)) {
            qWarning("keeping the shortcut \"%s\" for %s unused: not a single key combination", keys.c_str(),
                     id.c_str());
            m_kept.emplace(id, keys);
            continue;
        }
        setOverride(*command, parsed);
    }
    rebuildShortcuts();
}

void Commands::setShortcutStore(std::function<void(const ShortcutMap&)> store)
{
    m_store = std::move(store);
}

QKeyCombination Commands::shortcut(QStringView id) const
{
    const Binding* found = binding(id);
    return found != nullptr && found->active ? found->keys : QKeyCombination();
}

QString Commands::match(QKeyCombination keys, CommandScope window) const
{
    if (!bound(keys)) {
        return {};
    }
    const auto& commands = m_table.all();
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const Binding& candidate = m_bindings[i];
        const CommandScope scope = commands[i].scope;
        if (candidate.active && candidate.keys == keys && (scope == CommandScope::Global || scope == window)) {
            return commands[i].id;
        }
    }
    return {};
}

bool Commands::trigger(const QString& id, const QString& target)
{
    const Command* command = m_table.find(id);
    if (command == nullptr) {
        return false;
    }
    const QString resolved = command->target == CommandTarget::Topic ? target : resolve(*command, target);
    if (command->target == CommandTarget::Device && resolved.isEmpty()) {
        return false;
    }
    if (!command->enabled(resolved)) {
        return false;
    }
    command->run(resolved);
    if (command->target == CommandTarget::Device && resolved != m_lastDevice) {
        m_lastDevice = resolved;
        emit stateChanged();
    }
    return true;
}

bool Commands::isEnabled(const QString& id, const QString& target) const
{
    const Command* command = m_table.find(id);
    if (command == nullptr) {
        return false;
    }
    const QString resolved = resolve(*command, target);
    return (command->target != CommandTarget::Device || !resolved.isEmpty()) && command->enabled(resolved);
}

bool Commands::isChecked(const QString& id, const QString& target) const
{
    const Command* command = m_table.find(id);
    if (command == nullptr || !command->checked) {
        return false;
    }
    const QString resolved = resolve(*command, target);
    return (command->target != CommandTarget::Device || !resolved.isEmpty()) && command->checked(resolved);
}

QString Commands::title(const QString& id, const QString& target) const
{
    const Command* command = m_table.find(id);
    if (command == nullptr) {
        return {};
    }
    const bool checked = command->checkedTitle != nullptr && isChecked(id, target);
    return QCoreApplication::translate("Commands", checked ? command->checkedTitle : command->title);
}

QString Commands::icon(const QString& id) const
{
    const Command* command = m_table.find(id);
    return command == nullptr ? QString() : command->icon;
}

QString Commands::shortcutText(const QString& id) const
{
    return keyText(shortcut(id));
}

QStringList Commands::shortcutCaps(const QString& id) const
{
    return keyCaps(shortcut(id));
}

QVariantMap Commands::device(const QString& serial) const
{
    const auto facts = m_host.device(serial);
    if (!facts) {
        return {};
    }
    return {{u"serial"_s, facts->serial}, {u"name"_s, facts->name}, {u"deviceId"_s, facts->deviceId}};
}

std::vector<ShortcutInfo> Commands::shortcutList() const
{
    std::vector<ShortcutInfo> list;
    const auto& commands = m_table.all();
    list.reserve(commands.size());
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const Command& command = commands[i];
        const Binding& assigned = m_bindings[i];
        list.push_back({.id = command.id,
                        .title = QCoreApplication::translate("Commands", command.title),
                        .device = command.target == CommandTarget::Device,
                        .keys = assigned.keys,
                        .defaults = command.shortcut,
                        .custom = assigned.user,
                        .conflicted = bound(assigned.keys) && !assigned.active});
    }
    return list;
}

QStringList Commands::keyCapsOf(const QString& keys) const
{
    return keyCaps(parseKeys(keys));
}

bool Commands::plainKeys(const QString& keys) const
{
    return plain(parseKeys(keys));
}

void Commands::applyRecentCommands(const std::vector<std::string>& ids)
{
    m_recent.clear();
    for (const std::string& id : ids) {
        const QString text = QString::fromStdString(id);
        if (!text.isEmpty() && !m_recent.contains(text) && static_cast<std::size_t>(m_recent.size()) < kMaxRecent) {
            m_recent.append(text);
        }
    }
    emit recentChanged();
}

void Commands::setRecentStore(std::function<void(const std::vector<std::string>&)> store)
{
    m_recentStore = std::move(store);
}

void Commands::remember(const QString& id)
{
    if (id.isEmpty() || (!m_recent.isEmpty() && m_recent.front() == id)) {
        return;
    }
    m_recent.removeAll(id);
    m_recent.prepend(id);
    while (static_cast<std::size_t>(m_recent.size()) > kMaxRecent) {
        m_recent.removeLast();
    }
    emit recentChanged();
    if (m_recentStore) {
        m_recentStore(standardIds(m_recent));
    }
}

QStringList Commands::conflicts(const QString& id, const QString& keys) const
{
    QStringList titles;
    const Command* command = m_table.find(id);
    const QKeyCombination wanted = parseKeys(keys);
    if (command == nullptr || !bound(wanted)) {
        return titles;
    }
    const auto& commands = m_table.all();
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].id != id && m_bindings[i].keys == wanted && overlaps(command->scope, commands[i].scope)) {
            titles.append(QCoreApplication::translate("Commands", commands[i].title));
        }
    }
    return titles;
}

bool Commands::setShortcut(const QString& id, const QString& keys)
{
    const Command* command = m_table.find(id);
    const QKeyCombination wanted = parseKeys(keys);
    if (command == nullptr || (!keys.isEmpty() && !bound(wanted))) {
        return false;
    }
    if (bound(wanted)) {
        const auto& commands = m_table.all();
        for (std::size_t i = 0; i < commands.size(); ++i) {
            if (commands[i].id != id && m_bindings[i].keys == wanted && overlaps(command->scope, commands[i].scope)) {
                setOverride(commands[i], {});
            }
        }
    }
    setOverride(*command, wanted);
    rebuildShortcuts();
    storeShortcuts();
    return true;
}

void Commands::resetShortcut(const QString& id)
{
    const std::string key = id.toStdString();
    const bool user = m_user.erase(key) > 0;
    const bool kept = m_kept.erase(key) > 0;
    if (!user && !kept) {
        return;
    }
    rebuildShortcuts();
    storeShortcuts();
}

void Commands::resetAllShortcuts()
{
    const std::size_t kept = std::erase_if(
        m_kept, [this](const auto& entry) { return m_table.find(QString::fromStdString(entry.first)) != nullptr; });
    if (m_user.empty() && kept == 0) {
        return;
    }
    m_user.clear();
    rebuildShortcuts();
    storeShortcuts();
}

void Commands::refresh()
{
    emit stateChanged();
}

void Commands::retranslate()
{
    emit shortcutsChanged();
    emit stateChanged();
}

void Commands::scheduleRefresh()
{
    if (m_refreshQueued) {
        return;
    }
    m_refreshQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_refreshQueued = false;
            emit stateChanged();
        },
        Qt::QueuedConnection);
}

const Commands::Binding* Commands::binding(QStringView id) const
{
    const auto& commands = m_table.all();
    const auto found = std::ranges::find_if(commands, [id](const Command& command) { return command.id == id; });
    return found == commands.end() ? nullptr : &m_bindings[static_cast<std::size_t>(found - commands.begin())];
}

void Commands::setOverride(const Command& command, QKeyCombination keys)
{
    const std::string id = command.id.toStdString();
    m_kept.erase(id);
    const bool unchanged = bound(keys) ? keys == command.shortcut : !bound(command.shortcut);
    if (unchanged) {
        m_user.erase(id);
    } else {
        m_user.insert_or_assign(id, portableKeys(keys).toStdString());
    }
}

void Commands::rebuildShortcuts()
{
    const auto& commands = m_table.all();
    m_bindings.assign(commands.size(), {});
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const auto user = m_user.find(commands[i].id.toStdString());
        if (user == m_user.end()) {
            m_bindings[i].keys = commands[i].shortcut;
        } else {
            m_bindings[i].keys = parseKeys(QString::fromStdString(user->second));
            m_bindings[i].user = true;
        }
    }
    std::vector<std::size_t> order(commands.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_partition(order, [this](std::size_t i) { return m_bindings[i].user; });
    for (const std::size_t i : order) {
        Binding& candidate = m_bindings[i];
        if (!bound(candidate.keys)) {
            continue;
        }
        candidate.active = std::ranges::none_of(order, [&](std::size_t j) {
            const Binding& other = m_bindings[j];
            return other.active && other.keys == candidate.keys && overlaps(commands[i].scope, commands[j].scope);
        });
    }
    emit shortcutsChanged();
    emit stateChanged();
}

void Commands::storeShortcuts()
{
    if (!m_store) {
        return;
    }
    ShortcutMap stored = m_kept;
    for (const auto& [id, keys] : m_user) {
        stored.insert_or_assign(id, keys);
    }
    m_store(stored);
}

}
