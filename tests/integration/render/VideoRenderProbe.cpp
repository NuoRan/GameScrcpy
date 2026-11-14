// Plays a recorded video channel (tests/data/video) through the whole display path: the shared
// Direct3D 11 device, VideoDecoder on a thread of its own fed at the recorded pace, FrameSlot,
// VideoItem and its D3D11 node. It checks that decoding and Qt Quick can share one device (M4.1,
// PLAN 4.4) and doubles as a stress tool:
//
//   gs_video_render_probe <recording> [--seconds N] [--resize] [--debug-layer] [--software]
//                         [--sharpness S] [--low-latency] [--check-sharpness] [--fps N]
//                         [--gui-busy MS] [--gpu-time] [--fullscreen] [--on-top]
//
// Exits with 0 when every packet decoded, pictures reached the screen and the debug layer, when
// present, reported no error. It prints the mean arrival-to-render time and the frames presented
// per second. With --check-sharpness it then stops feeding, grabs the last picture at sharpness 0
// and at 1, and also fails unless the two differ, neither is black and sharpening raised the
// high-frequency energy.

#include <gs/media/VideoDecoder.h>
#include <gs/protocol/Codec.h>
#include <gs/protocol/Constants.h>
#include <gs/render/FrameSlot.h>
#include <gs/render/GraphicsDevice.h>
#include <gs/render/VideoFilter.h>
#include <gs/render/VideoItem.h>

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQuickGraphicsDevice>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

constexpr double kSharpenedEnergy = 1.1;
constexpr qreal kFastScreenHz = 100.0;

struct LatencySamples {
    void arrived(std::int64_t ptsUs, std::int64_t receivedUs)
    {
        const std::scoped_lock lock(mutex);
        if (arrivals.size() >= 4096) {
            arrivals.clear();
        }
        arrivals[ptsUs] = receivedUs;
    }

    void taken(std::int64_t ptsUs, std::int64_t shownUs)
    {
        const std::scoped_lock lock(mutex);
        const auto it = arrivals.find(ptsUs);
        if (it == arrivals.end()) {
            return;
        }
        arrivalToTakenMs.push_back(static_cast<double>(shownUs - it->second) / 1000.0);
        arrivals.erase(it);
    }

    void screened(std::int64_t ptsUs, std::int64_t receivedUs, std::int64_t takenUs, std::int64_t screenUs)
    {
        (void)ptsUs;
        const std::scoped_lock lock(mutex);
        arrivalToScreenMs.push_back(static_cast<double>(screenUs - receivedUs) / 1000.0);
        renderToScreenMs.push_back(static_cast<double>(screenUs - takenUs) / 1000.0);
    }

    void gpu(double ms)
    {
        const std::scoped_lock lock(mutex);
        gpuMs.push_back(ms);
    }

    std::mutex mutex;
    std::unordered_map<std::int64_t, std::int64_t> arrivals;
    std::vector<double> arrivalToTakenMs;
    std::vector<double> arrivalToScreenMs;
    std::vector<double> renderToScreenMs;
    std::vector<double> gpuMs;
};

class SlotSink final : public gs::media::FrameSink
{
public:
    explicit SlotSink(gs::render::FrameSlot& slot, gs::render::FrameSlot* second = nullptr,
                      LatencySamples* latency = nullptr)
        : m_slot(slot), m_second(second), m_latency(latency)
    {
    }

    void onFrame(gs::media::VideoFrame frame) override
    {
        ++decoded;
        hardware = frame.hardware();
        if (m_latency != nullptr) {
            m_latency->arrived(frame.ptsUs(), frame.receivedUs());
        }
        if (m_second != nullptr) {
            m_second->put(frame.share());
        }
        m_slot.put(std::move(frame));
    }

    std::atomic<std::uint64_t> decoded{0};
    std::atomic<bool> hardware{false};

private:
    gs::render::FrameSlot& m_slot;
    gs::render::FrameSlot* m_second;
    LatencySamples* m_latency;
};

struct Measure {
    bool lit = false;
    double energy = 0;
};

const char* modeName(gs::render::PresentationMode mode)
{
    switch (mode) {
    case gs::render::PresentationMode::Composed:
        return "composed by Windows";
    case gs::render::PresentationMode::HardwareOverlay:
        return "hardware overlay";
    case gs::render::PresentationMode::Unknown:
        break;
    }
    return "presentation unknown";
}

