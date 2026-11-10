#pragma once

#include "ExtensionBisect.h"
#include "ExtensionContent.h"
#include "ExtensionProfile.h"
#include "Settings.h"

#include <gs/ext/Diagnostics.h>
#include <gs/ext/FilterSource.h>
#include <gs/ext/Manifest.h>
#include <gs/ext/NativeCheck.h>
#include <gs/ext/Package.h>
#include <gs/ext/Version.h>
#include <gs/render/VideoFilter.h>

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

class AppSettings;
class NativeExtensions;
class TouchMethodSource;
struct NativeWanted;

struct ExtensionRoots {
    std::filesystem::path user;
    std::filesystem::path builtin;
    std::filesystem::path hostProgram;
};

[[nodiscard]] QString extensionSettingPath(std::string_view id, std::string_view key);
[[nodiscard]] QString extensionText(const ext::Manifest& manifest, std::string_view key, std::string_view fallback,
                                    const QString& language);
[[nodiscard]] QVariant extensionVariant(const ext::SettingDeclaration& setting, const ext::SettingValue& value);

class ExtensionHost : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged FINAL)
    Q_PROPERTY(QVariantMap overrides READ overrides NOTIFY valuesChanged FINAL)
    Q_PROPERTY(QString mode READ modeName NOTIFY extensionsChanged FINAL)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY extensionsChanged FINAL)
    Q_PROPERTY(QVariantList themes READ themes NOTIFY extensionsChanged FINAL)
    Q_PROPERTY(bool installing READ installing NOTIFY installingChanged FINAL)
    Q_PROPERTY(QVariantMap bisect READ bisectState NOTIFY bisectChanged FINAL)
    Q_PROPERTY(QVariantList touchMethods READ touchMethodList NOTIFY nativeChanged FINAL)
    Q_PROPERTY(QVariantList nativeAttachments READ nativeAttachments NOTIFY nativeChanged FINAL)
    Q_PROPERTY(QVariantList nativeBehaviors READ nativeBehaviors NOTIFY nativeChanged FINAL)
    Q_PROPERTY(int addOnsRevision READ addOnsRevision NOTIFY addOnsChanged FINAL)

