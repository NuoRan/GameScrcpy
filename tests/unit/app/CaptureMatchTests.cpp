#include "CaptureMatch.h"

#include "unit/adb/FakeAdbServer.h"

#include <doctest/doctest.h>

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QIODevice>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QThread>
#include <QThreadPool>
#include <QTransform>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using gs::adb::test::closedPort;
using gs::adb::test::FakeAdbServer;
using gs::adb::test::FakeConnection;
using gs::app::bestPhoneMatch;
using gs::app::CaptureMatcher;
using gs::app::decodeScreencap;
using gs::app::PhoneCandidate;
using gs::app::PhoneMatch;
using gs::app::PhoneSource;
using gs::app::pictureSimilarity;

namespace {

class Application
{
public:
    Application()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_application = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    std::string m_name = "gs_app_tests";
    std::array<char*, 2> m_argv{m_name.data(), nullptr};
    std::unique_ptr<QCoreApplication> m_application;
};

template <typename Predicate>
bool waitUntil(Predicate done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::yield();
    }
    return true;
}

using Scene = int (*)(double across, double down);

int homeScreen(double across, double down)
{
    const bool banded = across >= 0.1 && across < 0.9;
    if (banded && down >= 0.1 && down < 0.3) {
        return 230;
    }
    if (banded && down >= 0.7 && down < 0.9) {
        return 20;
    }
    return static_cast<int>(std::lround(40 + 160 * down));
}

int gameScreen(double across, double down)
{
    if (across >= 0.3 && across < 0.7 && down >= 0.4 && down < 0.6) {
        return 20;
    }
    const bool topLeft = across >= 0.05 && across < 0.25 && down >= 0.05 && down < 0.15;
    const bool bottomRight = across >= 0.75 && across < 0.95 && down >= 0.85 && down < 0.95;
    if (topLeft || bottomRight) {
        return 235;
    }
    return static_cast<int>(std::lround(50 + 300 * std::min(across, 1 - across)));
}

QImage render(int width, int height, Scene scene)
{
    QImage picture(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(picture.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int level = scene((x + 0.5) / width, (y + 0.5) / height);
            line[x] = qRgb(level, level, level);
        }
    }
    return picture;
}

QImage withBlackEdges(const QImage& picture, int sides, int ends)
{
    const QImage inner = picture.convertToFormat(QImage::Format_RGB32);
    QImage framed(inner.width() + 2 * sides, inner.height() + 2 * ends, QImage::Format_RGB32);
    framed.fill(Qt::black);
    for (int y = 0; y < inner.height(); ++y) {
        const auto* from = reinterpret_cast<const QRgb*>(inner.constScanLine(y));
        auto* to = reinterpret_cast<QRgb*>(framed.scanLine(y + ends));
        std::copy_n(from, inner.width(), to + sides);
    }
    return framed;
}

bool samePixels(const QImage& left, const QImage& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (int y = 0; y < left.height(); ++y) {
        for (int x = 0; x < left.width(); ++x) {
            if (left.pixel(x, y) != right.pixel(x, y)) {
                return false;
            }
        }
    }
    return true;
}

std::string pngOf(const QImage& picture)
{
    QByteArray png;
    QBuffer buffer(&png);
    REQUIRE(buffer.open(QIODevice::WriteOnly));
    REQUIRE(picture.save(&buffer, "PNG"));
    return png.toStdString();
}

std::string shellPacket(char id, std::string_view data)
{
    std::string packet(1, id);
    const auto size = static_cast<std::uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) {
        packet.push_back(static_cast<char>(size >> (8 * i)));
    }
    return packet + std::string(data);
}

std::string exitPacket(int status)
{
    return shellPacket(3, std::string(1, static_cast<char>(status)));
}

std::string screenshotReply(std::string_view png)
{
    constexpr std::size_t kChunk = 16 * 1024;
    std::string reply = "OKAY";
    for (std::size_t at = 0; at < png.size(); at += kChunk) {
        reply += shellPacket(1, png.substr(at, kChunk));
    }
    return reply + exitPacket(0);
}

