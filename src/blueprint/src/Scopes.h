#pragma once

#include "Syntax.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string_view>
#include <vector>

namespace gs::blueprint::syntax {

enum class BindingKind : std::uint8_t { Var, Let, Const, Function, Class, Param, Catch, Import };

struct Scope;

struct Binding {
    std::string_view name;
    const Node* id = nullptr;
    const Node* owner = nullptr;
    BindingKind kind = BindingKind::Let;
    std::uint32_t ready = 0;
    const Scope* scope = nullptr;
};

struct Scope {
    const Node* node = nullptr;
    Scope* parent = nullptr;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    bool function = false;
    std::deque<Binding> bindings;
    std::vector<Scope*> children;
};

class Scopes
{
public:
    explicit Scopes(const Node* program);
    Scopes(const Scopes&) = delete;
    Scopes& operator=(const Scopes&) = delete;

    [[nodiscard]] const Scope* at(std::uint32_t offset) const;
    [[nodiscard]] const Binding* lookup(std::string_view name, std::uint32_t offset) const;
    [[nodiscard]] std::vector<const Binding*> visible(std::uint32_t offset) const;
    [[nodiscard]] const Scope* of(const Node* node) const;
    [[nodiscard]] const Binding* declared(const Node* id) const;
    [[nodiscard]] const Scope* root() const noexcept { return m_root; }

private:
    Scope* open(const Node* node, Scope* parent, bool function, std::uint32_t start, std::uint32_t end);
    void declare(Scope* scope, const Node* id, const Node* owner, BindingKind kind, std::uint32_t ready);
    void pattern(Scope* scope, const Node* target, const Node* owner, BindingKind kind, std::uint32_t ready);
    void walk(const Node* node, Scope* scope);
    void walkFunction(const Node* node, Scope* scope, const Node* nameNode);
    void statements(const NodeList& items, Scope* scope);
    [[nodiscard]] Scope* functionScope(Scope* scope) const;

    std::deque<Scope> m_scopes;
    std::map<const Node*, Scope*> m_byNode;
    std::map<const Node*, const Binding*> m_byId;
    Scope* m_root = nullptr;
};

void collectNames(const Node* pattern, std::vector<const Node*>& names);

}
