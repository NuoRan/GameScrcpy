#include "HostProtocol.h"

#include <array>
#include <charconv>
#include <format>

namespace gs::adb::detail {

namespace {

constexpr std::size_t kMaxRequest = 0xFFFF;

AdbResult<std::string> readText(TcpStream& stream, std::string_view header)
{
    std::size_t length = 0;
    const auto [end, error] = std::from_chars(header.data(), header.data() + header.size(), length, 16);
    if (error != std::errc{} || end != header.data() + header.size()) {
        return failure(AdbErrorKind::Protocol, "malformed length prefix");
    }
    std::string text(length, '\0');
    if (!text.empty() && !stream.readExact(bytesOf(text))) {
        return failure(AdbErrorKind::Io, "connection closed inside a reply");
    }
    return text;
}

}

Unexpected<AdbError> failure(AdbErrorKind kind, std::string message)
{
    return makeUnexpected(AdbError{kind, std::move(message)});
}

std::span<std::uint8_t> bytesOf(std::string& text) noexcept
{
    return {reinterpret_cast<std::uint8_t*>(text.data()), text.size()};
}

AdbResult<TcpStream> connectServer(std::uint16_t port, std::chrono::milliseconds timeout)
{
    std::optional<TcpStream> stream = TcpStream::connectLoopback(port);
    if (!stream) {
        return failure(AdbErrorKind::ServerUnavailable, std::format("no adb server on port {}", port));
    }
    stream->setReadTimeout(timeout);
    stream->setWriteTimeout(timeout);
    return std::move(*stream);
}

AdbResult<void> sendRequest(TcpStream& stream, std::string_view request)
{
    if (request.size() > kMaxRequest) {
        return failure(AdbErrorKind::Protocol, "request too long");
    }
    std::string framed = std::format("{:04x}", request.size());
    framed.append(request);
    if (!stream.writeAll(framed)) {
        return failure(AdbErrorKind::Io, "failed to send request");
    }
    return {};
}

AdbResult<std::string> readLengthPrefixed(TcpStream& stream)
{
    std::array<char, 4> header{};
    if (!stream.readExact(std::span(reinterpret_cast<std::uint8_t*>(header.data()), header.size()))) {
        return failure(AdbErrorKind::Io, "connection closed before the length prefix");
    }
    return readText(stream, std::string_view(header.data(), header.size()));
}

AdbResult<std::string> readTextOrFailure(TcpStream& stream)
{
    std::array<char, 4> header{};
    if (!stream.readExact(std::span(reinterpret_cast<std::uint8_t*>(header.data()), header.size()))) {
        return failure(AdbErrorKind::Io, "connection closed before the reply");
    }
    const std::string_view id(header.data(), header.size());
    if (id == "FAIL") {
        AdbResult<std::string> reason = readLengthPrefixed(stream);
        if (!reason) {
            return makeUnexpected(std::move(reason.error()));
        }
        return failure(AdbErrorKind::Rejected, std::move(*reason));
    }
    return readText(stream, id);
}

AdbResult<void> readStatus(TcpStream& stream)
{
    std::array<char, 4> status{};
    if (!stream.readExact(std::span(reinterpret_cast<std::uint8_t*>(status.data()), status.size()))) {
        return failure(AdbErrorKind::Io, "connection closed before the status");
    }
    const std::string_view id(status.data(), status.size());
    if (id == "OKAY") {
        return {};
    }
    if (id == "FAIL") {
        AdbResult<std::string> reason = readLengthPrefixed(stream);
        if (!reason) {
            return makeUnexpected(std::move(reason.error()));
        }
        return failure(AdbErrorKind::Rejected, std::move(*reason));
    }
    return failure(AdbErrorKind::Protocol, "unexpected status");
}

AdbResult<void> request(TcpStream& stream, std::string_view text)
{
    if (AdbResult<void> sent = sendRequest(stream, text); !sent) {
        return sent;
    }
    return readStatus(stream);
}

}
