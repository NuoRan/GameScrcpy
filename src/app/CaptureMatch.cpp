#include "CaptureMatch.h"

#include <gs/base/Expected.h>

#include <QByteArrayView>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QSize>
#include <QStringList>
#include <QThreadPool>
#include <QTransform>
#include <QtLogging>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numeric>
#include <string>
#include <utility>

namespace gs::app {

namespace {

constexpr int kSampleWidth = 64;
constexpr int kLeastSampleHeight = 16;
constexpr int kMostSampleHeight = 256;
constexpr double kFlatDeviation = 1.0;
constexpr qreal kQuarterTurn = 90.0;
constexpr double kSuggestedScore = 0.8;
constexpr double kSuggestedLead = 0.1;
constexpr std::size_t kMostScreenshotBytes = std::size_t{64} << 20U;
constexpr std::string_view kScreencap = "screencap -p";

struct Sample {
    std::vector<double> values;
    double energy = 0;
};

struct Reference {
    QSize shape;
    QSize size;
    Sample sample;
};

struct Suggestion {
    QString deviceId;
    double score = 0;
};

QSize sampleSize(QSize capture)
{
    const double height = static_cast<double>(kSampleWidth) * capture.height() / capture.width();
    return {kSampleWidth, std::clamp(static_cast<int>(std::lround(height)), kLeastSampleHeight, kMostSampleHeight)};
}

bool crosswise(QSize capture, QSize phone)
{
    const bool wideCapture = capture.width() > capture.height();
    const bool tallCapture = capture.height() > capture.width();
    const bool widePhone = phone.width() > phone.height();
    const bool tallPhone = phone.height() > phone.width();
    return (wideCapture && tallPhone) || (tallCapture && widePhone);
}

QImage shrunkGray(const QImage& picture, QSize size)
{
    return picture.convertToFormat(QImage::Format_RGB32)
        .scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_Grayscale8);
}

Sample sampleOf(const QImage& picture)
{
    const QImage gray = picture.convertToFormat(QImage::Format_Grayscale8);
    Sample sample;
    if (gray.isNull()) {
        return sample;
    }
    sample.values.reserve(static_cast<std::size_t>(gray.width()) * static_cast<std::size_t>(gray.height()));
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            sample.values.push_back(line[x]);
        }
    }
    const double mean =
        std::accumulate(sample.values.begin(), sample.values.end(), 0.0) / static_cast<double>(sample.values.size());
    for (double& value : sample.values) {
        value -= mean;
        sample.energy += value * value;
    }
    return sample;
}

bool flat(const Sample& sample)
{
    return sample.values.empty() ||
           sample.energy < kFlatDeviation * kFlatDeviation * static_cast<double>(sample.values.size());
}

double correlation(const Sample& left, const Sample& right)
{
    if (left.values.size() != right.values.size() || flat(left) || flat(right)) {
        return 0.0;
    }
    const double cross = std::inner_product(left.values.begin(), left.values.end(), right.values.begin(), 0.0);
    return std::clamp(cross / std::sqrt(left.energy * right.energy), 0.0, 1.0);
}

Reference referenceOf(const QImage& capture)
{
    const QSize size = sampleSize(capture.size());
    return {.shape = capture.size(), .size = size, .sample = sampleOf(shrunkGray(capture, size))};
}

double similarity(const Reference& reference, const QImage& phone)
{
    if (phone.isNull() || flat(reference.sample)) {
        return 0.0;
    }
    double best = correlation(reference.sample, sampleOf(shrunkGray(phone, reference.size)));
    if (crosswise(reference.shape, phone.size())) {
        const QImage turned = shrunkGray(phone, reference.size.transposed());
        for (const qreal angle : {kQuarterTurn, -kQuarterTurn}) {
            const QImage upright = turned.transformed(QTransform().rotate(angle));
            best = std::max(best, correlation(reference.sample, sampleOf(upright)));
        }
    }
    return best;
}

std::vector<PhoneMatch> scoresOf(const QImage& capture, const std::vector<PhoneCandidate>& phones)
{
    std::vector<PhoneMatch> scores;
    if (capture.isNull()) {
        return scores;
    }
    const Reference reference = referenceOf(capture);
    for (const PhoneCandidate& phone : phones) {
        if (!phone.picture.isNull()) {
            scores.push_back(PhoneMatch{.deviceId = phone.deviceId, .score = similarity(reference, phone.picture)});
        }
    }
    return scores;
}

