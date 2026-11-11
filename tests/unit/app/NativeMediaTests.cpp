#include "ExtensionMedia.h"
#include "MediaSinks.h"
#include "NativeExtensions.h"

#include "unit/ext/TestFiles.h"

#include <gs/capture/CaptureSource.h>
#include <gs/exthost/Hosted.h>
#include <gs/exthost/InProcess.h>
#include <gs/exthost/Media.h>
#include <gs/media/FrameSnapshot.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <QCoreApplication>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace gs;
using namespace gs::ext::test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

const fs::path kTestDll(GS_TEST_NATIVE_DLL);
const fs::path kProgram(GS_EXTHOST_PROGRAM);

class ApplicationScope
{
public:
    ApplicationScope()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_application = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

    ApplicationScope(const ApplicationScope&) = delete;
    ApplicationScope& operator=(const ApplicationScope&) = delete;

private:
    int m_argc = 1;
    std::string m_name = "gs_app_tests";
    std::array<char*, 2> m_argv{m_name.data(), nullptr};
    std::unique_ptr<QCoreApplication> m_application;
};

bool eventually(const std::function<bool()>& done)
{
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

std::string utf8(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

exthost::HostedOptions hostedOptions(const Scratch& scratch)
{
    exthost::HostedOptions options;
    options.program = kProgram;
    options.dll = kTestDll;
    options.store = scratch / "store/gstest.native.json";
    return options;
}

struct Seen {
    int frames = 0;
    exthost::PixelFormat format = exthost::PixelFormat::None;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::array<std::uint8_t, 4> pixel{};
    std::uint8_t chromaBlue = 0;
    std::uint8_t chromaRed = 0;
    std::size_t sound = 0;
    std::vector<float> firstSound;
    std::optional<std::string> ended;
};

Seen drain(exthost::MediaStream& stream, int stopAfterFrames = 0)
{
    Seen seen;
    std::stop_source stop;
    std::jthread watchdog([&stop](const std::stop_token& token) {
        std::mutex mutex;
        std::condition_variable_any idle;
        std::unique_lock lock(mutex);
        static_cast<void>(idle.wait_for(lock, token, 20s, [] { return false; }));
        stop.request_stop();
    });
    while (stream.wait(stop.get_token())) {
        while (std::optional<exthost::SoundBlock> block = stream.sound()) {
            if (seen.firstSound.empty()) {
                seen.firstSound = block->samples;
            }
            seen.sound += block->samples.size();
        }
        if (const std::optional<exthost::FrameView> frame = stream.frame()) {
            ++seen.frames;
            seen.format = frame->format;
            seen.width = frame->width;
            seen.height = frame->height;
            if (frame->format == exthost::PixelFormat::Bgra) {
                const std::uint8_t* pixel = frame->data + std::size_t{7} * frame->stride + std::size_t{5} * 4;
                seen.pixel = {pixel[0], pixel[1], pixel[2], pixel[3]};
            } else {
                seen.pixel = {frame->data[std::size_t{7} * frame->stride + 5], 0, 0, 0};
                seen.chromaBlue = frame->chroma[std::size_t{3} * frame->chromaStride + 4];
                seen.chromaRed = frame->chroma[std::size_t{3} * frame->chromaStride + 5];
            }
            if (stopAfterFrames > 0 && seen.frames >= stopAfterFrames) {
                break;
            }
        }
        if (std::optional<std::string> why = stream.ended()) {
            seen.ended = std::move(why);
            break;
        }
    }
    return seen;
}

void checkPictures(exthost::Module& module, bool hosted)
{
    REQUIRE(module.info().videoSources.size() == 1);
    CHECK(module.info().videoSources[0].name == "bars");
    const exthost::CallResult listed = module.listSources(exthost::MediaKind::Video, 0);
    REQUIRE_MESSAGE(listed.ok(), listed.problem);
    const nlohmann::json pictures = nlohmann::json::parse(listed.value.text);
    REQUIRE(pictures.size() == 2);
    CHECK(pictures[0]["id"] == "bars");
    CHECK(pictures[0]["name"] == "Test bars");

    const std::shared_ptr<exthost::MediaStream> bars =
        module.openSource(exthost::MediaKind::Video, 0, "bars", R"({"frames":3,"pause":20,"width":64,"height":48})");
    REQUIRE(bars != nullptr);
    const Seen shown = drain(*bars);
    CHECK(shown.frames >= 1);
    CHECK(shown.format == exthost::PixelFormat::Bgra);
    CHECK(shown.width == 64);
    CHECK(shown.height == 48);
    CHECK(shown.pixel == std::array<std::uint8_t, 4>{5, 7, 80, 255});
    CHECK(shown.ended == std::optional<std::string>("done"));
    module.closeSource(bars->id());

    const std::shared_ptr<exthost::MediaStream> planar = module.openSource(
        exthost::MediaKind::Video, 0, "bars", R"({"frames":2,"pause":20,"width":32,"height":16,"format":"nv12"})");
    REQUIRE(planar != nullptr);
    const Seen luma = drain(*planar);
    CHECK(luma.format == exthost::PixelFormat::Nv12);
    CHECK(luma.width == 32);
    CHECK(luma.height == 16);
    CHECK(luma.pixel[0] == 5 + 7 + 1);
    CHECK(luma.chromaBlue == 101);
    CHECK(luma.chromaRed == 150);
    module.closeSource(planar->id());

    const std::shared_ptr<exthost::MediaStream> endless =
        module.openSource(exthost::MediaKind::Video, 0, "bars", R"({"frames":0,"pause":5})");
    REQUIRE(endless != nullptr);
    CHECK(drain(*endless, 2).frames == 2);
    module.closeSource(endless->id());
    CHECK(endless->ended().has_value());

    const std::shared_ptr<exthost::MediaStream> unknown =
        module.openSource(exthost::MediaKind::Video, 0, "nothing", "{}");
    if (hosted) {
        REQUIRE(unknown != nullptr);
        CHECK(drain(*unknown).ended == std::optional<std::string>("the source did not open"));
        module.closeSource(unknown->id());
    } else {
        CHECK(unknown == nullptr);
    }
    CHECK(module.openSource(exthost::MediaKind::Video, 0, "", "{}") == nullptr);
    CHECK(module.openSource(exthost::MediaKind::Video, 3, "bars", "{}") == nullptr);
}

void checkSound(exthost::Module& module)
{
    REQUIRE(module.info().audioSources.size() == 1);
    const exthost::CallResult listed = module.listSources(exthost::MediaKind::Audio, 0);
    REQUIRE_MESSAGE(listed.ok(), listed.problem);
    CHECK(nlohmann::json::parse(listed.value.text)[0]["id"] == "bars");
    const std::shared_ptr<exthost::MediaStream> tone =
        module.openSource(exthost::MediaKind::Audio, 0, "bars", R"({"blocks":3,"pause":5})");
    REQUIRE(tone != nullptr);
    const Seen heard = drain(*tone);
    CHECK(heard.sound == std::size_t{3} * 480 * 2);
    REQUIRE(heard.firstSound.size() == std::size_t{480} * 2);
    CHECK(heard.firstSound[2] == doctest::Approx(1.0 / 32768.0));
    CHECK(heard.firstSound[3] == doctest::Approx(-1.0 / 32768.0));
    CHECK(heard.ended == std::optional<std::string>("done"));
    module.closeSource(tone->id());
}

std::optional<std::size_t> nodeIndex(const exthost::Module& module, std::string_view name)
{
    const std::vector<exthost::NodeInfo>& nodes = module.info().nodes;
    const auto found = std::ranges::find(nodes, name, &exthost::NodeInfo::name);
    return found == nodes.end() ? std::nullopt
                                : std::optional<std::size_t>(static_cast<std::size_t>(found - nodes.begin()));
}

nlohmann::json sinkReport(exthost::Module& module)
{
    const std::optional<std::size_t> index = nodeIndex(module, "sinkReport");
    REQUIRE(index.has_value());
    const exthost::CallResult result = module.call(*index, {});
    REQUIRE_MESSAGE(result.ok(), result.problem);
    return nlohmann::json::parse(result.value.text);
}

void refuseFrames(exthost::Module& module, bool refuse)
{
    const std::optional<std::size_t> index = nodeIndex(module, "refuseFrames");
    REQUIRE(index.has_value());
    exthost::Value flag;
    flag.type = exthost::ValueType::Boolean;
    flag.flag = refuse;
    const std::vector<exthost::Value> values{flag};
    REQUIRE(module.call(*index, values).ok());
}

std::vector<std::uint8_t> frameOf(std::uint32_t width, std::uint32_t height)
{
    std::vector<std::uint8_t> pixels(std::size_t{width} * height * 4, 0);
    const auto put = [&](std::uint32_t x, std::uint32_t y, std::array<std::uint8_t, 4> color) {
        std::copy(color.begin(), color.end(), pixels.begin() + static_cast<std::ptrdiff_t>((y * width + x) * 4));
    };
    put(0, 0, {1, 2, 3, 255});
    put(width / 2, height / 2, {9, 8, 7, 255});
    return pixels;
}

void checkSink(exthost::Module& module)
{
    REQUIRE(module.info().sinks.size() == 1);
    CHECK(module.info().sinks[0].name == "collector");
    CHECK(module.info().sinks[0].video);
    CHECK(module.info().sinks[0].audio);
    const nlohmann::json before = sinkReport(module);
    const exthost::SinkStart start{.device = "phone-1", .name = "Phone", .width = 64, .height = 48, .fps = 30,
                                   .audio = true};
    const std::shared_ptr<exthost::SinkPort> port = module.openSink(0, start);
    REQUIRE(port != nullptr);
    const std::vector<std::uint8_t> pixels = frameOf(64, 48);
    const exthost::FrameView frame{.format = exthost::PixelFormat::Bgra,
                                   .width = 64,
                                   .height = 48,
                                   .stride = 64 * 4,
                                   .chromaStride = 0,
                                   .data = pixels.data(),
                                   .chroma = nullptr,
                                   .timeUs = 1};
    const std::vector<float> samples(960, 0.25F);
    const exthost::SoundView sound{.format = exthost::SampleFormat::F32,
                                   .channels = 2,
                                   .rate = exthost::kSoundRate,
                                   .frames = 480,
                                   .data = samples.data(),
                                   .timeUs = 1};
    CHECK(port->frame(frame));
    CHECK(port->sound(sound));
    CHECK_FALSE(port->frame(exthost::FrameView{.format = exthost::PixelFormat::Bgra,
                                               .width = 32,
                                               .height = 48,
                                               .stride = 32 * 4,
                                               .chromaStride = 0,
                                               .data = pixels.data(),
                                               .chroma = nullptr,
                                               .timeUs = 2}));
    REQUIRE(eventually([&] {
        const nlohmann::json now = sinkReport(module);
        return now["frames"].get<int>() > before["frames"].get<int>() &&
               now["audioFrames"].get<int>() >= before["audioFrames"].get<int>() + 480;
    }));
    const nlohmann::json during = sinkReport(module);
    CHECK(during["opened"].get<int>() == before["opened"].get<int>() + 1);
    CHECK(during["width"] == 64);
    CHECK(during["height"] == 48);
    CHECK(during["fps"] == 30);
    CHECK(during["channels"] == 2);
    CHECK(during["device"] == "phone-1");
    CHECK(during["name"] == "Phone");
    CHECK(during["corner"] == nlohmann::json::array({1, 2, 3, 255}));
    CHECK(during["centre"] == nlohmann::json::array({9, 8, 7, 255}));
    CHECK_FALSE(port->ended().has_value());

    refuseFrames(module, true);
    CHECK(port->frame(frame));
    REQUIRE(eventually([&] { return port->ended().has_value(); }));
    CHECK(*port->ended() == "the extension did not take the picture");
    refuseFrames(module, false);
    module.closeSink(port->id());
    REQUIRE(eventually([&] { return sinkReport(module)["closed"].get<int>() == before["closed"].get<int>() + 1; }));
    CHECK_FALSE(port->frame(frame));
}

struct Exported {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<double> fractions;
    std::optional<std::pair<bool, std::string>> done;

    exthost::JobEvents events()
    {
        return {.progress =
                    [this](double fraction, std::string) {
                        const std::scoped_lock lock(mutex);
                        fractions.push_back(fraction);
                    },
                .log = {},
                .done =
                    [this](bool worked, std::string text) {
                        {
                            const std::scoped_lock lock(mutex);
                            done.emplace(worked, std::move(text));
                        }
                        changed.notify_all();
                    }};
    }

    std::pair<bool, std::string> wait()
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, 20s, [this] { return done.has_value(); }));
        return *done;
    }
};

