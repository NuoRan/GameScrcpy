#include "ReliableStream.h"

#include "Handshake.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <utility>

namespace gs::session::detail {

namespace {

constexpr unsigned kMaxBackoff = 6;

bool sameTag(std::span<const std::uint8_t> expected, std::span<const std::uint8_t> actual) noexcept
{
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        difference |= static_cast<std::uint8_t>(expected[i] ^ actual[i]);
    }
    return difference == 0;
}

}

ReliableStream::ReliableStream(Kind kind, std::uint32_t connection, const Token& token, std::size_t receiveLimit)
    : m_kind(kind), m_connection(connection), m_token(token), m_receiveLimit(receiveLimit),
      m_maxSegment(kind == Kind::Control ? protocol::kMaxControlSegment : protocol::kMaxBulkSegment)
{
}

void ReliableStream::send(std::span<const std::uint8_t> bytes)
{
    while (!bytes.empty()) {
        const std::size_t size = std::min(bytes.size(), m_maxSegment);
        Outgoing segment;
        segment.seq = m_nextSeq++;
        segment.bytes.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
        m_outgoing.push_back(std::move(segment));
        bytes = bytes.subspan(size);
    }
}

ReliableStream::Outcome ReliableStream::receive(std::span<const std::uint8_t> datagram, TimePoint now)
{
    protocol::ReliableBlock block;
    if (m_kind == Kind::Control) {
        const auto parts = protocol::splitControlDatagram(datagram);
        if (!parts) {
            return {Verdict::Dropped, "a control datagram too short for its tag"};
        }
        if (!sameTag(controlTag(m_token, parts->authenticated), parts->tag)) {
            return {Verdict::Dropped, "a control datagram with a wrong tag"};
        }
        const auto body = protocol::decodeControlBody(parts->authenticated);
        if (!body) {
            return {Verdict::Broken, "a malformed control datagram"};
        }
        if (!acceptCounter(body->counter)) {
            return {Verdict::Dropped, "a replayed control datagram"};
        }
        block = body->block;
    } else {
        const auto decoded = protocol::decodeBulkDatagram(datagram);
        if (!decoded) {
            return {Verdict::Dropped, "a malformed bulk datagram"};
        }
        block = *decoded;
    }
    if (const auto outcome = acknowledge(block, now); outcome.verdict != Verdict::Accepted) {
        return outcome;
    }
    take(block, now);
    return {};
}

std::span<const std::uint8_t> ReliableStream::poll(TimePoint now)
{
    if (const auto oldest = oldestUnacked(); oldest != nullptr && now - oldest->lastSent >= rto()) {
        Outgoing& segment = m_outgoing[static_cast<std::size_t>(oldest->seq - m_base)];
        segment.lost = true;
        segment.resent = true;
        m_backoff = std::min(m_backoff + 1, kMaxBackoff);
    }

    std::array<std::size_t, 255> chosen{};
    std::size_t count = 0;
    std::size_t room = protocol::kReliableDatagramLimit -
                       (m_kind == Kind::Control ? protocol::kControlOverhead : protocol::kBulkOverhead);
    const auto fits = [&](const Outgoing& segment) {
        return count < chosen.size() && protocol::kSegmentHeaderSize + segment.bytes.size() <= room;
    };
    const auto choose = [&](std::size_t index) {
        chosen[count++] = index;
        room -= protocol::kSegmentHeaderSize + m_outgoing[index].bytes.size();
    };

    const std::size_t sent = sentCount();
    std::size_t retransmissions = 0;
    for (std::size_t i = 0; i < sent; ++i) {
        if (m_outgoing[i].lost && !m_outgoing[i].acked && fits(m_outgoing[i])) {
            choose(i);
            ++retransmissions;
        }
    }
    std::size_t fresh = 0;
    for (std::size_t i = sent; i < m_outgoing.size(); ++i) {
        if (m_outgoing[i].seq - m_base >= kWindow || !fits(m_outgoing[i])) {
            break;
        }
        choose(i);
        ++fresh;
    }
    std::size_t riders = 0;
    if (m_kind == Kind::Control && fresh != 0) {
        for (std::size_t i = sent; i-- > 0 && riders < kPiggyback;) {
            const Outgoing& segment = m_outgoing[i];
            if (!segment.acked && !segment.lost && fits(segment)) {
                choose(i);
                ++riders;
            }
        }
    }
    if (count == 0 && !(m_ackDue && now >= *m_ackDue)) {
        return {};
    }
    std::sort(chosen.begin(), chosen.begin() + static_cast<std::ptrdiff_t>(count));

    auto writer = m_kind == Kind::Control
                      ? protocol::ReliableWriter::control(m_datagram, m_connection, ++m_counter, m_ack, sackBits())
                      : protocol::ReliableWriter::bulk(m_datagram, m_connection, m_ack, sackBits());
    const std::uint64_t transmission = ++m_transmissions;
    for (std::size_t k = 0; k < count; ++k) {
        Outgoing& segment = m_outgoing[chosen[k]];
        [[maybe_unused]] const bool added = writer.add(segment.seq, segment.bytes);
        assert(added);
        if (segment.lastTransmission == 0) {
            segment.firstSent = now;
            m_sentEnd = std::max(m_sentEnd, segment.seq + 1);
        }
        segment.lastSent = now;
        segment.lastTransmission = transmission;
        segment.lost = false;
    }
    m_ackDue.reset();
    ++m_stats.datagramsSent;
    m_stats.segmentsSent += count;
    m_stats.retransmitted += retransmissions;
    m_stats.piggybacked += riders;

    const auto body = writer.finish();
    if (m_kind == Kind::Bulk) {
        return body;
    }
    return writer.seal(controlTag(m_token, body));
}

