#pragma once

#include <gs/protocol/Codec.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::protocol::testing {

inline std::string toHex(std::span<const std::uint8_t> bytes)
{
    static constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        text.push_back(kDigits[byte >> 4U]);
        text.push_back(kDigits[byte & 0x0FU]);
    }
    return text;
}

inline Bytes fromHex(std::string_view text)
{
    const auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') {
            return static_cast<std::uint8_t>(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<std::uint8_t>(c - 'a' + 10);
        }
        if (c >= 'A' && c <= 'F') {
            return static_cast<std::uint8_t>(c - 'A' + 10);
        }
        throw std::invalid_argument("invalid hex digit");
    };
    Bytes bytes;
    bytes.reserve(text.size() / 2);
    std::uint8_t high = 0;
    bool haveHigh = false;
    for (const char c : text) {
        if (c == ' ') {
            continue;
        }
        const std::uint8_t value = nibble(c);
        if (haveHigh) {
            bytes.push_back(static_cast<std::uint8_t>((high << 4U) | value));
        } else {
            high = value;
        }
        haveHigh = !haveHigh;
    }
    if (haveHigh) {
        throw std::invalid_argument("odd number of hex digits");
    }
    return bytes;
}

template <typename T>
nlohmann::json toJson(const T& value);

template <typename T>
T fromJson(const nlohmann::json& in);

class JsonWriter
{
public:
    explicit JsonWriter(nlohmann::json& out)
        : m_out(out)
    {
        m_out = nlohmann::json::object();
    }

    void magic(std::uint32_t) {}
    void u8(std::string_view name, std::uint8_t value) { set(name, value); }
    void u16(std::string_view name, std::uint16_t value) { set(name, value); }
    void u32(std::string_view name, std::uint32_t value) { set(name, value); }
    void u64(std::string_view name, std::uint64_t value) { set(name, std::to_string(value)); }
    void i16(std::string_view name, std::int16_t value) { set(name, value); }
    void i64(std::string_view name, std::int64_t value) { set(name, std::to_string(value)); }
    void u8Range(std::string_view name, std::uint8_t value, std::uint8_t, std::uint8_t) { set(name, value); }
    void u16Range(std::string_view name, std::uint16_t value, std::uint16_t, std::uint16_t) { set(name, value); }
    void u32Mask(std::string_view name, std::uint32_t value, std::uint32_t) { set(name, value); }
    void boolean(std::string_view name, bool value) { set(name, value); }

    template <typename E>
    void enumeration(std::string_view name, E value)
    {
        set(name, std::string(enumName(value)));
    }

    template <std::size_t N>
    void bytes(std::string_view name, const std::array<std::uint8_t, N>& value)
    {
        set(name, toHex(value));
    }

    void str8(std::string_view name, const std::string& value, std::size_t) { set(name, value); }
    void str16(std::string_view name, const std::string& value, std::size_t) { set(name, value); }
    void str32(std::string_view name, const std::string& value, std::size_t) { set(name, value); }
    void blob8(std::string_view name, const Bytes& value, std::size_t) { set(name, toHex(value)); }
    void blob16(std::string_view name, const Bytes& value, std::size_t) { set(name, toHex(value)); }
    void blob32(std::string_view name, const Bytes& value, std::size_t) { set(name, toHex(value)); }
    void flag8(std::string_view name, bool bit0) { set(name, bit0); }

    void flag8x2(std::string_view name0, bool bit0, std::string_view name1, bool bit1)
    {
        set(name0, bit0);
        set(name1, bit1);
    }

    void flag8x3(std::string_view name0, bool bit0, std::string_view name1, bool bit1, std::string_view name2,
                 bool bit2)
    {
        set(name0, bit0);
        set(name1, bit1);
        set(name2, bit2);
    }

    template <typename T>
    void object(std::string_view name, const T& value)
    {
        set(name, toJson(value));
    }

    template <typename List>
    void list8(std::string_view name, const List& items, std::size_t, std::size_t)
    {
        setList(name, items);
    }

    template <typename List>
    void list16(std::string_view name, const List& items, std::size_t, std::size_t)
    {
        setList(name, items);
    }

private:
    template <typename V>
    void set(std::string_view name, V&& value)
    {
        m_out[std::string(name)] = std::forward<V>(value);
    }

