#include "LogicUploads.h"

#include <QJsonValue>
#include <QString>

#include <cmath>

namespace gs::app {

namespace {

constexpr double kMostStage = 9007199254740991.0;

std::optional<std::uint64_t> stageOf(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!(number >= 1.0 && number <= kMostStage) || std::floor(number) != number) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(number);
}

}

std::optional<QByteArray> pieceOf(const QJsonObject& body)
{
    const QJsonValue text = body.value(u"text");
    const QJsonValue data = body.value(u"data");
    if (!text.isUndefined()) {
        if (!text.isString() || !data.isUndefined()) {
            return std::nullopt;
        }
        return text.toString().toUtf8();
    }
    if (data.isUndefined()) {
        return QByteArray();
    }
    if (!data.isString()) {
        return std::nullopt;
    }
    QByteArray::FromBase64Result decoded =
        QByteArray::fromBase64Encoding(data.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
        return std::nullopt;
    }
    return std::move(decoded.decoded);
}

LogicUploads::LogicUploads(qsizetype mostPerLogic)
    : m_most(mostPerLogic)
{
}

LogicUploads::Result LogicUploads::add(std::uint64_t logic, const QJsonObject& part, qsizetype most)
{
    const std::optional<std::uint64_t> stage = stageOf(part.value(u"stage"));
    const std::optional<QByteArray> piece = pieceOf(part);
    if (!stage || !piece) {
        return Result::Invalid;
    }
    const std::scoped_lock lock(m_mutex);
    qsizetype held = 0;
    std::size_t stages = 0;
    for (auto it = m_staged.lower_bound({logic, 0}); it != m_staged.end() && it->first.first == logic; ++it) {
        held += it->second.bytes.size();
        ++stages;
    }
    auto found = m_staged.find({logic, *stage});
    if (found == m_staged.end()) {
        if (stages >= kMostStages) {
            return Result::TooMany;
        }
        found = m_staged.try_emplace({logic, *stage}).first;
    }
    Staged& staged = found->second;
    if (staged.failed || staged.bytes.size() + piece->size() > most || held + piece->size() > m_most) {
        staged.failed = true;
        staged.bytes.clear();
        return Result::TooLarge;
    }
    staged.bytes.append(*piece);
    return Result::Done;
}

LogicUploads::Taken LogicUploads::take(std::uint64_t logic, const QJsonObject& last, qsizetype most)
{
    const std::optional<QByteArray> piece = pieceOf(last);
    QByteArray bytes;
    bool tooLarge = false;
    bool invalid = !piece.has_value();
    if (const QJsonValue given = last.value(u"stage"); !given.isUndefined()) {
        const std::optional<std::uint64_t> stage = stageOf(given);
        const std::scoped_lock lock(m_mutex);
        auto node = stage ? m_staged.extract({logic, *stage}) : decltype(m_staged)::node_type();
        if (node.empty()) {
            invalid = true;
        } else {
            tooLarge = node.mapped().failed;
            bytes = std::move(node.mapped().bytes);
        }
    }
    if (invalid) {
        return {Result::Invalid, {}};
    }
    if (tooLarge || bytes.size() + piece->size() > most) {
        return {Result::TooLarge, {}};
    }
    bytes.append(*piece);
    return {Result::Done, std::move(bytes)};
}

void LogicUploads::drop(std::uint64_t logic)
{
    const std::scoped_lock lock(m_mutex);
    m_staged.erase(m_staged.lower_bound({logic, 0}), m_staged.lower_bound({logic + 1, 0}));
}

}