std::optional<PhoneMatch> confidentMatch(std::vector<PhoneMatch> scores, double threshold, double margin)
{
    std::ranges::stable_sort(scores, std::ranges::greater{}, &PhoneMatch::score);
    if (scores.empty() || scores.front().score < threshold) {
        return std::nullopt;
    }
    if (scores.size() > 1 && scores.front().score - scores[1].score < margin) {
        return std::nullopt;
    }
    return scores.front();
}

Expected<QImage, QString> screenshotOf(const adb::AdbClient& client, const QString& serial)
{
    if (serial.isEmpty()) {
        return makeUnexpected(QStringLiteral("no adb connection"));
    }
    const adb::AdbResult<adb::ShellResult> shot = client.shell(serial.toStdString(), kScreencap);
    if (!shot) {
        return makeUnexpected(QString::fromStdString(shot.error().message));
    }
    if (shot->exitCode != 0) {
        return makeUnexpected(QStringLiteral("screencap exited with %1").arg(shot->exitCode));
    }
    if (shot->out.size() > kMostScreenshotBytes) {
        return makeUnexpected(QStringLiteral("the screenshot is larger than %1 MiB").arg(kMostScreenshotBytes >> 20U));
    }
    QImage picture = decodeScreencap(shot->out);
    if (picture.isNull()) {
        return makeUnexpected(QStringLiteral("the screenshot is not a PNG picture"));
    }
    return picture;
}

Suggestion suggestionFor(const QString& captureSerial, const QImage& capture, const std::vector<PhoneSource>& phones,
                         std::uint16_t port)
{
    const adb::AdbClient client(port);
    std::vector<PhoneCandidate> candidates;
    QStringList skipped;
    for (const PhoneSource& phone : phones) {
        if (!phone.current.isNull()) {
            candidates.push_back(PhoneCandidate{.deviceId = phone.deviceId, .picture = phone.current});
            continue;
        }
        Expected<QImage, QString> shot = screenshotOf(client, phone.serial);
        if (!shot) {
            skipped.append(QStringLiteral("%1 skipped (%2)").arg(phone.deviceId, shot.error()));
            continue;
        }
        candidates.push_back(PhoneCandidate{.deviceId = phone.deviceId, .picture = std::move(*shot)});
    }
    const std::vector<PhoneMatch> scores = scoresOf(capture, candidates);
    QStringList notes;
    for (const PhoneMatch& scored : scores) {
        notes.append(QStringLiteral("%1 %2").arg(scored.deviceId, QString::number(scored.score, 'f', 2)));
    }
    notes += skipped;
    const std::optional<PhoneMatch> chosen = confidentMatch(scores, kSuggestedScore, kSuggestedLead);
    qInfo("compared the picture of %s with %zu phone(s): %s; suggested %s", qUtf8Printable(captureSerial),
          phones.size(), qUtf8Printable(notes.join(QStringLiteral(", "))),
          chosen ? qUtf8Printable(chosen->deviceId) : "none");
    if (chosen) {
        return {.deviceId = chosen->deviceId, .score = chosen->score};
    }
    const auto best = std::ranges::max_element(scores, {}, &PhoneMatch::score);
    return {.deviceId = {}, .score = best != scores.end() ? best->score : 0.0};
}

}

double pictureSimilarity(const QImage& capture, const QImage& phone)
{
    if (capture.isNull() || phone.isNull()) {
        return 0.0;
    }
    return similarity(referenceOf(capture), phone);
}

std::optional<PhoneMatch> bestPhoneMatch(const QImage& capture, const std::vector<PhoneCandidate>& phones,
                                         double threshold, double margin)
{
    return confidentMatch(scoresOf(capture, phones), threshold, margin);
}

QImage decodeScreencap(std::string_view png)
{
    if (png.empty()) {
        return {};
    }
    return QImage::fromData(QByteArrayView(png.data(), static_cast<qsizetype>(png.size())), "PNG");
}

CaptureMatcher::CaptureMatcher(std::uint16_t adbPort, QObject* parent)
    : QObject(parent)
    , m_port(adbPort)
{
}

int CaptureMatcher::suggest(const QString& captureSerial, const QImage& capture, std::vector<PhoneSource> phones)
{
    if (capture.isNull() || phones.empty()) {
        return 0;
    }
    const int ticket = ++m_tickets;
    const QPointer<CaptureMatcher> self(this);
    QThreadPool::globalInstance()->start(
        [self, ticket, captureSerial, capture, phones = std::move(phones), port = m_port] {
            const Suggestion found = suggestionFor(captureSerial, capture, phones, port);
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self, ticket, captureSerial, found] {
                    if (self != nullptr) {
                        emit self->suggested(ticket, captureSerial, found.deviceId, found.score);
                    }
                },
                Qt::QueuedConnection);
        });
    return ticket;
}

}
