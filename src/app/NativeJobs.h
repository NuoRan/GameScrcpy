#pragma once

#include "LogicServices.h"

#include <gs/script/Services.h>

#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gs::app {

class NativeExtensions;

struct JobReplies {
    std::function<void(std::uint64_t logic, quint64 request, const QString& json)> answer;
    std::function<void(std::uint64_t logic, quint64 request, const QString& message)> refuse;
    std::function<void(std::uint64_t logic, const QString& kind, const QString& id, const QString& json)> tell;
};

struct HostJobEvents {
    std::function<void(double fraction, const QString& text)> progress;
    std::function<void(bool worked, const QString& text)> done;
    std::function<void(const QString& json)> output;
};

class NativeJobs final : public QObject
{
    Q_OBJECT

public:
    explicit NativeJobs(const NativeExtensions& extensions, QObject* parent = nullptr);
    ~NativeJobs() override;
    NativeJobs(const NativeJobs&) = delete;
    NativeJobs& operator=(const NativeJobs&) = delete;

    void offerServices();
    void setReplies(JobReplies replies);

    [[nodiscard]] script::ServiceAnswer start(const LogicCaller& caller, std::string_view json);
    [[nodiscard]] script::ServiceAnswer startForHost(std::string_view json, HostJobEvents events);
    void cancelForHost(std::uint64_t id);
    [[nodiscard]] script::ServiceAnswer cancel(const LogicCaller& caller, std::string_view json);
    void result(const LogicCaller& caller, quint64 request, const QString& json);
    void dropResult(const LogicCaller& caller, quint64 request);
    void endLogic(std::uint64_t logic);
    [[nodiscard]] std::size_t running() const;

    struct Shared;

private:
    [[nodiscard]] script::ServiceAnswer begin(std::uint64_t logic, std::string_view json,
                                              std::optional<HostJobEvents> host);
    void progressed(std::uint64_t id, double fraction, const std::string& text);
    void logged(std::uint64_t id, int level, const std::string& text);
    void outputted(std::uint64_t id, const std::string& json);
    void ended(std::uint64_t id, bool worked, const std::string& text);

    const NativeExtensions& m_extensions;
    std::shared_ptr<Shared> m_shared;
    JobReplies m_replies;
    bool m_offered = false;
};

}