struct Spread {
    double p50 = 0;
    double p95 = 0;
    double worst = 0;
    std::size_t count = 0;
};

Spread spreadOf(std::vector<double> values)
{
    Spread result;
    result.count = values.size();
    if (values.empty()) {
        return result;
    }
    std::sort(values.begin(), values.end());
    const auto at = [&values](double fraction) {
        const std::size_t rank = static_cast<std::size_t>(fraction * static_cast<double>(values.size()) + 0.999);
        return values[rank == 0 ? 0 : std::min(rank, values.size()) - 1];
    };
    result.p50 = at(0.5);
    result.p95 = at(0.95);
    result.worst = values.back();
    return result;
}

Measure measure(const QImage& grab)
{
    const QImage rgb = grab.convertToFormat(QImage::Format_RGB32);
    const auto width = static_cast<std::size_t>(rgb.width());
    const auto height = static_cast<std::size_t>(rgb.height());
    std::vector<int> luma;
    luma.reserve(width * height);
    Measure result;
    for (std::size_t y = 0; y < height; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(static_cast<int>(y)));
        for (std::size_t x = 0; x < width; ++x) {
            result.lit = result.lit || (line[x] & 0xFFFFFFU) != 0;
            luma.push_back(qGray(line[x]));
        }
    }
    for (std::size_t y = 1; y + 1 < height; ++y) {
        for (std::size_t x = 1; x + 1 < width; ++x) {
            const std::size_t at = y * width + x;
            const int laplacian =
                4 * luma[at] - luma[at - 1] - luma[at + 1] - luma[at - width] - luma[at + width];
            result.energy += std::abs(laplacian);
        }
    }
    return result;
}

bool checkSharpness(QQuickWindow& window, gs::render::VideoItem& video)
{
    video.setSharpness(0);
    video.update();
    (void)window.grabWindow();
    const QImage plain = window.grabWindow();
    video.setSharpness(1);
    const QImage sharp = window.grabWindow();
    video.setSharpness(0);
    const QImage plainAgain = window.grabWindow();

    const Measure plainMeasure = measure(plain);
    const Measure sharpMeasure = measure(sharp);
    const double ratio = plainMeasure.energy > 0 ? sharpMeasure.energy / plainMeasure.energy : 0;
    std::printf("sharpness check: %dx%d grabs, high-frequency energy %.0f at 0 and %.0f at 1 "
                "(x%.2f, needs x%.2f)\n",
                plain.width(), plain.height(), plainMeasure.energy, sharpMeasure.energy, ratio,
                kSharpenedEnergy);
    const auto fail = [](const char* reason) {
        std::fprintf(stderr, "sharpness check failed: %s\n", reason);
        return false;
    };
    if (plain.isNull() || sharp.isNull() || plainAgain.isNull()) {
        return fail("the window could not be grabbed");
    }
    if (plain != plainAgain) {
        return fail("the picture at sharpness 0 changed between grabs");
    }
    if (!plainMeasure.lit || !sharpMeasure.lit) {
        return fail("a grab is black");
    }
    if (plain == sharp) {
        return fail("sharpness 1 changed nothing");
    }
    if (ratio < kSharpenedEnergy) {
        return fail("sharpening did not raise the high-frequency energy enough");
    }
    return true;
}

constexpr char kPassthrough[] = "//! PASS copy\nfloat4 copy(float2 uv)\n{\n    return Source.Sample(Point, uv);\n}\n";

std::vector<gs::render::VideoFilter> readFilters(const QStringList& files, const QStringList& params)
{
    std::vector<gs::render::VideoFilter> filters;
    for (const QString& path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "cannot read the filter %s\n", qPrintable(path));
            continue;
        }
        gs::render::VideoFilter filter;
        filter.id = QFileInfo(path).fileName().toStdString();
        filter.fileName = filter.id;
        filter.source = file.readAll().toStdString();
        for (const QString& param : params) {
            const qsizetype equals = param.indexOf(u'=');
            if (equals > 0) {
                filter.values[param.left(equals).toStdString()] = param.mid(equals + 1).toDouble();
            }
        }
        filters.push_back(std::move(filter));
    }
    return filters;
}

std::optional<gs::render::FilterStatus> statusOf(const gs::render::VideoItem& video, const std::string& id)
{
    for (const gs::render::FilterStatus& status : video.filterStatus()) {
        if (status.id == id) {
            return status;
        }
    }
    return std::nullopt;
}

