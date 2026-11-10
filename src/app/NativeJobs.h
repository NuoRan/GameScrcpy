#pragma once

#include "LogicServices.h"

#include <gs/script/Services.h>

#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace gs::app {

class NativeExtensions;

struct JobReplies {
    std::function<void(std::uint64_t logic, quint64 request, const QString& json)> answer;
    std::function<void(std::uint64_t logic, quint64 request, const QString& message)> refuse;
    std::function<void(std::uint64_t logic, const QString& kind, const QString& id, const QString& json)> tell;
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
    [[nodiscard]] script::ServiceAnswer cancel(const LogicCaller& caller, std::string_view json);
    void result(const LogicCaller& caller, quint64 request, const QString& json);
    void dropResult(const LogicCaller& caller, quint64 request);
    void endLogic(std::uint64_t logic);
    [[nodiscard]] std::size_t running() const;

    struct Shared;

private:
    void progressed(std::uint64_t id, const QString& json);
    void ended(std::uint64_t id, bool worked, const std::string& text);

    const NativeExtensions& m_extensions;
    std::shared_ptr<Shared> m_shared;
    JobReplies m_replies;
    bool m_offered = false;
};

}
