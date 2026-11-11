#pragma once

#include "LogicServices.h"

#include <gs/script/Services.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::platform {
class SystemSpeech;
enum class SpeechOutput : std::uint8_t;
}

namespace gs::app {

class ExtensionHost;
class NativeJobs;

struct SpeechVoiceInfo {
    QString id;
    QString name;
    QString language;

    friend bool operator==(const SpeechVoiceInfo&, const SpeechVoiceInfo&) = default;
};

struct SpeechLineRequest {
    QString text;
    QString voice;
    double rate = 1.0;
    double volume = 1.0;
};

struct SpeechListenRequest {
    std::optional<QStringList> phrases;
    QString language;
    double confidence = 0.6;
};

struct SpeechHeardInfo {
    std::optional<QString> phrase;
    QString text;
    double confidence = 0.0;
};

struct SpeechEngineInfo {
    QString id;
    QString title;
    bool say = false;
    bool listen = false;
    bool dictation = false;
    QStringList languages;
};

class SpeechEngine
{
public:
    using Voices = std::function<void(std::vector<SpeechVoiceInfo> voices, QString problem)>;
    using Spoken = std::function<void(bool finished, QString problem)>;
    using Heard = std::function<void(SpeechHeardInfo heard)>;
    using Ended = std::function<void(QString problem)>;

    SpeechEngine() = default;
    virtual ~SpeechEngine() = default;
    SpeechEngine(const SpeechEngine&) = delete;
    SpeechEngine& operator=(const SpeechEngine&) = delete;

    [[nodiscard]] virtual SpeechEngineInfo info() const = 0;
    virtual void voices(Voices done) = 0;
    virtual std::uint64_t say(const SpeechLineRequest& line, Spoken spoken) = 0;
    virtual void stop(std::uint64_t token) = 0;
    virtual std::uint64_t listen(const SpeechListenRequest& wanted, Heard heard, Ended ended) = 0;
    virtual void unlisten(std::uint64_t token) = 0;
};

class SystemSpeechEngine final : public SpeechEngine
{
public:
    explicit SystemSpeechEngine(platform::SpeechOutput output);
    ~SystemSpeechEngine() override;

    [[nodiscard]] SpeechEngineInfo info() const override;
    void voices(Voices done) override;
    std::uint64_t say(const SpeechLineRequest& line, Spoken spoken) override;
    void stop(std::uint64_t token) override;
    std::uint64_t listen(const SpeechListenRequest& wanted, Heard heard, Ended ended) override;
    void unlisten(std::uint64_t token) override;

    [[nodiscard]] platform::SystemSpeech& speech() noexcept { return *m_speech; }

private:
    std::unique_ptr<platform::SystemSpeech> m_speech;
    std::vector<SpeechVoiceInfo> m_voices;
    QStringList m_languages;
};

struct ExtensionSpeechEngineInfo {
    QString extension;
    QString name;
    QString title;
    QString say;
    QString voices;
    QString listen;
    bool dictation = false;
    QStringList languages;

    friend bool operator==(const ExtensionSpeechEngineInfo&, const ExtensionSpeechEngineInfo&) = default;
};

class ExtensionSpeechEngine final : public SpeechEngine
{
public:
    ExtensionSpeechEngine(ExtensionSpeechEngineInfo declared, NativeJobs& jobs);

    [[nodiscard]] const ExtensionSpeechEngineInfo& declared() const noexcept { return m_declared; }
    [[nodiscard]] SpeechEngineInfo info() const override;
    void voices(Voices done) override;
    std::uint64_t say(const SpeechLineRequest& line, Spoken spoken) override;
    void stop(std::uint64_t token) override;
    std::uint64_t listen(const SpeechListenRequest& wanted, Heard heard, Ended ended) override;
    void unlisten(std::uint64_t token) override;

private:
    ExtensionSpeechEngineInfo m_declared;
    NativeJobs& m_jobs;
};

struct SpeechReplies {
    std::function<void(std::uint64_t logic, quint64 request, const QString& json)> answer;
    std::function<void(std::uint64_t logic, quint64 request, const QString& message)> refuse;
    std::function<void(std::uint64_t logic, const QString& kind, const QString& id, const QString& json)> tell;
};

struct SpeechDefaults {
    QString engine = QStringLiteral("system");
    QString voice;
    double rate = 1.0;
    double volume = 1.0;
    QString listenEngine = QStringLiteral("system");
    QString language;
    QString interfaceLanguage;
};

class SpeechEngines final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList engines READ engineList NOTIFY enginesChanged FINAL)

