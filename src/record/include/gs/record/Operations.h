#pragma once

#include <gs/protocol/Messages.h>
#include <gs/script/Session.h>

#include <cstddef>
#include <string_view>

namespace gs::record {

[[nodiscard]] script::SessionTouch touchOf(const protocol::TouchFrame& frame);
[[nodiscard]] script::SessionKey keyOf(const protocol::Key& key);
[[nodiscard]] script::SessionScroll scrollOf(const protocol::Scroll& scroll);
[[nodiscard]] script::SessionText textOf(std::string_view utf8) noexcept;

}
