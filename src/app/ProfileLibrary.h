#pragma once

#include "ExtensionProfile.h"

#include <gs/input/Profile.h>

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

struct ProfileEntry {
    QString id;
    std::filesystem::path path;
    QString name;
    QString app;
    bool portrait = false;
    int controls = 0;
    int warnings = 0;
    QString error;
    QVariantList markers;
    QDateTime modified;
    QString extensionId;
    QString extensionName;
    QString version;
    QStringList patchedBy;
    QVariantList conflicts;
    bool userPatch = false;
    int userEdits = 0;
    QString userPatchProblem;
    std::shared_ptr<const input::Profile> profile;
    std::filesystem::path folder;
    bool logic = false;
    std::filesystem::path extensionFolder;

    [[nodiscard]] bool readOnly() const noexcept { return !extensionId.isEmpty(); }
};

class ProfileLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged FINAL)
    Q_PROPERTY(QString folder READ folder CONSTANT FINAL)

public:
    using Discard = std::function<bool(const QString& path)>;

    explicit ProfileLibrary(std::filesystem::path directory, Discard discard = {}, QObject* parent = nullptr);
    ~ProfileLibrary() override;

    static ProfileLibrary* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static ProfileLibrary* instance() noexcept { return s_instance; }

    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] QString folder() const;
    [[nodiscard]] const std::vector<ProfileEntry>& all() const noexcept { return m_entries; }
    [[nodiscard]] const ProfileEntry* find(const QString& id) const;
    [[nodiscard]] std::shared_ptr<const input::Profile> profile(const QString& id) const;
    [[nodiscard]] QString neighbour(const QString& id, int step) const;
    [[nodiscard]] const ProfileEntry* linkedTo(const QString& package) const;
    [[nodiscard]] int usableCount() const;

    Q_INVOKABLE bool isProfileFile(const QUrl& url) const;
    Q_INVOKABLE QVariantMap importFiles(const QList<QUrl>& files, bool allowLogic = false);
    Q_INVOKABLE QVariantList templates() const;
    Q_INVOKABLE QString createFromTemplate(const QString& templateId, const QString& name, const QString& app,
                                           bool portrait);
    Q_INVOKABLE QString copyName(const QString& name) const;
    QString add(const input::Profile& profile);
    Q_INVOKABLE QString duplicate(const QString& id);
    Q_INVOKABLE QString ensureFolder(const QString& id);
    Q_INVOKABLE bool setApp(const QString& id, const QString& package);
    bool save(const QString& id, const input::Profile& profile);
    Q_INVOKABLE bool restore(const QString& id);
    Q_INVOKABLE bool remove(const QString& id);
    Q_INVOKABLE bool exportTo(const QString& id, const QUrl& target) const;
    Q_INVOKABLE bool exportPackage(const QString& id, const QString& fileOrUrl) const;
    Q_INVOKABLE QVariantList usersOf(const QString& ref) const;
    Q_INVOKABLE QString exportName(const QString& id) const;
    Q_INVOKABLE void reveal(const QString& id) const;
    Q_INVOKABLE void openFolder() const;
    Q_INVOKABLE bool lastWriteBusy() const { return m_busy; }
    void setExtensionProfiles(std::vector<ExtensionProfile> profiles);
    void reload();

signals:
    void entriesChanged();
    void profileChanged(const QString& id);
    void logicConfirmed(const QStringList& ids);
    void profileDuplicated(const QString& from, const QString& to);
    void profileRemoved(const QString& id);

private:
    [[nodiscard]] ProfileEntry read(const std::filesystem::path& path, const QString& id, QByteArray& contents) const;
    void adoptComponents(const std::filesystem::path& folder);
    [[nodiscard]] QString importFolder(const QString& source, const QStringList& reserved, QString& name,
                                       QString& reason);
    [[nodiscard]] QString importPackage(const QString& source, const QStringList& reserved, QString& name,
                                        QString& reason);
    void describe(ProfileEntry& entry, const QByteArray& contents) const;
    [[nodiscard]] std::filesystem::path patchPath(const QString& id) const;
    void applyUserPatch(ProfileEntry& entry, QByteArray& contents) const;
    bool savePatch(const ProfileEntry& entry, const input::Profile& profile);
    bool write(const std::filesystem::path& path, std::string_view contents) const;
    [[nodiscard]] QString freeId(const QString& base, const QStringList& reserved = {}) const;
    [[nodiscard]] QString problemText(const input::LoadResult& loaded) const;

    static inline ProfileLibrary* s_instance = nullptr;

    std::filesystem::path m_directory;
    Discard m_discard;
    std::vector<ExtensionProfile> m_extensionProfiles;
    std::vector<ProfileEntry> m_entries;
    QVariantList m_list;
    std::map<QString, QByteArray> m_contents;
    QFileSystemWatcher m_watcher;
    mutable bool m_busy = false;
};

struct ProfileChoice {
    bool change = false;
    QString id;
};

[[nodiscard]] std::string logicEntryOf(const std::filesystem::path& folder);

[[nodiscard]] ProfileChoice chooseForApp(const ProfileLibrary& library,
                                         const std::map<std::string, std::string, std::less<>>& remembered,
                                         const QString& package, const QString& current);

[[nodiscard]] bool offersStart(const ProfileLibrary& library,
                               const std::map<std::string, std::string, std::less<>>& remembered,
                               const std::vector<std::string>& closed, const QString& package, bool game);

}