PhoneCandidate candidate(const char* deviceId, const QImage& picture)
{
    return {.deviceId = QString::fromUtf8(deviceId), .picture = picture};
}

PhoneSource source(const char* deviceId, const char* serial, const QImage& current = {})
{
    return {.deviceId = QString::fromUtf8(deviceId), .serial = QString::fromUtf8(serial), .current = current};
}

struct Answer {
    int ticket = 0;
    QString capture;
    QString deviceId;
    double score = 0;
    bool onGuiThread = false;
};

void recordAnswers(CaptureMatcher& matcher, std::vector<Answer>& answers)
{
    QObject::connect(&matcher, &CaptureMatcher::suggested,
                     [&answers](int ticket, const QString& capture, const QString& deviceId, double score) {
                         answers.push_back(Answer{.ticket = ticket,
                                                  .capture = capture,
                                                  .deviceId = deviceId,
                                                  .score = score,
                                                  .onGuiThread = QThread::currentThread() ==
                                                                 QCoreApplication::instance()->thread()});
                     });
}

}

TEST_CASE("ADR-0108: the same picture scores near 1, also when it is scaled to another size")
{
    const QImage phone = render(270, 600, homeScreen);
    const double same = pictureSimilarity(phone, phone);
    CHECK(same > 0.95);
    CHECK(same <= 1.0);
    CHECK(pictureSimilarity(phone.scaled(540, 1200, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), phone) > 0.9);
    CHECK(pictureSimilarity(render(1080, 2400, homeScreen), phone) > 0.9);
    CHECK(pictureSimilarity(render(135, 300, homeScreen), phone) > 0.9);
}

TEST_CASE("ADR-0108: thin black edges the crop left around the phone screen still score high")
{
    const QImage phone = render(270, 600, homeScreen);
    const QImage capture = withBlackEdges(render(540, 1200, homeScreen), 8, 4);
    REQUIRE(capture.size() == QSize(556, 1208));
    CHECK(pictureSimilarity(capture, phone) > 0.8);
}

TEST_CASE("ADR-0108: a different picture scores low; an inverted, flat or missing picture scores 0")
{
    const QImage phone = render(270, 600, homeScreen);
    const QImage game = render(270, 600, gameScreen);
    CHECK(pictureSimilarity(game, phone) < 0.3);
    CHECK(pictureSimilarity(phone, game) < 0.3);
    CHECK(pictureSimilarity(game.transformed(QTransform().rotate(90)), phone) < 0.5);

    QImage inverted = phone;
    inverted.invertPixels();
    CHECK(pictureSimilarity(inverted, phone) == 0.0);

    QImage flat(270, 600, QImage::Format_RGB32);
    flat.fill(qRgb(128, 128, 128));
    CHECK(pictureSimilarity(flat, phone) == 0.0);
    CHECK(pictureSimilarity(phone, flat) == 0.0);
    CHECK(pictureSimilarity(QImage(), phone) == 0.0);
    CHECK(pictureSimilarity(phone, QImage()) == 0.0);
}

TEST_CASE("ADR-0108: a landscape capture matches a portrait phone picture turned either way, and the other way round")
{
    const QImage phone = render(270, 600, homeScreen);
    const QImage left = phone.transformed(QTransform().rotate(-90));
    const QImage right = phone.transformed(QTransform().rotate(90));
    REQUIRE(left.size() == QSize(600, 270));
    REQUIRE(right.size() == QSize(600, 270));
    CHECK(pictureSimilarity(left, phone) > 0.9);
    CHECK(pictureSimilarity(right, phone) > 0.9);
    CHECK(pictureSimilarity(left.scaled(1200, 540, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), phone) > 0.9);
    CHECK(pictureSimilarity(phone, left) > 0.9);
    CHECK(pictureSimilarity(phone, right) > 0.9);
}

