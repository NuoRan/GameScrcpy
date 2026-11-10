#include "Scopes.h"

#include <algorithm>

namespace gs::blueprint::syntax {

void collectNames(const Node* pattern, std::vector<const Node*>& names)
{
    if (pattern == nullptr) {
        return;
    }
    switch (pattern->kind) {
    case Kind::Identifier:
        names.push_back(pattern);
        return;
    case Kind::ObjectPattern:
    case Kind::Object:
        for (const Node* item : pattern->items) {
            if (item->kind == Kind::Rest) {
                collectNames(item->target, names);
            } else if (item->kind == Kind::Property) {
                collectNames(item->value, names);
            }
        }
        return;
    case Kind::ArrayPattern:
    case Kind::Array:
        for (const Node* item : pattern->items) {
            collectNames(item, names);
        }
        return;
    case Kind::Rest:
    case Kind::Spread:
        collectNames(pattern->target, names);
        return;
    case Kind::AssignPattern:
        collectNames(pattern->target, names);
        return;
    default:
        return;
    }
}

Scopes::Scopes(const Node* program)
{
    m_root = open(program, nullptr, true, 0, program != nullptr ? program->end : 0);
    if (program != nullptr) {
        statements(program->items, m_root);
    }
    for (Scope& scope : m_scopes) {
        std::ranges::sort(scope.children, [](const Scope* a, const Scope* b) { return a->start < b->start; });
    }
}

Scope* Scopes::open(const Node* node, Scope* parent, bool function, std::uint32_t start, std::uint32_t end)
{
    Scope& scope = m_scopes.emplace_back();
    scope.node = node;
    scope.parent = parent;
    scope.function = function;
    scope.start = start;
    scope.end = end;
    if (parent != nullptr) {
        parent->children.push_back(&scope);
    }
    if (node != nullptr) {
        m_byNode[node] = &scope;
    }
    return &scope;
}

Scope* Scopes::functionScope(Scope* scope) const
{
    while (scope != nullptr && !scope->function) {
        scope = scope->parent;
    }
    return scope != nullptr ? scope : m_root;
}

void Scopes::declare(Scope* scope, const Node* id, const Node* owner, BindingKind kind, std::uint32_t ready)
{
    if (id == nullptr || id->kind != Kind::Identifier) {
        return;
    }
    scope->bindings.push_back({id->text, id, owner, kind, ready, scope});
    m_byId[id] = &scope->bindings.back();
}

void Scopes::pattern(Scope* scope, const Node* target, const Node* owner, BindingKind kind, std::uint32_t ready)
{
    std::vector<const Node*> names;
    collectNames(target, names);
    for (const Node* name : names) {
        declare(scope, name, owner, kind, ready);
    }
    std::vector<const Node*> pending{target};
    while (!pending.empty()) {
        const Node* node = pending.back();
        pending.pop_back();
        if (node == nullptr) {
            continue;
        }
        if (node->kind == Kind::AssignPattern) {
            walk(node->value, scope);
            pending.push_back(node->target);
        } else if (node->kind == Kind::ObjectPattern || node->kind == Kind::ArrayPattern) {
            for (const Node* item : node->items) {
                pending.push_back(item->kind == Kind::Property ? item->value : item);
            }
        } else if (node->kind == Kind::Rest) {
            pending.push_back(node->target);
        }
    }
}

void Scopes::statements(const NodeList& items, Scope* scope)
{
    for (const Node* item : items) {
        walk(item, scope);
    }
}

void Scopes::walkFunction(const Node* node, Scope* scope, const Node* nameNode)
{
    Scope* inner = open(node, scope, true, node->start, node->end);
    if (nameNode != nullptr) {
        declare(inner, nameNode, node, BindingKind::Function, node->start);
    }
    const std::uint32_t ready = node->body != nullptr ? node->body->start : node->end;
    for (const Node* param : node->items) {
        pattern(inner, param, node, BindingKind::Param, ready);
    }
    if (node->body == nullptr) {
        return;
    }
    if (node->body->kind == Kind::Block && !node->has(flag::kExpressionBody)) {
        m_byNode[node->body] = inner;
        statements(node->body->items, inner);
    } else {
        walk(node->body, inner);
    }
}

void Scopes::walk(const Node* node, Scope* scope)
{
    if (node == nullptr) {
        return;
    }
    switch (node->kind) {
    case Kind::VarDecl: {
        const BindingKind kind = node->text == "var" ? BindingKind::Var
                               : node->text == "const" ? BindingKind::Const
                                                       : BindingKind::Let;
        Scope* target = kind == BindingKind::Var ? functionScope(scope) : scope;
        for (const Node* declarator : node->items) {
            walk(declarator->value, scope);
            pattern(target, declarator->target, node, kind, declarator->end);
        }
        return;
    }
    case Kind::Function:
        if (node->has(flag::kDeclaration) && node->target != nullptr) {
            declare(scope, node->target, node, BindingKind::Function, scope->start);
        }
        walkFunction(node, scope, node->has(flag::kDeclaration) ? nullptr : node->target);
        return;
    case Kind::Arrow:
        walkFunction(node, scope, nullptr);
        return;
    case Kind::Class: {
        if (node->has(flag::kDeclaration) && node->target != nullptr) {
            declare(scope, node->target, node, BindingKind::Class, node->end);
        }
        walk(node->other, scope);
        Scope* inner = open(node, scope, false, node->start, node->end);
        if (!node->has(flag::kDeclaration) && node->target != nullptr) {
            declare(inner, node->target, node, BindingKind::Class, node->start);
        }
        for (const Node* member : node->items) {
            if (member->kind == Kind::StaticBlock) {
                Scope* block = open(member, inner, true, member->start, member->end);
                if (member->body != nullptr) {
                    m_byNode[member->body] = block;
                    statements(member->body->items, block);
                }
                continue;
            }
            if (member->has(flag::kComputed)) {
                walk(member->target, inner);
            }
            if (member->value != nullptr && member->value->kind == Kind::Function) {
                walkFunction(member->value, inner, nullptr);
            } else if (member->value != nullptr) {
                Scope* field = open(member, inner, true, member->start, member->end);
                walk(member->value, field);
            }
        }
        return;
    }
    case Kind::Block: {
        Scope* inner = open(node, scope, false, node->start, node->end);
        statements(node->items, inner);
        return;
    }
    case Kind::For:
    case Kind::ForIn:
    case Kind::ForOf: {
        Scope* inner = open(node, scope, false, node->start, node->end);
        if (node->target != nullptr && node->target->kind == Kind::VarDecl) {
            const Node* declaration = node->target;
            const BindingKind kind = declaration->text == "var" ? BindingKind::Var
                                   : declaration->text == "const" ? BindingKind::Const
                                                                  : BindingKind::Let;
            Scope* target = kind == BindingKind::Var ? functionScope(scope) : inner;
            for (const Node* declarator : declaration->items) {
                walk(declarator->value, inner);
                const std::uint32_t ready = node->kind == Kind::For || node->body == nullptr ? declarator->end
                                                                                             : node->body->start;
                pattern(target, declarator->target, declaration, kind, ready);
            }
        } else {
            walk(node->target, inner);
        }
        walk(node->value, inner);
        walk(node->test, inner);
        walk(node->other, inner);
        walk(node->body, inner);
        return;
    }
    case Kind::Catch: {
        Scope* inner = open(node, scope, false, node->start, node->end);
        if (node->target != nullptr) {
            pattern(inner, node->target, node, BindingKind::Catch, node->body != nullptr ? node->body->start : node->start);
        }
        if (node->body != nullptr) {
            m_byNode[node->body] = inner;
            statements(node->body->items, inner);
        }
        return;
    }
    case Kind::Switch: {
        walk(node->test, scope);
        Scope* inner = open(node, scope, false, node->start, node->end);
        for (const Node* item : node->items) {
            walk(item->test, inner);
            statements(item->items, inner);
        }
        return;
    }
    case Kind::Import:
        for (const Node* item : node->items) {
            declare(m_root, item, node, BindingKind::Import, 0);
        }
        return;
    case Kind::Export:
        walk(node->target, scope);
        return;
    default:
        break;
    }
    walk(node->target, scope);
    walk(node->value, scope);
    walk(node->test, scope);
    walk(node->body, scope);
    walk(node->other, scope);
    for (const Node* item : node->items) {
        walk(item, scope);
    }
}

const Scope* Scopes::at(std::uint32_t offset) const
{
    const Scope* scope = m_root;
    for (;;) {
        const auto after = std::ranges::upper_bound(scope->children, offset, {}, &Scope::start);
        if (after == scope->children.begin()) {
            return scope;
        }
        const Scope* candidate = *(after - 1);
        if (offset >= candidate->end) {
            return scope;
        }
        scope = candidate;
    }
}

const Binding* Scopes::lookup(std::string_view name, std::uint32_t offset) const
{
    for (const Scope* scope = at(offset); scope != nullptr; scope = scope->parent) {
        for (const Binding& binding : scope->bindings) {
            if (binding.name == name) {
                return &binding;
            }
        }
    }
    return nullptr;
}

std::vector<const Binding*> Scopes::visible(std::uint32_t offset) const
{
    std::vector<const Binding*> list;
    for (const Scope* scope = at(offset); scope != nullptr; scope = scope->parent) {
        for (const Binding& binding : scope->bindings) {
            const bool shadowed = std::ranges::any_of(list, [&binding](const Binding* seen) {
                return seen->name == binding.name;
            });
            if (!shadowed) {
                list.push_back(&binding);
            }
        }
    }
    return list;
}

const Scope* Scopes::of(const Node* node) const
{
    const auto found = m_byNode.find(node);
    return found == m_byNode.end() ? nullptr : found->second;
}

const Binding* Scopes::declared(const Node* id) const
{
    const auto found = m_byId.find(id);
    return found == m_byId.end() ? nullptr : found->second;
}

}
