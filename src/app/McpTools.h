#pragma once

#include "McpServer.h"
#include "ProfileEditor.h"
#include "ScriptRuntime.h"

#include <gs/base/Clock.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/script/ScriptHost.h>

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::app {

class ComponentLibrary;
class McpDrafts;
class ProfileLibrary;
class TaskLibrary;

struct McpPhone {
    QString serial;
    QString name;
    int width = 0;
    int height = 0;
    QString app;
    QString profileId;
    QString profileName;
    std::filesystem::path folder;
};

struct McpMaterialItem {
    QString name;
    QString kind;
    QString note;
};

struct McpMaterialsRequest {
    QString client;
    QString serial;
    QString profileId;
    QString profileName;
    std::filesystem::path folder;
    std::vector<McpMaterialItem> items;
};

struct McpMaterialResult {
    QString name;
    QString kind;
    QString state;
    QString resource;
    QVariantMap value;
};

using McpMaterialsDone = std::function<void(const std::vector<McpMaterialResult>&)>;

using McpFrameCapture = std::function<std::shared_ptr<const media::LumaSnapshot>()>;

struct McpDevices {
    std::function<std::vector<McpPhone>()> phones;
    std::function<bool(const QString& serial, std::function<void(const QImage&)> done)> screenshot;
    std::function<McpFrameCapture(const QString& serial)> frames;
    std::function<QString(const QString& serial, ScriptJob job, quint64& number)> startScript;
    std::function<void(quint64 number)> stopScript;
    std::function<bool(const QString& serial)> recording;
    std::function<QString(const QString& serial)> startRecording;
    std::function<QVariantMap(const QString& serial)> stopRecording;
    std::function<QString()> recordingsFolder;
    std::function<ProfileEditor*(const QString& serial, const QString& profileId, QString& problem)> openEditor;
    std::function<QString(const McpMaterialsRequest& request, McpMaterialsDone done)> materials;
    std::function<void(const QString& client)> endMaterials;
    std::function<bool(const QString& target, const QString& serial, QString& problem)> openDraftPreview;
};

struct McpToolSetup {
    ProfileLibrary* profiles = nullptr;
    std::filesystem::path logFile;
    McpDevices devices;
    ComponentLibrary* components = nullptr;
    TaskLibrary* tasks = nullptr;
    McpDrafts* drafts = nullptr;
    std::filesystem::path program;
};

struct McpExtensionTool {
    QString name;
    QString tool;
    QString extension;
    std::uint64_t logic = 0;
    QString description;
    QJsonObject input;
};

[[nodiscard]] QString mcpExtensionToolName(const QString& extension, const QString& tool);

class McpTools final : public QObject, public McpToolHost
{
    Q_OBJECT

public:
    static constexpr auto kRunLimit = std::chrono::seconds(60);
    static constexpr auto kScreenshotWait = std::chrono::seconds(5);
    static constexpr auto kProgramLimit = std::chrono::seconds(120);
    static constexpr qsizetype kMostCodeBytes = qsizetype{256} << 10;
    static constexpr qsizetype kMostFileBytes = qsizetype{2} << 20;
    static constexpr qsizetype kMostWrittenBytes = qsizetype{8} << 20;
    static constexpr qsizetype kMostWrittenFiles = 32;
    static constexpr qsizetype kMostConsoleLines = 2000;
    static constexpr qsizetype kMostProposedControls = 32;
    static constexpr auto kToolLimit = std::chrono::seconds(60);
    static constexpr auto kMaterialsLimit = std::chrono::minutes(10);
    static constexpr qsizetype kMostMaterials = 12;
    static constexpr qsizetype kMostToolSchemaBytes = qsizetype{16} << 10;
    static constexpr qsizetype kMostToolDescriptionChars = 1000;
    static constexpr qsizetype kMostToolsPerExtension = 32;

    McpTools(McpToolSetup setup, const Clock& clock, QObject* parent = nullptr);
    ~McpTools() override;
    McpTools(const McpTools&) = delete;
    McpTools& operator=(const McpTools&) = delete;

    [[nodiscard]] QJsonArray tools() const override;
    void call(const McpCall& call, Reply reply) override;
    void cancel(quint64 call) override;
    void forget(const QString& client) override;
    [[nodiscard]] QVariantList allowedNow() const override;
    void endAllowed(const QString& client, const QString& key) override;
    void expire();
    [[nodiscard]] std::vector<McpExtensionTool> extensionTools() const;

signals:
    void toolsChanged();

private:
    struct Registry;

    struct Waiting {
        Reply reply;
        QString serial;
        QString phone;
        std::optional<Microseconds> deadline;
        bool screenshot = false;
        int maxSide = 0;
        quint64 run = 0;
        std::shared_ptr<QTemporaryDir> scratch;
        QStringList console;
        qsizetype dropped = 0;
        bool timedOut = false;
        std::chrono::milliseconds limit{0};
        QPointer<QProcess> process;
        std::uint64_t logic = 0;
        bool materials = false;
        QString client;
    };

    struct Folder {
        std::filesystem::path path;
        QString name;
        bool draft = false;
        std::shared_ptr<QTemporaryDir> scratch;
    };

    struct Proposal {
        QString client;
        QString phone;
        QPointer<ProfileEditor> editor;
        QStringList suggestions;
        QStringList names;
        QStringList types;
        QStringList states;
        QStringList controls;
    };

    using Parse = std::function<McpOutcome(const QByteArray& output, const QByteArray& errors)>;

    [[nodiscard]] McpOutcome apiReference(const QJsonObject& given) const;
    [[nodiscard]] McpOutcome listPhones(const QJsonObject& given) const;
    [[nodiscard]] McpOutcome listProfiles(const McpCall& call) const;
    [[nodiscard]] McpOutcome readProfile(const McpCall& call) const;
    [[nodiscard]] McpOutcome readLogs(const QJsonObject& given) const;
    [[nodiscard]] McpOutcome listComponents(const McpCall& call) const;
    [[nodiscard]] McpOutcome readComponent(const McpCall& call) const;
    [[nodiscard]] McpOutcome listTasks(const McpCall& call) const;
    [[nodiscard]] McpOutcome readTask(const McpCall& call) const;
    [[nodiscard]] McpOutcome readDraft(const McpCall& call) const;
    [[nodiscard]] McpOutcome draftDiff(const McpCall& call) const;
    [[nodiscard]] McpOutcome discardDraft(const McpCall& call);
    [[nodiscard]] McpOutcome openDraftPreview(const McpCall& call) const;
    [[nodiscard]] McpOutcome readBlueprint(const McpCall& call) const;
    [[nodiscard]] McpOutcome editBlueprint(const McpCall& call);
    [[nodiscard]] McpOutcome stopRecording(const McpCall& call);
    [[nodiscard]] McpOutcome listRecordings(const QJsonObject& given) const;
    [[nodiscard]] McpOutcome proposeKeymap(const McpCall& call);
    [[nodiscard]] McpOutcome keymapStatus(const McpCall& call) const;
    [[nodiscard]] McpOutcome createExtension(const McpCall& call);
    [[nodiscard]] McpOutcome writeExtension(const McpCall& call);
    [[nodiscard]] McpOutcome writeDraft(const McpCall& call, QStringView kind);
    void runReplay(const McpCall& call, Reply reply);
    void getFrame(quint64 call, const QJsonObject& given, Reply reply);
    void findImage(quint64 call, const QJsonObject& given, Reply reply);
    void applyDraft(const McpCall& call, Reply reply);
    void runScript(const McpCall& call, Reply reply);
    void check(const McpCall& call, Reply reply);
    void startRecording(const McpCall& call, Reply reply);
    void requestMaterials(const McpCall& call, Reply reply);
    void validateExtension(const McpCall& call, Reply reply);
    void packExtension(const McpCall& call, Reply reply);
    void captureFrame(quint64 call);
    void startRun(quint64 call, const QString& client, const McpPhone& phone, const QString& code,
                  const QString& function, const std::string& entry);
    void runProgram(quint64 call, const QStringList& arguments, const std::filesystem::path& folder, Parse parse);
    void ended(quint64 call, const QString& result, const QString& message);
    void finish(quint64 call, McpOutcome outcome);
    void schedule();
    [[nodiscard]] std::optional<McpPhone> phoneFor(const QString& serial, McpOutcome& problem) const;
    [[nodiscard]] std::optional<Folder> folderFor(const QString& client, const QString& target, bool draft,
                                                  McpOutcome& problem) const;
    [[nodiscard]] std::filesystem::path extensionFolder(const QString& client, const QString& given,
                                                        McpOutcome& problem) const;
    [[nodiscard]] std::vector<script::ComponentModule> componentModules(const std::string& profileJson,
                                                                        const QString& drafted,
                                                                        const std::filesystem::path& draftFolder) const;
    [[nodiscard]] bool drafted(const QString& client, const QString& target) const;
    [[nodiscard]] QString program() const;
    void callExtensionTool(const McpCall& call, Reply reply);
    void startExtensionCall(quint64 call, const McpExtensionTool& tool, const QJsonObject& arguments);
    void toolAnswered(std::uint64_t logic, quint64 call, bool ok, const QString& text);
    void logicEnded(quint64 logic);
    static script::ServiceAnswer registerTool(Registry& registry, const QPointer<McpTools>& self,
                                              const LogicCaller& caller, std::string_view json);
    static script::ServiceAnswer unregisterTool(Registry& registry, const QPointer<McpTools>& self,
                                                const LogicCaller& caller, std::string_view json);
    static script::ServiceAnswer toolResult(const QPointer<McpTools>& self, const LogicCaller& caller,
                                            std::string_view json);

    McpToolSetup m_setup;
    const Clock& m_clock;
    std::shared_ptr<Registry> m_registry;
    QTimer m_deadline;
    std::map<quint64, Waiting> m_waiting;
    std::map<std::pair<QString, QString>, QString> m_quiet;
    std::map<QString, Proposal> m_proposals;
    quint64 m_lastProposal = 0;
};

}