void settle(QQuickWindow& window, gs::render::VideoItem& video, const std::vector<gs::render::VideoFilter>& filters)
{
    video.setFilters(filters);
    for (int frame = 0; frame < 8; ++frame) {
        (void)window.grabWindow();
        QCoreApplication::processEvents();
    }
}

bool checkFilters(QQuickWindow& window, gs::render::VideoItem& video,
                  const std::vector<gs::render::VideoFilter>& given)
{
    const auto fail = [](const char* reason) {
        std::fprintf(stderr, "filter check failed: %s\n", reason);
        return false;
    };
    const QSize frame = video.frameSize();
    if (frame.isEmpty()) {
        return fail("no picture to filter");
    }
    window.resize(frame);
    QElapsedTimer waited;
    waited.start();
    while (video.contentRect().size().toSize() != frame && waited.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    gs::render::VideoFilter passthrough;
    passthrough.id = "passthrough";
    passthrough.fileName = "passthrough.hlsl";
    passthrough.source = kPassthrough;

    settle(window, video, {});
    const QImage plain = window.grabWindow();
    settle(window, video, {passthrough});
    const QImage copied = window.grabWindow();
    const std::optional<gs::render::FilterStatus> copy = statusOf(video, "passthrough");
    settle(window, video, {});
    const QImage plainAgain = window.grabWindow();
    std::printf("filter check: %dx%d picture, %dx%d grabs, passthrough %s\n", frame.width(), frame.height(),
                plain.width(), plain.height(), copy && copy->ready ? "ready" : "not ready");
    if (plain.isNull() || copied.isNull() || plainAgain.isNull()) {
        return fail("the window could not be grabbed");
    }
    if (plain.size() != frame) {
        return fail("the grab is not the size of the picture, so the comparison would not be pixel exact");
    }
    if (!measure(plain).lit) {
        return fail("the plain grab is black");
    }
    if (!copy || !copy->ready) {
        std::fprintf(stderr, "%s\n", copy ? copy->error.c_str() : "no status arrived");
        return fail("the passthrough filter did not run");
    }
    if (copied != plain) {
        return fail("the passthrough filter changed the picture");
    }
    if (plainAgain != plain) {
        return fail("the picture is not the same after the filter was removed");
    }
    for (const gs::render::VideoFilter& filter : given) {
        settle(window, video, {filter});
        const QImage filtered = window.grabWindow();
        const std::optional<gs::render::FilterStatus> status = statusOf(video, filter.id);
        if (!status || !status->ready) {
            std::fprintf(stderr, "%s\n", status ? status->error.c_str() : "no status");
            return fail("a given filter did not run");
        }
        if (filtered == plain || !measure(filtered).lit) {
            return fail("a given filter left the picture as it was, or made it black");
        }
        std::printf("filter %s:", filter.id.c_str());
        for (std::size_t i = 0; i < status->passes.size(); ++i) {
            std::printf(" %s %.3f ms", status->passes[i].c_str(), i < status->passMs.size() ? status->passMs[i] : 0.0);
        }
        std::printf("\n");
    }
    settle(window, video, {});
    return true;
}

}

