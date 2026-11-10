#pragma once

#include "gs/adb/AdbClient.h"
#include "gs/platform/TcpStream.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gs::adb::detail {

using platform::TcpStream;

[[nodiscard]] Unexpected<AdbError> failure(AdbErrorKind kind, std::string message);

[[nodiscard]] std::span<std::uint8_t> bytesOf(std::string& text) noexcept;

[[nodiscard]] AdbResult<TcpStream> connectServer(std::uint16_t port, std::chrono::milliseconds timeout);

[[nodiscard]] AdbResult<void> sendRequest(TcpStream& stream, std::string_view request);
[[nodiscard]] AdbResult<void> readStatus(TcpStream& stream);
[[nodiscard]] AdbResult<std::string> readLengthPrefixed(TcpStream& stream);
[[nodiscard]] AdbResult<std::string> readTextOrFailure(TcpStream& stream);
[[nodiscard]] AdbResult<void> request(TcpStream& stream, std::string_view text);

}