ReliableStream::TimePoint ReliableStream::deadline() const noexcept
{
    if (sendable()) {
        return TimePoint{};
    }
    TimePoint next = TimePoint::max();
    if (m_ackDue) {
        next = std::min(next, *m_ackDue);
    }
    if (const auto oldest = oldestUnacked(); oldest != nullptr) {
        next = std::min(next, oldest->lastSent + rto());
    }
    return next;
}

std::span<const std::uint8_t> ReliableStream::readable() const noexcept
{
    return std::span(m_readable).subspan(m_readOffset);
}

void ReliableStream::consume(std::size_t count) noexcept
{
    m_readOffset += std::min(count, m_readable.size() - m_readOffset);
    if (m_readOffset == m_readable.size()) {
        m_readable.clear();
        m_readOffset = 0;
    } else if (m_readOffset > 64 * 1024 && 2 * m_readOffset > m_readable.size()) {
        m_readable.erase(m_readable.begin(), m_readable.begin() + static_cast<std::ptrdiff_t>(m_readOffset));
        m_readOffset = 0;
    }
}

ReliableStream::Duration ReliableStream::rto() const noexcept
{
    Duration timeout = m_srtt ? std::max<Duration>(kMinRto, *m_srtt + 4 * m_rttvar) : Duration(kInitialRto);
    for (unsigned i = 0; i < m_backoff && timeout < kMaxRto; ++i) {
        timeout *= 2;
    }
    return std::min<Duration>(timeout, kMaxRto);
}

const ReliableStream::Outgoing* ReliableStream::oldestUnacked() const noexcept
{
    const std::size_t sent = sentCount();
    for (std::size_t i = 0; i < sent; ++i) {
        if (!m_outgoing[i].acked && !m_outgoing[i].lost) {
            return &m_outgoing[i];
        }
    }
    return nullptr;
}

bool ReliableStream::sendable() const noexcept
{
    const std::size_t sent = sentCount();
    for (std::size_t i = 0; i < sent; ++i) {
        if (m_outgoing[i].lost && !m_outgoing[i].acked) {
            return true;
        }
    }
    return sent < m_outgoing.size() && m_outgoing[sent].seq - m_base < kWindow;
}

bool ReliableStream::acceptCounter(std::uint64_t counter) noexcept
{
    if (counter > m_highestCounter) {
        const std::uint64_t shift = counter - m_highestCounter;
        m_seenCounters = shift >= 64 ? 0 : m_seenCounters << shift;
        m_seenCounters |= 1;
        m_highestCounter = counter;
        return true;
    }
    const std::uint64_t age = m_highestCounter - counter;
    if (age >= 64 || ((m_seenCounters >> age) & 1) != 0) {
        return false;
    }
    m_seenCounters |= std::uint64_t{1} << age;
    return true;
}

