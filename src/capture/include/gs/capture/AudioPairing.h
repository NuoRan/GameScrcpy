#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::capture {

struct AudioEndpoint {
    std::wstring id;
    std::string name;
    std::wstring container;

    friend bool operator==(const AudioEndpoint&, const AudioEndpoint&) = default;
};

[[nodiscard]] bool sameContainer(std::wstring_view a, std::wstring_view b) noexcept;
[[nodiscard]] std::optional<std::size_t> pairedEndpoint(std::wstring_view videoContainer,
                                                        std::span<const AudioEndpoint> endpoints) noexcept;
[[nodiscard]] std::vector<AudioEndpoint> listCaptureEndpoints();

}