void checkExporter(exthost::Module& module, const Scratch& scratch)
{
    REQUIRE(module.info().exporters.size() == 1);
    CHECK(module.info().exporters[0].name == "copy");
    const fs::path input = scratch / "recording.mp4";
    const fs::path output = scratch / "out/recording.gif";
    writeFile(input, "abc");
    fs::create_directories(output.parent_path());
    Exported copied;
    REQUIRE(module.startExport(0, utf8(input), utf8(output), copied.events()) != 0);
    CHECK(copied.wait() == std::pair<bool, std::string>{true, R"({"bytes":3})"});
    CHECK(readFile(output) == "exported:abc");
    {
        const std::scoped_lock lock(copied.mutex);
        CHECK(copied.fractions == std::vector<double>{0.5});
    }

    Exported missing;
    REQUIRE(module.startExport(0, utf8(scratch / "nothing.mp4"), utf8(output), missing.events()) != 0);
    CHECK(missing.wait() == std::pair<bool, std::string>{false, "the input cannot be read"});
    CHECK(module.startExport(1, utf8(input), utf8(output), {}) == 0);
    CHECK(module.startExport(0, "", utf8(output), {}) == 0);
}

std::shared_ptr<media::LumaSnapshot> snapshot(int width, int height)
{
    auto shot = std::make_shared<media::LumaSnapshot>();
    shot->width = width;
    shot->height = height;
    shot->luma.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::size_t index = 0; index < shot->luma.size(); ++index) {
        shot->luma[index] = static_cast<std::uint8_t>(index % 256);
    }
    shot->chroma.assign(std::size_t{2} * static_cast<std::size_t>(shot->chromaWidth()) *
                            static_cast<std::size_t>(shot->chromaHeight()),
                        128);
    shot->matrix.rows = {{{1.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}}};
    return shot;
}