public:
    enum class Mode : std::uint8_t { Normal, SafeRequested, SafeAfterCrash };

    struct Extension {
        std::filesystem::path directory;
        std::optional<ext::Manifest> manifest;
        std::vector<ext::Diagnostic> diagnostics;
        bool builtin = false;
        bool active = false;
        bool superseded = false;

        [[nodiscard]] bool usable() const noexcept
        {
            return manifest.has_value() && !superseded && !ext::hasErrors(diagnostics);
        }
    };

    ExtensionHost(AppSettings& settings, ExtensionRoots roots, QObject* parent = nullptr);
    ~ExtensionHost() override;

    static ExtensionHost* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static ExtensionHost* instance() noexcept { return s_instance; }

    void start(bool safeModeRequested, std::filesystem::path runMarker = {}, std::filesystem::path bisectFile = {});
    void finish();

    [[nodiscard]] Mode mode() const noexcept { return m_mode; }
    [[nodiscard]] const std::vector<Extension>& extensions() const noexcept { return m_extensions; }
    [[nodiscard]] const std::vector<ext::Diagnostic>& problems() const noexcept { return m_problems; }
    [[nodiscard]] std::vector<ext::Diagnostic> problemsOf(std::string_view id) const;
    [[nodiscard]] std::vector<render::VideoFilter> filters(const std::string& deviceId) const;
    [[nodiscard]] const std::vector<ExtensionProfile>& profiles() const noexcept { return m_profiles; }
    [[nodiscard]] const std::vector<ExtensionComponent>& components() const noexcept { return m_components; }
    [[nodiscard]] const std::vector<ExtensionTask>& tasks() const noexcept { return m_tasks; }
    [[nodiscard]] const std::vector<ExtensionScript>& scripts() const noexcept { return m_scripts; }
    [[nodiscard]] std::vector<std::string> grantedPermissions(std::string_view id) const;
    void setScanner(ext::NativeScanner scanner);
    [[nodiscard]] std::vector<const ext::Manifest*> settingsManifests() const;
    [[nodiscard]] QVariantMap values() const;
    [[nodiscard]] QVariantMap overrides() const;
    [[nodiscard]] std::string nativeSettings(std::string_view id) const;
    [[nodiscard]] QString modeName() const;
    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] QVariantList themes() const;
    [[nodiscard]] bool installing() const noexcept { return m_installing; }
    [[nodiscard]] QVariantMap bisectState() const;
    [[nodiscard]] const std::optional<ExtensionBisect>& bisect() const noexcept { return m_bisect; }
    [[nodiscard]] NativeExtensions* native() const noexcept { return m_native.get(); }
    [[nodiscard]] TouchMethodSource* touchSource() const noexcept;
    [[nodiscard]] QVariantList touchMethodList() const;
    [[nodiscard]] QVariantList nativeAttachments() const;
    [[nodiscard]] QVariantList nativeBehaviors() const;
    [[nodiscard]] int addOnsRevision() const noexcept { return m_addOnsRevision; }

    Q_INVOKABLE QVariantList globalAddOns(const QString& id) const;
    Q_INVOKABLE bool setGlobalAddOn(const QString& id, const QString& name, bool on);
    Q_INVOKABLE bool setGlobalAddOnParam(const QString& id, const QString& name, const QString& key,
                                         const QString& json);
    Q_INVOKABLE bool setValue(const QString& path, const QVariant& value);
    Q_INVOKABLE bool setDeviceValue(const QString& device, const QString& path, const QVariant& value);
    Q_INVOKABLE void resetDeviceValue(const QString& device, const QString& path);
    Q_INVOKABLE bool enable(const QString& id, bool on);
    Q_INVOKABLE bool remove(const QString& id, const QString& version);
    Q_INVOKABLE bool removeFolder(const QString& folder);
    Q_INVOKABLE bool isLocalFile(const QString& fileOrUrl) const;
    Q_INVOKABLE QVariantMap inspect(const QString& fileOrUrl) const;
    Q_INVOKABLE void installPackage(const QString& fileOrUrl);
    Q_INVOKABLE bool exportTo(const QString& id, const QString& version, const QString& fileOrUrl) const;
    Q_INVOKABLE QVariantMap inspectBundle(const QString& fileOrUrl) const;
    Q_INVOKABLE void installBundle(const QString& fileOrUrl);
    Q_INVOKABLE bool exportBundle(const QStringList& ids, const QString& name, const QString& fileOrUrl) const;
    Q_INVOKABLE bool startBisect();
    Q_INVOKABLE void answerBisect(bool stillHappens);
    Q_INVOKABLE void stopBisect();
    Q_INVOKABLE void concludeBisect(bool turnOff);

    bool setEnabled(const std::string& id, bool enabled);
    bool setValue(const std::string& id, const std::string& key, const settings::Scalar& value);
    bool setDeviceValue(const std::string& deviceId, const std::string& id, const std::string& key,
                        const settings::Scalar& value);
    bool resetDeviceValue(const std::string& deviceId, const std::string& id, const std::string& key);
    ext::InstalledExtension install(const std::filesystem::path& package);
    bool uninstall(const std::string& id, const ext::Version& version);

signals:
    void extensionsChanged();
    void filtersChanged();
    void profilesChanged();
    void componentsChanged();
    void tasksChanged();
    void scriptsChanged();
    void valuesChanged();
    void installingChanged();
    void installFinished(const QVariantMap& result);
    void bundleFinished(const QVariantMap& result);
    void bisectChanged();
    void saveBusy();
    void nativeChanged();
    void addOnsChanged();
    void nativeStopped(const QString& name);
    void modulesFailed(const QString& name);

