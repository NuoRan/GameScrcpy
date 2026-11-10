#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::app {

class ExtensionBisect
{
public:
    enum class Outcome : std::uint8_t { Running, Found, NotExtensions };

    [[nodiscard]] static ExtensionBisect begin(std::vector<std::string> enabled);
    [[nodiscard]] static std::optional<ExtensionBisect> load(std::string_view json);
    [[nodiscard]] std::string save() const;

    void answer(bool stillHappens);

    [[nodiscard]] Outcome outcome() const noexcept { return m_outcome; }
    [[nodiscard]] const std::vector<std::string>& original() const noexcept { return m_original; }
    [[nodiscard]] const std::vector<std::string>& suspects() const noexcept { return m_suspects; }
    [[nodiscard]] const std::vector<std::string>& trial() const noexcept { return m_trial; }
    [[nodiscard]] int step() const noexcept { return m_step; }
    [[nodiscard]] int steps() const noexcept;
    [[nodiscard]] bool loads(std::string_view id) const;
    [[nodiscard]] std::optional<std::string> found() const;

private:
    void split();

    std::vector<std::string> m_original;
    std::vector<std::string> m_suspects;
    std::vector<std::string> m_trial;
    int m_step = 0;
    Outcome m_outcome = Outcome::Running;
};

}
