#include "stability/FakePhone.h"
#include "stability/ProcessMetrics.h"
#include "stability/Trend.h"

#include "DemoFeed.h"
#include "SessionController.h"
#include "Settings.h"

#include "fakes/FakeDeviceServer.h"

#include <gs/adb/AdbClient.h>
#include <gs/base/Log.h>
#include <gs/media/VideoDecoder.h>
#include <gs/protocol/Codec.h>
#include <gs/protocol/Constants.h>
#include <gs/render/FrameSlot.h>
#include <gs/render/GraphicsDevice.h>
#include <gs/session/AdbServerHost.h>
#include <gs/session/SessionState.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QGuiApplication>
#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using gs::session::SessionPhase;
using gs::stability::ProcessSample;
using gs::stability::Trend;
using gs::stability::TrendPoint;

constexpr std::string_view kSerial = "fake-phone-1";
constexpr std::size_t kServerBytes = 96 * 1024;
constexpr std::uint8_t kServerFill = 0x47;
constexpr auto kStreamLimit = 20s;
constexpr auto kStopLimit = 15s;
constexpr int kMostFailuresInARow = 5;
constexpr auto kDisplayInterval = 16ms;
constexpr double kMiB = 1024.0 * 1024.0;
constexpr double kKiB = 1024.0;
constexpr std::size_t kRecentLogLines = 40;
constexpr std::size_t kMinimumSamples = 3;
#if defined(__SANITIZE_ADDRESS__)
constexpr bool kSanitized = true;
#else
constexpr bool kSanitized = false;
#endif
constexpr std::string_view kNotJudgedWhy =
    kSanitized ? "AddressSanitizer keeps freed memory in its quarantine, so the private bytes grow until it is full"
               : "on the graphics card the driver keeps memory for each video decoder it makes";

struct Options {
    int cycles = 1000;
    int sampleEvery = 10;
    int warmupCycles = 50;
    double minutes = 480.0;
    double sampleSeconds = 60.0;
    double warmupMinutes = 10.0;
    double handleTolerance = 8.0;
    double threadTolerance = 2.0;
    double cycleMemoryKib = 4096.0;
    double decodeMemoryMib = 4.0;
    bool cyclesOn = true;
    bool decodeOn = true;
    bool hardware = false;
    fs::path recording;
    fs::path out;
    QString command;
};

struct Recording {
    std::vector<gs::protocol::Bytes> packets;
    std::uint64_t frames = 0;
};

struct Rig {
    Recording recording;
    std::shared_ptr<const gs::session::ServerPackage> package;
    std::unique_ptr<gs::render::GraphicsDevice> graphics;
    gs::app::settings::Settings settings;
};

class RecentLog final : public gs::log::Sink
{
public:
    void write(const gs::log::Record& record) noexcept override
    {
        if (record.level < gs::log::Level::Warning) {
            return;
        }
        try {
            const std::lock_guard lock(m_mutex);
            m_lines.push_back(std::format("{}: {}", record.category, record.message));
            if (m_lines.size() > kRecentLogLines) {
                m_lines.pop_front();
            }
        } catch (...) {
            return;
        }
    }

    [[nodiscard]] std::vector<std::string> take()
    {
        const std::lock_guard lock(m_mutex);
        std::vector<std::string> lines(std::make_move_iterator(m_lines.begin()), std::make_move_iterator(m_lines.end()));
        m_lines.clear();
        return lines;
    }

private:
    std::mutex m_mutex;
    std::deque<std::string> m_lines;
};

class Csv
{
public:
    Csv(const fs::path& path, std::string_view header) : m_out(path, std::ios::binary | std::ios::trunc)
    {
        m_out << header << '\n' << std::flush;
    }

    [[nodiscard]] bool good() const { return static_cast<bool>(m_out); }

    void row(std::string_view line) { m_out << line << '\n' << std::flush; }

private:
    std::ofstream m_out;
};

struct Series {
    std::vector<TrendPoint> handles;
    std::vector<TrendPoint> threads;
    std::vector<TrendPoint> privateBytes;
    std::vector<TrendPoint> workingSet;

    void add(double x, const ProcessSample& sample)
    {
        handles.push_back({x, static_cast<double>(sample.handles)});
        threads.push_back({x, static_cast<double>(sample.threads)});
        privateBytes.push_back({x, static_cast<double>(sample.privateBytes)});
        workingSet.push_back({x, static_cast<double>(sample.workingSet)});
    }
};