private:
    struct Filter {
        std::string extensionId;
        std::string id;
        std::string fileName;
        std::string source;
        std::vector<ext::FilterParam> params;
        std::vector<ext::SettingDeclaration> settings;
        std::map<std::string, QImage, std::less<>> textures;
    };

    struct ProfileSource {
        std::string id;
        std::string extensionId;
        std::string version;
        std::string where;
        std::string text;
        std::filesystem::path folder;
    };

    struct ContentSource {
        std::string extensionId;
        std::string name;
        std::filesystem::path folder;
    };

    struct PatchSource {
        std::string extensionId;
        std::string target;
        std::string where;
        std::string text;
    };

    struct ContributedScript {
        std::string extensionId;
        ext::ScriptContribution contribution;
        std::filesystem::path folder;
    };

    struct BundleWork;

    ext::InstalledExtension finishInstall(ext::InstalledExtension outcome);
    void finishBundle(BundleWork work);
    [[nodiscard]] const Extension* exactly(std::string_view id, const ext::Version& version, bool builtin) const;
    void reload();
    void resolve();
    void load(const Extension& extension);
    void composeProfiles();
    void composeLibraries();
    [[nodiscard]] std::optional<ext::SignatureCheck> installedSignature(std::string_view id) const;
    void markRun() const;
    void report(ext::Code code, std::string path, std::string detail = {});
    [[nodiscard]] const ext::Manifest* manifestOf(std::string_view id) const;
    [[nodiscard]] const ext::Manifest* preferredManifest(std::string_view id) const;
    [[nodiscard]] std::filesystem::path consentFile(std::string_view id) const;
    [[nodiscard]] bool consented(const ext::Manifest& manifest) const;
    void recordConsent(const ext::Manifest& manifest);
    void forgetConsent(const std::string& id);
    void settleConsent(const std::string& id, std::optional<ext::Manifest> wanted, const std::error_code& error);
    void forget(const std::string& id);
    void storeBisect();
    void applyBisect();
    [[nodiscard]] QString nameOf(std::string_view id) const;
    [[nodiscard]] const ext::SettingDeclaration* declarationOf(std::string_view id, std::string_view key) const;
    [[nodiscard]] NativeWanted nativeWanted(const Extension& extension) const;
    [[nodiscard]] std::string settingsSnapshot(const ext::Manifest& manifest) const;
    void settleNative();
    void pushGlobals();
    [[nodiscard]] const ext::NativeAttachment* declaredAttachment(std::string_view id, std::string_view name) const;

    static inline ExtensionHost* s_instance = nullptr;

    AppSettings& m_settings;
    ExtensionRoots m_roots;
    Mode m_mode = Mode::Normal;
    std::filesystem::path m_runMarker;
    std::filesystem::path m_bisectFile;
    std::optional<ExtensionBisect> m_bisect;
    bool m_bisectUnsaved = false;
    std::map<std::string, std::optional<ext::Manifest>, std::less<>> m_unsavedConsents;
    std::vector<Extension> m_extensions;
    std::vector<ext::Diagnostic> m_problems;
    std::vector<Filter> m_filters;
    std::vector<ProfileSource> m_profileSources;
    std::vector<PatchSource> m_patchSources;
    std::vector<ExtensionProfile> m_profiles;
    std::vector<ContentSource> m_componentSources;
    std::vector<ContentSource> m_taskSources;
    std::vector<ExtensionComponent> m_components;
    std::vector<ExtensionTask> m_tasks;
    std::vector<ContributedScript> m_scriptSources;
    std::vector<ExtensionScript> m_scripts;
    ext::NativeScanner m_scanner;
    bool m_installing = false;
    std::unique_ptr<NativeExtensions> m_native;
    std::set<std::string, std::less<>> m_nativeStopped;
    std::set<std::string, std::less<>> m_modulesFailed;
    int m_addOnsRevision = 0;
    std::jthread m_installer;
};

}
