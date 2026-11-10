#include "Handshake.h"
#include "ReliableStream.h"

#include "gs/protocol/Reliable.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

using namespace gs;
using namespace gs::session;
using namespace std::chrono_literals;
using detail::ReliableStream;
using Kind = ReliableStream::Kind;
using Verdict = ReliableStream::Verdict;
using TimePoint = ReliableStream::TimePoint;
using Duration = ReliableStream::Duration;

namespace {

const TimePoint t0{std::chrono::hours(1)};
const Token token{9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6};
constexpr std::uint32_t kConnection = 0x0BADF00D;

std::vector<std::uint8_t> copy(std::span<const std::uint8_t> bytes)
{
    return {bytes.begin(), bytes.end()};
}

std::vector<std::uint8_t> pattern(std::size_t size, std::size_t seed)
{
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>(seed * 31 + i * 7);
    }
    return bytes;
}

class SimulatedLink
{
public:
    struct Settings {
        Duration delay = 5ms;
        double loss = 0;
        double duplicate = 0;
        double reorder = 0;
        Duration reorderBy = 3ms;
    };

    SimulatedLink(Settings settings, std::uint32_t seed) : m_settings(settings), m_random(seed) {}

    void drop(std::vector<std::size_t> numbers) { m_drop = std::move(numbers); }

    void push(std::span<const std::uint8_t> datagram, TimePoint now)
    {
        const std::size_t number = m_count++;
        if (std::ranges::find(m_drop, number) != m_drop.end() || chance(m_settings.loss)) {
            return;
        }
        enqueue(datagram, now + m_settings.delay + (chance(m_settings.reorder) ? m_settings.reorderBy : Duration{}));
        if (chance(m_settings.duplicate)) {
            enqueue(datagram, now + m_settings.delay + 1ms);
        }
    }

    std::optional<std::vector<std::uint8_t>> pop(TimePoint now)
    {
        if (m_queue.empty() || m_queue.front().at > now) {
            return std::nullopt;
        }
        auto bytes = std::move(m_queue.front().bytes);
        m_queue.erase(m_queue.begin());
        return bytes;
    }

    [[nodiscard]] TimePoint next() const { return m_queue.empty() ? TimePoint::max() : m_queue.front().at; }

private:
    struct InFlight {
        TimePoint at;
        std::vector<std::uint8_t> bytes;
    };

    bool chance(double probability)
    {
        return probability > 0 && std::uniform_real_distribution<double>(0, 1)(m_random) < probability;
    }

    void enqueue(std::span<const std::uint8_t> datagram, TimePoint at)
    {
        const auto place = std::ranges::upper_bound(m_queue, at, {}, &InFlight::at);
        m_queue.insert(place, InFlight{at, copy(datagram)});
    }

    Settings m_settings;
    std::mt19937 m_random;
    std::vector<std::size_t> m_drop;
    std::size_t m_count = 0;
    std::vector<InFlight> m_queue;
};

struct Peer {
    explicit Peer(Kind kind, std::size_t receiveLimit = std::size_t{4} << 20)
        : stream(kind, kConnection, token, receiveLimit)
    {
    }

    void write(std::span<const std::uint8_t> bytes)
    {
        stream.send(bytes);
        sent.insert(sent.end(), bytes.begin(), bytes.end());
    }

    ReliableStream stream;
    std::vector<std::uint8_t> sent;
    std::vector<std::uint8_t> received;
    std::vector<std::pair<std::size_t, TimePoint>> arrivals;
    bool reading = true;
    bool overfull = false;
    bool broken = false;
};

void transmit(Peer& peer, SimulatedLink& link, TimePoint now)
{
    for (auto datagram = peer.stream.poll(now); !datagram.empty(); datagram = peer.stream.poll(now)) {
        peer.overfull = peer.overfull || datagram.size() > protocol::kReliableDatagramLimit;
        link.push(datagram, now);
    }
    peer.overfull = peer.overfull || peer.stream.inFlight() > ReliableStream::kWindow;
}

