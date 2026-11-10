#pragma once

#include <gs/capture/CaptureSource.h>
#include <gs/protocol/Codec.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ID3D11Device;

namespace gs::app {

class CaptureDemoSource final : public capture::CaptureSource
{
public:
    static constexpr int kOuterWidth = 1280;
    static constexpr int kOuterHeight = 720;

    CaptureDemoSource(ID3D11Device* device, std::filesystem::path recording);
    ~CaptureDemoSource() override;
    CaptureDemoSource(const CaptureDemoSource&) = delete;
    CaptureDemoSource& operator=(const CaptureDemoSource&) = delete;

    [[nodiscard]] bool start(capture::SampleSink& sink, const std::string& format) override;
    void stop() override;
    [[nodiscard]] std::string problem() const override;

private:
    [[nodiscard]] std::string load();
    void run(const std::stop_token& stop, capture::SampleSink& sink);
    void fail(std::string reason);

    ID3D11Device* const m_device;
    const std::filesystem::path m_recording;
    std::vector<std::uint8_t> m_bytes;
    std::vector<protocol::MediaPacket> m_packets;
    std::vector<std::int64_t> m_delaysUs;
    std::int64_t m_loopUs = 0;
    mutable std::mutex m_mutex;
    std::string m_problem;
    std::jthread m_player;
};

}