TEST_CASE("ADR-0108: the best phone is suggested only when it is similar enough and clearly ahead of the second best")
{
    const QImage capture = render(540, 1200, homeScreen);
    const QImage home = render(270, 600, homeScreen);
    const QImage game = render(270, 600, gameScreen);

    const std::optional<PhoneMatch> found = bestPhoneMatch(capture, {candidate("game", game), candidate("home", home)});
    REQUIRE(found.has_value());
    CHECK(found->deviceId == QStringLiteral("home"));
    CHECK(found->score > 0.9);
    const std::optional<PhoneMatch> only = bestPhoneMatch(capture, {candidate("home", home)});
    REQUIRE(only.has_value());
    CHECK(only->deviceId == QStringLiteral("home"));

    CHECK_FALSE(bestPhoneMatch(capture, {}).has_value());
    CHECK_FALSE(bestPhoneMatch(capture, {candidate("game", game)}).has_value());
    CHECK_FALSE(bestPhoneMatch(capture, {candidate("home", home)}, 1.01).has_value());
    const std::optional<PhoneMatch> lowered = bestPhoneMatch(capture, {candidate("game", game)}, 0.0);
    REQUIRE(lowered.has_value());
    CHECK(lowered->deviceId == QStringLiteral("game"));

    const std::vector<PhoneCandidate> twins{candidate("home", home), candidate("copy", render(405, 900, homeScreen))};
    CHECK_FALSE(bestPhoneMatch(capture, twins).has_value());
    CHECK(bestPhoneMatch(capture, twins, 0.8, 0.0).has_value());
}

TEST_CASE("ADR-0108: phones without a picture are left out of the choice")
{
    const QImage capture = render(270, 600, homeScreen);
    const QImage home = render(270, 600, homeScreen);
    const std::optional<PhoneMatch> found =
        bestPhoneMatch(capture, {candidate("blank", QImage()), candidate("home", home)});
    REQUIRE(found.has_value());
    CHECK(found->deviceId == QStringLiteral("home"));
    CHECK_FALSE(bestPhoneMatch(capture, {candidate("blank", QImage())}, 0.0, 0.0).has_value());
    CHECK_FALSE(bestPhoneMatch(QImage(), {candidate("home", home)}, 0.0, 0.0).has_value());
}

TEST_CASE("ADR-0108: a PNG from screencap -p is read back as the same picture; garbage or a cut-off file gives none")
{
    const QImage picture = render(270, 600, homeScreen);
    const std::string png = pngOf(picture);
    const QImage decoded = decodeScreencap(png);
    REQUIRE_FALSE(decoded.isNull());
    CHECK(samePixels(decoded, picture));
    const QImage withAlpha = decodeScreencap(pngOf(picture.convertToFormat(QImage::Format_ARGB32)));
    REQUIRE_FALSE(withAlpha.isNull());
    CHECK(samePixels(withAlpha, picture));

    CHECK(decodeScreencap("not a picture").isNull());
    CHECK(decodeScreencap({}).isNull());
    CHECK(decodeScreencap(std::string_view(png).substr(0, png.size() / 2)).isNull());
}

TEST_CASE("ADR-0108: the matcher gives no ticket when there is no capture picture or no phone")
{
    CaptureMatcher matcher(closedPort());
    const QImage home = render(270, 600, homeScreen);
    CHECK(matcher.suggest(QStringLiteral("capture-1"), home, {}) == 0);
    CHECK(matcher.suggest(QStringLiteral("capture-1"), QImage(), {source("phone-a", "SA", home)}) == 0);
}

TEST_CASE("ADR-0108: the matcher compares on a worker and answers each ticket on the GUI thread")
{
    const Application application;
    CaptureMatcher matcher(closedPort());
    std::vector<Answer> answers;
    recordAnswers(matcher, answers);
    const QImage capture = render(540, 1200, homeScreen);
    const QImage home = render(270, 600, homeScreen);
    const QImage game = render(270, 600, gameScreen);

    CHECK(matcher.suggest(QStringLiteral("capture-1"), capture, {}) == 0);
    const int first = matcher.suggest(QStringLiteral("capture-1"), capture,
                                      {source("phone-a", "SA", game), source("phone-b", "SB", home)});
    const int second = matcher.suggest(QStringLiteral("capture-2"), capture, {source("phone-a", "SA", game)});
    CHECK(first == 1);
    CHECK(second == 2);
    CHECK(answers.empty());

    REQUIRE(waitUntil([&answers] { return answers.size() == 2; }));
    std::ranges::sort(answers, {}, &Answer::ticket);
    CHECK(answers[0].ticket == 1);
    CHECK(answers[0].capture == QStringLiteral("capture-1"));
    CHECK(answers[0].deviceId == QStringLiteral("phone-b"));
    CHECK(answers[0].score > 0.9);
    CHECK(answers[0].onGuiThread);
    CHECK(answers[1].ticket == 2);
    CHECK(answers[1].capture == QStringLiteral("capture-2"));
    CHECK(answers[1].deviceId.isEmpty());
    CHECK(answers[1].score < 0.3);
    CHECK(answers[1].onGuiThread);
}

