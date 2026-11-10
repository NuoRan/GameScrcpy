#include "McpTrace.h"

#include "QtConversions.h"

#include <gs/media/Thumbnailer.h>
#include <gs/media/VideoDecoder.h>
#include <gs/record/MediaFile.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <utility>

namespace gs::app {

namespace {

double millisecondsOf(Microseconds at)
{
    return static_cast<double>(at.count()) / 1000.0;
}

QString kindOf(const script::ReplayLine& line, const QString& text)
{
    if (line.output) {
        return text.startsWith(QStringLiteral("vision ")) ? QStringLiteral("vision") : QStringLiteral("action");
    }
    if (text.startsWith(QStringLiteral("vision answer "))) {
        return QStringLiteral("found");
    }
    if (text.startsWith(QStringLiteral("vision refused "))) {
        return QStringLiteral("refused");
    }
    if (text.startsWith(QStringLiteral("log "))) {
        return QStringLiteral("log");
    }
    if (text.startsWith(QStringLiteral("error "))) {
        return QStringLiteral("error");
    }
    return QStringLiteral("note");
}

class LatestFrame final : public media::FrameSink
{
public:
    void onFrame(media::VideoFrame frame) override { latest = std::move(frame); }

    media::VideoFrame latest;
};

}

void McpTrace::addHits(const std::string& json, std::int64_t epochMs)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        return;
    }
    for (const QJsonValue& value : document.array()) {
        if (m_hits.size() >= kMostSteps) {
            ++m_dropped;
            continue;
        }
        const QJsonObject hit = value.toObject();
        m_hits.push_back(Hit{.atMs = hit.value(QStringLiteral("at")).toDouble() - static_cast<double>(epochMs),
                             .file = hit.value(QStringLiteral("file")).toString(),
                             .position = hit.value(QStringLiteral("position")).toString(),
                             .values = hit.value(QStringLiteral("values")).toObject()});
    }
}

QJsonObject McpTrace::steps(const std::vector<script::ReplayLine>& lines) const
{
    struct Step {
        double at = 0;
        QJsonObject item;
    };
    std::vector<Step> all;
    all.reserve(m_hits.size() + lines.size());
    for (const Hit& hit : m_hits) {
        QJsonObject item{{QStringLiteral("at"), hit.atMs},
                         {QStringLiteral("kind"), QStringLiteral("code")},
                         {QStringLiteral("file"), hit.file},
                         {QStringLiteral("position"), hit.position}};
        if (!hit.values.isEmpty()) {
            item.insert(QStringLiteral("values"), hit.values);
        }
        all.push_back({.at = hit.atMs, .item = std::move(item)});
    }
    for (const script::ReplayLine& line : lines) {
        const QString text = toQString(line.text);
        const QString kind = kindOf(line, text);
        QJsonObject item{{QStringLiteral("at"), millisecondsOf(line.at)}, {QStringLiteral("kind"), kind}};
        if (kind == u"found") {
            const QByteArray answer = text.mid(14).toUtf8();
            const QJsonDocument parsed = QJsonDocument::fromJson("[" + answer + "]");
            item.insert(QStringLiteral("result"), parsed.isArray() && !parsed.array().isEmpty() ? parsed.array().at(0)
                                                                                                : QJsonValue(text.mid(14)));
        } else {
            item.insert(QStringLiteral("text"), text);
        }
        all.push_back({.at = millisecondsOf(line.at), .item = std::move(item)});
    }
    std::ranges::stable_sort(all, {}, &Step::at);
    QJsonArray list;
    std::size_t leftOut = m_dropped;
    for (const Step& step : all) {
        if (static_cast<std::size_t>(list.size()) >= kMostSteps) {
            ++leftOut;
            continue;
        }
        list.append(step.item);
    }
    QJsonObject result{{QStringLiteral("steps"), list}};
    if (leftOut > 0) {
        result.insert(QStringLiteral("leftOut"), static_cast<qint64>(leftOut));
    }
    return result;
}

std::vector<double> McpTrace::frameTimes(const std::vector<script::ReplayLine>& lines, double endedMs)
{
    std::vector<double> finds;
    for (const script::ReplayLine& line : lines) {
        if (!line.output || !line.text.starts_with("vision ")) {
            continue;
        }
        const double at = millisecondsOf(line.at);
        if (finds.empty() || at - finds.back() >= kFrameGapMs) {
            finds.push_back(at);
        }
    }
    std::vector<double> chosen;
    if (finds.size() > kMostFrames) {
        for (std::size_t index = 0; index < kMostFrames; ++index) {
            chosen.push_back(finds[index * finds.size() / kMostFrames]);
        }
    } else {
        chosen = finds;
    }
    const std::size_t room = kMostFrames - chosen.size();
    for (std::size_t index = 0; index < room && endedMs > 0; ++index) {
        const double at = endedMs * (static_cast<double>(index) + 0.5) / static_cast<double>(room);
        const bool near = std::ranges::any_of(chosen, [at](double other) { return std::abs(other - at) < kFrameGapMs; });
        if (!near) {
            chosen.push_back(at);
        }
    }
    std::ranges::sort(chosen);
    return chosen;
}

std::vector<McpTraceFrame> mcpTraceFrames(const std::filesystem::path& video, std::int64_t startPtsUs,
                                          const std::vector<double>& timesMs, int longSide)
{
    std::vector<McpTraceFrame> frames;
    record::MediaReader reader(video, protocol::MediaChannel::Video);
    if (!reader.opened() || timesMs.empty()) {
        return frames;
    }
    media::VideoDecoder decoder;
    LatestFrame sink;
    media::Thumbnailer thumbnailer;
    for (const double atMs : timesMs) {
        const std::int64_t target = startPtsUs + static_cast<std::int64_t>(std::llround(atMs * 1000.0));
        for (;;) {
            const std::optional<protocol::MediaHeader> header = reader.peek();
            if (!header || (header->kind == protocol::MediaKind::Frame && sink.latest && header->ptsUs > target)) {
                break;
            }
            const std::optional<protocol::MediaPacket> packet = reader.next();
            if (!packet) {
                break;
            }
            static_cast<void>(decoder.push(*packet, sink));
        }
        if (!sink.latest) {
            continue;
        }
        const std::optional<media::Thumbnail> thumbnail = thumbnailer.make(sink.latest, longSide);
        if (!thumbnail || thumbnail->width <= 0 || thumbnail->height <= 0 ||
            thumbnail->pixels.size() < static_cast<std::size_t>(thumbnail->width) * static_cast<std::size_t>(thumbnail->height)) {
            continue;
        }
        QImage image(thumbnail->width, thumbnail->height, QImage::Format_RGB32);
        for (int y = 0; y < thumbnail->height; ++y) {
            std::memcpy(image.scanLine(y), thumbnail->pixels.data() + static_cast<std::ptrdiff_t>(y) * thumbnail->width,
                        static_cast<std::size_t>(thumbnail->width) * sizeof(std::uint32_t));
        }
        frames.push_back({.atMs = atMs, .image = std::move(image)});
    }
    return frames;
}

}