class Collector final : public capture::SampleSink
{
public:
    void offered(const std::vector<capture::CaptureFormat>&, const std::optional<capture::CaptureFormat>& chosen) override
    {
        const std::scoped_lock lock(mutex);
        if (chosen) {
            formats.push_back(*chosen);
        }
    }

    void sample(const capture::CaptureSample& sample) override
    {
        const std::scoped_lock lock(mutex);
        ++samples;
        kind = sample.kind;
        width = sample.width;
        height = sample.height;
        if (sample.kind == capture::SampleKind::Bgra &&
            sample.bytes.size() >= static_cast<std::size_t>(sample.pitch) * 8) {
            const std::uint8_t* at = sample.bytes.data() + 7 * sample.pitch + 5 * 4;
            pixel = {at[0], at[1], at[2], at[3]};
        }
    }

    void frame(media::VideoFrame) override {}

    void ended(const std::string& problem) override
    {
        const std::scoped_lock lock(mutex);
        stopped = problem;
    }

    void heard(std::span<const float> interleaved, int channels, int sampleRate, std::int64_t) override
    {
        const std::scoped_lock lock(mutex);
        sound += interleaved.size();
        heardChannels = channels;
        heardRate = sampleRate;
    }

    std::mutex mutex;
    std::vector<capture::CaptureFormat> formats;
    int samples = 0;
    capture::SampleKind kind = capture::SampleKind::Nv12;
    int width = 0;
    int height = 0;
    std::array<std::uint8_t, 4> pixel{};
    std::optional<std::string> stopped;
    std::size_t sound = 0;
    int heardChannels = 0;
    int heardRate = 0;
};