ReliableStream::Outcome ReliableStream::acknowledge(const protocol::ReliableBlock& block, TimePoint now)
{
    const bool beyondCumulative = block.ack > m_sentEnd;
    const bool beyondSelective =
        block.sack != 0 &&
        std::uint64_t{block.ack} + 1 + static_cast<unsigned>(31 - std::countl_zero(block.sack)) >= m_sentEnd;
    if (beyondCumulative || beyondSelective) {
        return {m_kind == Kind::Control ? Verdict::Broken : Verdict::Dropped,
                "an acknowledgement of segments never sent"};
    }

    const Outgoing* newest = nullptr;
    std::uint64_t newestTransmission = 0;
    const std::size_t sent = sentCount();
    for (std::size_t i = 0; i < sent; ++i) {
        Outgoing& segment = m_outgoing[i];
        if (segment.acked) {
            continue;
        }
        bool covered = segment.seq < block.ack;
        if (!covered && segment.seq > block.ack && segment.seq - block.ack - 1 < 32) {
            covered = ((block.sack >> (segment.seq - block.ack - 1)) & 1u) != 0;
        }
        if (!covered) {
            continue;
        }
        segment.acked = true;
        segment.lost = false;
        if (newest == nullptr || segment.seq > newest->seq) {
            newest = &segment;
        }
        newestTransmission = std::max(newestTransmission, segment.lastTransmission);
    }
    if (newest != nullptr && !newest->resent) {
        sample(now - newest->firstSent);
    }
    for (std::size_t i = 0; i < sent; ++i) {
        Outgoing& segment = m_outgoing[i];
        if (!segment.acked && !segment.lost && segment.lastTransmission < newestTransmission) {
            segment.lost = true;
            segment.resent = true;
        }
    }
    if (block.ack > m_base) {
        while (!m_outgoing.empty() && m_outgoing.front().seq < block.ack) {
            m_outgoing.pop_front();
        }
        m_base = block.ack;
        m_backoff = 0;
    }
    return {};
}

void ReliableStream::take(const protocol::ReliableBlock& block, TimePoint now)
{
    if (block.count == 0) {
        return;
    }
    const bool full = m_readable.size() - m_readOffset >= m_receiveLimit;
    bool fresh = false;
    protocol::SegmentReader reader(block);
    while (const auto segment = reader.next()) {
        if (segment->seq < m_ack) {
            ++m_stats.duplicatesReceived;
            continue;
        }
        const std::uint32_t offset = segment->seq - m_ack;
        if (offset > kWindow || full) {
            continue;
        }
        auto& slot = m_held[offset];
        if (slot) {
            ++m_stats.duplicatesReceived;
            continue;
        }
        slot.emplace(segment->bytes.begin(), segment->bytes.end());
        fresh = true;
    }
    while (m_held[0]) {
        m_readable.insert(m_readable.end(), m_held[0]->begin(), m_held[0]->end());
        std::rotate(m_held.begin(), m_held.begin() + 1, m_held.end());
        m_held.back().reset();
        ++m_ack;
    }
    const bool gap = std::any_of(m_held.begin() + 1, m_held.end(), [](const auto& held) { return held.has_value(); });
    if (gap || !fresh) {
        m_ackDue = now;
    } else if (!m_ackDue) {
        m_ackDue = now + kAckDelay;
    }
}

void ReliableStream::sample(Duration rtt) noexcept
{
    if (!m_srtt) {
        m_srtt = rtt;
        m_rttvar = rtt / 2;
        return;
    }
    const Duration delta = *m_srtt > rtt ? *m_srtt - rtt : rtt - *m_srtt;
    m_rttvar = (3 * m_rttvar + delta) / 4;
    m_srtt = (7 * *m_srtt + rtt) / 8;
}

std::uint32_t ReliableStream::sackBits() const noexcept
{
    std::uint32_t bits = 0;
    for (std::uint32_t i = 0; i < kWindow; ++i) {
        if (m_held[i + 1]) {
            bits |= std::uint32_t{1} << i;
        }
    }
    return bits;
}

}