int main(int argc, char* argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    qputenv("QT_QPA_DISABLE_REDIRECTION_SURFACE", "1");
    if (!qEnvironmentVariableIsSet("QT_D3D_NO_VBLANK_THREAD")) {
        qputenv("QT_D3D_NO_VBLANK_THREAD", "1");
    }
    if (!qEnvironmentVariableIsSet("QT_QPA_UPDATE_IDLE_TIME")) {
        qputenv("QT_QPA_UPDATE_IDLE_TIME", "0");
    }
    if (std::any_of(argv + 1, argv + argc, [](const char* argument) { return std::strcmp(argument, "--check-filter") == 0; })) {
        qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    }
    QGuiApplication app(argc, argv);
    if (!qEnvironmentVariableIsSet("QT_D3D_MAX_FRAME_LATENCY")) {
        if (const QScreen* screen = QGuiApplication::primaryScreen();
            screen != nullptr && screen->refreshRate() < kFastScreenHz) {
            qputenv("QT_D3D_MAX_FRAME_LATENCY", "1");
        }
    }
    QCommandLineParser parser;
    parser.addPositionalArgument("recording", "A recorded video channel, as in tests/data/video.");
    const QCommandLineOption secondsOption("seconds", "How long to play.", "seconds", "5");
    const QCommandLineOption resizeOption("resize", "Resize the window four times a second.");
    const QCommandLineOption debugOption("debug-layer", "Use the D3D11 debug layer and fail on its errors.");
    const QCommandLineOption softwareOption("software", "Decode in software.");
    const QCommandLineOption sharpnessOption("sharpness", "Sharpen the picture, from 0 to 1.",
                                             "strength", "0");
    const QCommandLineOption lowLatencyOption("low-latency",
                                              "Present without waiting for vertical sync.");
    const QCommandLineOption checkOption(
        "check-sharpness", "Then grab the last picture at sharpness 0 and 1 and compare them.");
    const QCommandLineOption filterOption("filter", "Run the picture through this HLSL filter; repeatable.", "file");
    const QCommandLineOption paramOption("param", "A filter parameter as name=value; repeatable.", "value");
    const QCommandLineOption checkFilterOption(
        "check-filter", "Then compare the last picture without a filter, through a passthrough filter and after it, "
                        "pixel by pixel at its own size, and check that every given filter changes it.");
    const QCommandLineOption secondWindowOption(
        "second-window", "Also play the picture in a second window sharing the device, and check that both windows "
                         "keep presenting.");
    const QCommandLineOption fpsOption("fps", "Feed the recording's frames at this rate instead of the recorded "
                                              "pace.",
                                       "fps");
    const QCommandLineOption guiBusyOption("gui-busy",
                                           "Keep the interface thread busy for this many milliseconds every frame.",
                                           "ms", "0");
    const QCommandLineOption fullScreenOption("fullscreen", "Show the window full screen.");
    const QCommandLineOption onTopOption("on-top", "Keep the window above all others, the taskbar included.");
    const QCommandLineOption gpuTimeOption("gpu-time",
                                           "Measure how long the graphics card draws the picture, filters included.");
    const QCommandLineOption arrivalOption(
        "arrival", "How frames arrive: even, pairs (two at once every other frame time, like the phone's input "
                   "surface at 120 fps) or jitter (each frame early or late at random).",
        "pattern", "even");
    const QCommandLineOption jitterOption("jitter-ms", "The standard deviation of the arrival jitter.", "ms", "3.5");
    parser.addOptions({secondsOption, resizeOption, debugOption, softwareOption, sharpnessOption,
                       lowLatencyOption, checkOption, filterOption, paramOption, checkFilterOption,
                       secondWindowOption, fpsOption, guiBusyOption, fullScreenOption, onTopOption, gpuTimeOption,
                       arrivalOption, jitterOption});
    parser.process(app);
    const std::vector<gs::render::VideoFilter> filters =
        readFilters(parser.values(filterOption), parser.values(paramOption));
    if (filters.size() != static_cast<std::size_t>(parser.values(filterOption).size())) {
        return 2;
    }
    if (parser.positionalArguments().size() != 1) {
        parser.showHelp(2);
    }

    std::ifstream file(parser.positionalArguments().front().toStdWString(), std::ios::binary);
    const std::vector<std::uint8_t> recording{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<gs::protocol::MediaPacket> packets;
    for (std::size_t offset = 0; offset < recording.size();) {
        auto packet = gs::protocol::decodeMediaPacket(gs::protocol::MediaChannel::Video,
                                                      std::span(recording).subspan(offset));
        if (!packet) {
            std::fprintf(stderr, "not a video recording at byte %zu\n", offset);
            return 2;
        }
        offset += gs::protocol::kMediaHeaderSize + packet->header.size;
        packets.push_back(*packet);
    }

    const int requestedFps = parser.value(fpsOption).toInt();
    double recordedFrameMs = 0;
    {
        std::int64_t firstPts = -1;
        std::int64_t lastPts = -1;
        std::size_t frames = 0;
        for (const auto& packet : packets) {
            if (packet.header.kind != gs::protocol::MediaKind::Frame) {
                continue;
            }
            firstPts = firstPts < 0 ? packet.header.ptsUs : firstPts;
            lastPts = packet.header.ptsUs;
            ++frames;
        }
        if (frames > 1 && lastPts > firstPts) {
            recordedFrameMs = static_cast<double>(lastPts - firstPts) / 1000.0 / static_cast<double>(frames - 1);
        }
    }
    if (requestedFps > 0) {
        std::int64_t index = 0;
        for (auto& packet : packets) {
            if (packet.header.kind == gs::protocol::MediaKind::Frame) {
                packet.header.ptsUs = index * 1'000'000 / requestedFps;
                ++index;
            }
        }
    }

    const QString arrival = parser.value(arrivalOption);
    if (arrival != u"even" && arrival != u"pairs" && arrival != u"jitter") {
        std::fprintf(stderr, "--arrival must be even, pairs or jitter\n");
        return 2;
    }
    std::vector<std::int64_t> dueUs(packets.size(), 0);
    {
        std::vector<std::size_t> frames;
        for (std::size_t i = 0; i < packets.size(); ++i) {
            if (packets[i].header.kind == gs::protocol::MediaKind::Frame) {
                frames.push_back(i);
            }
        }
        const std::int64_t firstPts = frames.empty() ? 0 : packets[frames.front()].header.ptsUs;
        std::mt19937 random(20261114);
        const double jitterUs = parser.value(jitterOption).toDouble() * 1000.0;
        std::normal_distribution<double> noise(0.0, jitterUs);
        std::int64_t previous = 0;
        for (std::size_t k = 0; k < frames.size(); ++k) {
            const std::int64_t own = packets[frames[k]].header.ptsUs - firstPts;
            std::int64_t due = own;
            if (arrival == u"pairs") {
                due = k % 2 == 0 && k + 1 < frames.size() ? packets[frames[k + 1]].header.ptsUs - firstPts
                                                          : own + 300;
            } else if (arrival == u"jitter") {
                due = own + static_cast<std::int64_t>(std::clamp(noise(random), -2.0 * jitterUs, 2.0 * jitterUs));
            }
            due = std::max(due, previous);
            previous = due;
            dueUs[frames[k]] = due;
        }
    }

    const auto graphics = gs::render::GraphicsDevice::create(parser.isSet(debugOption));
    if (!graphics) {
        std::fprintf(stderr, "no Direct3D 11 device\n");
        return 1;
    }
    std::printf("device: %s%s%s\n", graphics->description().c_str(), graphics->warp() ? " (WARP)" : "",
                graphics->debugLayer() ? ", debug layer" : "");

    std::atomic<std::uint64_t> presented{0};
    LatencySamples latency;
    QQuickWindow window;
    if (parser.isSet(lowLatencyOption)) {
        QSurfaceFormat unthrottled = window.requestedFormat();
        unthrottled.setSwapInterval(0);
        window.setFormat(unthrottled);
    }
    graphics->share(window);
    window.setColor(Qt::black);
    window.setTitle("gs_video_render_probe");
    auto* video = new gs::render::VideoItem(window.contentItem());
    video->setSharpness(parser.value(sharpnessOption).toDouble());
    if (!parser.isSet(checkFilterOption)) {
        video->setFilters(filters);
    }
    const qreal sharpness = video->sharpness();
    const auto fill = [&] { video->setSize(window.size()); };
    QObject::connect(&window, &QWindow::widthChanged, fill);
    QObject::connect(&window, &QWindow::heightChanged, fill);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&presented] { ++presented; },
                     Qt::DirectConnection);
    const auto slot = std::make_shared<gs::render::FrameSlot>();
    video->setSource(slot);
    window.resize(480, 720);
    fill();
    window.setFlag(Qt::WindowStaysOnTopHint, parser.isSet(onTopOption));
    if (parser.isSet(fullScreenOption)) {
        window.showFullScreen();
    } else {
        window.show();
    }

    std::atomic<std::uint64_t> presentedSecond{0};
    const auto secondSlot = std::make_shared<gs::render::FrameSlot>();
    std::unique_ptr<QQuickWindow> second;
    if (parser.isSet(secondWindowOption)) {
        second = std::make_unique<QQuickWindow>();
        graphics->share(*second);
        second->setColor(Qt::black);
        second->setTitle("gs_video_render_probe (second)");
        auto* secondVideo = new gs::render::VideoItem(second->contentItem());
        secondVideo->setSource(secondSlot);
        QObject::connect(second.get(), &QQuickWindow::frameSwapped, second.get(),
                         [&presentedSecond] { ++presentedSecond; }, Qt::DirectConnection);
        second->resize(360, 540);
        secondVideo->setSize(second->size());
        second->setPosition(window.position() + QPoint(window.width() + 16, 0));
        second->show();
    }

    SlotSink sink(*slot, second != nullptr ? secondSlot.get() : nullptr, &latency);
    slot->setShownListener([&latency](std::int64_t ptsUs, std::int64_t shownUs) { latency.taken(ptsUs, shownUs); });
    slot->setScreenListener([&latency](std::int64_t ptsUs, std::int64_t receivedUs, std::int64_t takenUs,
                                       std::int64_t screenUs) {
        latency.screened(ptsUs, receivedUs, takenUs, screenUs);
    });
    if (parser.isSet(gpuTimeOption)) {
        slot->setGpuTimeListener([&latency](double ms) { latency.gpu(ms); });
        slot->setGpuTiming(true);
    }
    std::atomic<bool> failed{false};
    std::jthread player([&](std::stop_token stop) {
        gs::media::VideoDecoder decoder(parser.isSet(softwareOption) ? nullptr : graphics->device());
        std::mutex mutex;
        std::condition_variable_any wake;
        while (!stop.stop_requested()) {
            const auto start = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < packets.size(); ++i) {
                const auto& packet = packets[i];
                if (packet.header.kind == gs::protocol::MediaKind::Frame) {
                    std::unique_lock lock(mutex);
                    wake.wait_until(lock, stop, start + std::chrono::microseconds(dueUs[i]), [] { return false; });
                }
                if (stop.stop_requested()) {
                    return;
                }
                if (const auto pushed = decoder.push(packet, sink); !pushed) {
                    std::fprintf(stderr, "decoding failed: %s\n", pushed.error().message.c_str());
                    failed = true;
                    return;
                }
            }
        }
    });

    QTimer resizer;
    if (parser.isSet(resizeOption)) {
        QObject::connect(&resizer, &QTimer::timeout, [&] {
            auto* random = QRandomGenerator::global();
            window.resize(random->bounded(240, 1200), random->bounded(240, 900));
        });
        resizer.start(250);
    }
    QTimer busy;
    const int busyMs = parser.value(guiBusyOption).toInt();
    if (busyMs > 0) {
        const double frameMs = requestedFps > 0 ? 1000.0 / static_cast<double>(requestedFps) : recordedFrameMs;
        if (frameMs > 0) {
            QObject::connect(&busy, &QTimer::timeout, [busyMs] {
                const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(busyMs);
                while (std::chrono::steady_clock::now() < until) {
                }
            });
            busy.start(std::max(1, static_cast<int>(frameMs)));
        }
    }
    double waitedMs = 0;
    std::uint64_t waitedFrames = 0;
    std::uint64_t sampledTaken = 0;
    const auto sampleLatency = [&] {
        const std::uint64_t taken = slot->taken();
        if (const auto mean = slot->takeArrivalToRenderMs()) {
            waitedMs += *mean * static_cast<double>(taken - sampledTaken);
            waitedFrames += taken - sampledTaken;
        }
        sampledTaken = taken;
    };
    QTimer sampler;
    QObject::connect(&sampler, &QTimer::timeout, sampleLatency);
    sampler.start(500);
    const auto started = std::chrono::steady_clock::now();
    std::chrono::duration<double> played{};
    std::uint64_t presentedFrames = 0;
    std::uint64_t presentedSecondFrames = 0;
    std::uint64_t decodedFrames = 0;
    const auto stopPlaying = [&] {
        if (player.joinable()) {
            played = std::chrono::steady_clock::now() - started;
            presentedFrames = presented;
            presentedSecondFrames = presentedSecond;
            decodedFrames = sink.decoded;
            player.request_stop();
            player.join();
            sampleLatency();
        }
    };
    bool sharpnessShows = !parser.isSet(checkOption);
    bool filtersHold = true;
    QTimer::singleShot(std::chrono::seconds(parser.value(secondsOption).toInt()), &app, [&] {
        stopPlaying();
        if (parser.isSet(checkOption)) {
            sharpnessShows = checkSharpness(window, *video);
        }
        if (parser.isSet(checkFilterOption)) {
            filtersHold = checkFilters(window, *video, filters);
        }
        QCoreApplication::quit();
    });
    app.exec();
    stopPlaying();

    const auto errors = graphics->debugErrors();
    for (const auto& error : errors) {
        std::fprintf(stderr, "debug layer: %s\n", error.c_str());
    }
    std::printf("decoded %llu (%s), shown %llu, replaced before shown %llu, dropped by Windows %llu, "
                "debug layer errors %zu\n",
                static_cast<unsigned long long>(sink.decoded.load()), sink.hardware ? "GPU" : "software",
                static_cast<unsigned long long>(slot->taken()), static_cast<unsigned long long>(slot->replaced()),
                static_cast<unsigned long long>(slot->discarded()), errors.size());
    std::printf("swap interval %d, sharpness %.2f: presented %.1f frames per second, "
                "arrival to render %.2f ms\n",
                window.requestedFormat().swapInterval(), sharpness,
                static_cast<double>(presentedFrames) / played.count(),
                waitedFrames > 0 ? waitedMs / static_cast<double>(waitedFrames) : 0.0);
    std::vector<double> takenValues;
    std::vector<double> screenValues;
    std::vector<double> renderValues;
    std::vector<double> gpuValues;
    {
        const std::scoped_lock lock(latency.mutex);
        takenValues = latency.arrivalToTakenMs;
        screenValues = latency.arrivalToScreenMs;
        renderValues = latency.renderToScreenMs;
        gpuValues = latency.gpuMs;
    }
    const Spread taken = spreadOf(std::move(takenValues));
    const Spread toScreen = spreadOf(std::move(screenValues));
    const Spread afterRender = spreadOf(std::move(renderValues));
    const Spread gpu = spreadOf(std::move(gpuValues));
    const QByteArray latencySetting = qgetenv("QT_D3D_MAX_FRAME_LATENCY");
    std::printf("wake route %s, frame latency %s, presentation %s\n",
                qEnvironmentVariable("GS_RENDER_WAKE") == u"gui" ? "interface" : "render thread",
                latencySetting.isEmpty() ? "2 (Qt default)" : latencySetting.constData(),
                modeName(slot->presentationMode()));
    std::printf("arrival to render: p50 %.2f ms, p95 %.2f ms, max %.2f ms (%zu frames)\n", taken.p50, taken.p95,
                taken.worst, taken.count);
    std::printf("arrival to screen: p50 %.2f ms, p95 %.2f ms, max %.2f ms (%zu frames)\n", toScreen.p50, toScreen.p95,
                toScreen.worst, toScreen.count);
    std::printf("render to screen: p50 %.2f ms, p95 %.2f ms, max %.2f ms (%zu frames)\n", afterRender.p50,
                afterRender.p95, afterRender.worst, afterRender.count);
    if (parser.isSet(gpuTimeOption)) {
        std::printf("gpu: p50 %.2f ms, p95 %.2f ms, max %.2f ms (%zu frames)\n", gpu.p50, gpu.p95, gpu.worst,
                    gpu.count);
    }
    if (!parser.isSet(checkFilterOption)) {
        for (const gs::render::VideoFilter& filter : filters) {
            const std::optional<gs::render::FilterStatus> status = statusOf(*video, filter.id);
            if (!status || !status->ready) {
                std::fprintf(stderr, "filter %s did not run: %s\n", filter.id.c_str(),
                             status ? status->error.c_str() : "no status");
                filtersHold = false;
                continue;
            }
            std::printf("filter %s:", filter.id.c_str());
            for (std::size_t i = 0; i < status->passes.size(); ++i) {
                std::printf(" %s %.3f ms", status->passes[i].c_str(),
                            i < status->passMs.size() ? status->passMs[i] : 0.0);
            }
            std::printf("\n");
        }
    }
    const bool passed = !failed && sink.decoded > 0 && slot->taken() > 0 && errors.empty();
    bool bothPresent = true;
    if (second != nullptr) {
        const double decodedRate = static_cast<double>(decodedFrames) / played.count();
        const double firstRate = static_cast<double>(presentedFrames) / played.count();
        const double secondRate = static_cast<double>(presentedSecondFrames) / played.count();
        std::printf("two windows sharing the device: decoded %.1f, first window presented %.1f, second %.1f frames "
                    "per second\n",
                    decodedRate, firstRate, secondRate);
        bothPresent = firstRate >= 0.5 * decodedRate && secondRate >= 0.5 * decodedRate;
        if (!bothPresent) {
            std::fprintf(stderr, "a window sharing the device stopped keeping up with the video\n");
        }
    }
    return passed && sharpnessShows && filtersHold && bothPresent ? 0 : 1;
}
