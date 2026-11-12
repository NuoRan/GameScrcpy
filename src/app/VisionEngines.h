#pragma once

#include "LogicServices.h"

#include <gs/exthost/Module.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/script/Services.h>

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::app {

class ExtensionHost;
class NativeExtensions;

struct VisionEngineInfo {
    QString id;
    QString title;
    QStringList kinds;
    QStringList languages;

    friend bool operator==(const VisionEngineInfo&, const VisionEngineInfo&) = default;
};

struct VisionPicture {
    std::shared_ptr<const media::LumaSnapshot> shot;
    script::Region region;
};

struct VisionAsk {
    exthost::VisionKind kind = exthost::VisionKind::Ocr;
    std::string params = "{}";
};

struct VisionReply {
    std::string json;
    script::Region area;
    QString problem;
};

class VisionEngine
{
public:
    VisionEngine() = default;
    virtual ~VisionEngine() = default;
    VisionEngine(const VisionEngine&) = delete;
    VisionEngine& operator=(const VisionEngine&) = delete;

    [[nodiscard]] virtual VisionEngineInfo info() const = 0;
    [[nodiscard]] virtual VisionReply recognize(const VisionPicture& picture, const VisionAsk& ask) = 0;
};

class SystemVisionEngine final : public VisionEngine
{
public:
    SystemVisionEngine();

    [[nodiscard]] VisionEngineInfo info() const override;
    [[nodiscard]] VisionReply recognize(const VisionPicture& picture, const VisionAsk& ask) override;

private:
    QStringList m_languages;
};

struct ExtensionVisionEngineInfo {
    QString extension;
    QString name;
    QString title;
    QStringList kinds;
    QStringList languages;

    friend bool operator==(const ExtensionVisionEngineInfo&, const ExtensionVisionEngineInfo&) = default;
};

class ExtensionVisionEngine final : public VisionEngine
{
public:
    ExtensionVisionEngine(ExtensionVisionEngineInfo declared, const NativeExtensions& native);

    [[nodiscard]] const ExtensionVisionEngineInfo& declared() const noexcept { return m_declared; }
    [[nodiscard]] VisionEngineInfo info() const override;
    [[nodiscard]] VisionReply recognize(const VisionPicture& picture, const VisionAsk& ask) override;

private:
    ExtensionVisionEngineInfo m_declared;
    const NativeExtensions& m_native;
};

[[nodiscard]] VisionReply visionResult(exthost::VisionKind kind, const VisionReply& reply);

struct VisionReplies {
    std::function<void(std::uint64_t logic, quint64 request, const QString& json)> answer;
    std::function<void(std::uint64_t logic, quint64 request, const QString& message)> refuse;
};

struct VisionFrames {
    std::function<bool(const LogicCaller& caller, quint64 request, bool color, LogicServices::FrameTaken taken)> grab;
    std::function<void(const LogicCaller& caller, quint64 request)> ungrab;
};

class VisionEngines final : public QObject
{
    Q_OBJECT

public:
    static constexpr std::string_view kSystem = "system";
    static constexpr std::string_view kPermission = "device.screen";
    static constexpr std::size_t kMostWaiting = 16;
    static constexpr std::size_t kMostResults = 1024;
    static constexpr std::size_t kMostTextBytes = 4096;
    static constexpr std::size_t kMostLabelBytes = 256;
    static constexpr std::size_t kMostEngineIdBytes = 160;

    explicit VisionEngines(QObject* parent);
    ~VisionEngines() override;
    VisionEngines(const VisionEngines&) = delete;
    VisionEngines& operator=(const VisionEngines&) = delete;

    [[nodiscard]] static VisionEngines* instance() noexcept { return s_instance; }

    void offerServices();
    void setReplies(VisionReplies replies);
    void setFrames(VisionFrames frames);
    void setPermitted(const QStringList& extensions);
    void addEngine(std::shared_ptr<VisionEngine> engine);
    void removeEngine(const QString& id);
    void follow(ExtensionHost& extensions, const NativeExtensions& native);

    void request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json);
    void dropRequest(const LogicCaller& caller, quint64 id);
    [[nodiscard]] script::ServiceAnswer call(const LogicCaller& caller, std::string_view service,
                                             std::string_view json);
    void endLogic(std::uint64_t logic);
    [[nodiscard]] std::vector<VisionEngineInfo> engines() const;
    [[nodiscard]] std::size_t pending() const noexcept { return m_pending.size(); }

signals:
    void enginesChanged();

private:
    struct Shared;
    class Worker;

    using Key = std::pair<std::uint64_t, quint64>;

    enum class Stage : std::uint8_t { Framing, Queued };

    struct Pending {
        LogicCaller caller;
        QString engine;
        VisionAsk ask;
        script::Region region;
        Stage stage = Stage::Framing;
    };

    [[nodiscard]] static script::ServiceAnswer callWith(const std::shared_ptr<Shared>& shared,
                                                        std::string_view service);

    void framed(const Key& key, std::shared_ptr<const media::LumaSnapshot> shot);
    void finished(const Key& key, const VisionReply& reply);
    void forget(const Key& key);
    void answer(const Key& key, const QString& json);
    void refuse(const Key& key, const QString& message);
    void publish();
    [[nodiscard]] Worker& workerFor(const QString& engine);

    static inline VisionEngines* s_instance = nullptr;

    std::shared_ptr<Shared> m_shared;
    VisionReplies m_replies;
    VisionFrames m_frames;
    std::map<QString, std::shared_ptr<VisionEngine>> m_engines;
    std::map<QString, std::unique_ptr<Worker>> m_workers;
    std::map<Key, Pending> m_pending;
    std::set<QString> m_permitted;
    bool m_offered = false;
};

}