TEST_CASE("ADR-0108: phones without a current picture are photographed with screencap -p over adb; failed "
          "screenshots are left out")
{
    const Application application;
    const std::string home = pngOf(render(270, 600, homeScreen).convertToFormat(QImage::Format_ARGB32));
    const std::string game = pngOf(render(270, 600, gameScreen));
    std::mutex mutex;
    std::vector<std::string> transports;
    std::vector<std::string> services;
    const FakeAdbServer server([&](FakeConnection& c) {
        constexpr std::string_view kTransport = "host:transport:";
        const std::string transport = c.readRequest();
        {
            const std::lock_guard lock(mutex);
            transports.push_back(transport);
        }
        const std::string serial =
            transport.starts_with(kTransport) ? transport.substr(kTransport.size()) : std::string();
        if (serial != "SA" && serial != "SB" && serial != "SX" && serial != "SF") {
            c.write("FAIL" + FakeConnection::prefixed("device '" + serial + "' not found"));
            return;
        }
        c.write("OKAY");
        const std::string service = c.readRequest();
        {
            const std::lock_guard lock(mutex);
            services.push_back(service);
        }
        if (serial == "SA") {
            c.write(screenshotReply(game));
        } else if (serial == "SB") {
            c.write(screenshotReply(home));
        } else if (serial == "SX") {
            c.write("OKAY" + shellPacket(1, "not a picture") + exitPacket(0));
        } else {
            c.write("OKAY" + shellPacket(2, "screencap failed\n") + exitPacket(1));
        }
    });
    CaptureMatcher matcher(server.port());
    std::vector<Answer> answers;
    recordAnswers(matcher, answers);

    const int ticket =
        matcher.suggest(QStringLiteral("capture-7"), render(540, 1200, homeScreen),
                        {source("phone-a", "SA"), source("phone-b", "SB"),
                         source("phone-c", "SC", render(270, 600, gameScreen)), source("phone-x", "SX"),
                         source("phone-f", "SF"), source("phone-g", "gone"), source("phone-n", "")});
    CHECK(ticket == 1);
    REQUIRE(waitUntil([&answers] { return !answers.empty(); }));
    REQUIRE(answers.size() == 1);
    CHECK(answers[0].ticket == ticket);
    CHECK(answers[0].capture == QStringLiteral("capture-7"));
    CHECK(answers[0].deviceId == QStringLiteral("phone-b"));
    CHECK(answers[0].score > 0.9);

    const std::lock_guard lock(mutex);
    CHECK(transports == std::vector<std::string>{"host:transport:SA", "host:transport:SB", "host:transport:SX",
                                                 "host:transport:SF", "host:transport:gone"});
    CHECK(services == std::vector<std::string>(4, "shell,v2,raw:screencap -p"));
}

TEST_CASE("ADR-0108: a matcher deleted before its answer is ready says nothing")
{
    const Application application;
    int heard = 0;
    {
        CaptureMatcher matcher(closedPort());
        QObject::connect(&matcher, &CaptureMatcher::suggested, [&heard] { ++heard; });
        const QImage home = render(270, 600, homeScreen);
        CHECK(matcher.suggest(QStringLiteral("capture-1"), home, {source("phone-a", "SA", home)}) == 1);
    }
    CHECK(QThreadPool::globalInstance()->waitForDone(10000));
    QCoreApplication::processEvents();
    CHECK(heard == 0);
}
