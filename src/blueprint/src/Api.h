#pragma once

#include "gs/blueprint/Environment.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gs::blueprint {

struct TypeNode;
using TypeRef = std::shared_ptr<const TypeNode>;

struct TypeParam {
    std::string name;
    TypeRef type;
    bool optional = false;
    bool rest = false;
};

struct TypeProp {
    std::string name;
    TypeRef type;
    bool optional = false;
    bool readonly = false;
    bool method = false;
};

struct TypeNode {
    enum class Kind : std::uint8_t { Unknown, Name, Literal, Function, Object, Array, Tuple, Union, Intersection, Template, Keyof };

    Kind kind = Kind::Unknown;
    std::string name;
    std::vector<TypeRef> items;
    std::vector<TypeParam> params;
    TypeRef result;
    std::vector<TypeProp> props;
};

[[nodiscard]] TypeRef parseType(std::string_view text);

struct ApiMember {
    std::string owner;
    std::string name;
    std::string label;
    bool property = false;
    bool readonly = false;
    bool construct = false;
    TypeRef type;
    std::vector<TypeParam> params;
    TypeRef returns;
    std::size_t order = 0;
};

struct ApiInterface {
    std::string name;
    std::string base;
    std::vector<ApiMember> members;
};

struct ApiNamespace {
    std::string name;
    std::vector<ApiMember> functions;
};

struct ApiAlias {
    std::vector<std::string> params;
    TypeRef definition;
};

class Api
{
public:
    std::vector<ApiMember> globals;
    std::map<std::string, TypeRef, std::less<>> values;
    std::vector<ApiNamespace> namespaces;
    std::map<std::string, ApiInterface, std::less<>> interfaces;
    std::vector<std::string> interfaceOrder;
    std::map<std::string, ApiAlias, std::less<>> aliases;

    [[nodiscard]] const ApiMember* global(std::string_view name) const;
    [[nodiscard]] const ApiNamespace* space(std::string_view name) const;
    [[nodiscard]] const ApiMember* function(std::string_view space, std::string_view name) const;
    [[nodiscard]] const ApiMember* member(std::string_view shape, std::string_view name) const;
    [[nodiscard]] std::vector<const ApiMember*> members(std::string_view shape) const;
    [[nodiscard]] bool derives(std::string_view shape, std::string_view base) const;
    [[nodiscard]] TypeRef resolve(const TypeRef& type) const;
    [[nodiscard]] const ApiMember* find(std::string_view label) const;

private:
    mutable std::map<const TypeNode*, std::pair<TypeRef, TypeRef>> m_resolved;
};

[[nodiscard]] bool isName(const TypeRef& type, std::string_view name) noexcept;
[[nodiscard]] TypeRef withoutEmpty(const TypeRef& type);
[[nodiscard]] TypeRef awaited(const TypeRef& type);
[[nodiscard]] bool awaitable(const TypeRef& type) noexcept;
[[nodiscard]] bool isFunctionType(const Api& api, const TypeRef& type);
[[nodiscard]] const TypeNode* functionType(const Api& api, const TypeRef& type);
[[nodiscard]] std::vector<TypeProp> propsOf(const Api& api, const TypeRef& type);
[[nodiscard]] bool isEventMember(const Api& api, const ApiMember& member);
[[nodiscard]] std::string typeText(const TypeRef& type);

}
