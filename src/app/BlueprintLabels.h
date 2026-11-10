#pragma once

#include <gs/blueprint/Environment.h>

#include <string>
#include <string_view>

namespace gs::app {

class BlueprintLabels final : public blueprint::Labels
{
public:
    [[nodiscard]] std::string text(std::string_view key) const override;
    [[nodiscard]] std::string english(std::string_view key) const override;
    [[nodiscard]] std::string keyName(std::string_view key) const override;

    [[nodiscard]] static const char* source(std::string_view key) noexcept;
};

}