app::NativeWanted mediaWanted(const fs::path& folder)
{
    app::NativeWanted wanted;
    wanted.id = "gstest.native";
    wanted.version = "1.0.0";
    wanted.name = QStringLiteral("Test native");
    wanted.directory = folder;
    wanted.official = true;
    wanted.native.dll = "native.dll";
    wanted.native.nodes = {{.name = "sinkReport", .params = {}, .returns = "json", .description = {}, .permissions = {}},
                           {.name = "refuseFrames",
                            .params = {{"on", "boolean"}},
                            .returns = "boolean",
                            .description = {},
                            .permissions = {}}};
    wanted.native.videoSources = {{.name = "bars", .title = "Bars", .kinds = {"phone"}}};
    wanted.native.audioSources = {{.name = "tone", .title = "Tone"}};
    wanted.native.mediaSinks = {
        {.name = "collector", .title = "Collector", .video = ext::SinkVideo{.fps = 30, .width = 64, .height = 48},
         .audio = true}};
    wanted.native.recordingExporters = {{.name = "copy", .title = "Copy", .extension = "gif"}};
    return wanted;
}

}

TEST_CASE("ADR-0112: a DLL's picture source hands BGRA and NV12 frames through a ring inside GameScrcpy, and says when "
          "it has nothing more to show")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::InProcessModule> module =
        exthost::InProcessModule::load({.dll = kTestDll, .store = scratch / "store.json"}, problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    checkPictures(*module, false);
    checkSound(*module);
}