void deliver(SimulatedLink& link, Peer& peer, TimePoint now)
{
    while (auto datagram = link.pop(now)) {
        peer.broken = peer.broken || peer.stream.receive(*datagram, now).verdict == Verdict::Broken;
    }
    const auto bytes = peer.stream.readable();
    if (peer.reading && !bytes.empty()) {
        peer.received.insert(peer.received.end(), bytes.begin(), bytes.end());
        peer.stream.consume(bytes.size());
        peer.arrivals.emplace_back(peer.received.size(), now);
    }
}

struct Message {
    TimePoint at;
    bool fromA = true;
    std::vector<std::uint8_t> bytes;
};

void run(Peer& a, Peer& b, SimulatedLink& ab, SimulatedLink& ba, std::vector<Message> messages, TimePoint start, TimePoint until)
{
    std::ranges::stable_sort(messages, {}, &Message::at);
    std::size_t next = 0;
    std::size_t steps = 0;
    for (TimePoint now = start; now <= until;) {
        if (++steps > 2'000'000) {
            FAIL("the simulation stopped advancing");
        }
        for (; next < messages.size() && messages[next].at <= now; ++next) {
            (messages[next].fromA ? a : b).write(messages[next].bytes);
        }
        deliver(ab, b, now);
        deliver(ba, a, now);
        transmit(a, ab, now);
        transmit(b, ba, now);
        const TimePoint wake = std::min({next < messages.size() ? messages[next].at : TimePoint::max(), ab.next(),
                                         ba.next(), a.stream.deadline(), b.stream.deadline()});
        if (wake == TimePoint::max()) {
            break;
        }
        now = std::max(wake, now + 1us);
    }
}

std::vector<Duration> latencies(const std::vector<Message>& messages, const Peer& receiver)
{
    std::vector<Duration> out;
    std::size_t end = 0;
    for (const Message& message : messages) {
        end += message.bytes.size();
        const auto arrival = std::ranges::find_if(receiver.arrivals, [end](const auto& a) { return a.first >= end; });
        out.push_back(arrival == receiver.arrivals.end() ? Duration::max() : arrival->second - message.at);
    }
    return out;
}

std::vector<Message> stream(bool fromA, std::size_t count, Duration every, std::size_t seed)
{
    std::vector<Message> messages;
    for (std::size_t i = 0; i < count; ++i) {
        messages.push_back({t0 + every * i, fromA, pattern(10 + (i * 37 + seed) % 190, seed + i)});
    }
    return messages;
}

protocol::ReliableBlock blockOf(Kind kind, const std::vector<std::uint8_t>& datagram)
{
    if (kind == Kind::Bulk) {
        const auto block = protocol::decodeBulkDatagram(datagram);
        REQUIRE(block.has_value());
        return *block;
    }
    const auto parts = protocol::splitControlDatagram(datagram);
    REQUIRE(parts.has_value());
    const auto body = protocol::decodeControlBody(parts->authenticated);
    REQUIRE(body.has_value());
    return body->block;
}

std::vector<std::uint32_t> seqsOf(Kind kind, const std::vector<std::uint8_t>& datagram)
{
    const auto block = blockOf(kind, datagram);
    std::vector<std::uint32_t> seqs;
    protocol::SegmentReader reader(block);
    while (const auto segment = reader.next()) {
        seqs.push_back(segment->seq);
    }
    return seqs;
}

std::vector<std::uint8_t> signedControl(std::span<const std::uint8_t> body)
{
    std::vector<std::uint8_t> datagram(body.begin(), body.end());
    const auto tag = detail::controlTag(token, body);
    datagram.insert(datagram.end(), tag.begin(), tag.end());
    return datagram;
}

}

TEST_CASE("a clean link delivers every message once, in order, one link delay after it was written")
{
    Peer a(Kind::Control), b(Kind::Control);
    SimulatedLink ab({}, 1), ba({}, 2);
    auto fromA = stream(true, 250, 4ms, 0);
    auto fromB = stream(false, 50, 20ms, 7);
    std::vector<Message> all = fromA;
    all.insert(all.end(), fromB.begin(), fromB.end());
    run(a, b, ab, ba, all, t0, t0 + 3s);

    CHECK(b.received == a.sent);
    CHECK(a.received == b.sent);
    CHECK(a.stream.settled());
    CHECK(b.stream.settled());
    CHECK_FALSE((a.overfull || b.overfull || a.broken || b.broken));
    CHECK(a.stream.stats().retransmitted == 0);
    CHECK(a.stream.stats().piggybacked > 0);
    for (const Duration latency : latencies(fromA, b)) {
        CHECK(latency == 5ms);
    }
}

