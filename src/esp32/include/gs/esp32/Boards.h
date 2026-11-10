#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::esp32 {

inline constexpr std::size_t kBoardKeySize = 32;
inline constexpr std::string_view kBoardsFormat = "gamescrcpy.esp32-boards";
inline constexpr int kBoardsVersion = 1;
inline constexpr std::string_view kBoardsFileName = "esp32-boards.json";

using BoardKey = std::array<std::uint8_t, kBoardKeySize>;

struct KnownBoard {
    std::string id;
    std::string name;
    BoardKey key{};
    std::string address;

    friend bool operator==(const KnownBoard&, const KnownBoard&) = default;
};

[[nodiscard]] std::string boardId(std::span<const std::uint8_t, 6> mac);
[[nodiscard]] bool validBoardId(std::string_view id) noexcept;
[[nodiscard]] BoardKey newBoardKey();
[[nodiscard]] std::vector<KnownBoard> parseBoards(std::string_view json);
[[nodiscard]] std::string writeBoards(const std::vector<KnownBoard>& boards);
[[nodiscard]] std::vector<KnownBoard> loadBoards(const std::filesystem::path& file);
[[nodiscard]] bool saveBoards(const std::filesystem::path& file, const std::vector<KnownBoard>& boards);
[[nodiscard]] bool rememberBoard(const std::filesystem::path& file, const KnownBoard& board);
[[nodiscard]] bool forgetBoard(const std::filesystem::path& file, std::string_view id);
[[nodiscard]] std::optional<KnownBoard> findBoard(const std::vector<KnownBoard>& boards, std::string_view id);

}
