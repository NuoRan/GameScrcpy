#include "CrashReport.h"

#include "AppSettings.h"

#include <gs/platform/CrashHandler.h>

#include <QJSEngine>
#include <QString>

#include <algorithm>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path pathOf(std::string_view text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

}

bool isCrashFolder(std::string_view name) noexcept
{
    if (name.size() != platform::kCrashFolderLength || !name.starts_with(platform::kCrashFolderPrefix)) {
        return false;
    }
    const std::string_view stamp = name.substr(platform::kCrashFolderPrefix.size());
    for (std::size_t i = 0; i < stamp.size(); ++i) {
        const bool dash = i == 8 || i == 15;
        if (dash ? stamp[i] != '-' : (stamp[i] < '0' || stamp[i] > '9')) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> crashFolders(const std::filesystem::path& directory)
{
    std::vector<std::string> folders;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        std::string name = utf8(it->path().filename());
        std::error_code kind;
        if (isCrashFolder(name) && it->is_directory(kind)) {
            folders.push_back(std::move(name));
        }
    }
    std::ranges::sort(folders);
    return folders;
}

void keepNewestCrashes(const std::filesystem::path& directory, std::size_t count)
{
    const std::vector<std::string> folders = crashFolders(directory);
    for (std::size_t i = 0; i + count < folders.size(); ++i) {
        std::error_code error;
        std::filesystem::remove_all(directory / pathOf(folders[i]), error);
    }
}

void keepNewestCrashesInFolders(const std::filesystem::path& directory, std::size_t count)
{
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        std::error_code kind;
        if (it->is_directory(kind) && !it->is_symlink(kind)) {
            keepNewestCrashes(it->path(), count);
        }
    }
}

std::optional<std::string> unacknowledgedCrash(const std::vector<std::string>& folders, std::string_view acknowledged)
{
    if (folders.empty()) {
        return std::nullopt;
    }
    const std::string& newest = *std::ranges::max_element(folders);
    if (isCrashFolder(acknowledged) && std::string_view(newest) <= acknowledged) {
        return std::nullopt;
    }
    return newest;
}

CrashReport::CrashReport(AppSettings& settings, std::filesystem::path directory, QObject* parent)
    : QObject(parent),
      m_settings(settings),
      m_directory(std::move(directory))
{
    Q_ASSERT(s_instance == nullptr);
    s_instance = this;
    keepNewestCrashes(m_directory, kKept);
    const std::vector<std::string> folders = crashFolders(m_directory);
    if (!folders.empty()) {
        m_newest = folders.back();
    }
    m_pending = unacknowledgedCrash(folders, m_settings.acknowledgedCrash().toStdString()).has_value();
}

CrashReport::~CrashReport()
{
    s_instance = nullptr;
}

CrashReport* CrashReport::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

std::optional<std::filesystem::path> CrashReport::newestFolder() const
{
    if (!m_newest) {
        return std::nullopt;
    }
    return m_directory / pathOf(*m_newest);
}

void CrashReport::acknowledge()
{
    if (!m_pending) {
        return;
    }
    m_settings.acknowledgeCrash(QString::fromStdString(*m_newest));
    m_pending = false;
    emit pendingChanged();
}

}
