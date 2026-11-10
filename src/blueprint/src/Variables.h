#pragma once

#include "Syntax.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::blueprint {

struct VariableType {
    std::string kind;
};

struct VariableDoc {
    std::string description;
    std::string category;
    bool decimal = false;
    bool duration = false;
    bool keep = false;
    bool editable = false;
};

struct VariableInfo {
    std::string name;
    std::string kind = "text";
    std::string type = "string";
    std::string container;
    std::string keyKind = "text";
    std::string category;
    std::string description;
    std::string initial;
    bool keep = false;
    bool editable = false;
    bool constant = false;
    bool local = false;
    bool inClass = false;
    std::string page;
    const syntax::Node* declaration = nullptr;
    const syntax::Node* declarator = nullptr;
    const syntax::Node* id = nullptr;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};

struct VariableSpec {
    std::string name;
    std::string kind = "integer";
    std::string type;
    std::string container;
    std::string keyKind = "text";
    std::string value;
    std::string category;
    std::string description;
    std::string scope = "file";
    bool keep = false;
    bool editable = false;
    bool constant = false;
};

[[nodiscard]] const std::vector<VariableType>& variableTypes();
[[nodiscard]] std::string variableAnnotation(std::string_view kind, std::string_view type, std::string_view container,
                                             std::string_view keyKind);
[[nodiscard]] std::string variableDefault(std::string_view kind, std::string_view type, std::string_view container,
                                          std::string_view keyKind, char quote);
[[nodiscard]] std::string variableInitial(const VariableSpec& spec, char quote);
[[nodiscard]] std::string variableDocComment(const VariableSpec& spec);
[[nodiscard]] std::string variableDeclaration(const VariableSpec& spec, char quote, bool semicolons);
[[nodiscard]] VariableDoc parseVariableDoc(std::string_view text, std::span<const syntax::Comment> comments,
                                           std::uint32_t start);
[[nodiscard]] std::string_view variableElement(std::string_view annotation);
[[nodiscard]] std::string variableKindOfName(std::string_view name);
[[nodiscard]] bool variableEditableKind(std::string_view kind);
[[nodiscard]] std::string variablePropKind(std::string_view kind);

}
