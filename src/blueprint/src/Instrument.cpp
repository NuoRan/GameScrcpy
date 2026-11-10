#include "gs/blueprint/Instrument.h"

#include "Model.h"

#include <gs/blueprint/Text.h>

#include <algorithm>
#include <set>
#include <utility>

namespace gs::blueprint {

using syntax::Binding;
using syntax::BindingKind;
using syntax::Node;

namespace {

struct Insertion {
    std::size_t offset = 0;
    int phase = 0;
    int rank = 0;
    std::size_t order = 0;
    std::string text;
};

int depthOf(const Chain& chain)
{
    int depth = 0;
    const Chain* current = &chain;
    while (current != nullptr && depth < 4096) {
        ++depth;
        current = current->head != nullptr ? current->head->chain : nullptr;
    }
    return depth;
}

bool hidden(std::string_view name) noexcept
{
    return name.starts_with("__gs") || name == "arguments" || name == "await" || name == "yield" || name == "eval";
}

std::string locals(const syntax::Scopes& scopes, std::uint32_t offset)
{
    std::string names;
    for (const Binding* binding : scopes.visible(offset)) {
        if (binding->ready > offset || binding->kind == BindingKind::Function || binding->kind == BindingKind::Class
            || binding->kind == BindingKind::Import || hidden(binding->name)) {
            continue;
        }
        if (!names.empty()) {
            names += ",";
        }
        names += binding->name;
    }
    return "{" + names + "}";
}

}

Instrumented instrument(std::string_view source, const Environment& environment)
{
    Instrumented result;
    const Document document(std::string(source), environment, {});
    const Document::Impl& d = document.internals();
    const Source& file = d.file();
    result.javascript = file.javascript;
    if (!document.ok() || file.lines == nullptr || file.scopes == nullptr) {
        result.problems.push_back(document.problem());
        return result;
    }
    std::vector<Insertion> insertions;
    const auto add = [&insertions](std::size_t offset, int phase, int rank, std::string text) {
        insertions.push_back({offset, phase, rank, insertions.size(), std::move(text)});
    };
    const auto position = [&file](std::uint32_t offset) { return file.lines->position(offset); };
    const auto probe = [&](std::uint32_t offset, bool async) {
        const std::string where = position(offset);
        const std::string values = locals(*file.scopes, offset);
        result.probes.push_back({where, "statement", async});
        return async ? "if(__gs.probe(\"" + where + "\"," + values + "))await __gs.pause(\"" + where + "\");"
                     : "__gs.probe(\"" + where + "\"," + values + ");";
    };
    std::set<const Node*> wrapped;
    for (const Chain& chain : d.chains) {
        if (chain.source != &file || chain.shape == Shape::Missing || chain.shape == Shape::Floating
            || chain.statements.empty()) {
            continue;
        }
        const int depth = depthOf(chain);
        const bool async = chain.async;
        const Node* function = chain.function;
        const bool bodyChain = function != nullptr
                            && ((chain.shape == Shape::Braced && chain.owner == function->body)
                                || (chain.shape == Shape::Expression && chain.owner == function));
        if (chain.shape == Shape::Expression) {
            if (!bodyChain) {
                continue;
            }
            const Node* expression = chain.statements.front();
            const std::string where = position(function->start);
            result.probes.push_back({where, "entry", async});
            add(expression->start, 1, depth * 2,
                "{const __gsf=__gs.enter(\"" + where + "\");try{" + probe(expression->start, async) + "return (");
            add(expression->end, 0, -depth, ")}finally{__gs.exit(__gsf)}}");
            continue;
        }
        if (bodyChain && wrapped.insert(function).second) {
            const Node* body = function->body;
            const std::string where = position(function->start);
            result.probes.push_back({where, "entry", async});
            add(body->start + 1, 1, depth * 2, "const __gsf=__gs.enter(\"" + where + "\");try{");
            add(body->end - 1, 0, -depth, "}finally{__gs.exit(__gsf)}");
        }
        if (chain.shape == Shape::Single) {
            const Node* statement = chain.statements.front();
            add(statement->start, 1, depth * 2, "{");
            add(statement->end, 0, -depth, "}");
        }
        for (const Item* item : chain.items) {
            if (item->index >= chain.statements.size()) {
                continue;
            }
            const Node* statement = chain.statements[item->index];
            add(statement->start, 1, depth * 2 + 1, probe(statement->start, async));
        }
    }
    for (const Item& item : d.items) {
        if (item.source == &file && item.chain == nullptr && item.statement != nullptr && item.role == Role::Event) {
            add(item.statement->start, 1, 1 << 20, probe(item.statement->start, false));
        }
    }
    std::ranges::stable_sort(insertions, [](const Insertion& left, const Insertion& right) {
        if (left.offset != right.offset) {
            return left.offset < right.offset;
        }
        if (left.phase != right.phase) {
            return left.phase < right.phase;
        }
        if (left.rank != right.rank) {
            return left.rank < right.rank;
        }
        return left.order < right.order;
    });
    std::vector<TextChange> changes;
    changes.reserve(insertions.size());
    for (Insertion& insertion : insertions) {
        changes.push_back({insertion.offset, insertion.offset, std::move(insertion.text)});
    }
    result.javascript = applied(file.javascript, changes);
    return result;
}

}
