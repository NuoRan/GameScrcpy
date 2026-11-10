#include "ControlChannel.h"
#include "DemoFeed.h"

#include <gs/protocol/Codec.h>
#include <gs/record/MediaFile.h>
#include <gs/record/Recorder.h>
#include <gs/render/GraphicsDevice.h>

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

class AcceptingSink final : public gs::app::ControlSink
{
public:
    bool send(const gs::protocol::TouchFrame&) override { return true; }
    bool send(const gs::protocol::Key&) override { return true; }
    bool send(const gs::protocol::Scroll&) override { return true; }
    bool send(const gs::protocol::Text& text) override
    {
        texts.push_back(text.text);
        return true;
    }

    std::vector<std::string> texts;
};

class Watcher final : public gs::app::OperationObserver
{
public:
    void touched(const gs::protocol::TouchFrame& frame) override { touches += static_cast<int>(frame.points.size()); }
    void keyed(const gs::protocol::Key& key) override { keys.push_back(key.keycode); }
    void scrolled(const gs::protocol::Scroll& scroll) override { scrolls.push_back(scroll.vScroll); }
    void typed(std::size_t characters) override { lengths.push_back(characters); }

    int touches = 0;
    std::vector<std::uint32_t> keys;
    std::vector<int> scrolls;
    std::vector<std::size_t> lengths;
};

class Folder
{
public:
    Folder()
        : m_root(std::filesystem::temp_directory_path()
                 / ("gs_recording_source_" + std::to_string(std::random_device{}()) + std::to_string(std::random_device{}())))
    {
        std::filesystem::create_directories(m_root);
    }

    ~Folder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_root; }

private:
    std::filesystem::path m_root;
};

}

TEST_CASE("the control channel tells a recording what it sent, and of typed text only how many characters")
{
    gs::app::ControlChannel channel;
    AcceptingSink sink;
    channel.attach(sink);
    channel.setVideo(1, 1080, 2400);
    const auto watcher = std::make_shared<Watcher>();
    channel.setOperationObserver(watcher);
    gs::protocol::TouchFrame frame;
    frame.videoSession = 1;
    frame.points.push_back(gs::protocol::TouchPoint{.action = gs::protocol::TouchAction::Down, .x = 100, .y = 200});
    CHECK(channel.send(frame));
    gs::protocol::Key key;
    key.keycode = 4;
    CHECK(channel.send(key));
    CHECK(channel.scroll({0.5, 0.5}, 0, -1));
    CHECK(channel.type("h\xC3\xA9llo"));
    CHECK(watcher->touches == 1);
    CHECK(watcher->keys == std::vector<std::uint32_t>{4});
    CHECK(watcher->scrolls == std::vector<int>{-1});
    CHECK(watcher->lengths == std::vector<std::size_t>{5});
    CHECK(sink.texts == std::vector<std::string>{"h\xC3\xA9llo"});
    channel.setOperationObserver(nullptr);
    CHECK(channel.type("more"));
    CHECK(watcher->lengths.size() == 1);
    channel.detach();
    channel.setOperationObserver(watcher);
    CHECK_FALSE(channel.send(key));
    CHECK(watcher->keys.size() == 1);
}

TEST_CASE("the demo device records what it plays as a recording that the demo device plays again")
{
    const auto graphics = gs::render::GraphicsDevice::create();
    REQUIRE(graphics);
    const Folder root;
    auto started = gs::record::Recorder::start(gs::record::RecorderOptions{.folder = root.path() / "demo.gsrec"});
    REQUIRE(started);
    const std::shared_ptr<gs::record::Recorder> recorder = *started;
    gs::app::DemoFeed feed(*graphics, std::filesystem::path(GS_VIDEO_TEST_DATA_DIR) / "emulator_h265.media");
    feed.setRecorder(recorder);
    REQUIRE(feed.start());
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!recorder->hasVideo() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(recorder->hasVideo());
    std::this_thread::sleep_for(400ms);
    feed.setRecorder(nullptr);
    feed.stop();
    const gs::record::RecorderSummary summary = recorder->stop();
    CHECK(summary.video);
    CHECK(summary.problem.empty());
    CHECK(summary.videoSessions >= 1);

    gs::record::MediaReader reader(root.path() / "demo.gsrec" / "video.media", gs::protocol::MediaChannel::Video);
    REQUIRE(reader.opened());
    std::vector<gs::protocol::MediaHeader> headers;
    while (const auto packet = reader.next()) {
        headers.push_back(packet->header);
    }
    CHECK_FALSE(reader.truncated());
    REQUIRE(headers.size() >= 4);
    CHECK(headers[0].kind == gs::protocol::MediaKind::Session);
    CHECK(headers[1].kind == gs::protocol::MediaKind::Config);
    CHECK(headers[2].kind == gs::protocol::MediaKind::Frame);
    CHECK(headers[2].keyFrame);
    std::optional<std::int64_t> previous;
    for (const gs::protocol::MediaHeader& header : headers) {
        if (header.kind != gs::protocol::MediaKind::Frame) {
            continue;
        }
        if (previous) {
            CHECK(header.ptsUs > *previous);
        }
        previous = header.ptsUs;
    }

    gs::app::DemoFeed again(*graphics, root.path() / "demo.gsrec" / "video.media");
    CHECK(again.start());
    CHECK(again.problem().isEmpty());
    again.stop();
}