    template <typename List>
    void setList(std::string_view name, const List& items)
    {
        auto array = nlohmann::json::array();
        for (const auto& item : items) {
            array.push_back(toJson(item));
        }
        set(name, std::move(array));
    }

    nlohmann::json& m_out;
};

class JsonReader
{
public:
    explicit JsonReader(const nlohmann::json& in)
        : m_in(in)
    {
    }

    void magic(std::uint32_t) {}
    void u8(std::string_view name, std::uint8_t& out) { out = at(name).get<std::uint8_t>(); }
    void u16(std::string_view name, std::uint16_t& out) { out = at(name).get<std::uint16_t>(); }
    void u32(std::string_view name, std::uint32_t& out) { out = at(name).get<std::uint32_t>(); }
    void u64(std::string_view name, std::uint64_t& out) { out = std::stoull(at(name).get<std::string>()); }
    void i16(std::string_view name, std::int16_t& out) { out = at(name).get<std::int16_t>(); }
    void i64(std::string_view name, std::int64_t& out) { out = std::stoll(at(name).get<std::string>()); }
    void u8Range(std::string_view name, std::uint8_t& out, std::uint8_t, std::uint8_t) { u8(name, out); }
    void u16Range(std::string_view name, std::uint16_t& out, std::uint16_t, std::uint16_t) { u16(name, out); }
    void u32Mask(std::string_view name, std::uint32_t& out, std::uint32_t) { u32(name, out); }
    void boolean(std::string_view name, bool& out) { out = at(name).get<bool>(); }

    template <typename E>
    void enumeration(std::string_view name, E& out)
    {
        const auto value = enumFromName<E>(at(name).get<std::string>());
        if (!value) {
            throw std::invalid_argument("unknown enum name in vector");
        }
        out = *value;
    }

    template <std::size_t N>
    void bytes(std::string_view name, std::array<std::uint8_t, N>& out)
    {
        const Bytes data = fromHex(at(name).get<std::string>());
        if (data.size() != N) {
            throw std::invalid_argument("fixed-size byte field has the wrong length");
        }
        std::copy(data.begin(), data.end(), out.begin());
    }

    void str8(std::string_view name, std::string& out, std::size_t) { out = at(name).get<std::string>(); }
    void str16(std::string_view name, std::string& out, std::size_t) { out = at(name).get<std::string>(); }
    void str32(std::string_view name, std::string& out, std::size_t) { out = at(name).get<std::string>(); }
    void blob8(std::string_view name, Bytes& out, std::size_t) { out = fromHex(at(name).get<std::string>()); }
    void blob16(std::string_view name, Bytes& out, std::size_t) { out = fromHex(at(name).get<std::string>()); }
    void blob32(std::string_view name, Bytes& out, std::size_t) { out = fromHex(at(name).get<std::string>()); }
    void flag8(std::string_view name, bool& bit0) { bit0 = at(name).get<bool>(); }

    void flag8x2(std::string_view name0, bool& bit0, std::string_view name1, bool& bit1)
    {
        bit0 = at(name0).get<bool>();
        bit1 = at(name1).get<bool>();
    }

    void flag8x3(std::string_view name0, bool& bit0, std::string_view name1, bool& bit1, std::string_view name2,
                 bool& bit2)
    {
        bit0 = at(name0).get<bool>();
        bit1 = at(name1).get<bool>();
        bit2 = at(name2).get<bool>();
    }

    template <typename T>
    void object(std::string_view name, T& out)
    {
        out = fromJson<T>(at(name));
    }

    template <typename List>
    void list8(std::string_view name, List& out, std::size_t, std::size_t)
    {
        readList(name, out);
    }

    template <typename List>
    void list16(std::string_view name, List& out, std::size_t, std::size_t)
    {
        readList(name, out);
    }

private:
    [[nodiscard]] const nlohmann::json& at(std::string_view name) const
    {
        return m_in.at(std::string(name));
    }

    template <typename List>
    void readList(std::string_view name, List& out)
    {
        out.clear();
        for (const auto& item : at(name)) {
            out.push_back(fromJson<typename List::value_type>(item));
        }
    }

    const nlohmann::json& m_in;
};

template <typename T>
nlohmann::json toJson(const T& value)
{
    nlohmann::json out;
    JsonWriter writer{out};
    T::visit(writer, value);
    return out;
}

template <typename T>
T fromJson(const nlohmann::json& in)
{
    T value{};
    JsonReader reader{in};
    T::visit(reader, value);
    return value;
}

}
