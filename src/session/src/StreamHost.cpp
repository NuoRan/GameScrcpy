#include "gs/session/StreamHost.h"

#include "StreamTransport.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <utility>

namespace gs::session {

namespace {

using SteadyClock = std::chrono::steady_clock;

SessionFailure failure(FailureKind kind, std::string detail)
{
    SessionFailure result;
    result.kind = kind;
    result.detail = std::move(detail);
    return result;
}

template <typename Ready>
bool waitWithin(std::condition_variable_any& changed, std::unique_lock<std::mutex>& lock,
                std::chrono::milliseconds timeout, Ready ready)
{
    if (timeout <= std::chrono::milliseconds::zero()) {
        changed.wait(lock, ready);
        return true;
    }
    return changed.wait_for(lock, timeout, ready);
}

class StreamEnd final : public ByteStream
{
public:
    explicit StreamEnd(std::shared_ptr<TransportStream> stream) noexcept : m_stream(std::move(stream)) {}

    ~StreamEnd() override { m_stream->shut(); }

    StreamEnd(const StreamEnd&) = delete;
    StreamEnd& operator=(const StreamEnd&) = delete;

    void setReadTimeout(std::chrono::milliseconds timeout) noexcept override { m_stream->setReadTimeout(timeout); }
    void setWriteTimeout(std::chrono::milliseconds timeout) noexcept override { m_stream->setWriteTimeout(timeout); }
    bool writeAll(std::span<const std::uint8_t> bytes) override { return m_stream->writeAll(bytes); }
    bool readExact(std::span<std::uint8_t> bytes) override { return m_stream->readExact(bytes); }
    std::size_t readSome(std::span<std::uint8_t> bytes) override { return m_stream->readSome(bytes); }
    bool readTimedOut() const noexcept override { return m_stream->readTimedOut(); }
    std::string closedReason() const override { return m_stream->reason(); }
    void interrupt() noexcept override { m_stream->interrupt(); }

private:
    std::shared_ptr<TransportStream> m_stream;
};

}

std::string_view streamChannelName(StreamChannel channel) noexcept
{
    switch (channel) {
    case StreamChannel::Control:
        return "control";
    case StreamChannel::Video:
        return "video";
    case StreamChannel::Audio:
        return "audio";
    case StreamChannel::Bulk:
        return "bulk";
    }
    return "control";
}

TransportStream::TransportStream(Sender sender, Closer closer)
    : m_sender(std::move(sender)), m_closer(std::move(closer))
{
}

void TransportStream::opened()
{
    {
        const std::lock_guard lock(m_mutex);
        if (m_state != State::Opening) {
            return;
        }
        m_state = State::Open;
    }
    m_changed.notify_all();
}

void TransportStream::deliver(std::span<const std::uint8_t> bytes)
{
    if (bytes.empty()) {
        return;
    }
    {
        const std::lock_guard lock(m_mutex);
        if (m_state != State::Open) {
            return;
        }
        if (heldLocked() + bytes.size() > kMostHeldBytes) {
            failLocked("more came from the phone than GameScrcpy could hold");
        } else {
            if (m_taken > 0 && m_taken * 2 >= m_held.size()) {
                m_held.erase(m_held.begin(), m_held.begin() + static_cast<std::ptrdiff_t>(m_taken));
                m_taken = 0;
            }
            m_held.insert(m_held.end(), bytes.begin(), bytes.end());
        }
    }
    m_changed.notify_all();
}

void TransportStream::writable()
{
    {
        const std::lock_guard lock(m_mutex);
        ++m_writable;
    }
    m_changed.notify_all();
}

void TransportStream::closed(std::string reason)
{
    {
        const std::lock_guard lock(m_mutex);
        if (m_state != State::Opening && m_state != State::Open) {
            return;
        }
        m_state = State::Ended;
        m_reason = reason.empty() ? std::string("the connection was closed") : std::move(reason);
    }
    m_changed.notify_all();
}

void TransportStream::shut()
{
    bool tell = false;
    {
        const std::lock_guard lock(m_mutex);
        tell = m_state == State::Opening || m_state == State::Open || m_state == State::Failed;
        m_state = State::Shut;
        m_held.clear();
        m_taken = 0;
    }
    m_changed.notify_all();
    if (tell && m_closer) {
        m_closer();
    }
}

TransportStream::Opening TransportStream::awaitOpen(SteadyClock::time_point deadline, const std::stop_token& stop)
{
    std::unique_lock lock(m_mutex);
    m_changed.wait_until(lock, stop, deadline, [this] { return m_state != State::Opening; });
    if (m_state == State::Open) {
        return Opening::Opened;
    }
    if (m_state != State::Opening) {
        return Opening::Refused;
    }
    return stop.stop_requested() ? Opening::Stopped : Opening::TimedOut;
}

std::string TransportStream::reason() const
{
    const std::lock_guard lock(m_mutex);
    return m_reason;
}

void TransportStream::setReadTimeout(std::chrono::milliseconds timeout) noexcept
{
    m_readTimeoutMs.store(timeout.count(), std::memory_order_relaxed);
}

void TransportStream::setWriteTimeout(std::chrono::milliseconds timeout) noexcept
{
    m_writeTimeoutMs.store(timeout.count(), std::memory_order_relaxed);
}

bool TransportStream::writeAll(std::span<const std::uint8_t> bytes)
{
    const std::lock_guard writing(m_writeMutex);
    while (!bytes.empty()) {
        const std::span<const std::uint8_t> chunk = bytes.first(std::min(bytes.size(), kChunkBytes));
        std::uint64_t seen = 0;
        {
            const std::lock_guard lock(m_mutex);
            if (m_interrupted || m_state != State::Open) {
                return false;
            }
            seen = m_writable;
        }
        const StreamSend sent = m_sender ? m_sender(chunk) : StreamSend::Failed;
        if (sent == StreamSend::Taken) {
            bytes = bytes.subspan(chunk.size());
            continue;
        }
        std::unique_lock lock(m_mutex);
        if (sent == StreamSend::Failed) {
            if (m_state == State::Open) {
                failLocked("the connection did not take what GameScrcpy sent");
                lock.unlock();
                m_changed.notify_all();
            }
            return false;
        }
        const auto ready = [&] { return m_interrupted || m_state != State::Open || m_writable != seen; };
        const std::chrono::milliseconds timeout(m_writeTimeoutMs.load(std::memory_order_relaxed));
        if (!waitWithin(m_changed, lock, timeout, ready) || m_interrupted || m_state != State::Open) {
            return false;
        }
    }
    return true;
}

bool TransportStream::readExact(std::span<std::uint8_t> bytes)
{
    std::unique_lock lock(m_mutex);
    m_readTimedOut.store(false, std::memory_order_relaxed);
    const auto ready = [&] { return m_interrupted || m_state != State::Open || heldLocked() >= bytes.size(); };
    const std::chrono::milliseconds timeout(m_readTimeoutMs.load(std::memory_order_relaxed));
    if (!waitWithin(m_changed, lock, timeout, ready)) {
        m_readTimedOut.store(true, std::memory_order_relaxed);
        return false;
    }
    if (m_interrupted || heldLocked() < bytes.size()) {
        return false;
    }
    takeLocked(bytes);
    return true;
}

std::size_t TransportStream::readSome(std::span<std::uint8_t> bytes)
{
    if (bytes.empty()) {
        return 0;
    }
    std::unique_lock lock(m_mutex);
    m_readTimedOut.store(false, std::memory_order_relaxed);
    const auto ready = [&] { return m_interrupted || m_state != State::Open || heldLocked() > 0; };
    const std::chrono::milliseconds timeout(m_readTimeoutMs.load(std::memory_order_relaxed));
    if (!waitWithin(m_changed, lock, timeout, ready)) {
        m_readTimedOut.store(true, std::memory_order_relaxed);
        return 0;
    }
    if (m_interrupted) {
        return 0;
    }
    const std::size_t count = std::min(bytes.size(), heldLocked());
    takeLocked(bytes.first(count));
    return count;
}

bool TransportStream::readTimedOut() const noexcept
{
    return m_readTimedOut.load(std::memory_order_relaxed);
}

void TransportStream::interrupt() noexcept
{
    {
        const std::lock_guard lock(m_mutex);
        m_interrupted = true;
    }
    m_changed.notify_all();
}

void TransportStream::takeLocked(std::span<std::uint8_t> out)
{
    if (!out.empty()) {
        std::memcpy(out.data(), m_held.data() + m_taken, out.size());
    }
    m_taken += out.size();
    if (m_taken == m_held.size()) {
        m_held.clear();
        m_taken = 0;
    }
}

void TransportStream::failLocked(std::string reason)
{
    m_state = State::Failed;
    m_reason = std::move(reason);
    m_held.clear();
    m_taken = 0;
}

std::unique_ptr<ByteStream> sessionEndOf(std::shared_ptr<TransportStream> stream)
{
    return std::make_unique<StreamEnd>(std::move(stream));
}

Expected<std::unique_ptr<ByteStream>, SessionFailure> awaitStream(std::shared_ptr<TransportStream> stream,
                                                                  std::string_view through,
                                                                  std::chrono::milliseconds timeout,
                                                                  const std::stop_token& stop)
{
    switch (stream->awaitOpen(SteadyClock::now() + timeout, stop)) {
    case TransportStream::Opening::Opened:
        return sessionEndOf(std::move(stream));
    case TransportStream::Opening::Refused: {
        const std::string reason = stream->reason();
        stream->shut();
        return makeUnexpected(
            failure(FailureKind::ConnectFailed, std::format("{} could not reach the phone: {}", through, reason)));
    }
    case TransportStream::Opening::TimedOut:
        stream->shut();
        return makeUnexpected(failure(FailureKind::Timeout, std::format("{} did not reach the phone within {} ms",
                                                                        through, timeout.count())));
    case TransportStream::Opening::Stopped:
        break;
    }
    stream->shut();
    return makeUnexpected(failure(FailureKind::LaunchFailed, "stopped while starting"));
}

StreamServerHost::StreamServerHost(std::shared_ptr<PhoneStreams> streams) noexcept : m_streams(std::move(streams)) {}

Expected<void, SessionFailure> StreamServerHost::start(const LaunchParams& params, Events& events, std::stop_token)
{
    m_params = params;
    events.hostPhase(SessionPhase::Connecting);
    return {};
}

Expected<platform::TcpStream, SessionFailure> StreamServerHost::connect()
{
    return makeUnexpected(failure(FailureKind::ConnectFailed, "a phone reached through an extension has no adb tunnel"));
}

std::unique_ptr<Transport> StreamServerHost::transport(std::chrono::milliseconds handshakeTimeout)
{
    return std::make_unique<detail::StreamTransport>(m_streams, m_params, handshakeTimeout);
}

Expected<void, SessionFailure> StreamServerHost::restore(bool afterFailure, SteadyClock::time_point deadline,
                                                         std::stop_token stop)
{
    if (!afterFailure) {
        return {};
    }
    return m_streams->awaitChange(deadline, std::move(stop));
}

void StreamServerHost::stop(std::chrono::milliseconds) {}

}