TEST_CASE("ADR-0112: a DLL in gs-exthost.exe hands its pictures and sound through shared memory the same way")
{
    Scratch scratch;
    std::string problem;
    const std::unique_ptr<exthost::HostedModule> module = exthost::HostedModule::start(hostedOptions(scratch), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    REQUIRE(module->waitReady(20s));
    checkPictures(*module, true);
    checkSound(*module);
}

TEST_CASE("ADR-0112: a media sink gets frames and sound of the declared size, and a sink that refuses a frame stops, "
          "in GameScrcpy and in gs-exthost.exe")
{
    Scratch scratch;
    std::string problem;
    {
        const std::unique_ptr<exthost::InProcessModule> inside =
            exthost::InProcessModule::load({.dll = kTestDll, .store = scratch / "store.json"}, problem);
        REQUIRE_MESSAGE(inside != nullptr, problem);
        checkSink(*inside);
    }
    const std::unique_ptr<exthost::HostedModule> outside = exthost::HostedModule::start(hostedOptions(scratch), problem);
    REQUIRE_MESSAGE(outside != nullptr, problem);
    REQUIRE(outside->waitReady(20s));
    checkSink(*outside);
}

TEST_CASE("ADR-0112: a recording exporter is a job that writes the file the user chose, in GameScrcpy and in "
          "gs-exthost.exe")
{
    Scratch scratch;
    std::string problem;
    {
        const std::unique_ptr<exthost::InProcessModule> inside =
            exthost::InProcessModule::load({.dll = kTestDll, .store = scratch / "store.json"}, problem);
        REQUIRE_MESSAGE(inside != nullptr, problem);
        checkExporter(*inside, scratch);
    }
    const std::unique_ptr<exthost::HostedModule> outside = exthost::HostedModule::start(hostedOptions(scratch), problem);
    REQUIRE_MESSAGE(outside != nullptr, problem);
    REQUIRE(outside->waitReady(20s));
    checkExporter(*outside, scratch);
}

TEST_CASE("ADR-0112: a hosted source ends when gs-exthost.exe stops, and a DLL says its sources changed")
{
    Scratch scratch;
    std::string problem;
    std::mutex mutex;
    std::vector<std::string> changed;
    exthost::HostedOptions options = hostedOptions(scratch);
    options.sourcesChanged = [&](const std::string& source) {
        const std::scoped_lock lock(mutex);
        changed.push_back(source);
    };
    const std::unique_ptr<exthost::HostedModule> module = exthost::HostedModule::start(std::move(options), problem);
    REQUIRE_MESSAGE(module != nullptr, problem);
    REQUIRE(module->waitReady(20s));
    const std::optional<std::size_t> change = nodeIndex(*module, "changeSources");
    REQUIRE(change.has_value());
    CHECK(module->call(*change, {}).value.number == 0.0);
    REQUIRE(eventually([&] {
        const std::scoped_lock lock(mutex);
        return changed == std::vector<std::string>{"bars"};
    }));

    const std::shared_ptr<exthost::MediaStream> bars =
        module->openSource(exthost::MediaKind::Video, 0, "bars", R"({"frames":0,"pause":5})");
    REQUIRE(bars != nullptr);
    CHECK(drain(*bars, 1).frames == 1);
    const std::optional<std::size_t> crash = nodeIndex(*module, "crash");
    REQUIRE(crash.has_value());
    CHECK(module->call(*crash, {}).status != exthost::CallStatus::Ok);
    CHECK(drain(*bars).ended == std::optional<std::string>("the extension's process stopped"));
}

TEST_CASE("ADR-0112: pictures and sound are checked against their limits, and a sink's port says when its extension "
          "stopped")
{
    std::shared_ptr<exthost::SinkPort> port;
    {
        std::string problem;
        Scratch scratch;
        const std::unique_ptr<exthost::InProcessModule> module =
            exthost::InProcessModule::load({.dll = kTestDll, .store = scratch / "store.json"}, problem);
        REQUIRE_MESSAGE(module != nullptr, problem);
        port = module->openSink(0, {.device = "x", .name = "x", .width = 0, .height = 0, .fps = 0, .audio = true});
        REQUIRE(port != nullptr);
        const std::vector<float> samples(2 * exthost::kMostSoundFrames, 0.5F);
        const exthost::SoundView most{.format = exthost::SampleFormat::F32,
                                      .channels = 2,
                                      .rate = exthost::kSoundRate,
                                      .frames = exthost::kMostSoundFrames,
                                      .data = samples.data(),
                                      .timeUs = 3};
        CHECK(port->sound(most));
        CHECK_FALSE(port->frame(exthost::FrameView{}));
        CHECK(exthost::checkSound(exthost::SoundView{.format = exthost::SampleFormat::F32,
                                                     .channels = 9,
                                                     .rate = exthost::kSoundRate,
                                                     .frames = 1,
                                                     .data = samples.data(),
                                                     .timeUs = 0})
                  .find("8 channels") != std::string::npos);
        CHECK_FALSE(exthost::checkSound(exthost::SoundView{.format = exthost::SampleFormat::S16,
                                                           .channels = 1,
                                                           .rate = 44'100,
                                                           .frames = 1,
                                                           .data = samples.data(),
                                                           .timeUs = 0})
                        .empty());
    }
    REQUIRE(port->ended().has_value());
    CHECK(*port->ended() == "the extension stopped");
    CHECK_FALSE(port->sound(exthost::SoundView{}));

    const std::uint8_t pixel[16] = {};
    CHECK(exthost::checkFrame({.format = exthost::PixelFormat::Nv12,
                               .width = 3,
                               .height = 2,
                               .stride = 4,
                               .chromaStride = 4,
                               .data = pixel,
                               .chroma = pixel,
                               .timeUs = 0})
              .find("even") != std::string::npos);
    CHECK_FALSE(exthost::checkFrame({.format = exthost::PixelFormat::Bgra,
                                     .width = 4097,
                                     .height = 2,
                                     .stride = 4097 * 4,
                                     .chromaStride = 0,
                                     .data = pixel,
                                     .chroma = nullptr,
                                     .timeUs = 0})
                    .empty());
    CHECK(exthost::packedBytes(exthost::PixelFormat::Nv12, 4, 2) == 12);
    CHECK(exthost::packedBytes(exthost::PixelFormat::Bgra, 4, 2) == 32);
    CHECK(exthost::validSourceId("bars"));
    CHECK_FALSE(exthost::validSourceId(""));
    CHECK_FALSE(exthost::validSourceId(std::string(65, 'a')));
}

TEST_CASE("ADR-0112: extension pictures join the list of capture cards only with media.capture, play the sound of the "
          "same id, and leave it when the extension no longer lists them")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = mediaWanted(folder);
    registry.apply({wanted});
    REQUIRE(registry.loaded("gstest.native"));
    CHECK(registry.mediaSources().empty());
    CHECK(registry.mediaSinks().empty());
    CHECK(registry.exporters().empty());

    wanted.granted = {"media.capture"};
    registry.apply({wanted});
    const std::vector<app::MediaSourceDevice> sources = registry.mediaSources();
    REQUIRE(sources.size() == 2);
    const app::MediaSourceDevice& video = sources[0];
    CHECK(video.kind == exthost::MediaKind::Video);
    CHECK(video.source == "bars");
    CHECK(video.sourceTitle == "Bars");
    CHECK(video.id == "bars");
    CHECK(video.name == "Test bars");
    CHECK(video.kinds == std::vector<std::string>{"phone"});
    const std::optional<app::MediaSourceDevice> sound = app::pairedSound(sources, video);
    REQUIRE(sound.has_value());
    CHECK(sound->source == "tone");
    CHECK(sound->name == "Test tone");

    const QString serial = app::extensionSourceSerial(video);
    CHECK(app::isExtensionSourceSerial(serial));
    CHECK_FALSE(app::isExtensionSourceSerial(QStringLiteral("capture-1234")));
    const app::SourcePlan fresh = app::planExtensionSources(sources, {QStringLiteral("capture-1234")});
    REQUIRE(fresh.added.size() == 1);
    CHECK(app::extensionSourceSerial(fresh.added[0]) == serial);
    CHECK(fresh.removed == std::vector<QString>{QStringLiteral("capture-1234")});
    const app::SourcePlan kept = app::planExtensionSources(sources, {serial});
    CHECK(kept.added.empty());
    CHECK(kept.removed.empty());
    CHECK(app::planExtensionSources({}, {serial}).removed == std::vector<QString>{serial});

    Collector collector;
    app::ExtensionVideoSource source(
        [&registry, video, sound](exthost::MediaKind kind,
                                   const std::string& params) -> std::unique_ptr<app::SourceLease> {
            return registry.openSource(kind == exthost::MediaKind::Video ? video : *sound, params);
        },
        [] { return std::string("phone-1"); });
    REQUIRE(source.start(collector, {}));
    CHECK(source.givesSound());
    REQUIRE(eventually([&] {
        const std::scoped_lock lock(collector.mutex);
        return collector.samples >= 2 && collector.sound >= 960;
    }));
    {
        const std::scoped_lock lock(collector.mutex);
        CHECK(collector.kind == capture::SampleKind::Bgra);
        CHECK(collector.width == 64);
        CHECK(collector.height == 48);
        REQUIRE_FALSE(collector.formats.empty());
        CHECK(collector.formats.back().width == 64);
        CHECK(collector.formats.back().height == 48);
        CHECK(collector.pixel[0] == 5);
        CHECK(collector.pixel[1] == 7);
        CHECK(collector.pixel[3] == 255);
        CHECK(collector.heardChannels == 2);
        CHECK(collector.heardRate == 48'000);
    }
    source.stop();
    {
        const std::scoped_lock lock(collector.mutex);
        CHECK_FALSE(collector.stopped.has_value());
    }

    wanted.granted = {};
    registry.apply({wanted});
    CHECK(registry.mediaSources().empty());
    CHECK_FALSE(source.start(collector, {}));
    CHECK_FALSE(source.problem().empty());
}

TEST_CASE("ADR-0112: a mirror window is sent to a sink from its More menu with its picture letterboxed and its sound, "
          "and the sending stops with the window")
{
    ApplicationScope application;
    Scratch scratch;
    const fs::path folder = scratch / "gstest.native/1.0.0";
    fs::create_directories(folder);
    fs::copy_file(kTestDll, folder / "native.dll");
    app::NativeExtensions registry({.program = kProgram, .storeRoot = scratch / ".store", .clock = nullptr});
    app::NativeWanted wanted = mediaWanted(folder);
    wanted.granted = {"media.output"};
    registry.apply({wanted});
    REQUIRE(registry.loaded("gstest.native"));
    const auto report = [&registry] {
        return nlohmann::json::parse(registry.call("gstest.native", "sinkReport", {}).value.text);
    };
    const std::vector<app::ExporterOffer> exporters = registry.exporters();
    REQUIRE(exporters.size() == 1);
    CHECK(exporters[0].fileExtension == "gif");
    CHECK(exporters[0].title == "Copy");

    QVariantList windows{QVariantMap{{QStringLiteral("serial"), QStringLiteral("phone-1")},
                                     {QStringLiteral("name"), QStringLiteral("Phone")}}};
    media::AudioTap tap;
    int listens = 0;
    app::MediaSinks sinks(registry,
                          {.windows = [&windows] { return windows; },
                           .camera = [](const QString&) -> app::MediaSinks::Camera {
                               const std::shared_ptr<const media::LumaSnapshot> shot = snapshot(32, 16);
                               return [shot] { return shot; };
                           },
                           .listen = [&tap, &listens](const QString&, media::AudioTap given) {
                               ++listens;
                               tap = std::move(given);
                               return true;
                           }});
    REQUIRE(sinks.sinks().size() == 1);
    const QVariantMap offered = sinks.sinks()[0].toMap();
    const QString key = offered.value(QStringLiteral("key")).toString();
    CHECK(key == "gstest.native/collector");
    CHECK(offered.value(QStringLiteral("title")).toString() == "Collector");
    CHECK(offered.value(QStringLiteral("picture")).toBool());
    CHECK(offered.value(QStringLiteral("sound")).toBool());
    CHECK_FALSE(sinks.toggle(QStringLiteral("phone-2"), key).isEmpty());
    CHECK_FALSE(sinks.toggle(QStringLiteral("phone-1"), QStringLiteral("gstest.native/nothing")).isEmpty());

    const nlohmann::json before = report();
    CHECK(sinks.toggle(QStringLiteral("phone-1"), key).isEmpty());
    CHECK(sinks.sending(QStringLiteral("phone-1"), key));
    CHECK(sinks.running().size() == 1);
    REQUIRE(tap);
    const std::vector<float> samples(960, 0.25F);
    tap(samples, 2, 48'000, 0);
    tap(samples, 2, 44'100, 0);
    REQUIRE(eventually([&] {
        const nlohmann::json now = report();
        return now["frames"].get<int>() > before["frames"].get<int>() &&
               now["audioFrames"].get<int>() == before["audioFrames"].get<int>() + 480;
    }));
    const nlohmann::json during = report();
    CHECK(during["width"] == 64);
    CHECK(during["height"] == 48);
    CHECK(during["fps"] == 30);
    CHECK(during["device"] == "phone-1");
    CHECK(during["name"] == "Phone");
    CHECK(during["corner"] == nlohmann::json::array({0, 0, 0, 255}));
    CHECK(during["centre"] == nlohmann::json::array({16, 16, 16, 255}));

    windows.clear();
    sinks.windowsChanged();
    CHECK_FALSE(sinks.sending(QStringLiteral("phone-1"), key));
    CHECK(sinks.running().isEmpty());
    CHECK_FALSE(tap);
    CHECK(listens == 2);
    CHECK(report()["closed"].get<int>() == before["closed"].get<int>() + 1);

    windows.append(QVariantMap{{QStringLiteral("serial"), QStringLiteral("phone-1")},
                               {QStringLiteral("name"), QStringLiteral("Phone")}});
    std::vector<QString> stopped;
    QObject::connect(&sinks, &app::MediaSinks::stopped,
                     [&stopped](const QString&, const QString& text) { stopped.push_back(text); });
    REQUIRE(registry.call("gstest.native", "refuseFrames", {script::NodeValue{.type = script::NodeType::Boolean,
                                                                               .flag = true}})
                .code.empty());
    CHECK(sinks.toggle(QStringLiteral("phone-1"), key).isEmpty());
    REQUIRE(eventually([&] { return !stopped.empty(); }));
    CHECK(stopped[0] == "Stopped sending to Collector: the extension did not take the picture");
    CHECK(sinks.running().isEmpty());
    REQUIRE(registry.call("gstest.native", "refuseFrames", {script::NodeValue{.type = script::NodeType::Boolean,
                                                                               .flag = false}})
                .code.empty());

    CHECK(sinks.toggle(QStringLiteral("phone-1"), key).isEmpty());
    wanted.granted = {};
    registry.apply({wanted});
    CHECK(sinks.sinks().isEmpty());
    CHECK(sinks.running().isEmpty());
}

TEST_CASE("ADR-0112: a window is letterboxed into the size a sink asks for, with black bars")
{
    const std::shared_ptr<media::LumaSnapshot> shot = snapshot(4, 2);
    std::vector<std::uint8_t> out;
    app::letterboxBgra(*shot, 8, 8, out);
    REQUIRE(out.size() == std::size_t{8} * 8 * 4);
    const auto at = [&out](std::size_t x, std::size_t y) {
        const std::uint8_t* pixel = out.data() + (y * 8 + x) * 4;
        return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
    };
    CHECK(at(0, 0) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
    CHECK(at(7, 1) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
    CHECK(at(0, 2) == std::array<std::uint8_t, 4>{0, 0, 0, 255});
    CHECK(at(7, 5) == std::array<std::uint8_t, 4>{7, 7, 7, 255});
    CHECK(at(2, 4) == std::array<std::uint8_t, 4>{5, 5, 5, 255});
    CHECK(at(0, 6) == std::array<std::uint8_t, 4>{0, 0, 0, 255});

    media::LumaSnapshot grey;
    app::letterboxBgra(grey, 2, 2, out);
    CHECK(out == std::vector<std::uint8_t>{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255});
}
