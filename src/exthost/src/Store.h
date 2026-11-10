#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace gs::exthost::detail {

inline constexpr std::size_t kStoreQuota = std::size_t{1} << 20;
inline constexpr std::size_t kMostStoreKeyBytes = 256;

enum class StoreResult : std::uint8_t { Ok, Unavailable, BadArgument, Full, Failed };

class ModuleStore
{
public:
    explicit ModuleStore(std::filesystem::path file);

    [[nodiscard]] std::optional<std::string> get(std::string_view key);
    [[nodiscard]] StoreResult set(std::string_view key, std::optional<std::string_view> json);

private:
    void loadOnce();
    [[nodiscard]] std::optional<std::string> serialized(
        const std::map<std::string, std::string, std::less<>>& values) const;

    std::filesystem::path m_file;
    std::mutex m_mutex;
    bool m_loaded = false;
    std::map<std::string, std::string, std::less<>> m_values;
};

}