TEST_CASE("a lost control datagram's messages come with the next datagram, without waiting for a resend")
{
    Peer a(Kind::Control), b(Kind::Control);
    SimulatedLink ab({}, 3), ba({}, 4);
    ab.drop({10, 11});
    const auto fromA = stream(true, 40, 4ms, 1);
    run(a, b, ab, ba, fromA, t0, t0 + 2s);

    CHECK(b.received == a.sent);
    CHECK(a.stream.settled());
    CHECK(a.stream.stats().retransmitted == 0);
    const auto latency = latencies(fromA, b);
    CHECK(latency[9] == 5ms);
    CHECK(latency[10] == 5ms + 2 * 4ms);
    CHECK(latency[11] == 5ms + 4ms);
    CHECK(latency[12] == 5ms);
}

TEST_CASE("random loss, duplication and reordering lose nothing and reorder nothing (WIF-004)")
{
    for (const double loss : {0.02, 0.05, 0.10}) {
        CAPTURE(loss);
        const SimulatedLink::Settings settings{.delay = 4ms, .loss = loss, .duplicate = 0.02, .reorder = 0.05};
        const auto seed = static_cast<std::uint32_t>(loss * 1000);

        Peer a(Kind::Control), b(Kind::Control);
        SimulatedLink ab(settings, seed), ba(settings, seed + 1);
        auto messages = stream(true, 600, 4ms, 3);
        auto fromB = stream(false, 150, 16ms, 5);
        messages.insert(messages.end(), fromB.begin(), fromB.end());
        messages.push_back({t0 + 1s, true, pattern(20000, 9)});
        run(a, b, ab, ba, messages, t0, t0 + 30s);
        CHECK(b.received == a.sent);
        CHECK(a.received == b.sent);
        CHECK(a.stream.settled());
        CHECK(b.stream.settled());
        CHECK_FALSE((a.overfull || b.overfull || a.broken || b.broken));

        Peer x(Kind::Bulk), y(Kind::Bulk);
        SimulatedLink xy(settings, seed + 2), yx(settings, seed + 3);
        run(x, y, xy, yx, {{t0, true, pattern(300'000, 11)}, {t0 + 50ms, false, pattern(50'000, 12)}}, t0, t0 + 60s);
        CHECK(y.received == x.sent);
        CHECK(x.received == y.sent);
        CHECK(x.stream.settled());
        CHECK(y.stream.settled());
        CHECK_FALSE((x.overfull || y.overfull || x.broken || y.broken));
        CHECK(x.stream.stats().retransmitted > 0);
    }
}

TEST_CASE("bulk keeps 32 segments in flight and resends a loss as soon as the selective ack shows it")
{
    Peer a(Kind::Bulk), b(Kind::Bulk);
    a.write(pattern(64 * protocol::kMaxBulkSegment, 1));
    std::vector<std::vector<std::uint8_t>> out;
    for (auto datagram = a.stream.poll(t0); !datagram.empty(); datagram = a.stream.poll(t0)) {
        out.push_back(copy(datagram));
    }
    CHECK(out.size() == ReliableStream::kWindow);
    CHECK(a.stream.inFlight() == ReliableStream::kWindow);

    for (std::size_t i = 0; i < out.size(); ++i) {
        if (i != 3) {
            REQUIRE(b.stream.receive(out[i], t0 + 5ms).verdict == Verdict::Accepted);
        }
    }
    const auto ack = copy(b.stream.poll(t0 + 5ms));
    const auto block = blockOf(Kind::Bulk, ack);
    CHECK(block.count == 0);
    CHECK(block.ack == 3);
    CHECK(block.sack == 0x0FFFFFFFu);

    REQUIRE(a.stream.receive(ack, t0 + 10ms).verdict == Verdict::Accepted);
    CHECK(a.stream.deadline() <= t0 + 10ms);
    CHECK(seqsOf(Kind::Bulk, copy(a.stream.poll(t0 + 10ms))) == std::vector<std::uint32_t>{3});
    CHECK(seqsOf(Kind::Bulk, copy(a.stream.poll(t0 + 10ms))) == std::vector<std::uint32_t>{32});
    CHECK(a.stream.stats().retransmitted == 1);

    SimulatedLink ab({}, 5), ba({}, 6);
    b.received = copy(b.stream.readable());
    b.stream.consume(b.received.size());
    run(a, b, ab, ba, {}, t0 + 10ms, t0 + 5s);
    CHECK(b.received == a.sent);
    CHECK(a.stream.settled());
}

