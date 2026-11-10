#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <set>
#include <string_view>

namespace gs::app {

class ProfileLibrary;
class TaskLibrary;

enum class InputReader : std::uint8_t { Run, Profile, Task };

struct InputGrant {
    InputReader reader = InputReader::Run;
    QString id;

    friend bool operator==(const InputGrant&, const InputGrant&) = default;
};

class InputReadGrants final : public QObject
{
    Q_OBJECT

public:
    static constexpr std::string_view kFormatName = "gamescrcpy.inputRead";
    static constexpr int kFormatVersion = 1;
    static constexpr std::size_t kMostBytes = std::size_t{1} << 20;
    static constexpr qsizetype kMostIdChars = 256;

    explicit InputReadGrants(std::filesystem::path file, QObject* parent = nullptr);
    ~InputReadGrants() override;
    InputReadGrants(const InputReadGrants&) = delete;
    InputReadGrants& operator=(const InputReadGrants&) = delete;

    [[nodiscard]] static InputReadGrants* instance() noexcept { return s_instance; }

    [[nodiscard]] bool fresh() const noexcept { return m_fresh; }
    [[nodiscard]] bool readOnly() const noexcept { return m_readOnly; }
    void follow(ProfileLibrary& profiles, TaskLibrary& tasks);
    [[nodiscard]] bool allows(const InputGrant& grant) const;
    void allow(const InputGrant& grant);
    void forget(const InputGrant& grant);
    bool allowFolder(const QString& folder);
    [[nodiscard]] QStringList listed(InputReader reader) const;

private:
    void load();
    void adopt(const ProfileLibrary& profiles, const TaskLibrary& tasks);
    void change(InputReader reader, const QStringList& ids, bool allowed);
    void save();

    static inline InputReadGrants* s_instance = nullptr;

    const std::filesystem::path m_file;
    mutable std::mutex m_mutex;
    std::set<QString> m_profiles;
    std::set<QString> m_tasks;
    QPointer<ProfileLibrary> m_profileLibrary;
    QPointer<TaskLibrary> m_taskLibrary;
    bool m_fresh = false;
    bool m_readOnly = false;
    bool m_unsaved = false;
};

}
