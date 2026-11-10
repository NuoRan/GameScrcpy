#pragma once

#include "TestSupport.h"

#include <gs/esp32/Port.h>
#include <gs/espflash/Chips.h>
#include <gs/espflash/Loader.h>
#include <gs/espflash/Md5.h>
#include <gs/espflash/Slip.h>

#include <miniz.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace esp32test {

struct LineChange {
    bool dtr = false;
    bool rts = false;
};

struct RegisterWrite {
    std::uint32_t address = 0;
    std::uint32_t value = 0;
    std::uint32_t mask = 0;
};

struct FlashBegin {
    bool compressed = false;
    std::vector<std::uint32_t> words;
};

class FakeRom final : public gs::esp32::Port
{
public:
    struct Options {
        gs::espflash::Chip chip = gs::espflash::Chip::Esp32S3;
        bool inLoader = true;
        bool enterOnClassicReset = false;
        bool silent = false;
        bool corruptFlash = false;
        bool refuseDeflate = false;
    };

    static constexpr std::size_t kFlashSize = 4U << 20U;

    explicit FakeRom(Options options)
        : m_options(options)
        , m_flash(kFlashSize, 0xFF)
    {
    }

    [[nodiscard]] bool write(std::span<const std::uint8_t> bytes) override
    {
        const std::lock_guard lock(m_mutex);
        if (m_options.silent || !m_loader || m_hostBaud != m_romBaud) {
            return true;
        }
        m_decoder.feed(bytes);
        while (std::optional<Bytes> packet = m_decoder.next()) {
            handle(*packet);
        }
        m_changed.notify_all();
        return true;
    }

    [[nodiscard]] std::size_t read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) override
    {
        std::unique_lock lock(m_mutex);
        m_changed.wait_for(lock, timeout, [this] { return !m_toHost.empty() || m_interrupted; });
        std::size_t size = 0;
        while (!m_toHost.empty() && size < buffer.size()) {
            buffer[size++] = m_toHost.front();
            m_toHost.pop_front();
        }
        return size;
    }

    [[nodiscard]] bool setBaud(std::uint32_t baud) override
    {
        const std::lock_guard lock(m_mutex);
        m_hostBaud = baud;
        return true;
    }

    [[nodiscard]] bool setDtr(bool on) override
    {
        const std::lock_guard lock(m_mutex);
        m_dtr = on;
        lineChanged();
        return true;
    }

    [[nodiscard]] bool setRts(bool on) override
    {
        const std::lock_guard lock(m_mutex);
        m_rts = on;
        lineChanged();
        return true;
    }

    void discardInput() override
    {
        const std::lock_guard lock(m_mutex);
        m_toHost.clear();
    }

    void interrupt() override
    {
        {
            const std::lock_guard lock(m_mutex);
            m_interrupted = true;
        }
        m_changed.notify_all();
    }

    [[nodiscard]] bool datagrams() const override { return false; }
    [[nodiscard]] bool gone() const override { return false; }

    [[nodiscard]] Bytes flash(std::size_t offset, std::size_t size) const
    {
        const std::lock_guard lock(m_mutex);
        return Bytes(m_flash.begin() + static_cast<std::ptrdiff_t>(offset),
                     m_flash.begin() + static_cast<std::ptrdiff_t>(offset + size));
    }

    [[nodiscard]] std::vector<std::uint8_t> commands() const
    {
        const std::lock_guard lock(m_mutex);
        return m_commands;
    }

    [[nodiscard]] std::size_t count(std::uint8_t op) const
    {
        const std::vector<std::uint8_t> all = commands();
        return static_cast<std::size_t>(std::ranges::count(all, op));
    }

    [[nodiscard]] std::optional<FlashBegin> begin() const
    {
        const std::lock_guard lock(m_mutex);
        return m_begin;
    }

    [[nodiscard]] std::vector<RegisterWrite> registerWrites() const
    {
        const std::lock_guard lock(m_mutex);
        return m_registerWrites;
    }

    [[nodiscard]] std::vector<LineChange> lines() const
    {
        const std::lock_guard lock(m_mutex);
        return m_lines;
    }

    [[nodiscard]] std::uint32_t romBaud() const
    {
        const std::lock_guard lock(m_mutex);
        return m_romBaud;
    }

    [[nodiscard]] std::optional<std::uint32_t> endFlag() const
    {
        const std::lock_guard lock(m_mutex);
        return m_endFlag;
    }

    [[nodiscard]] std::uint32_t flashSizeSet() const
    {
        const std::lock_guard lock(m_mutex);
        return m_flashSizeSet;
    }

