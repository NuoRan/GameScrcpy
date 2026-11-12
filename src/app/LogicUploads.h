#pragma once

#include <QByteArray>
#include <QJsonObject>

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

namespace gs::app {

[[nodiscard]] std::optional<QByteArray> pieceOf(const QJsonObject& body);

class LogicUploads final
{
public:
    enum class Result : std::uint8_t { Done, Invalid, TooLarge, TooMany };

    static constexpr std::size_t kMostStages = 64;

    struct Taken {
        Result result = Result::Done;
        QByteArray bytes;
    };

    explicit LogicUploads(qsizetype mostPerLogic);
    LogicUploads(const LogicUploads&) = delete;
    LogicUploads& operator=(const LogicUploads&) = delete;

    [[nodiscard]] Result add(std::uint64_t logic, const QJsonObject& part, qsizetype most);
    [[nodiscard]] Taken take(std::uint64_t logic, const QJsonObject& last, qsizetype most);
    void drop(std::uint64_t logic);

private:
    struct Staged {
        QByteArray bytes;
        bool failed = false;
    };

    std::mutex m_mutex;
    std::map<std::pair<std::uint64_t, std::uint64_t>, Staged> m_staged;
    qsizetype m_most;
};

}
