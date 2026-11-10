#pragma once

#include "ExtensionModules.h"
#include "TouchMethods.h"

#include <gs/base/Clock.h>
#include <gs/ext/Diagnostics.h>
#include <gs/ext/Manifest.h>
#include <gs/script/Services.h>

#include <QObject>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::platform {
class TrayNotice;
}

namespace gs::app {

struct NativeTexts {
    QString title;
    QString description;
    std::vector<QString> paramTitles;
    std::vector<QString> paramDescriptions;
    std::vector<std::vector<QString>> paramOptions;
    std::vector<QString> slotTitles;
};

struct NativeWanted {
    std::string id;
    std::string version;
    QString name;
    std::filesystem::path directory;
    bool official = false;
    ext::NativeContribution native;
    std::vector<std::string> granted;
    std::vector<QString> methodTitles;
    std::vector<QString> methodDescriptions;
    std::vector<QString> nodeDescriptions;
    std::vector<NativeTexts> attachmentTexts;
    std::vector<NativeTexts> behaviorTexts;
};

struct DeclaredModule {
    std::string module;
    std::string extension;
    QString extensionName;
    exthost::InstanceKind kind = exthost::InstanceKind::Attachment;
    std::vector<std::string> appliesTo;
    std::vector<ext::DeclaredSlot> slotList;
    int fingers = 1;
    bool frames = false;
    std::vector<ext::DeclaredParam> params;
    NativeTexts texts;
};

struct NativeJobTarget {
    std::shared_ptr<exthost::Module> module;
    std::size_t index = 0;
    ext::NativeJob declared;
    std::string extensionName;
    std::string code;
    std::string problem;
};

struct NativeOptions {
    std::filesystem::path program;
    std::filesystem::path storeRoot;
    const Clock* clock = nullptr;
    std::chrono::milliseconds readyWait{2'000};
};

class NativeExtensions final : public QObject, public TouchMethodSource
{
    Q_OBJECT

public:
    using TouchOptions = std::function<std::string(std::string_view key, std::string_view device)>;
    using GlobalChoices = std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>>;

    explicit NativeExtensions(NativeOptions options, QObject* parent = nullptr);
    ~NativeExtensions() override;
    NativeExtensions(const NativeExtensions&) = delete;
    NativeExtensions& operator=(const NativeExtensions&) = delete;

    [[nodiscard]] static NativeExtensions* instance() noexcept { return s_instance.load(); }

    void apply(std::vector<NativeWanted> wanted);
    void setGlobals(GlobalChoices globals);
    void setTouchOptions(TouchOptions options) { m_touchOptions = std::move(options); }
    [[nodiscard]] std::vector<script::NodeInfo> nodes() const;
    [[nodiscard]] script::NodeAnswer call(std::string_view extension, std::string_view node,
                                          const std::vector<script::NodeValue>& arguments) const;
    [[nodiscard]] NativeJobTarget jobTarget(std::string_view extension, std::string_view job) const;
    [[nodiscard]] QVariantList touchMethods() const;
    [[nodiscard]] std::vector<ext::Diagnostic> problems() const;
    [[nodiscard]] bool loaded(std::string_view id) const;

    [[nodiscard]] std::unique_ptr<ExtensionTouch> openTouch(std::string_view key, std::string_view device,
                                                             ControlSink& injection,
                                                             std::function<void()> fellBack) override;
    [[nodiscard]] QString touchTitle(std::string_view key) const override;

    [[nodiscard]] std::vector<DeclaredModule> declaredModules() const;
    [[nodiscard]] std::optional<DeclaredModule> declaredModule(std::string_view module) const;
    [[nodiscard]] QVariantList attachmentList() const;
    [[nodiscard]] QVariantList behaviorList() const;
    [[nodiscard]] std::shared_ptr<ExtensionModules> moduleFactory() const;
    [[nodiscard]] input::ModuleCatalog moduleCatalog() const;

    struct Entry;
    struct Shared;

signals:
    void changed();
    void globalsChanged();
    void stopped(const QString& id, const QString& name);
    void moduleTrouble(const QString& module, const QString& control, int kind);
    void modulesFailed(const QString& id, const QString& name);
    void extensionEvent(const QString& id, const QString& topic, const QString& json);

private:
    [[nodiscard]] std::shared_ptr<Entry> load(const NativeWanted& wanted) const;
    void matchLate(const exthost::Module* module);
    void failLate(const exthost::Module* module, const std::string& why);
    [[nodiscard]] std::vector<std::shared_ptr<Entry>> entries() const;
    void showNotice(const QString& title, const QString& text);
    [[nodiscard]] std::shared_ptr<ExtensionModules> makeModules() const;
    [[nodiscard]] std::vector<std::pair<std::string, const exthost::Module*>> moduleKeys() const;
    void noteTrouble(const ModuleNotice& notice);

    static inline std::atomic<NativeExtensions*> s_instance{nullptr};

    NativeOptions m_options;
    TouchOptions m_touchOptions;
    std::shared_ptr<Shared> m_shared;
    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<Entry>> m_entries;
    std::unique_ptr<platform::TrayNotice> m_tray;
    std::shared_ptr<ExtensionModules> m_modules;
    GlobalChoices m_globals;
    std::map<std::string, std::deque<Microseconds>, std::less<>> m_faults;
    SteadyClock m_steady;
};

}