private:
    void lineChanged()
    {
        m_lines.push_back({m_dtr, m_rts});
        if (m_options.enterOnClassicReset && m_lastRts && !m_rts && m_dtr) {
            m_loader = true;
        }
        m_lastRts = m_rts;
    }

    static std::uint32_t word(const Bytes& data, std::size_t index)
    {
        const std::size_t at = index * 4;
        return static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8U) |
               (static_cast<std::uint32_t>(data[at + 2]) << 16U) | (static_cast<std::uint32_t>(data[at + 3]) << 24U);
    }

    [[nodiscard]] std::uint32_t magic() const noexcept
    {
        switch (m_options.chip) {
        case gs::espflash::Chip::Esp32:
            return 0x00F01D83;
        case gs::espflash::Chip::Esp32S2:
            return 0x000007C6;
        case gs::espflash::Chip::Esp32S3:
            return 0x00000009;
        case gs::espflash::Chip::Esp32C3:
            return 0x1B31506F;
        case gs::espflash::Chip::Esp32C6:
            return 0x2CE0806F;
        case gs::espflash::Chip::Esp32P4:
            return 0xDEADBEEF;
        }
        return 0;
    }

    void respond(std::uint8_t op, std::uint32_t value, const Bytes& data, std::uint8_t status, std::uint8_t error)
    {
        Bytes packet{gs::espflash::rom::kResponse, op};
        const auto size = static_cast<std::uint16_t>(data.size() + 4);
        packet.push_back(static_cast<std::uint8_t>(size & 0xFFU));
        packet.push_back(static_cast<std::uint8_t>(size >> 8U));
        for (int shift = 0; shift < 32; shift += 8) {
            packet.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
        packet.insert(packet.end(), data.begin(), data.end());
        packet.push_back(status);
        packet.push_back(error);
        packet.push_back(0);
        packet.push_back(0);
        const Bytes framed = gs::espflash::slipEncode(packet);
        m_toHost.insert(m_toHost.end(), framed.begin(), framed.end());
    }

    void ok(std::uint8_t op, std::uint32_t value = 0, const Bytes& data = {}) { respond(op, value, data, 0, 0); }

    void fail(std::uint8_t op, std::uint8_t error) { respond(op, 0, {}, 1, error); }

    void inflatePending()
    {
        if (!m_begin || !m_begin->compressed || m_compressed.empty()) {
            return;
        }
        const std::uint32_t declared = m_begin->words[0];
        const std::uint32_t offset = m_begin->words[3];
        Bytes out(declared);
        mz_ulong size = declared;
        if (mz_uncompress(out.data(), &size, m_compressed.data(), static_cast<mz_ulong>(m_compressed.size())) != MZ_OK) {
            return;
        }
        std::copy_n(out.begin(), size, m_flash.begin() + static_cast<std::ptrdiff_t>(offset));
        m_compressed.clear();
    }

    void handle(const Bytes& packet)
    {
        namespace rom = gs::espflash::rom;
        if (packet.size() < 8 || packet[0] != rom::kRequest) {
            return;
        }
        const std::uint8_t op = packet[1];
        const std::size_t size = static_cast<std::size_t>(packet[2]) | (static_cast<std::size_t>(packet[3]) << 8U);
        const std::uint32_t checksum = word(packet, 1);
        if (packet.size() != 8 + size) {
            fail(op, rom::kInvalidMessage);
            return;
        }
        const Bytes data(packet.begin() + 8, packet.end());
        m_commands.push_back(op);
        const bool encryptedWord = m_options.chip != gs::espflash::Chip::Esp32;
        switch (op) {
        case rom::kSync:
            if (data.size() != 36 || data[0] != 0x07 || data[1] != 0x07 || data[2] != 0x12 || data[3] != 0x20) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            for (int answer = 0; answer < 3; ++answer) {
                ok(op);
            }
            return;
        case rom::kReadRegister:
            if (data.size() != 4) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            ok(op, word(data, 0) == gs::espflash::kChipMagicRegister ? magic() : 0);
            return;
        case rom::kWriteRegister:
            if (data.size() != 16) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            m_registerWrites.push_back({word(data, 0), word(data, 1), word(data, 2)});
            ok(op);
            return;
        case rom::kGetSecurityInfo: {
            if (m_options.chip == gs::espflash::Chip::Esp32) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            Bytes info(20, 0);
            const std::uint32_t id = gs::espflash::chipInfo(m_options.chip).imageId;
            info[12] = static_cast<std::uint8_t>(id & 0xFFU);
            info[13] = static_cast<std::uint8_t>((id >> 8U) & 0xFFU);
            if (m_options.chip == gs::espflash::Chip::Esp32S2) {
                info.resize(12);
            }
            ok(op, 0, info);
            return;
        }
        case rom::kSpiAttach:
            data.size() == 8 ? ok(op) : fail(op, rom::kInvalidMessage);
            return;
        case rom::kSpiSetParams:
            if (data.size() != 24) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            m_flashSizeSet = word(data, 1);
            ok(op);
            return;
        case rom::kChangeBaudrate:
            if (data.size() != 8 || word(data, 1) != 0) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            ok(op);
            m_romBaud = word(data, 0);
            return;
        case rom::kFlashDeflBegin:
        case rom::kFlashBegin: {
            const bool compressed = op == rom::kFlashDeflBegin;
            if (data.size() != (encryptedWord ? 20U : 16U)) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            if (compressed && m_options.refuseDeflate) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            FlashBegin started{.compressed = compressed};
            for (std::size_t i = 0; i < data.size() / 4; ++i) {
                started.words.push_back(word(data, i));
            }
            m_begin = started;
            m_compressed.clear();
            m_nextSequence = 0;
            ok(op);
            return;
        }
        case rom::kFlashDeflData:
        case rom::kFlashData: {
            const bool compressed = op == rom::kFlashDeflData;
            if (!m_begin || m_begin->compressed != compressed || data.size() < 16) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            const std::uint32_t length = word(data, 0);
            const std::uint32_t sequence = word(data, 1);
            const Bytes block(data.begin() + 16, data.end());
            if (length != block.size() || sequence != m_nextSequence || gs::espflash::dataChecksum(block) != checksum) {
                fail(op, 0x07);
                return;
            }
            const std::uint32_t packets = m_begin->words[1];
            const std::uint32_t packetSize = m_begin->words[2];
            if (block.size() > packetSize || sequence >= packets) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            ++m_nextSequence;
            if (compressed) {
                m_compressed.insert(m_compressed.end(), block.begin(), block.end());
                if (m_nextSequence == packets) {
                    inflatePending();
                }
            } else {
                if (block.size() != packetSize) {
                    fail(op, rom::kInvalidMessage);
                    return;
                }
                const std::size_t at = static_cast<std::size_t>(m_begin->words[3]) +
                                       static_cast<std::size_t>(sequence) * packetSize;
                std::copy(block.begin(), block.end(), m_flash.begin() + static_cast<std::ptrdiff_t>(at));
            }
            ok(op);
            return;
        }
        case rom::kFlashDeflEnd:
        case rom::kFlashEnd:
            if (data.size() != 4) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            m_endFlag = word(data, 0);
            ok(op);
            return;
        case rom::kSpiFlashMd5: {
            if (data.size() != 16) {
                fail(op, rom::kInvalidMessage);
                return;
            }
            const std::uint32_t offset = word(data, 0);
            const std::uint32_t length = word(data, 1);
            if (m_options.corruptFlash) {
                m_flash[offset + length / 2] = static_cast<std::uint8_t>(m_flash[offset + length / 2] ^ 0x01U);
            }
            const std::string text = gs::espflash::md5Hex(std::span<const std::uint8_t>(m_flash).subspan(offset, length));
            ok(op, 0, Bytes(text.begin(), text.end()));
            return;
        }
        default:
            fail(op, rom::kInvalidMessage);
            return;
        }
    }

    Options m_options;
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    gs::espflash::SlipDecoder m_decoder;
    std::deque<std::uint8_t> m_toHost;
    Bytes m_flash;
    Bytes m_compressed;
    std::vector<std::uint8_t> m_commands;
    std::vector<RegisterWrite> m_registerWrites;
    std::vector<LineChange> m_lines;
    std::optional<FlashBegin> m_begin;
    std::optional<std::uint32_t> m_endFlag;
    std::uint32_t m_nextSequence = 0;
    std::uint32_t m_hostBaud = gs::espflash::rom::kBaud;
    std::uint32_t m_romBaud = gs::espflash::rom::kBaud;
    std::uint32_t m_flashSizeSet = 0;
    bool m_loader = m_options.inLoader;
    bool m_dtr = false;
    bool m_rts = false;
    bool m_lastRts = false;
    bool m_interrupted = false;
};

class PortProxy final : public gs::esp32::Port
{
public:
    explicit PortProxy(gs::esp32::Port& target) noexcept
        : m_target(target)
    {
    }

    [[nodiscard]] bool write(std::span<const std::uint8_t> bytes) override { return m_target.write(bytes); }
    [[nodiscard]] std::size_t read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) override
    {
        return m_target.read(buffer, timeout);
    }
    [[nodiscard]] bool setBaud(std::uint32_t baud) override { return m_target.setBaud(baud); }
    [[nodiscard]] bool setDtr(bool on) override { return m_target.setDtr(on); }
    [[nodiscard]] bool setRts(bool on) override { return m_target.setRts(on); }
    void discardInput() override { m_target.discardInput(); }
    void interrupt() override { m_target.interrupt(); }
    [[nodiscard]] bool datagrams() const override { return m_target.datagrams(); }
    [[nodiscard]] bool gone() const override { return m_target.gone(); }

private:
    gs::esp32::Port& m_target;
};

}
