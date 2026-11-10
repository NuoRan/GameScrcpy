#include "InputReadGrants.h"

#include "ProfileLibrary.h"
#include "ScriptWorkbenchConsole.h"
#include "TaskLibrary.h"

#include <gs/input/Profile.h>
#include <gs/platform/AtomicFile.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <string>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

QString pathText(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

QString fromView(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

bool usableId(const QString& id)
{
    return !id.isEmpty() && id.size() <= InputReadGrants::kMostIdChars;
}

QJsonArray idArray(const std::set<QString>& ids)
{
    QJsonArray array;
    for (const QString& id : ids) {
        array.append(id);
    }
    return array;
}

std::set<QString> idsIn(const QJsonValue& value)
{
    std::set<QString> ids;
    for (const QJsonValue& each : value.toArray()) {
        if (const QString id = each.toString(); usableId(id)) {
            ids.insert(id);
        }
    }
    return ids;
}

}

InputReadGrants::InputReadGrants(std::filesystem::path file, QObject* parent)
    : QObject(parent)
    , m_file(std::move(file))
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    load();
}

InputReadGrants::~InputReadGrants()
{
    if (m_unsaved) {
        save();
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void InputReadGrants::follow(ProfileLibrary& profiles, TaskLibrary& tasks)
{
    m_profileLibrary = &profiles;
    m_taskLibrary = &tasks;
    connect(&profiles, &ProfileLibrary::logicConfirmed, this,
            [this](const QStringList& ids) { change(InputReader::Profile, ids, true); });
    connect(&profiles, &ProfileLibrary::profileDuplicated, this, [this](const QString& from, const QString& to) {
        if (allows({.reader = InputReader::Profile, .id = from})) {
            change(InputReader::Profile, {to}, true);
        }
    });
    connect(&profiles, &ProfileLibrary::profileRemoved, this,
            [this](const QString& id) { change(InputReader::Profile, {id}, false); });
    connect(&tasks, &TaskLibrary::taskCreated, this,
            [this](const QString& id) { change(InputReader::Task, {id}, true); });
    connect(&tasks, &TaskLibrary::taskRemoved, this,
            [this](const QString& id) { change(InputReader::Task, {id}, false); });
    if (m_fresh) {
        adopt(profiles, tasks);
    }
}

bool InputReadGrants::allows(const InputGrant& grant) const
{
    const std::scoped_lock lock(m_mutex);
    switch (grant.reader) {
    case InputReader::Profile:
        return m_profiles.contains(grant.id);
    case InputReader::Task:
        return m_tasks.contains(grant.id);
    case InputReader::Run:
        break;
    }
    return false;
}

void InputReadGrants::allow(const InputGrant& grant)
{
    change(grant.reader, {grant.id}, true);
}

void InputReadGrants::forget(const InputGrant& grant)
{
    change(grant.reader, {grant.id}, false);
}

bool InputReadGrants::allowFolder(const QString& folder)
{
    if (m_profileLibrary != nullptr) {
        for (const ProfileEntry& entry : m_profileLibrary->all()) {
            if (!entry.readOnly() && !entry.folder.empty() && sameFolder(pathText(entry.folder), folder)) {
                allow({.reader = InputReader::Profile, .id = entry.id});
                return true;
            }
        }
    }
    if (m_taskLibrary != nullptr) {
        for (const TaskEntry& entry : m_taskLibrary->entries()) {
            if (!entry.readOnly && sameFolder(entry.folder, folder)) {
                allow({.reader = InputReader::Task, .id = entry.id});
                return true;
            }
        }
    }
    return false;
}

QStringList InputReadGrants::listed(InputReader reader) const
{
    if (reader == InputReader::Run) {
        return {};
    }
    const std::scoped_lock lock(m_mutex);
    const std::set<QString>& chosen = reader == InputReader::Profile ? m_profiles : m_tasks;
    return QStringList(chosen.cbegin(), chosen.cend());
}

void InputReadGrants::load()
{
    std::string contents;
    const std::error_code error = platform::readFile(m_file, kMostBytes, contents);
    if (platform::fileMissing(error)) {
        m_fresh = true;
        return;
    }
    if (error) {
        m_readOnly = true;
        qWarning("input reading: %s cannot be read (error %d); no logic may read the keys and the cursor until it is "
                 "allowed again, and the file is not saved over",
                 qUtf8Printable(pathText(m_file)), error.value());
        return;
    }
    QJsonParseError problem;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(contents), &problem);
    const QJsonObject root = document.object();
    if (problem.error != QJsonParseError::NoError || !document.isObject() ||
        root.value(QStringLiteral("format")).toString() != fromView(kFormatName)) {
        std::filesystem::path aside = m_file;
        aside += L".bad";
        std::error_code renamed;
        std::filesystem::rename(m_file, aside, renamed);
        qWarning("input reading: %s is not a list GameScrcpy can read; starting with no logic allowed, %s",
                 qUtf8Printable(pathText(m_file)),
                 renamed ? "and the file stays where it is" : "and the file is kept with .bad added to its name");
        return;
    }
    if (root.value(QStringLiteral("version")).toInt() != kFormatVersion) {
        m_readOnly = true;
        qWarning("input reading: %s comes from another GameScrcpy; starting with no logic allowed and not saving "
                 "over it",
                 qUtf8Printable(pathText(m_file)));
        return;
    }
    const std::scoped_lock lock(m_mutex);
    m_profiles = idsIn(root.value(QStringLiteral("profiles")));
    m_tasks = idsIn(root.value(QStringLiteral("tasks")));
}

void InputReadGrants::adopt(const ProfileLibrary& profiles, const TaskLibrary& tasks)
{
    QStringList logic;
    for (const ProfileEntry& entry : profiles.all()) {
        if (!entry.readOnly() && !entry.folder.empty() && usableId(entry.id) &&
            (entry.logic || (entry.profile != nullptr && input::usesComponents(*entry.profile)))) {
            logic.append(entry.id);
        }
    }
    QStringList own;
    for (const TaskEntry& entry : tasks.entries()) {
        if (!entry.readOnly && usableId(entry.id)) {
            own.append(entry.id);
        }
    }
    {
        const std::scoped_lock lock(m_mutex);
        m_profiles.insert(logic.cbegin(), logic.cend());
        m_tasks.insert(own.cbegin(), own.cend());
    }
    m_fresh = false;
    save();
    qInfo("input reading: %lld profiles with logic and %lld tasks made before may read the keys and the cursor",
          static_cast<long long>(logic.size()), static_cast<long long>(own.size()));
}

void InputReadGrants::change(InputReader reader, const QStringList& ids, bool allowed)
{
    if (reader == InputReader::Run) {
        return;
    }
    bool changed = false;
    {
        const std::scoped_lock lock(m_mutex);
        std::set<QString>& chosen = reader == InputReader::Profile ? m_profiles : m_tasks;
        for (const QString& id : ids) {
            if (!usableId(id)) {
                continue;
            }
            const bool done = allowed ? chosen.insert(id).second : chosen.erase(id) > 0;
            changed = changed || done;
        }
    }
    if (changed) {
        save();
    }
}

void InputReadGrants::save()
{
    if (m_readOnly) {
        return;
    }
    QJsonObject root{{QStringLiteral("format"), fromView(kFormatName)}, {QStringLiteral("version"), kFormatVersion}};
    {
        const std::scoped_lock lock(m_mutex);
        root.insert(QStringLiteral("profiles"), idArray(m_profiles));
        root.insert(QStringLiteral("tasks"), idArray(m_tasks));
    }
    std::error_code error;
    std::filesystem::create_directories(m_file.parent_path(), error);
    error = platform::writeFileAtomically(m_file, QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString());
    m_unsaved = static_cast<bool>(error);
    if (error) {
        qWarning("input reading: %s could not be saved (%serror %d); what was allowed holds until GameScrcpy closes",
                 qUtf8Printable(pathText(m_file)), platform::fileBusy(error) ? "another program is using it, " : "",
                 error.value());
    }
}

}
