#include "JsonFile.h"
#include "PersistedFiles.h"
#include "ProfileLibrary.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace {

using gs::app::JsonFile;
using gs::app::ProfileLibrary;
using gs::stability::FileKind;
using gs::stability::WriterProgress;
using gs::stability::WriteSlot;

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

class SharedProgress
{
public:
    explicit SharedProgress(const char* name)
        : m_mapping(::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name))
        , m_view(m_mapping != nullptr ? ::MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(WriterProgress))
                                      : nullptr)
    {
    }

    ~SharedProgress()
    {
        if (m_view != nullptr) {
            ::UnmapViewOfFile(m_view);
        }
        if (m_mapping != nullptr) {
            ::CloseHandle(m_mapping);
        }
    }

    SharedProgress(const SharedProgress&) = delete;
    SharedProgress& operator=(const SharedProgress&) = delete;

    [[nodiscard]] WriterProgress* get() const noexcept { return static_cast<WriterProgress*>(m_view); }

private:
    HANDLE m_mapping;
    void* m_view;
};

class Writer
{
public:
    Writer(const std::filesystem::path& data, gs::input::Profile base, WriterProgress& progress)
        : m_base(std::move(base))
        , m_extensionBase(gs::stability::extensionBase(m_base))
        , m_progress(progress)
        , m_settings(gs::stability::settingsPath(data), std::chrono::milliseconds::zero())
        , m_devices(gs::stability::devicesPath(data), std::chrono::milliseconds::zero())
        , m_library(gs::stability::profilesPath(data), [](const QString& path) { return QFile::remove(path); })
    {
        m_library.setExtensionProfiles(
            {{.id = toQString(gs::stability::extensionProfileId()),
              .extensionId = toQString(gs::stability::kExtensionId),
              .extensionName = QStringLiteral("Kill test"),
              .version = QStringLiteral("1.0.0"),
              .json = QByteArray::fromStdString(gs::stability::extensionProfileJson(m_base)),
              .patchedBy = {},
              .conflicts = {}}});
        publish(FileKind::Settings, gs::stability::versionOf(m_settings.document()));
        publish(FileKind::Devices, gs::stability::versionOf(m_devices.document()));
        publish(FileKind::Profile, profileVersion(toQString(gs::stability::kSavedProfileName)));
        publish(FileKind::AddedProfile, keepNewestAdded());
        publish(FileKind::Patch, profileVersion(toQString(gs::stability::extensionProfileId())));
    }

    [[noreturn]] void run(std::size_t first)
    {
        m_progress.ready.store(1);
        for (std::size_t kind = first % gs::stability::kFileKinds;; kind = (kind + 1) % gs::stability::kFileKinds) {
            WriteSlot& slot = m_progress.files[kind];
            const std::int64_t version = slot.started.load() + 1;
            slot.started.store(version);
            m_progress.writing.store(static_cast<std::int32_t>(kind));
            const bool written = write(static_cast<FileKind>(kind), version);
            m_progress.writing.store(-1);
            if (written) {
                slot.done.store(version);
                slot.writes.fetch_add(1);
            } else {
                slot.failures.fetch_add(1);
            }
        }
    }

private:
    void publish(FileKind kind, std::int64_t version)
    {
        WriteSlot& slot = m_progress.files[gs::stability::indexOf(kind)];
        slot.done.store(version);
        slot.started.store(version);
    }

    [[nodiscard]] std::int64_t profileVersion(const QString& id) const
    {
        const gs::app::ProfileEntry* entry = m_library.find(id);
        return entry != nullptr && entry->profile != nullptr ? gs::stability::versionOf(*entry->profile) : -1;
    }

    std::int64_t keepNewestAdded()
    {
        std::int64_t newest = -1;
        for (const gs::app::ProfileEntry& entry : m_library.all()) {
            const std::optional<std::int64_t> version = gs::stability::addedVersionOf(entry.id.toStdString());
            if (version && *version > newest) {
                newest = *version;
                m_added = entry.id;
            }
        }
        QStringList older;
        for (const gs::app::ProfileEntry& entry : m_library.all()) {
            if (gs::stability::addedVersionOf(entry.id.toStdString()) && entry.id != m_added) {
                older.append(entry.id);
            }
        }
        for (const QString& id : older) {
            m_library.remove(id);
        }
        return newest;
    }

    [[nodiscard]] bool write(FileKind kind, std::int64_t version)
    {
        switch (kind) {
        case FileKind::Settings:
            return save(m_settings, gs::stability::settingsAt(version));
        case FileKind::Devices:
            return save(m_devices, gs::stability::devicesAt(version));
        case FileKind::Profile:
            return saveProfile(version);
        case FileKind::AddedProfile:
            return addProfile(version);
        case FileKind::Patch:
            return m_library.save(
                toQString(gs::stability::extensionProfileId()),
                gs::stability::profileAt(m_extensionBase, std::string(gs::stability::kExtensionProfileName), version));
        }
        return false;
    }

    template <typename Document>
    [[nodiscard]] static bool save(JsonFile<Document>& file, const Document& document)
    {
        const std::uint64_t before = file.writes();
        file.update(document);
        file.flush();
        return file.writes() > before;
    }

    [[nodiscard]] bool saveProfile(std::int64_t version)
    {
        const QString id = toQString(gs::stability::kSavedProfileName);
        const gs::input::Profile profile =
            gs::stability::profileAt(m_base, std::string(gs::stability::kSavedProfileName), version);
        if (m_library.find(id) == nullptr) {
            return m_library.add(profile) == id;
        }
        return m_library.save(id, profile);
    }

    [[nodiscard]] bool addProfile(std::int64_t version)
    {
        const QString id =
            m_library.add(gs::stability::profileAt(m_base, gs::stability::addedProfileName(version), version));
        if (id.isEmpty()) {
            return false;
        }
        if (!m_added.isEmpty()) {
            m_library.remove(m_added);
        }
        m_added = id;
        return true;
    }

    const gs::input::Profile m_base;
    const gs::input::Profile m_extensionBase;
    WriterProgress& m_progress;
    JsonFile<gs::app::settings::Settings> m_settings;
    JsonFile<gs::app::settings::Devices> m_devices;
    ProfileLibrary m_library;
    QString m_added;
};

}

int main(int argc, char** argv)
{
    const QString data = qEnvironmentVariable("GS_DATA_DIR");
    if (argc != 3 || data.isEmpty()) {
        return 2;
    }
    const SharedProgress shared(argv[1]);
    std::optional<gs::input::Profile> base = gs::stability::loadTemplate();
    if (shared.get() == nullptr || !base) {
        return 3;
    }
    QCoreApplication application(argc, argv);
    Writer writer(data.toStdWString(), std::move(*base), *shared.get());
    writer.run(static_cast<std::size_t>(std::strtoul(argv[2], nullptr, 10)));
}
