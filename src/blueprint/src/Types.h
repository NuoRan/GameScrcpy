#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gs::blueprint {

struct StructFieldInfo {
    std::string name;
    std::string type;
    bool optional = false;
};

struct StructInfo {
    std::string name;
    std::vector<StructFieldInfo> fields;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    bool alias = false;
};

struct EnumInfo {
    std::string name;
    std::vector<std::string> options;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};

struct UserTypes {
    std::vector<StructInfo> structs;
    std::vector<EnumInfo> enums;
};

[[nodiscard]] UserTypes scanTypes(std::string_view text);
[[nodiscard]] const StructInfo* structNamed(const UserTypes& types, std::string_view name);
[[nodiscard]] const EnumInfo* enumNamed(const UserTypes& types, std::string_view name);
[[nodiscard]] const StructFieldInfo* fieldNamed(const StructInfo& info, std::string_view name);
[[nodiscard]] std::string structDeclaration(std::string_view name, const std::vector<StructFieldInfo>& fields,
                                            std::string_view indent, std::string_view lineBreak);
[[nodiscard]] std::string enumDeclaration(std::string_view name, const std::vector<std::string>& options,
                                          std::string_view indent, std::string_view lineBreak, char quote);
[[nodiscard]] std::string enumTypeLine(std::string_view name);

}
