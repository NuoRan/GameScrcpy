#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gs::record {

enum class PictureProblem : std::uint8_t { None, Missing, TooLarge, Unreadable };

struct Picture {
    int width = 0;
    int height = 0;
    bool alpha = false;
    std::vector<std::uint8_t> rgba;

    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0 || rgba.empty(); }
};

class PictureReader
{
public:
    PictureReader();
    ~PictureReader();
    PictureReader(const PictureReader&) = delete;
    PictureReader& operator=(const PictureReader&) = delete;

    [[nodiscard]] PictureProblem read(const std::filesystem::path& file, Picture& picture);
    [[nodiscard]] Picture scaled(const Picture& picture, int width, int height);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

}
