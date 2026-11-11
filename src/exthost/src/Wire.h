#pragma once

#include "gs/exthost/Module.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace gs::exthost::detail {

class WireWriter
{
public:
    void u32(std::uint32_t value) { raw(value); }
    void u64(std::uint64_t value) { raw(value); }
    void f64(double value) { raw(value); }
    void text(std::string_view value);
    void value(const Value& value);
    void contact(const Contact& contact);
    void info(const ModuleInfo& info);
    void event(const InputEvent& event);
    void command(const TouchCommand& command);
    void answer(const StepOutput& answer);
    void inputs(const std::vector<DeclaredInput>& declared);

    [[nodiscard]] const std::string& bytes() const noexcept { return m_bytes; }

private:
    template <typename T>
    void raw(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const std::size_t at = m_bytes.size();
        m_bytes.resize(at + sizeof(T));
        std::memcpy(m_bytes.data() + at, &value, sizeof(T));
    }

    std::string m_bytes;
};

class WireReader
{
public:
    explicit WireReader(std::string_view bytes) noexcept
        : m_bytes(bytes)
    {
    }

    [[nodiscard]] bool u32(std::uint32_t& out) { return raw(out); }
    [[nodiscard]] bool u64(std::uint64_t& out) { return raw(out); }
    [[nodiscard]] bool f64(double& out) { return raw(out); }
    [[nodiscard]] bool text(std::string& out, std::size_t most);
    [[nodiscard]] bool value(Value& out);
    [[nodiscard]] bool contact(Contact& out);
    [[nodiscard]] bool info(ModuleInfo& out);
    [[nodiscard]] bool event(InputEvent& out);
    [[nodiscard]] bool command(TouchCommand& out);
    [[nodiscard]] bool answer(StepOutput& out);
    [[nodiscard]] bool inputs(std::vector<DeclaredInput>& out);
    [[nodiscard]] bool done() const noexcept { return m_at == m_bytes.size(); }

private:
    template <typename T>
    bool raw(T& out)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (m_bytes.size() - m_at < sizeof(T)) {
            return false;
        }
        std::memcpy(&out, m_bytes.data() + m_at, sizeof(T));
        m_at += sizeof(T);
        return true;
    }

    std::string_view m_bytes;
    std::size_t m_at = 0;
};

}