public:
    static constexpr std::string_view kSystem = "system";
    static constexpr std::string_view kPermission = "speech.listen";
    static constexpr std::string_view kEventKind = "speech";
    static constexpr qsizetype kMostTextCharacters = 1000;
    static constexpr std::size_t kMostPhrases = 256;
    static constexpr qsizetype kMostPhraseCharacters = 64;
    static constexpr std::size_t kMostWaitingLines = 32;
    static constexpr std::size_t kMostListeners = 8;

    explicit SpeechEngines(QObject* parent);
    ~SpeechEngines() override;
    SpeechEngines(const SpeechEngines&) = delete;
    SpeechEngines& operator=(const SpeechEngines&) = delete;

    static SpeechEngines* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static SpeechEngines* instance() noexcept { return s_instance; }

    void offerServices();
    void setReplies(SpeechReplies replies);
    void setDefaults(std::function<SpeechDefaults()> defaults);
    void setPermitted(const QStringList& extensions);
    void addEngine(std::unique_ptr<SpeechEngine> engine);
    void removeEngine(const QString& id);
    void follow(ExtensionHost& extensions, NativeJobs& jobs);

    void request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json);
    void dropRequest(const LogicCaller& caller, quint64 id);
    [[nodiscard]] script::ServiceAnswer call(const LogicCaller& caller, std::string_view service,
                                             std::string_view json);
    void endLogic(std::uint64_t logic);

    [[nodiscard]] QVariantList engineList() const;
    Q_INVOKABLE void loadVoices(const QString& engine);
    Q_INVOKABLE bool preview();

signals:
    void enginesChanged();
    void voicesLoaded(const QString& engine, const QVariantList& voices, const QString& problem);

private:
    struct Shared;

    struct Line {
        quint64 key = 0;
        std::uint64_t logic = 0;
        quint64 request = 0;
        bool preview = false;
        SpeechLineRequest line;
    };

    struct Queue {
        std::deque<Line> waiting;
        std::optional<Line> current;
        std::uint64_t token = 0;
        bool stopping = false;
    };

    struct Listener {
        std::uint64_t logic = 0;
        QString owner;
        QString engine;
        std::uint64_t token = 0;
        double confidence = 0.6;
    };

    [[nodiscard]] static script::ServiceAnswer callWith(const std::shared_ptr<Shared>& shared,
                                                        const LogicCaller& caller, std::string_view service,
                                                        std::string_view json);

    void say(const LogicCaller& caller, quint64 id, const QString& json);
    void listVoices(const LogicCaller& caller, quint64 id, const QString& json);
    void listen(const LogicCaller& caller, quint64 id, const QString& json);
    void enqueue(const QString& engine, Line line, bool interrupt);
    void startNext(const QString& engine);
    void spoken(const QString& engine, quint64 key, bool finished, const QString& problem);
    void settle(const Line& line, bool finished, const QString& problem);
    void stopWhere(const std::function<bool(const Line&)>& matches);
    void heard(const QString& handle, const SpeechHeardInfo& heard);
    void ended(const QString& handle, const QString& problem);
    void close(std::uint64_t logic, const QString& handle);
    void drop(const QString& handle, const QString& reason, const QString& problem);
    void publish();
    [[nodiscard]] SpeechDefaults defaults() const;

    static inline SpeechEngines* s_instance = nullptr;

    std::shared_ptr<Shared> m_shared;
    SpeechReplies m_replies;
    std::function<SpeechDefaults()> m_defaults;
    std::map<QString, std::unique_ptr<SpeechEngine>> m_engines;
    std::map<QString, Queue> m_queues;
    std::map<QString, Listener> m_listeners;
    std::set<QString> m_permitted;
    quint64 m_lastKey = 0;
    std::uint64_t m_lastHandle = 0;
    bool m_offered = false;
};

}