TEST_CASE("acknowledgements wait up to 5 ms, but not when a gap opens or copies repeat")
{
    Peer a(Kind::Bulk), b(Kind::Bulk);
    a.write(pattern(100, 2));
    REQUIRE(b.stream.receive(copy(a.stream.poll(t0)), t0 + 5ms).verdict == Verdict::Accepted);
    CHECK(b.stream.poll(t0 + 5ms).empty());
    CHECK(b.stream.deadline() == t0 + 10ms);
    const auto pure = copy(b.stream.poll(t0 + 10ms));
    CHECK(blockOf(Kind::Bulk, pure).count == 0);
    CHECK(blockOf(Kind::Bulk, pure).ack == 1);
    CHECK(b.stream.poll(t0 + 10ms).empty());
    CHECK(b.stream.deadline() == TimePoint::max());

    Peer c(Kind::Bulk), d(Kind::Bulk);
    c.write(pattern(3 * protocol::kMaxBulkSegment, 3));
    const auto first = copy(c.stream.poll(t0));
    const auto second = copy(c.stream.poll(t0));
    const auto third = copy(c.stream.poll(t0));
    REQUIRE(d.stream.receive(second, t0 + 5ms).verdict == Verdict::Accepted);
    auto ack = blockOf(Kind::Bulk, copy(d.stream.poll(t0 + 5ms)));
    CHECK((ack.ack == 0 && ack.sack == 0b1));
    REQUIRE(d.stream.receive(third, t0 + 6ms).verdict == Verdict::Accepted);
    ack = blockOf(Kind::Bulk, copy(d.stream.poll(t0 + 6ms)));
    CHECK((ack.ack == 0 && ack.sack == 0b11));
    REQUIRE(d.stream.receive(first, t0 + 7ms).verdict == Verdict::Accepted);
    CHECK(d.stream.poll(t0 + 7ms).empty());
    CHECK(d.stream.deadline() == t0 + 12ms);
    CHECK(d.stream.readable().size() == 3 * protocol::kMaxBulkSegment);
    REQUIRE(d.stream.receive(first, t0 + 8ms).verdict == Verdict::Accepted);
    ack = blockOf(Kind::Bulk, copy(d.stream.poll(t0 + 8ms)));
    CHECK((ack.ack == 3 && ack.sack == 0));
    CHECK(d.stream.stats().duplicatesReceived == 1);
}

TEST_CASE("timeouts resend the oldest segment and double the timeout up to one second")
{
    Peer a(Kind::Control);
    a.write(pattern(30, 4));
    REQUIRE_FALSE(a.stream.poll(t0).empty());
    std::vector<TimePoint> sent{t0};
    for (int i = 0; i < 6; ++i) {
        const TimePoint at = a.stream.deadline();
        const auto datagram = copy(a.stream.poll(at));
        REQUIRE_FALSE(datagram.empty());
        CHECK(seqsOf(Kind::Control, datagram) == std::vector<std::uint32_t>{0});
        CHECK(a.stream.poll(at).empty());
        sent.push_back(at);
    }
    std::vector<Duration> gaps;
    for (std::size_t i = 1; i < sent.size(); ++i) {
        gaps.push_back(sent[i] - sent[i - 1]);
    }
    CHECK(gaps == std::vector<Duration>{100ms, 200ms, 400ms, 800ms, 1000ms, 1000ms});
    CHECK(a.stream.stats().retransmitted == 6);
}

