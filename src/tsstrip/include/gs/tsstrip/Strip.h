#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gs::tsstrip {

enum class ProblemCode : std::uint16_t {
    Enum = 1,
    ConstEnum = 2,
    Namespace = 3,
    ParameterProperty = 4,
    ImportRequire = 5,
    ImportAlias = 6,
    ExportAssignment = 7,
    Decorator = 8,
    Jsx = 9,
    UnsafeAssertion = 10,
    Syntax = 100,
};

struct Problem {
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    ProblemCode code = ProblemCode::Syntax;
    std::string message;

    friend bool operator==(const Problem&, const Problem&) = default;
};

struct Result {
    std::string javascript;
    std::vector<Problem> problems;
};

enum class Offsets : std::uint8_t { Utf16, Bytes };

[[nodiscard]] Result strip(std::string_view source, Offsets offsets = Offsets::Utf16);
[[nodiscard]] std::string codeName(ProblemCode code);

}
