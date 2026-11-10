#pragma once

#include <span>
#include <string_view>

namespace gs::script::detail::native {

inline constexpr std::string_view kHeaderPath = "include/gamescrcpy_ext.h";

struct Constant {
    std::string_view name;
    std::string_view value;
    std::string_view doc;
};

struct Enumerator {
    std::string_view name;
    int value = 0;
    std::string_view doc;
    std::string_view apiType = {};
};

struct Enumeration {
    std::string_view name;
    std::string_view doc;
    std::span<const Enumerator> values;
};

struct Field {
    std::string_view declaration;
    std::string_view doc;
};

struct Structure {
    std::string_view name;
    std::string_view doc;
    std::span<const Field> fields;
};

struct Entry {
    std::string_view name;
    std::string_view returns;
    std::string_view parameters;
    std::string_view doc;
};

[[nodiscard]] std::string_view headerDoc() noexcept;
[[nodiscard]] std::span<const Constant> constants() noexcept;
[[nodiscard]] std::span<const Enumeration> enumerations() noexcept;
[[nodiscard]] std::span<const Structure> structures() noexcept;
[[nodiscard]] const Entry& entry() noexcept;

}
