#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace gs::esp32 {

enum class BoardProblem : std::uint8_t {
    None,
    OldFirmware,
    Restarting,
    DownloadMode,
    WrongPort,
    Silent,
};

struct BoardTrouble {
    BoardProblem problem = BoardProblem::None;
    std::string resetReason;
    std::size_t bytes = 0;
    int restarts = 0;

    friend bool operator==(const BoardTrouble&, const BoardTrouble&) = default;
};

[[nodiscard]] BoardTrouble diagnoseHeard(std::span<const std::uint8_t> heard);

[[nodiscard]] const char* boardProblemName(BoardProblem problem) noexcept;

}
