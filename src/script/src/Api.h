#pragma once

#include <quickjs.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gs::script::detail::api {

inline constexpr std::string_view kModuleName = "gamescrcpy";

enum class Stability : std::uint8_t {
    Stable,
    Preview,
    Experimental,
};

struct Param {
    std::string_view name;
    std::string_view type;
    std::string_view doc;
    bool optional = false;
};

struct Failure {
    std::string_view code;
    std::string_view when;
    std::string_view fix;
};

struct Function {
    std::string_view name;
    std::span<const Param> params;
    std::string_view returns;
    std::string_view doc;
    std::string_view example;
    JSCFunctionMagic* native = nullptr;
    int magic = 0;
    std::span<const Failure> failures = {};
    std::string_view permission = {};
    Stability stability = Stability::Stable;
    std::string_view since = "1";
    std::string_view overload = {};
};

struct Namespace {
    std::string_view name;
    std::string_view doc;
    std::span<const Function> functions;
};

struct Type {
    std::string_view name;
    std::string_view definition;
    std::string_view doc;
};

struct Member {
    std::string_view name;
    std::string_view declaration;
    std::string_view doc;
    std::string_view example = {};
};

struct Interface {
    std::string_view name;
    std::string_view doc;
    std::span<const Member> members;
    std::string_view base = {};
};

struct Value {
    std::string_view name;
    std::string_view type;
    std::string_view doc;
    std::string_view example;
};

[[nodiscard]] std::span<const Function> globals() noexcept;
[[nodiscard]] std::span<const Namespace> namespaces() noexcept;
[[nodiscard]] std::span<const Namespace> attached() noexcept;
[[nodiscard]] std::span<const Type> types() noexcept;
[[nodiscard]] std::span<const Interface> interfaces() noexcept;
[[nodiscard]] std::span<const Value> values() noexcept;
[[nodiscard]] std::vector<std::string_view> moduleExports();
[[nodiscard]] std::string_view stabilityName(Stability stability) noexcept;

[[nodiscard]] bool install(JSContext* ctx, JSValueConst global, JSValueConst action, JSValueConst library);
[[nodiscard]] JSValue host(JSContext* ctx);

class Stores
{
public:
    Stores() = default;
    Stores(const Stores&) = delete;
    Stores& operator=(const Stores&) = delete;

    [[nodiscard]] JSValue object(JSContext* ctx);
    void forget(JSContext* ctx);
    void clear();

private:
    std::unordered_map<JSContext*, JSValue> m_objects;
};

}