TEST_CASE("the round trip time is measured, and the timeout follows it down to 20 ms")
{
    Peer a(Kind::Control), b(Kind::Control);
    SimulatedLink ab({}, 7), ba({}, 8);
    CHECK(a.stream.rto() == 100ms);
    run(a, b, ab, ba, stream(true, 20, 50ms, 2), t0, t0 + 2s);
    REQUIRE(a.stream.smoothedRtt().has_value());
    CHECK(*a.stream.smoothedRtt() == 15ms);
    CHECK(a.stream.rto() == 20ms);
}

TEST_CASE("forged, replayed and nonsensical datagrams are dropped, or end the session when authentic")
{
    Peer a(Kind::Control), b(Kind::Control);
    a.write(pattern(40, 5));
    const auto datagram = copy(a.stream.poll(t0));
    CHECK(b.stream.receive(datagram, t0 + 5ms).verdict == Verdict::Accepted);
    CHECK(b.stream.receive(datagram, t0 + 6ms).verdict == Verdict::Dropped);
    auto tampered = datagram;
    tampered[30] ^= 0x40;
    CHECK(b.stream.receive(tampered, t0 + 6ms).verdict == Verdict::Dropped);
    CHECK(b.stream.receive(std::span(datagram).first(40), t0 + 6ms).verdict == Verdict::Dropped);

    std::array<std::uint8_t, protocol::kReliableDatagramLimit> buffer{};
    const auto pureAck = [&buffer](std::uint64_t counter) {
        auto writer = protocol::ReliableWriter::control(buffer, kConnection, counter, 0, 0);
        return signedControl(writer.finish());
    };
    for (std::uint64_t counter = 1000; counter <= 1070; ++counter) {
        if (counter != 1001 && counter != 1050) {
            CHECK(b.stream.receive(pureAck(counter), t0 + 7ms).verdict == Verdict::Accepted);
        }
    }
    CHECK(b.stream.receive(pureAck(1050), t0 + 8ms).verdict == Verdict::Accepted);
    CHECK(b.stream.receive(pureAck(1050), t0 + 8ms).verdict == Verdict::Dropped);
    CHECK(b.stream.receive(pureAck(1001), t0 + 8ms).verdict == Verdict::Dropped);

    auto beyond = protocol::ReliableWriter::control(buffer, kConnection, 1000, 500, 0);
    const auto beyondBody = copy(beyond.finish());
    CHECK(a.stream.receive(signedControl(beyondBody), t0 + 9ms).verdict == Verdict::Broken);
    auto malformed = protocol::ReliableWriter::control(buffer, kConnection, 1001, 0, 0);
    auto malformedBody = copy(malformed.finish());
    malformedBody.push_back(0);
    CHECK(a.stream.receive(signedControl(malformedBody), t0 + 9ms).verdict == Verdict::Broken);

    Peer c(Kind::Bulk);
    c.write(pattern(10, 7));
    REQUIRE_FALSE(c.stream.poll(t0).empty());
    auto bulk = protocol::ReliableWriter::bulk(buffer, kConnection, 9, 0);
    auto bulkBody = copy(bulk.finish());
    CHECK(c.stream.receive(bulkBody, t0 + 1ms).verdict == Verdict::Dropped);
    bulkBody.push_back(0);
    CHECK(c.stream.receive(bulkBody, t0 + 1ms).verdict == Verdict::Dropped);
}

TEST_CASE("a reader that falls behind holds the sender back without losing anything")
{
    constexpr std::size_t limit = 4000;
    Peer a(Kind::Bulk), b(Kind::Bulk, limit);
    SimulatedLink ab({}, 9), ba({}, 10);
    b.reading = false;
    run(a, b, ab, ba, {{t0, true, pattern(20'000, 8)}}, t0, t0 + 500ms);
    CHECK(b.stream.readable().size() >= limit);
    CHECK(b.stream.readable().size() <= limit + protocol::kMaxBulkSegment);
    CHECK_FALSE(a.stream.settled());

    b.reading = true;
    run(a, b, ab, ba, {}, t0 + 500ms, t0 + 10s);
    CHECK(b.received == a.sent);
    CHECK(a.stream.settled());
}
