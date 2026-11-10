#pragma once

#include "gs/capture/CaptureSource.h"

#include <memory>
#include <string>
#include <thread>

struct ID3D11Device;

namespace gs::capture {

class MediaFoundationSource final : public CaptureSource
{
public:
    MediaFoundationSource(std::wstring link, std::wstring container, ID3D11Device* device);
    ~MediaFoundationSource() override;
    MediaFoundationSource(const MediaFoundationSource&) = delete;
    MediaFoundationSource& operator=(const MediaFoundationSource&) = delete;

    [[nodiscard]] bool start(SampleSink& sink, const std::string& format) override;
    void stop() override;
    [[nodiscard]] std::string problem() const override;
    [[nodiscard]] std::wstring container() const override { return m_container; }

    struct Shared;

private:
    void run(const std::stop_token& stop, std::string format);

    const std::wstring m_link;
    const std::wstring m_container;
    ID3D11Device* const m_device;
    std::shared_ptr<Shared> m_shared;
    std::jthread m_thread;
};

}
