#pragma once

#include <gs/base/Clock.h>
#include <gs/script/Replay.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace gs::record {

class RecordedVision final : public script::ReplayVision
{
public:
    RecordedVision(std::filesystem::path folder, const std::filesystem::path& video, std::int64_t videoStartPtsUs);
    ~RecordedVision() override;
    RecordedVision(const RecordedVision&) = delete;
    RecordedVision& operator=(const RecordedVision&) = delete;

    [[nodiscard]] bool opened() const noexcept;
    [[nodiscard]] std::int64_t shownPtsUs() const noexcept;
    [[nodiscard]] script::VisionAnswer find(Microseconds at, const script::FindQuery& query) override;
    [[nodiscard]] script::VisionAnswer findColor(Microseconds at, const script::ColorQuery& query) override;
    [[nodiscard]] script::VisionAnswer pickColor(Microseconds at, const script::PixelQuery& query) override;
    [[nodiscard]] script::VisionAnswer capture(Microseconds at, const script::CaptureQuery& query) override;
    [[nodiscard]] script::VisionAnswer changed(Microseconds at, const script::ChangeQuery& query) override;
    [[nodiscard]] script::VisionAnswer readText(Microseconds at, const script::TextQuery& query) override;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

}
