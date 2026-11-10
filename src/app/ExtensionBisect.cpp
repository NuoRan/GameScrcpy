#include "ExtensionBisect.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <iterator>
#include <utility>

namespace gs::app {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::array<std::string_view, 3> kOutcomeNames{"running", "found", "notExtensions"};
constexpr std::size_t kMostExtensions = 1024;

std::optional<std::vector<std::string>> idsOf(const Json& node)
{
    if (!node.is_array() || node.size() > kMostExtensions) {
        return std::nullopt;
    }
    std::vector<std::string> ids;
    for (const Json& item : node) {
        if (!item.is_string()) {
            return std::nullopt;
        }
        ids.push_back(item.get<std::string>());
    }
    std::ranges::sort(ids);
    if (std::ranges::adjacent_find(ids) != ids.end()) {
        return std::nullopt;
    }
    return ids;
}

bool within(const std::vector<std::string>& part, const std::vector<std::string>& whole)
{
    return std::ranges::includes(whole, part);
}

}

ExtensionBisect ExtensionBisect::begin(std::vector<std::string> enabled)
{
    std::ranges::sort(enabled);
    enabled.erase(std::ranges::unique(enabled).begin(), enabled.end());
    ExtensionBisect bisect;
    bisect.m_original = std::move(enabled);
    bisect.m_suspects = bisect.m_original;
    if (bisect.m_original.empty()) {
        bisect.m_outcome = Outcome::NotExtensions;
    }
    return bisect;
}

std::optional<ExtensionBisect> ExtensionBisect::load(std::string_view json)
{
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (!root.is_object() || !root.contains("original") || !root.contains("suspects") || !root.contains("trial") ||
        !root.contains("step") || !root.contains("outcome") || !root["step"].is_number_integer() ||
        !root["outcome"].is_string()) {
        return std::nullopt;
    }
    auto original = idsOf(root["original"]);
    auto suspects = idsOf(root["suspects"]);
    auto trial = idsOf(root["trial"]);
    const auto step = root["step"].get<std::int64_t>();
    const auto outcome = std::ranges::find(kOutcomeNames, root["outcome"].get<std::string>());
    if (!original || !suspects || !trial || step < 0 || step > 64 || outcome == kOutcomeNames.end() ||
        !within(*suspects, *original) || !within(*trial, *suspects)) {
        return std::nullopt;
    }
    ExtensionBisect bisect;
    bisect.m_original = std::move(*original);
    bisect.m_suspects = std::move(*suspects);
    bisect.m_trial = std::move(*trial);
    bisect.m_step = static_cast<int>(step);
    bisect.m_outcome = static_cast<Outcome>(std::distance(kOutcomeNames.begin(), outcome));
    if (bisect.m_outcome == Outcome::Found && bisect.m_suspects.size() != 1) {
        return std::nullopt;
    }
    return bisect;
}

std::string ExtensionBisect::save() const
{
    const Json root = {{"original", m_original},
                       {"suspects", m_suspects},
                       {"trial", m_trial},
                       {"step", m_step},
                       {"outcome", std::string(kOutcomeNames[static_cast<std::size_t>(m_outcome)])}};
    return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

void ExtensionBisect::answer(bool stillHappens)
{
    if (m_outcome != Outcome::Running) {
        return;
    }
    if (m_step == 0) {
        if (stillHappens) {
            m_outcome = Outcome::NotExtensions;
            m_trial.clear();
            return;
        }
        m_suspects = m_original;
    } else if (stillHappens) {
        m_suspects = m_trial;
    } else {
        std::vector<std::string> rest;
        std::ranges::set_difference(m_suspects, m_trial, std::back_inserter(rest));
        m_suspects = std::move(rest);
    }
    ++m_step;
    split();
}

int ExtensionBisect::steps() const noexcept
{
    const std::size_t count = m_original.size();
    return 1 + (count <= 1 ? 0 : static_cast<int>(std::bit_width(count - 1)));
}

bool ExtensionBisect::loads(std::string_view id) const
{
    return std::ranges::binary_search(m_trial, id, std::less<>{});
}

std::optional<std::string> ExtensionBisect::found() const
{
    return m_outcome == Outcome::Found ? std::optional<std::string>(m_suspects.front()) : std::nullopt;
}

void ExtensionBisect::split()
{
    if (m_suspects.size() <= 1) {
        m_outcome = m_suspects.empty() ? Outcome::NotExtensions : Outcome::Found;
        m_trial.clear();
        return;
    }
    const auto half = static_cast<std::ptrdiff_t>((m_suspects.size() + 1) / 2);
    m_trial.assign(m_suspects.begin(), m_suspects.begin() + half);
}

}
