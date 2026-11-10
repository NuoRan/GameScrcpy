#include "gs/esp32/Boards.h"

#include <gs/platform/AtomicFile.h>
#include <gs/platform/Crypto.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace gs::esp32 {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMostFileBytes = 256 * 1024;
constexpr std::size_t kMostBoards = 64;
constexpr std::size_t kMostName = 64;
constexpr std::size_t kMostAddress = 255;

int hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::optional<BoardKey> keyFromHex(std::string_view text)
{
    if (text.size() != kBoardKeySize * 2) {
        return std::nullopt;
    }
    BoardKey key{};
    for (std::size_t i = 0; i < kBoardKeySize; ++i) {
        const int high = hexValue(text[i * 2]);
        const int low = hexValue(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        key[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return key;
}

}

std::string boardId(std::span<const std::uint8_t, 6> mac)
{
    return platform::toHex(mac);
}

bool validBoardId(std::string_view id) noexcept
{
    return id.size() == 12 && std::ranges::all_of(id, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

BoardKey newBoardKey()
{
    BoardKey key{};
    platform::randomBytes(key);
    return key;
}

std::vector<KnownBoard> parseBoards(std::string_view json)
{
    std::vector<KnownBoard> boards;
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return boards;
    }
    const auto format = root.find("format");
    const auto version = root.find("version");
    if (format == root.end() || !format->is_string() || format->get<std::string>() != kBoardsFormat ||
        version == root.end() || !version->is_number_integer() || version->get<int>() != kBoardsVersion) {
        return boards;
    }
    const auto list = root.find("boards");
    if (list == root.end() || !list->is_object()) {
        return boards;
    }
    for (auto it = list->begin(); it != list->end() && boards.size() < kMostBoards; ++it) {
        if (!validBoardId(it.key()) || !it.value().is_object()) {
            continue;
        }
        const Json& entry = it.value();
        const auto key = entry.find("key");
        if (key == entry.end() || !key->is_string()) {
            continue;
        }
        const std::optional<BoardKey> parsed = keyFromHex(key->get<std::string>());
        if (!parsed) {
            continue;
        }
        KnownBoard board;
        board.id = it.key();
        board.key = *parsed;
        if (const auto name = entry.find("name"); name != entry.end() && name->is_string()) {
            board.name = name->get<std::string>().substr(0, kMostName);
        }
        if (const auto address = entry.find("address"); address != entry.end() && address->is_string()) {
            board.address = address->get<std::string>().substr(0, kMostAddress);
        }
        boards.push_back(std::move(board));
    }
    return boards;
}

std::string writeBoards(const std::vector<KnownBoard>& boards)
{
    Json list = Json::object();
    for (const KnownBoard& board : boards) {
        if (!validBoardId(board.id)) {
            continue;
        }
        Json entry = Json::object();
        if (!board.name.empty()) {
            entry["name"] = board.name;
        }
        entry["key"] = platform::toHex(board.key);
        if (!board.address.empty()) {
            entry["address"] = board.address;
        }
        list[board.id] = std::move(entry);
    }
    Json root = Json::object();
    root["format"] = std::string(kBoardsFormat);
    root["version"] = kBoardsVersion;
    root["boards"] = std::move(list);
    return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

std::vector<KnownBoard> loadBoards(const std::filesystem::path& file)
{
    std::string text;
    if (platform::readFile(file, kMostFileBytes, text)) {
        return {};
    }
    return parseBoards(text);
}

bool saveBoards(const std::filesystem::path& file, const std::vector<KnownBoard>& boards)
{
    std::error_code ignored;
    (void)std::filesystem::create_directories(file.parent_path(), ignored);
    return !platform::writeFileAtomically(file, writeBoards(boards));
}

bool rememberBoard(const std::filesystem::path& file, const KnownBoard& board)
{
    if (!validBoardId(board.id)) {
        return false;
    }
    std::vector<KnownBoard> boards = loadBoards(file);
    std::erase_if(boards, [&board](const KnownBoard& known) { return known.id == board.id; });
    boards.insert(boards.begin(), board);
    if (boards.size() > kMostBoards) {
        boards.resize(kMostBoards);
    }
    return saveBoards(file, boards);
}

bool forgetBoard(const std::filesystem::path& file, std::string_view id)
{
    std::vector<KnownBoard> boards = loadBoards(file);
    const auto removed = std::erase_if(boards, [id](const KnownBoard& known) { return known.id == id; });
    return removed > 0 && saveBoards(file, boards);
}

std::optional<KnownBoard> findBoard(const std::vector<KnownBoard>& boards, std::string_view id)
{
    const auto found = std::ranges::find(boards, id, &KnownBoard::id);
    if (found == boards.end()) {
        return std::nullopt;
    }
    return *found;
}

}