struct Report {
    std::vector<std::string> lines;
    bool pass = true;

    void say(std::string line)
    {
        std::puts(line.c_str());
        std::fflush(stdout);
        lines.push_back(std::move(line));
    }

    void fail(std::string line)
    {
        pass = false;
        say(std::move(line));
    }

    void verdict(bool ok, std::string line)
    {
        if (ok) {
            say(std::move(line));
        } else {
            fail(std::move(line));
        }
    }
};

std::string utf8(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

double seconds(Clock::duration duration)
{
    return std::chrono::duration<double>(duration).count();
}

std::string sampleColumns(const ProcessSample& sample)
{
    return std::format("{},{},{},{}", sample.handles, sample.threads, sample.privateBytes, sample.workingSet);
}

std::string sampleText(const ProcessSample& sample)
{
    return std::format("handles {}, threads {}, private {:.1f} MiB, working set {:.1f} MiB", sample.handles,
                       sample.threads, static_cast<double>(sample.privateBytes) / kMiB,
                       static_cast<double>(sample.workingSet) / kMiB);
}

template <typename Done>
bool spinUntil(Done done, Clock::duration limit)
{
    const auto deadline = Clock::now() + limit;
    while (true) {
        QCoreApplication::processEvents();
        if (done()) {
            return true;
        }
        if (Clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
}

void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

std::optional<Recording> loadRecording(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    Recording recording;
    for (std::size_t offset = 0; offset < bytes.size();) {
        const auto packet =
            gs::protocol::decodeMediaPacket(gs::protocol::MediaChannel::Video, std::span(bytes).subspan(offset));
        if (!packet) {
            return std::nullopt;
        }
        const std::size_t size = gs::protocol::kMediaHeaderSize + packet->header.size;
        recording.packets.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                       bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
        if (packet->header.kind == gs::protocol::MediaKind::Frame) {
            ++recording.frames;
        }
        offset += size;
    }
    if (recording.frames == 0) {
        return std::nullopt;
    }
    return recording;
}

struct CycleResult {
    bool ok = false;
    std::string problem;
    std::uint64_t frames = 0;
};

std::string phaseText(const std::optional<gs::session::SessionState>& state)
{
    if (!state) {
        return "none";
    }
    std::string text(gs::session::phaseName(state->phase));
    if (state->failure) {
        text += std::format(" ({}: {})", gs::session::failureName(state->failure->kind), state->failure->detail);
    }
    return text;
}

CycleResult runCycle(const Rig& rig)
{
    gs::session::test::FakeServerOptions serverOptions;
    serverOptions.videoPackets = rig.recording.packets;
    gs::session::test::FakeDeviceServer device(std::move(serverOptions));
    gs::stability::FakePhone phone(device, std::string(kSerial));
    gs::app::SessionController controller(gs::adb::AdbClient(phone.port()),
                                          QString::fromLatin1(kSerial.data(), static_cast<qsizetype>(kSerial.size())),
                                          rig.package, rig.graphics.get());
    std::optional<gs::session::SessionState> state;
    QObject::connect(&controller, &gs::app::SessionController::stateChanged,
                     [&state](const gs::session::SessionState& changed) { state = changed; });
    const std::shared_ptr<gs::render::FrameSlot> slot = controller.frames();
    const auto decoded = [&slot] { return slot->taken() + slot->replaced(); };
    const auto ended = [&state] {
        return state && (state->phase == SessionPhase::Stopped || state->phase == SessionPhase::Failed);
    };

    controller.start(gs::session::TransportChoice::AdbTunnel, rig.settings);
    const bool streamed = spinUntil(
        [&] {
            static_cast<void>(slot->take());
            return decoded() >= rig.recording.frames || ended();
        },
        kStreamLimit);
    CycleResult result;
    result.frames = decoded();
    const std::string reached = phaseText(state);
    controller.stop();
    const bool stopped = spinUntil(ended, kStopLimit);

    if (!streamed || result.frames < rig.recording.frames) {
        result.problem = std::format("{} of {} frames shown within {} s; the session was {}", result.frames,
                                     rig.recording.frames, kStreamLimit.count(), reached);
    } else if (!stopped) {
        result.problem =
            std::format("the session did not stop within {} s; it is {}", kStopLimit.count(), phaseText(state));
    } else if (state->phase != SessionPhase::Stopped) {
        result.problem = "the session ended as " + phaseText(state);
    } else if (phone.launches() != 1) {
        result.problem = std::format("the server was launched {} times", phone.launches());
    } else {
        result.ok = true;
    }
    for (const std::string& request : phone.unexpected()) {
        result.problem += (result.problem.empty() ? "" : "; ") + std::string("unexpected adb request: ") + request;
        result.ok = false;
    }
    return result;
}

struct CyclesOutcome {
    int done = 0;
    int failed = 0;
    std::uint64_t frames = 0;
    double seconds = 0.0;
    bool aborted = false;
};

CyclesOutcome runCycles(const Rig& rig, const Options& options, Series& series, RecentLog& recent, Report& report)
{
    CyclesOutcome outcome;
    Csv csv(options.out / "cycles.csv", "cycle,elapsed_s,warmup,handles,threads,private_bytes,working_set,frames");
    if (!csv.good()) {
        report.fail("cannot write " + utf8(options.out / "cycles.csv"));
        outcome.aborted = true;
        return outcome;
    }
    const auto start = Clock::now();
    const ProcessSample before = gs::stability::sampleProcess();
    csv.row(std::format("0,0.000,1,{},0", sampleColumns(before)));
    report.say("cycle 0: " + sampleText(before));
    int failedInARow = 0;
    for (int cycle = 1; cycle <= options.cycles; ++cycle) {
        const CycleResult result = runCycle(rig);
        settle();
        ++outcome.done;
        outcome.frames += result.frames;
        if (result.ok) {
            failedInARow = 0;
        } else {
            ++outcome.failed;
            ++failedInARow;
            report.fail(std::format("cycle {} failed: {}", cycle, result.problem));
            for (const std::string& line : recent.take()) {
                report.say("  log: " + line);
            }
        }
        if (cycle % options.sampleEvery == 0 || cycle == options.cycles) {
            const ProcessSample sample = gs::stability::sampleProcess();
            const bool warmup = cycle <= options.warmupCycles;
            csv.row(std::format("{},{:.3f},{},{},{}", cycle, seconds(Clock::now() - start), warmup ? 1 : 0,
                                sampleColumns(sample), result.frames));
            if (!warmup) {
                series.add(static_cast<double>(cycle), sample);
            }
            report.say(std::format("cycle {}/{}{}: {}", cycle, options.cycles, warmup ? " (warm-up)" : "",
                                   sampleText(sample)));
        }
        if (failedInARow >= kMostFailuresInARow) {
            report.fail(std::format("stopping the cycles after {} failures in a row", failedInARow));
            outcome.aborted = true;
            break;
        }
    }
    outcome.seconds = seconds(Clock::now() - start);
    return outcome;
}

struct DecodeOutcome {
    double minutes = 0.0;
    std::uint64_t decoded = 0;
    std::uint64_t shown = 0;
    std::string problem;
};

DecodeOutcome runDecode(const Rig& rig, const Options& options, Series& series, Report& report)
{
    DecodeOutcome outcome;
    Csv csv(options.out / "decode.csv", "elapsed_s,warmup,handles,threads,private_bytes,working_set,frames,fps");
    if (!csv.good()) {
        outcome.problem = "cannot write " + utf8(options.out / "decode.csv");
        return outcome;
    }
    gs::app::DemoFeed feed(*rig.graphics, options.recording);
    if (!feed.start()) {
        outcome.problem = feed.problem().toStdString();
        return outcome;
    }
    const std::shared_ptr<gs::render::FrameSlot> slot = feed.frames();
    const auto interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(options.sampleSeconds));
    const auto start = Clock::now();
    const auto end = start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(options.minutes * 60.0));
    auto due = start;
    std::uint64_t decodedBefore = 0;
    bool first = true;
    while (true) {
        const auto now = Clock::now();
        if (slot->take()) {
            ++outcome.shown;
        }
        QCoreApplication::processEvents();
        if (const QString problem = feed.problem(); !problem.isEmpty()) {
            outcome.problem = problem.toStdString();
            break;
        }
        const bool finished = now >= end;
        if (now >= due || finished) {
            const double elapsed = seconds(now - start);
            const std::uint64_t decoded = slot->taken() + slot->replaced();
            const ProcessSample sample = gs::stability::sampleProcess();
            const bool warmup = elapsed < options.warmupMinutes * 60.0;
            csv.row(std::format("{:.3f},{},{},{},{:.1f}", elapsed, warmup ? 1 : 0, sampleColumns(sample), decoded,
                                feed.framesPerSecond()));
            if (!warmup) {
                series.add(elapsed / 3600.0, sample);
            }
            report.say(std::format("decoding {:.1f}/{:.1f} min{}: {} frames, {:.1f} per second, {}", elapsed / 60.0,
                                   options.minutes, warmup ? " (warm-up)" : "", decoded, feed.framesPerSecond(),
                                   sampleText(sample)));
            if (!first && decoded == decodedBefore) {
                outcome.problem = "no frame was decoded since the last sample";
                break;
            }
            first = false;
            decodedBefore = decoded;
            due += interval;
        }
        if (finished) {
            break;
        }
        std::this_thread::sleep_for(kDisplayInterval);
    }
    outcome.decoded = slot->taken() + slot->replaced();
    outcome.minutes = seconds(Clock::now() - start) / 60.0;
    feed.stop();
    return outcome;
}

void judge(Report& report, const Series& series, const Options& options, double memoryLimit, double unitScale,
           std::string_view unit, bool judgeMemory)
{
    if (series.handles.size() < kMinimumSamples) {
        report.fail(std::format("  only {} samples after the warm-up; at least {} are needed to judge",
                                series.handles.size(), kMinimumSamples));
        return;
    }
    const auto growth = [&report](std::string_view name, const Trend& trend, double tolerance) {
        const bool ok = trend.growth <= tolerance;
        report.verdict(ok, std::format("  {}: {:.0f} -> {:.0f} ({:+.1f}, limit +{:.0f}, peak {:.0f}): {}", name,
                                       trend.first, trend.last, trend.growth, tolerance, trend.peak,
                                       ok ? "pass" : "FAIL"));
    };
    growth("handles", gs::stability::trendOf(series.handles), options.handleTolerance);
    growth("threads", gs::stability::trendOf(series.threads), options.threadTolerance);

    const Trend privateBytes = gs::stability::trendOf(series.privateBytes);
    const double slope = privateBytes.slope * unitScale;
    if (judgeMemory && !kSanitized) {
        const bool ok = slope <= memoryLimit;
        report.verdict(ok, std::format("  private bytes: {:.1f} -> {:.1f} MiB, {:+.2f} MiB per {} (limit {:.2f}): {}",
                                       privateBytes.first / kMiB, privateBytes.last / kMiB, slope / kMiB, unit,
                                       memoryLimit / kMiB, ok ? "pass" : "FAIL"));
    } else {
        report.say(std::format("  private bytes: {:.1f} -> {:.1f} MiB, {:+.2f} MiB per {} (recorded, not judged: {})",
                               privateBytes.first / kMiB, privateBytes.last / kMiB, slope / kMiB, unit, kNotJudgedWhy));
    }
    const Trend workingSet = gs::stability::trendOf(series.workingSet);
    report.say(std::format("  working set: {:.1f} -> {:.1f} MiB, {:+.2f} MiB per {} (recorded, not judged)",
                           workingSet.first / kMiB, workingSet.last / kMiB, workingSet.slope * unitScale / kMiB, unit));
}

bool useGraphics(Rig& rig, Report& report, bool onCard, std::string_view part)
{
    rig.graphics = gs::render::GraphicsDevice::create(false, !onCard);
    if (rig.graphics == nullptr) {
        std::fputs("no Direct3D 11 device\n", stderr);
        return false;
    }
    report.say(std::format("graphics for the {}: {}{}", part, rig.graphics->description(),
                           rig.graphics->warp() ? " (WARP)" : ""));
    return true;
}

std::optional<Options> parseOptions(const QGuiApplication& application)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Endurance harness (REL-010): full session start, video and stop against fake adb and device "
                       "servers, then continuous decoding of a recording. Writes cycles.csv, decode.csv and "
                       "summary.txt, and fails when handles or threads grow after the warm-up or private bytes grow "
                       "faster than the limit."));
    parser.addHelpOption();
    const QCommandLineOption smoke(QStringLiteral("smoke"),
                                   QStringLiteral("Short run that keeps the harness working: 50 cycles and 2 minutes."));
    const QCommandLineOption only(QStringLiteral("only"), QStringLiteral("Run only 'cycles' or 'decode'."),
                                  QStringLiteral("part"));
    const QCommandLineOption cycles(QStringLiteral("cycles"), QStringLiteral("Connection cycles (default 1000)."),
                                    QStringLiteral("count"));
    const QCommandLineOption sampleEvery(QStringLiteral("sample-every"),
                                         QStringLiteral("Cycles between samples (default 10)."), QStringLiteral("count"));
    const QCommandLineOption warmupCycles(QStringLiteral("warmup-cycles"),
                                          QStringLiteral("Cycles left out of the verdict (default 50)."),
                                          QStringLiteral("count"));
    const QCommandLineOption minutes(QStringLiteral("minutes"),
                                     QStringLiteral("Minutes of continuous decoding (default 480)."),
                                     QStringLiteral("minutes"));
    const QCommandLineOption sampleSeconds(QStringLiteral("sample-seconds"),
                                           QStringLiteral("Seconds between decoding samples (default 60)."),
                                           QStringLiteral("seconds"));
    const QCommandLineOption warmupMinutes(QStringLiteral("warmup-minutes"),
                                           QStringLiteral("Minutes of decoding left out of the verdict (default 10)."),
                                           QStringLiteral("minutes"));
    const QCommandLineOption handles(QStringLiteral("handle-tolerance"),
                                     QStringLiteral("Allowed handle growth after the warm-up (default 8)."),
                                     QStringLiteral("count"));
    const QCommandLineOption threads(QStringLiteral("thread-tolerance"),
                                     QStringLiteral("Allowed thread growth after the warm-up (default 2)."),
                                     QStringLiteral("count"));
    const QCommandLineOption cycleMemory(QStringLiteral("cycle-memory-kib"),
                                         QStringLiteral("Allowed private bytes slope in KiB per 1000 cycles (default "
                                                        "4096)."),
                                         QStringLiteral("kib"));
    const QCommandLineOption decodeMemory(QStringLiteral("decode-memory-mib"),
                                          QStringLiteral("Allowed private bytes slope in MiB per hour of decoding "
                                                         "(default 4)."),
                                          QStringLiteral("mib"));
    const QCommandLineOption recording(QStringLiteral("recording"),
                                       QStringLiteral("Recorded video channel to stream and decode (default "
                                                      "tests/data/video/emulator_h264.media)."),
                                       QStringLiteral("file"));
    const QCommandLineOption out(QStringLiteral("out"),
                                 QStringLiteral("Folder for the CSV curves and the summary (default "
                                                "./endurance-<time>)."),
                                 QStringLiteral("folder"));
    const QCommandLineOption hardware(QStringLiteral("hardware"),
                                      QStringLiteral("Run the connection cycles on the graphics card instead of WARP; "
                                                     "their private bytes are then recorded, not judged, because the "
                                                     "driver keeps memory for each video decoder it makes."));
    parser.addOptions({smoke, only, cycles, sampleEvery, warmupCycles, minutes, sampleSeconds, warmupMinutes, handles,
                       threads, cycleMemory, decodeMemory, recording, out, hardware});
    parser.process(application);

    Options options;
    options.command = QCoreApplication::arguments().join(u' ');
    if (parser.isSet(smoke)) {
        options.cycles = 80;
        options.sampleEvery = 1;
        options.warmupCycles = 40;
        options.minutes = 2.0;
        options.sampleSeconds = 5.0;
        options.warmupMinutes = 0.5;
        options.cycleMemoryKib = 262144.0;
        options.decodeMemoryMib = 1024.0;
    }
    bool valid = true;
    const auto integer = [&](const QCommandLineOption& option, int& value, int least) {
        if (parser.isSet(option)) {
            bool ok = false;
            value = parser.value(option).toInt(&ok);
            valid = valid && ok && value >= least;
        }
    };
    const auto real = [&](const QCommandLineOption& option, double& value, double least) {
        if (parser.isSet(option)) {
            bool ok = false;
            value = parser.value(option).toDouble(&ok);
            valid = valid && ok && value >= least;
        }
    };
    integer(cycles, options.cycles, 1);
    integer(sampleEvery, options.sampleEvery, 1);
    integer(warmupCycles, options.warmupCycles, 0);
    real(minutes, options.minutes, 0.1);
    real(sampleSeconds, options.sampleSeconds, 1.0);
    real(warmupMinutes, options.warmupMinutes, 0.0);
    real(handles, options.handleTolerance, 0.0);
    real(threads, options.threadTolerance, 0.0);
    real(cycleMemory, options.cycleMemoryKib, 0.0);
    real(decodeMemory, options.decodeMemoryMib, 0.0);
    options.hardware = parser.isSet(hardware);
    if (parser.isSet(only)) {
        const QString part = parser.value(only);
        options.cyclesOn = part == u"cycles";
        options.decodeOn = part == u"decode";
        valid = valid && (options.cyclesOn || options.decodeOn);
    }
    options.recording = parser.isSet(recording) ? fs::path(parser.value(recording).toStdWString())
                                                : fs::path(GS_VIDEO_TEST_DATA_DIR) / "emulator_h264.media";
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    options.out = parser.isSet(out) ? fs::path(parser.value(out).toStdWString())
                                    : fs::path(QDir::currentPath().toStdWString()) / ("endurance-" + stamp.toStdString());
    if (!valid) {
        std::fputs("invalid option value; see --help\n", stderr);
        return std::nullopt;
    }
    return options;
}

}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication::addLibraryPath(QStringLiteral(GS_QT_PLUGIN_DIR));
    const QGuiApplication application(argc, argv);
    const std::optional<Options> options = parseOptions(application);
    if (!options) {
        return 2;
    }
    std::error_code error;
    fs::create_directories(options->out, error);
    if (error) {
        std::fprintf(stderr, "cannot create %s\n", utf8(options->out).c_str());
        return 2;
    }

    Report report;
    report.say("command: " + options->command.toStdString());
    std::optional<Recording> recording = loadRecording(options->recording);
    if (!recording) {
        std::fprintf(stderr, "cannot read the recording %s\n", utf8(options->recording).c_str());
        return 2;
    }
    Rig rig;
    rig.recording = std::move(*recording);
    rig.package = std::make_shared<const gs::session::ServerPackage>(std::vector<std::uint8_t>(kServerBytes, kServerFill));
    rig.settings.audio.enabled = false;
    report.say(std::format("recording {}: {} packets, {} frames; results in {}", utf8(options->recording.filename()),
                           rig.recording.packets.size(), rig.recording.frames, utf8(options->out)));
    const bool forceWarp = qEnvironmentVariableIntValue("GS_FORCE_WARP") == 1;
    const bool cyclesOnCard = options->hardware && !forceWarp;

    RecentLog recent;
    gs::log::setSink(&recent);
    if (options->cyclesOn) {
        if (!useGraphics(rig, report, cyclesOnCard, "connection cycles")) {
            gs::log::setSink(nullptr);
            return 2;
        }
        Series series;
        const CyclesOutcome cycles = runCycles(rig, *options, series, recent, report);
        report.say(std::format("connection cycles: {} in {:.1f} min ({:.2f} s each), {} failed, {} frames shown",
                               cycles.done, cycles.seconds / 60.0, cycles.done > 0 ? cycles.seconds / cycles.done : 0.0,
                               cycles.failed, cycles.frames));
        if (cycles.failed > 0) {
            report.fail(std::format("  {} of {} cycles failed", cycles.failed, cycles.done));
        }
        if (!cycles.aborted) {
            judge(report, series, *options, options->cycleMemoryKib * kKiB, 1000.0, "1000 cycles", !cyclesOnCard);
        }
    }
    if (options->decodeOn) {
        if (!useGraphics(rig, report, !forceWarp, "continuous decoding")) {
            gs::log::setSink(nullptr);
            return 2;
        }
        Series series;
        const DecodeOutcome decode = runDecode(rig, *options, series, report);
        report.say(std::format("continuous decoding: {:.1f} min, {} frames decoded, {} shown", decode.minutes,
                               decode.decoded, decode.shown));
        if (!decode.problem.empty()) {
            report.fail("  decoding failed: " + decode.problem);
        } else {
            judge(report, series, *options, options->decodeMemoryMib * kMiB, 1.0, "hour", true);
        }
    }
    gs::log::setSink(nullptr);
    report.say(report.pass ? "result: pass" : "result: FAIL");

    std::ofstream summary(options->out / "summary.txt", std::ios::binary | std::ios::trunc);
    for (const std::string& line : report.lines) {
        summary << line << '\n';
    }
    return report.pass ? 0 : 1;
}
