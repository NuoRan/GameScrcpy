#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>

namespace gs::exthost::detail {

inline constexpr std::uint64_t kImageSectionStep = std::uint64_t{4} << 20;
inline constexpr std::uint64_t kMostImageSectionBytes = std::uint64_t{64} << 20;

class ImageSection
{
public:
    ImageSection() = default;
    ~ImageSection();
    ImageSection(const ImageSection&) = delete;
    ImageSection& operator=(const ImageSection&) = delete;

    [[nodiscard]] static std::shared_ptr<ImageSection> create(std::uint64_t bytes);
    [[nodiscard]] static std::shared_ptr<ImageSection> open(HANDLE mapping, std::uint64_t bytes);

    [[nodiscard]] HANDLE mapping() const noexcept { return m_mapping; }
    [[nodiscard]] std::uint64_t bytes() const noexcept { return m_bytes; }
    [[nodiscard]] std::uint8_t* data() const noexcept { return static_cast<std::uint8_t*>(m_view); }

private:
    HANDLE m_mapping = nullptr;
    void* m_view = nullptr;
    std::uint64_t m_bytes = 0;
};

}
