#include "Model.h"

#include <gs/tsstrip/Tokens.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <utility>

namespace gs::blueprint {

using Ordered = nlohmann::ordered_json;
using syntax::Binding;
using syntax::BindingKind;
using syntax::Kind;
using syntax::Node;
using syntax::unparen;
namespace flag = syntax::flag;

namespace {

constexpr std::size_t kEnd = kNone;
constexpr double kChainGap = 60;

bool breakAt(char c) noexcept
{
    return c == '\n' || c == '\r';
}

std::size_t lineStartOf(const std::string& text, std::size_t at)
{
    at = std::min(at, text.size());
    while (at > 0 && !breakAt(text[at - 1])) {
        --at;
    }
    return at;
}

std::size_t lineEndOf(const std::string& text, std::size_t at)
{
    while (at < text.size() && !breakAt(text[at])) {
        ++at;
    }
    return at;
}

std::size_t afterLine(const std::string& text, std::size_t at)
{
    at = lineEndOf(text, at);
    if (at < text.size() && text[at] == '\r') {
        ++at;
    }
    if (at < text.size() && text[at] == '\n') {
        ++at;
    }
    return at;
}

std::string indentOf(const std::string& text, std::size_t at)
{
    const std::size_t start = lineStartOf(text, at);
    std::size_t end = start;
    while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
        ++end;
    }
    return text.substr(start, end - start);
}

bool blankBefore(const std::string& text, std::size_t at)
{
    for (std::size_t i = lineStartOf(text, at); i < at; ++i) {
        if (text[i] != ' ' && text[i] != '\t') {
            return false;
        }
    }
    return true;
}

bool blankAfter(const std::string& text, std::size_t at)
{
    for (std::size_t i = at; i < text.size() && !breakAt(text[i]); ++i) {
        if (text[i] != ' ' && text[i] != '\t' && text[i] != ';') {
            return false;
        }
    }
    return true;
}

std::string renamedControl(std::string_view text, std::string_view from, std::string_view to)
{
    const std::vector<tsstrip::CodeToken> tokens = tsstrip::tokenize(text);
    std::string out;
    std::size_t copied = 0;
    for (std::size_t i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens[i].kind != tsstrip::TokenKind::Name || tokens[i].text != "controls") {
            continue;
        }
        if (tokens[i + 1].kind != tsstrip::TokenKind::Punctuator || tokens[i + 1].text != ".") {
            continue;
        }
        if (tokens[i + 2].kind != tsstrip::TokenKind::Name || tokens[i + 2].text != from) {
            continue;
        }
        if (i > 0 && tokens[i - 1].kind == tsstrip::TokenKind::Punctuator && tokens[i - 1].text == ".") {
            continue;
        }
        const std::size_t start = static_cast<std::size_t>(tokens[i].text.data() - text.data());
        const std::size_t end =
            static_cast<std::size_t>(tokens[i + 2].text.data() - text.data()) + tokens[i + 2].text.size();
        out.append(text.substr(copied, start - copied));
        out += "controls.";
        out += to;
        copied = end;
    }
    out.append(text.substr(copied));
    return out;
}

std::string reindent(std::string_view code, std::string_view indent, std::string_view lineBreak)
{
    std::string out;
    std::size_t at = 0;
    bool first = true;
    while (at <= code.size()) {
        std::size_t end = code.find('\n', at);
        if (end == std::string_view::npos) {
            end = code.size();
        }
        std::string_view line = code.substr(at, end - at);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        if (!first) {
            out += lineBreak;
        }
        first = false;
        if (!line.empty()) {
            out += indent;
            out += line;
        }
        at = end + 1;
    }
    return out;
}

std::string firstIndented(std::string_view code, std::string_view indent, std::string_view lineBreak)
{
    std::string text = reindent(code, indent, lineBreak);
    if (text.starts_with(indent)) {
        text.erase(0, indent.size());
    }
    return text;
}

bool pairText(std::string_view text)
{
    int depth = 0;
    char quoted = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted != 0) {
            if (c == '\\') {
                ++i;
            } else if (c == quoted) {
                quoted = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            quoted = c;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            --depth;
            continue;
        }
        if (c == ',' && depth == 0) {
            return true;
        }
    }
    return false;
}

struct Span {
    std::size_t start = 0;
    std::size_t end = 0;
    bool lines = false;
};

bool isIdentifierPoint(std::uint32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$'
        || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || (c >= 0x370 && c <= 0x52F)
        || (c >= 0x3040 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF)
        || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF);
}

std::string identifierFrom(std::string_view label, std::string_view fallback)
{
    std::string out;
    std::size_t i = 0;
    while (i < label.size()) {
        const auto lead = static_cast<unsigned char>(label[i]);
        std::size_t length = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
        length = std::min(length, label.size() - i);
        std::uint32_t value = lead;
        if (length == 2) {
            value = ((lead & 0x1Fu) << 6) | (static_cast<unsigned char>(label[i + 1]) & 0x3Fu);
        } else if (length == 3) {
            value = ((lead & 0x0Fu) << 12) | ((static_cast<unsigned char>(label[i + 1]) & 0x3Fu) << 6)
                  | (static_cast<unsigned char>(label[i + 2]) & 0x3Fu);
        } else if (length == 4) {
            value = 0x10000;
        }
        if (isIdentifierPoint(value)) {
            out.append(label.substr(i, length));
        }
        i += length;
    }
    if (out.empty() || (out.front() >= '0' && out.front() <= '9') || !identifierText(out)) {
        return std::string(fallback);
    }
    return out;
}

std::string stringLiteral(std::string_view text, char quote)
{
    std::string out(1, quote);
    for (const char c : text) {
        if (c == quote || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out.push_back(c);
        }
    }
    out.push_back(quote);
    return out;
}

std::string access(std::string_view object, std::string_view name, char quote)
{
    return identifierText(name) ? std::string(object) + "." + std::string(name)
                                : std::string(object) + "[" + stringLiteral(name, quote) + "]";
}

const Node* propertyValue(const Node* object, std::string_view name)
{
    object = unparen(object);
    if (object == nullptr || object->kind != Kind::Object) {
        return nullptr;
    }
    for (const Node* item : object->items) {
        if (item->kind != Kind::Property || item->has(flag::kSpread) || item->has(flag::kComputed)
            || item->target == nullptr) {
            continue;
        }
        const std::string key = item->target->kind == Kind::String ? unquote(item->target->text)
                                                                   : std::string(item->target->text);
        if (key == name) {
            return item->value;
        }
    }
    return nullptr;
}

const Node* topStatement(const Item* item)
{
    while (item != nullptr && item->role == Role::Pure) {
        item = item->consumer;
    }
    return item != nullptr ? item->statement : nullptr;
}

std::string controlObject(const Node* statement)
{
    if (statement == nullptr || statement->kind != Kind::ExpressionStatement) {
        return {};
    }
    const Node* call = unparen(statement->target);
    if (call == nullptr || call->kind != Kind::Call) {
        return {};
    }
    const Node* callee = unparen(call->target);
    if (callee == nullptr || callee->kind != Kind::Member) {
        return {};
    }
    const Node* object = unparen(callee->target);
    if (object == nullptr || object->kind != Kind::Member || object->has(flag::kComputed) || object->value == nullptr) {
        return {};
    }
    const Node* root = unparen(object->target);
    if (root == nullptr || root->kind != Kind::Identifier || root->text != "controls") {
        return {};
    }
    return std::string(object->value->text);
}

const Item* statementOwner(const Item* item)
{
    while (item != nullptr && item->role == Role::Pure) {
        item = item->consumer;
    }
    return item;
}

struct Plan {
    const Chain* chain = nullptr;
    std::set<std::size_t> removed;
    std::size_t at = kEnd;
    std::vector<std::string> inserted;
};

class Edit
{
public:
    explicit Edit(const Document::Impl& impl) : d(impl), layout(impl.layout) {}

    const Document::Impl& d;
    Layout layout;
    std::map<const Source*, std::vector<TextChange>> changes;
    std::vector<Plan> plans;
    std::set<const Source*> dropped;
    std::set<const Node*> asyncFunctions;
    std::string reason;
    std::string key;
    std::string signature;
    std::string page;

    [[nodiscard]] std::string say(std::string_view text) const { return d.say(text); }
    [[nodiscard]] const std::string& lineBreak() const { return d.style.lineBreak; }
    [[nodiscard]] const std::string& unit() const { return d.style.indent; }
    [[nodiscard]] std::string semi() const { return d.style.semicolons ? ";" : ""; }
    [[nodiscard]] char quote() const { return d.style.quote; }

    void change(const Source& source, std::size_t start, std::size_t end, std::string text)
    {
        changes[&source].push_back({start, end, std::move(text)});
    }

    Outcome refuse(std::string why)
    {
        Outcome outcome;
        outcome.reason = std::move(why);
        outcome.layout = d.layout;
        return outcome;
    }

    Plan& plan(const Chain* chain)
    {
        for (Plan& existing : plans) {
            if (existing.chain == chain) {
                return existing;
            }
        }
        Plan& made = plans.emplace_back();
        made.chain = chain;
        return made;
    }

    [[nodiscard]] std::string anchorOf(std::string_view pageKey) const
    {
        const Page* found = d.page(pageKey);
        return found != nullptr ? found->anchor : std::string(pageKey);
    }

    std::string floatCode(std::string_view pageKey, std::string code, double x, double y)
    {
        PageLayout& target = layout.pages[anchorOf(pageKey)];
        std::string fresh;
        for (int n = 1;; ++n) {
            fresh = "f" + std::to_string(n);
            const bool taken = std::ranges::any_of(target.floating, [&fresh](const FloatingCode& item) {
                return item.key == fresh;
            });
            if (!taken) {
                break;
            }
        }
        target.floating.push_back({fresh, std::move(code), x, y});
        return fresh;
    }

    [[nodiscard]] Span span(const Source& source, const Node* first, const Node* last) const
    {
        const std::string& text = source.text;
        const std::vector<syntax::Comment>& comments = source.tree.comments;
        std::size_t start = first->start;
        auto it = std::ranges::lower_bound(comments, first->start, {}, &syntax::Comment::start);
        std::size_t boundary = first->start;
        while (it != comments.begin()) {
            --it;
            if (it->end > boundary) {
                continue;
            }
            const std::string_view gap(text.data() + it->end, boundary - it->end);
            if (gap.find_first_not_of(" \t\r\n") != std::string_view::npos || std::ranges::count(gap, '\n') > 1) {
                break;
            }
            if (!blankBefore(text, it->start)) {
                break;
            }
            start = it->start;
            boundary = it->start;
        }
        std::size_t end = last->end;
        const auto after = std::ranges::lower_bound(comments, last->end, {}, &syntax::Comment::start);
        if (after != comments.end() && after->start >= last->end) {
            const std::string_view gap(text.data() + last->end, after->start - last->end);
            if (gap.find_first_not_of(" \t;,") == std::string_view::npos) {
                end = after->end;
            }
        }
        if (blankBefore(text, start) && blankAfter(text, end)) {
            return {lineStartOf(text, start), afterLine(text, end), true};
        }
        return {start, end, false};
    }

    [[nodiscard]] std::string statementText(const Chain& chain, std::size_t index) const
    {
        const Source& source = *chain.source;
        const Node* statement = chain.statements[index];
        if (chain.shape == Shape::Expression) {
            return d.text(source, statement);
        }
        const Span range = span(source, statement, statement);
        std::string text = d.text(source, static_cast<std::uint32_t>(range.start), static_cast<std::uint32_t>(range.end));
        return dedent(text);
    }

    [[nodiscard]] std::string runText(const Chain& chain, std::size_t from, std::size_t to = kEnd) const
    {
        std::string text;
        for (std::size_t i = from; i < std::min(to, chain.statements.size()); ++i) {
            if (!text.empty()) {
                text += "\n";
            }
            text += statementText(chain, i);
        }
        return text;
    }

    [[nodiscard]] std::string chainIndent(const Chain& chain) const
    {
        const Source& source = *chain.source;
        const std::string& text = source.text;
        if (chain.shape == Shape::Program || chain.shape == Shape::Floating) {
            return {};
        }
        if (!chain.statements.empty() && chain.shape == Shape::Braced) {
            const Node* first = chain.statements.front();
            if (blankBefore(text, first->start)) {
                return indentOf(text, first->start);
            }
        }
        if (chain.shape == Shape::Braced && chain.owner != nullptr) {
            const std::size_t close = chain.owner->end > 0 ? chain.owner->end - 1 : 0;
            const bool sameLine = lineStartOf(text, close) == lineStartOf(text, chain.owner->start);
            return indentOf(text, sameLine ? chain.owner->start : close) + unit();
        }
        const Node* parent = chain.parent != nullptr ? chain.parent : chain.owner;
        if (chain.shape == Shape::Expression && chain.owner != nullptr) {
            return indentOf(text, chain.owner->start) + unit();
        }
        return (parent != nullptr ? indentOf(text, parent->start) : std::string()) + unit();
    }

    void emit(const Plan& work)
    {
        const Chain& chain = *work.chain;
        const Source& source = *chain.source;
        const std::string& text = source.text;
        const std::string indent = chainIndent(chain);
        std::vector<std::string> inserted;
        for (const std::string& code : work.inserted) {
            inserted.push_back(code);
        }
        if (chain.shape == Shape::Single || chain.shape == Shape::Expression) {
            const Node* statement = chain.statements.empty() ? nullptr : chain.statements.front();
            std::vector<std::string> final;
            const bool kept = statement != nullptr && !work.removed.contains(0);
            if (kept) {
                std::string own = chain.shape == Shape::Expression ? d.text(source, statement) : statementText(chain, 0);
                if (chain.shape == Shape::Expression && !inserted.empty()) {
                    const Node* expression = unparen(statement);
                    if (expression->kind == Kind::Paren || expression->kind == Kind::Object) {
                        own = "(" + own + ")";
                    }
                    if (awaitableCall(chain, expression)) {
                        own = "await " + own;
                        asyncFunctions.insert(chain.function);
                    }
                }
                final.push_back(own);
            }
            const std::size_t at = work.at == kEnd ? final.size() : std::min(work.at, final.size());
            final.insert(final.begin() + static_cast<std::ptrdiff_t>(at), inserted.begin(), inserted.end());
            if (statement == nullptr) {
                return;
            }
            if (kept && inserted.empty()) {
                return;
            }
            const Node* parent = chain.parent;
            const std::string outer = chain.shape == Shape::Expression ? indentOf(text, chain.owner->start)
                                    : parent != nullptr                 ? indentOf(text, parent->start)
                                                                        : indentOf(text, statement->start);
            if (final.empty()) {
                if (parent != nullptr && parent->kind == Kind::If && parent->other == statement) {
                    const std::string_view between(text.data() + parent->body->end, statement->start - parent->body->end);
                    const std::size_t word = between.rfind("else");
                    const std::size_t from = word == std::string_view::npos ? statement->start : parent->body->end + word;
                    std::size_t trim = from;
                    while (trim > parent->body->end && (text[trim - 1] == ' ' || text[trim - 1] == '\t')) {
                        --trim;
                    }
                    change(source, trim, statement->end, {});
                    return;
                }
                change(source, statement->start, statement->end, "{}");
                return;
            }
            std::string body = "{" + lineBreak();
            for (const std::string& code : final) {
                body += reindent(code, indent, lineBreak()) + lineBreak();
            }
            body += outer + "}";
            change(source, statement->start, statement->end, std::move(body));
            return;
        }
        if (chain.shape == Shape::Missing) {
            if (inserted.empty() || chain.parent == nullptr) {
                return;
            }
            const Node* parent = chain.parent;
            const std::string outer = indentOf(text, parent->start);
            std::string tail = " else {" + lineBreak();
            for (const std::string& code : inserted) {
                tail += reindent(code, outer + unit(), lineBreak()) + lineBreak();
            }
            tail += outer + "}";
            const Node* consequent = parent->body;
            if (consequent->kind == Kind::Block) {
                change(source, consequent->end, consequent->end, std::move(tail));
            } else {
                std::string wrapped = "{" + lineBreak()
                                    + reindent(dedent(d.text(source, consequent)), outer + unit(), lineBreak())
                                    + lineBreak() + outer + "}" + tail;
                change(source, consequent->start, consequent->end, std::move(wrapped));
            }
            return;
        }
        for (const std::size_t index : work.removed) {
            if (index >= chain.statements.size()) {
                continue;
            }
            const Node* statement = chain.statements[index];
            const Span range = span(source, statement, statement);
            std::size_t end = range.end;
            if (!range.lines) {
                while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
                    ++end;
                }
            }
            change(source, range.start, end, {});
        }
        if (inserted.empty()) {
            return;
        }
        std::string block;
        for (const std::string& code : inserted) {
            block += reindent(code, indent, lineBreak()) + lineBreak();
        }
        const std::size_t at = work.at == kEnd ? chain.statements.size() : work.at;
        if (at < chain.statements.size()) {
            const Node* next = chain.statements[at];
            const Span range = span(source, next, next);
            if (range.lines) {
                change(source, range.start, range.start, std::move(block));
            } else {
                std::string inline_;
                for (const std::string& code : inserted) {
                    inline_ += code + "; ";
                }
                change(source, range.start, range.start, std::move(inline_));
            }
            return;
        }
        if (!chain.statements.empty()) {
            const Node* last = chain.statements.back();
            const Span range = span(source, last, last);
            if (range.lines) {
                std::size_t position = range.end;
                std::string prefix;
                if (position > 0 && !breakAt(text[position - 1])) {
                    prefix = lineBreak();
                }
                change(source, position, position, prefix + block);
            } else {
                std::string inline_;
                for (const std::string& code : inserted) {
                    inline_ += "; " + code;
                }
                change(source, range.end, range.end, std::move(inline_));
            }
            return;
        }
        if (chain.shape == Shape::Program || chain.shape == Shape::Floating) {
            std::string prefix = text.empty() || breakAt(text.back()) ? std::string() : lineBreak();
            change(source, text.size(), text.size(), prefix + block);
            return;
        }
        const Node* owner = chain.owner;
        const std::size_t open = owner->start;
        const std::size_t close = owner->end > 0 ? owner->end - 1 : owner->end;
        if (lineStartOf(text, open) == lineStartOf(text, close)) {
            const std::string outer = indentOf(text, open);
            change(source, open, owner->end, "{" + lineBreak() + block + outer + "}");
            return;
        }
        change(source, afterLine(text, open), afterLine(text, open), std::move(block));
    }

    [[nodiscard]] bool awaitableCall(const Chain& chain, const Node* expression) const
    {
        const Node* call = unparen(expression);
        if (call == nullptr || call->kind != Kind::Call) {
            return false;
        }
        for (const Item* item : chain.items) {
            if (item->expression == call) {
                if (item->apiMember != nullptr) {
                    return awaitable(item->apiMember->returns);
                }
                if (item->role == Role::Custom) {
                    const auto declared = d.functions.find(item->callee);
                    return declared != d.functions.end() && declared->second->has(flag::kAsync);
                }
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t functionHead(const Source& source, const Node* function) const
    {
        if (function->kind == Kind::Arrow) {
            return function->start;
        }
        std::size_t head = function->start;
        visit(source.tree.program, [&](const Node* node) {
            if ((node->kind == Kind::ClassMember || node->kind == Kind::Property) && node->value == function) {
                head = node->has(flag::kComputed) || node->target == nullptr ? node->start : node->target->start;
                if (node->has(flag::kGenerator) && head > 0) {
                    const std::string_view before(source.text.data() + node->start, head - node->start);
                    if (const std::size_t star = before.rfind('*'); star != std::string_view::npos) {
                        head = node->start + star;
                    }
                }
            }
        });
        return head;
    }

    void makeAsync()
    {
        std::set<const Node*> done;
        std::vector<const Node*> pending(asyncFunctions.begin(), asyncFunctions.end());
        while (!pending.empty()) {
            const Node* function = pending.back();
            pending.pop_back();
            if (function == nullptr || function->has(flag::kAsync) || !done.insert(function).second) {
                continue;
            }
            const Source* owner = nullptr;
            for (const Source& source : d.sources) {
                if (source.tree.program != nullptr && function->start < source.text.size()) {
                    bool inside = false;
                    visit(source.tree.program, [&](const Node* node) { inside = inside || node == function; });
                    if (inside) {
                        owner = &source;
                        break;
                    }
                }
            }
            if (owner == nullptr) {
                continue;
            }
            const std::size_t head = functionHead(*owner, function);
            change(*owner, head, head, "async ");
            if (function->kind != Kind::Function) {
                continue;
            }
            std::string name;
            for (const auto& [declared, node] : d.functions) {
                if (node == function) {
                    name = declared;
                }
            }
            for (const Item& item : d.items) {
                if (name.empty() || item.role != Role::Custom || item.callee != name || item.chain == nullptr
                    || item.statement == nullptr || item.source == nullptr) {
                    continue;
                }
                const Node* statement = item.statement;
                if (statement->kind != Kind::ExpressionStatement || unparen(statement->target)->kind == Kind::Await) {
                    continue;
                }
                if (item.chain->function == nullptr) {
                    continue;
                }
                const Node* call = unparen(statement->target);
                change(*item.source, call->start, call->start, "await ");
                pending.push_back(item.chain->function);
            }
        }
    }

    void writePointGroup(const Item& item, const Pin& pin, const std::string& text)
    {
        const Source& source = *item.source;
        const Slot& slot = pin.slot;
        const auto& args = slot.call->items;
        std::vector<std::size_t> members;
        for (std::size_t i = 0; i < item.inputs.size(); ++i) {
            const Pin& candidate = item.inputs[i];
            if (candidate.slot.kind == SlotKind::Argument && candidate.slot.call == slot.call
                && candidate.slot.group == slot.group) {
                members.push_back(i);
            }
        }
        if (members.empty() || members.size() > 2) {
            return;
        }
        std::vector<std::string> pieces;
        std::vector<bool> paired;
        for (const std::size_t index : members) {
            const Pin& member = item.inputs[index];
            if (&member == &pin) {
                pieces.push_back(text.empty() ? std::string("undefined") : text);
                paired.push_back(pairText(text));
                continue;
            }
            std::string piece;
            if (member.slot.pair && member.slot.index + 1 < args.size()) {
                piece = d.text(source, args[member.slot.index]->start, args[member.slot.index + 1]->end);
            } else if (member.slot.node != nullptr) {
                piece = d.text(source, member.slot.node);
            }
            pieces.push_back(piece.empty() ? std::string("undefined") : piece);
            paired.push_back(member.slot.pair && !piece.empty());
        }
        const Pin* duration = nullptr;
        for (std::size_t i = members.back() + 1; i < item.inputs.size(); ++i) {
            const Pin& candidate = item.inputs[i];
            if (candidate.slot.kind == SlotKind::Argument && candidate.slot.call == slot.call) {
                duration = &candidate;
                break;
            }
        }
        const bool followed = duration != nullptr && duration->slot.node != nullptr;
        std::vector<std::string> chunks;
        if (pieces.size() == 1) {
            chunks.push_back(pieces[0]);
            if (followed && !paired[0]) {
                chunks.push_back("undefined");
            }
        } else {
            chunks.push_back(pieces[0]);
            if (paired[0]) {
                chunks.push_back(pieces[1]);
                if (!paired[1]) {
                    chunks.push_back("undefined");
                }
            } else if (paired[1]) {
                chunks.push_back("undefined");
                chunks.push_back(pieces[1]);
            } else {
                chunks.push_back(pieces[1]);
            }
        }
        if (followed) {
            const std::string ms = d.text(source, duration->slot.node);
            if (!ms.empty()) {
                chunks.push_back(ms);
            }
        }
        std::string joined;
        for (const std::string& chunk : chunks) {
            joined += (joined.empty() ? "" : ", ") + chunk;
        }
        if (slot.group >= args.size()) {
            const std::size_t close = slot.call->end > 0 ? slot.call->end - 1 : slot.call->end;
            change(source, close, close, joined);
            return;
        }
        change(source, args[slot.group]->start, args.back()->end, joined);
    }

    void writeInput(const Item& item, const Pin& pin, const std::string& text)
    {
        const Source& source = *item.source;
        const Slot& slot = pin.slot;
        switch (slot.kind) {
        case SlotKind::Argument:
        case SlotKind::Rest: {
            if (slot.group != kNone) {
                writePointGroup(item, pin, text);
                return;
            }
            const Node* call = slot.call;
            const auto& args = call->items;
            const std::size_t index = slot.index;
            if (index < args.size()) {
                change(source, args[index]->start, args[index]->end, text);
                return;
            }
            std::string fillers;
            for (std::size_t i = args.size(); i < index; ++i) {
                fillers += "undefined, ";
            }
            const std::size_t close = call->end > 0 ? call->end - 1 : call->end;
            change(source, close, close, (args.empty() ? std::string() : std::string(", ")) + fillers + text);
            return;
        }
        case SlotKind::Option: {
            const Node* object = slot.owner;
            const std::string entry = access("", slot.option, quote()).substr(1);
            const std::string property = (identifierText(slot.option) ? slot.option : stringLiteral(slot.option, quote())) + ": "
                                       + text;
            if (object != nullptr && object->kind == Kind::Object) {
                if (slot.node != nullptr) {
                    change(source, slot.node->start, slot.node->end, text);
                    return;
                }
                if (object->items.empty()) {
                    change(source, object->start, object->end, "{ " + property + " }");
                    return;
                }
                const Node* last = object->items.back();
                change(source, last->end, last->end, ", " + property);
                return;
            }
            static_cast<void>(entry);
            Pin whole = pin;
            whole.slot.kind = SlotKind::Argument;
            whole.slot.width = 1;
            writeInput(item, whole, "{ " + property + " }");
            return;
        }
        case SlotKind::Expression:
            if (slot.node != nullptr) {
                change(source, slot.node->start, slot.node->end, text);
            } else if (slot.owner != nullptr && slot.owner->kind == Kind::Return) {
                change(source, slot.owner->start + 6, slot.owner->start + 6, " " + text);
            }
            return;
        case SlotKind::Declaration:
            if (slot.node != nullptr) {
                change(source, slot.node->start, slot.node->end, text);
            } else if (slot.owner != nullptr) {
                change(source, slot.owner->target->end, slot.owner->target->end, " = " + text);
            }
            return;
        case SlotKind::None:
            return;
        }
    }

    [[nodiscard]] std::string emptyValue(const Pin& pin) const
    {
        if (pin.slot.condition) {
            return "false";
        }
        switch (pin.type) {
        case PinType::Number:
        case PinType::Duration:
            return "0";
        case PinType::Bool:
            return "false";
        case PinType::Text:
            return pin.choices.empty() ? stringLiteral("", quote()) : stringLiteral(pin.choices.front().value, quote());
        default:
            return "undefined";
        }
    }

    void floatPure(const Item& pure)
    {
        if (pure.expression == nullptr || pure.source == nullptr) {
            return;
        }
        floatCode(pure.page, d.text(*pure.source, pure.expression), pure.x, pure.y);
    }

    void clearInput(const Item& item, const Pin& pin, bool keep = true)
    {
        const Source& source = *item.source;
        const Slot& slot = pin.slot;
        if (keep) {
            for (const Item* pure : feeders(item, pin.name)) {
                floatPure(*pure);
            }
        }
        switch (slot.kind) {
        case SlotKind::Argument:
        case SlotKind::Rest: {
            if (slot.group != kNone) {
                writePointGroup(item, pin, "undefined");
                return;
            }
            const auto& args = slot.call->items;
            if (slot.index >= args.size()) {
                return;
            }
            const std::size_t width = std::min(slot.width, args.size() - slot.index);
            const bool last = slot.index + width >= args.size();
            if (pin.optional && last) {
                std::size_t kept = slot.index;
                while (kept > 0) {
                    const Node* previous = unparen(args[kept - 1]);
                    if (previous->kind == Kind::Identifier && previous->text == "undefined") {
                        --kept;
                        continue;
                    }
                    break;
                }
                const std::size_t from = kept == 0 ? slot.call->target->end : args[kept - 1]->end;
                std::size_t open = from;
                if (kept == 0) {
                    while (open < source.text.size() && source.text[open] != '(') {
                        ++open;
                    }
                    ++open;
                }
                change(source, open, args.back()->end, {});
                return;
            }
            const std::string value = emptyValue(pin);
            if (width == 2) {
                change(source, args[slot.index]->start, args[slot.index + 1]->end, value + ", " + value);
            } else {
                change(source, args[slot.index]->start, args[slot.index]->end, value);
            }
            return;
        }
        case SlotKind::Option: {
            const Node* object = slot.owner;
            if (object == nullptr || slot.node == nullptr) {
                return;
            }
            const auto at = std::ranges::find_if(object->items, [&slot](const Node* entry) {
                return entry->kind == Kind::Property && entry->value == slot.node;
            });
            if (at == object->items.end()) {
                return;
            }
            const std::size_t index = static_cast<std::size_t>(at - object->items.begin());
            if (object->items.size() == 1) {
                const auto& args = slot.call->items;
                if (slot.index + 1 == args.size()) {
                    const std::size_t from = slot.index == 0 ? object->start : args[slot.index - 1]->end;
                    change(source, from, object->end, {});
                } else {
                    change(source, object->start, object->end, "{}");
                }
                return;
            }
            if (index + 1 < object->items.size()) {
                change(source, (*at)->start, object->items[index + 1]->start, {});
            } else {
                change(source, object->items[index - 1]->end, (*at)->end, {});
            }
            return;
        }
        case SlotKind::Expression:
            if (slot.node == nullptr) {
                return;
            }
            if (slot.owner != nullptr && slot.owner->kind == Kind::Return) {
                change(source, slot.owner->start + 6, slot.node->end, {});
                return;
            }
            change(source, slot.node->start, slot.node->end, emptyValue(pin));
            return;
        case SlotKind::Declaration:
            if (slot.node != nullptr && slot.owner != nullptr) {
                change(source, slot.owner->target->end, slot.node->end, {});
            }
            return;
        case SlotKind::None:
            return;
        }
    }

    [[nodiscard]] std::vector<const Item*> feeders(const Item& item, std::string_view pinName) const
    {
        std::vector<const Item*> list;
        for (const Item& candidate : d.items) {
            if (candidate.consumer == &item && candidate.consumerPin == pinName) {
                list.push_back(&candidate);
            }
        }
        return list;
    }

    [[nodiscard]] std::string literal(const Item& item, const Pin& pin, const nlohmann::json& value) const
    {
        if (pin.editor == "code") {
            return value.is_string() ? value.get<std::string>() : value.dump();
        }
        const auto number = [](const nlohmann::json& v) { return v.is_number() ? v.get<double>() : 0.0; };
        if (value.is_object()) {
            const double x = number(value.value("x", nlohmann::json(0.0)));
            const double y = number(value.value("y", nlohmann::json(0.0)));
            if (value.contains("w") || value.contains("width")) {
                const double w = number(value.contains("w") ? value["w"] : value["width"]);
                const double h = number(value.contains("h") ? value["h"] : value["height"]);
                return "{ x: " + numberText(x) + ", y: " + numberText(y) + ", width: " + numberText(w) + ", height: "
                     + numberText(h) + " }";
            }
            if (pin.slot.pair || pin.slot.group != kNone) {
                return numberText(x) + ", " + numberText(y);
            }
            return "{ x: " + numberText(x) + ", y: " + numberText(y) + " }";
        }
        switch (pin.type) {
        case PinType::Number:
            return numberText(number(value));
        case PinType::Duration:
            return numberText(std::round(number(value)));
        case PinType::Bool:
            return value.is_boolean() && value.get<bool>() ? "true" : "false";
        case PinType::Point:
            if (value.is_string()) {
                return "res.point(" + stringLiteral(value.get<std::string>(), quote()) + ")";
            }
            break;
        case PinType::Component: {
            const std::string id = value.is_string() ? value.get<std::string>() : std::string();
            return access("controls", id, quote());
        }
        case PinType::Image: {
            const std::string name = value.is_string() ? value.get<std::string>() : std::string();
            const Node* current = unparen(pin.slot.node);
            if (current != nullptr && current->kind == Kind::Member && identifierText(name)) {
                return "res." + name;
            }
            return stringLiteral(name, quote());
        }
        default:
            break;
        }
        static_cast<void>(item);
        if (value.is_boolean()) {
            return value.get<bool>() ? "true" : "false";
        }
        if (value.is_number()) {
            return numberText(value.get<double>());
        }
        if (value.is_string()) {
            return stringLiteral(value.get<std::string>(), quote());
        }
        return "undefined";
    }

    Outcome done()
    {
        for (const Plan& work : plans) {
            emit(work);
        }
        makeAsync();
        Outcome outcome;
        for (auto& [source, list] : changes) {
            std::ranges::stable_sort(list, [](const TextChange& a, const TextChange& b) { return a.start < b.start; });
            std::vector<TextChange> merged;
            for (TextChange& next : list) {
                if (!merged.empty() && (next.start < merged.back().end || next.start == merged.back().start)) {
                    TextChange& back = merged.back();
                    back.text += next.text;
                    back.end = std::max(back.end, next.end);
                    continue;
                }
                merged.push_back(std::move(next));
            }
            if (source->id.empty()) {
                outcome.changes = std::move(merged);
                continue;
            }
            const std::string code = applied(source->text, merged);
            for (auto& [anchor, pageLayout] : layout.pages) {
                static_cast<void>(anchor);
                for (FloatingCode& entry : pageLayout.floating) {
                    if (entry.key == source->id) {
                        entry.code = dedent(code);
                    }
                }
            }
        }
        for (auto& [anchor, pageLayout] : layout.pages) {
            static_cast<void>(anchor);
            std::erase_if(pageLayout.floating, [this](const FloatingCode& entry) {
                const bool gone = std::ranges::any_of(dropped, [&entry](const Source* source) { return source->id == entry.key; });
                return gone || entry.code.find_first_not_of(" \t\r\n;") == std::string::npos;
            });
        }
        outcome.layout = std::move(layout);
        outcome.key = key;
        outcome.signature = signature;
        outcome.page = page;
        return outcome;
    }
};

struct Exit {
    const Chain* chain = nullptr;
    std::size_t index = 0;
};

std::optional<Exit> exitOf(const Item& item, std::string_view pin)
{
    if ((pin == "exec" || pin == "then") && item.chain != nullptr && item.role != Role::Pure) {
        return Exit{item.chain, item.index + item.count};
    }
    for (const Pin& output : item.outputs) {
        if (output.name == pin && output.branch != nullptr) {
            return Exit{output.branch, 0};
        }
    }
    return std::nullopt;
}

bool hasIncoming(const Document::Impl& d, const Item& item)
{
    const Page* page = d.page(item.page);
    if (page == nullptr) {
        return false;
    }
    return std::ranges::any_of(page->wires, [&item](const Wire& wire) {
        return wire.to == item.key && (wire.type == "exec" || wire.type == "transition");
    });
}

std::vector<const Wire*> execLeaving(const Document::Impl& d, const Item& item, std::string_view pin)
{
    std::vector<const Wire*> list;
    const Page* page = d.page(item.page);
    if (page == nullptr) {
        return list;
    }
    for (const Wire& wire : page->wires) {
        if (wire.from == item.key && wire.fromPin == pin && wire.type == "exec") {
            list.push_back(&wire);
        }
    }
    return list;
}

const Chain* arrowChain(const Document::Impl& d, const Node* call, std::size_t index)
{
    if (call == nullptr || index >= call->items.size()) {
        return nullptr;
    }
    const Node* arrow = unparen(call->items[index]);
    for (const Chain& chain : d.chains) {
        if (chain.shape == Shape::Braced && chain.owner == arrow->body) {
            return &chain;
        }
    }
    return nullptr;
}

std::optional<std::size_t> arrowIndexOf(const Document::Impl& d, const Node* call, const Chain* chain)
{
    if (chain == nullptr) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < call->items.size(); ++i) {
        if (arrowChain(d, call, i) == chain) {
            return i;
        }
    }
    return std::nullopt;
}

struct FanMember {
    const Item* from = nullptr;
    std::string pin;
    std::size_t index = 0;
};

std::optional<FanMember> fanMemberOf(const Document::Impl& d, const Chain& chain)
{
    const Item* head = chain.head;
    if (head == nullptr || chain.pin.empty()) {
        return std::nullopt;
    }
    const std::optional<Exit> exit = exitOf(*head, chain.pin);
    if (!exit) {
        return std::nullopt;
    }
    const Node* call = parallelFan(*exit->chain, exit->index);
    if (call == nullptr) {
        return std::nullopt;
    }
    const std::optional<std::size_t> index = arrowIndexOf(d, call, &chain);
    if (!index) {
        return std::nullopt;
    }
    return FanMember{head, chain.pin, *index};
}

const Pin* findPin(const std::vector<Pin>& pins, std::string_view name)
{
    const auto found = std::ranges::find(pins, name, &Pin::name);
    return found == pins.end() ? nullptr : &*found;
}

bool convertible(PinType from, PinType to)
{
    if (from == to || from == PinType::Any || to == PinType::Any) {
        return true;
    }
    if (to == PinType::Text && (from == PinType::Number || from == PinType::Bool || from == PinType::Duration)) {
        return true;
    }
    if (to == PinType::Number && (from == PinType::Text || from == PinType::Duration)) {
        return true;
    }
    if (to == PinType::Duration && from == PinType::Number) {
        return true;
    }
    return to == PinType::Bool;
}

std::string wrap(PinType from, const Pin& to, std::string text)
{
    if (from == to.type || from == PinType::Any || to.type == PinType::Any) {
        return text;
    }
    if (to.type == PinType::Text && (from == PinType::Number || from == PinType::Bool || from == PinType::Duration)) {
        return "String(" + text + ")";
    }
    if (to.type == PinType::Number && from == PinType::Text) {
        return "Number(" + text + ")";
    }
    if (to.type == PinType::Bool && !to.slot.condition) {
        return "Boolean(" + text + ")";
    }
    return text;
}

bool insideRange(const Node* node, std::uint32_t start, std::uint32_t end)
{
    return node != nullptr && node->start >= start && node->end <= end;
}

bool escapesLoop(const Node* node)
{
    bool found = false;
    std::vector<const Node*> pending{node};
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current == nullptr) {
            continue;
        }
        if ((current->kind == Kind::Break || current->kind == Kind::Continue) && current->target == nullptr) {
            found = true;
        }
        if (syntax::isFunction(current) || current->kind == Kind::For || current->kind == Kind::ForIn
            || current->kind == Kind::ForOf || current->kind == Kind::While || current->kind == Kind::DoWhile
            || current->kind == Kind::Switch || current->kind == Kind::Class) {
            continue;
        }
        for (const Node* item : current->items) {
            pending.push_back(item);
        }
        pending.push_back(current->target);
        if (current->value != current->target) {
            pending.push_back(current->value);
        }
        pending.push_back(current->test);
        pending.push_back(current->body);
        pending.push_back(current->other);
    }
    return found;
}

bool returns(const Node* node)
{
    bool found = false;
    std::vector<const Node*> pending{node};
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (current->kind == Kind::Return) {
            found = true;
        }
        if (syntax::isFunction(current) || current->kind == Kind::Class) {
            continue;
        }
        for (const Node* item : current->items) {
            pending.push_back(item);
        }
        pending.push_back(current->target);
        if (current->value != current->target) {
            pending.push_back(current->value);
        }
        pending.push_back(current->test);
        pending.push_back(current->body);
        pending.push_back(current->other);
    }
    return found;
}

bool awaits(const Node* node)
{
    bool found = false;
    std::vector<const Node*> pending{node};
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (current->kind == Kind::Await || (current->kind == Kind::ForOf && current->has(flag::kAwait))) {
            found = true;
        }
        if (syntax::isFunction(current) || current->kind == Kind::Class) {
            continue;
        }
        for (const Node* item : current->items) {
            pending.push_back(item);
        }
        pending.push_back(current->target);
        if (current->value != current->target) {
            pending.push_back(current->value);
        }
        pending.push_back(current->test);
        pending.push_back(current->body);
        pending.push_back(current->other);
    }
    return found;
}

std::vector<const Node*> identifiersIn(const Node* node)
{
    std::vector<const Node*> list;
    visit(node, [&list](const Node* current) {
        if (current->kind == Kind::Identifier) {
            list.push_back(current);
        }
    });
    return list;
}

bool chainInside(const Chain* chain, std::uint32_t start, std::uint32_t end)
{
    for (const Chain* current = chain; current != nullptr;) {
        const Item* head = current->head;
        if (head == nullptr || head->statement == nullptr) {
            return false;
        }
        if (head->chain != nullptr && head->source == chain->source && head->statement->start >= start
            && head->statement->end <= end) {
            return true;
        }
        current = head->chain;
    }
    return false;
}

std::string textOf(const nlohmann::json& json, std::string_view key)
{
    const auto found = json.is_object() ? json.find(std::string(key)) : json.end();
    return found != json.end() && found->is_string() ? found->get<std::string>() : std::string();
}

bool flagOf(const nlohmann::json& json, std::string_view key)
{
    const auto found = json.is_object() ? json.find(std::string(key)) : json.end();
    return found != json.end() && found->is_boolean() && found->get<bool>();
}

VariableSpec specOf(const nlohmann::json& json)
{
    VariableSpec spec;
    spec.name = textOf(json, "name");
    const std::string kind = textOf(json, "kind");
    if (!kind.empty()) {
        spec.kind = kind;
    }
    spec.type = textOf(json, "type");
    spec.container = textOf(json, "container");
    const std::string keyKind = textOf(json, "keyKind");
    if (!keyKind.empty()) {
        spec.keyKind = keyKind;
    }
    spec.value = textOf(json, "value");
    spec.category = textOf(json, "category");
    spec.description = textOf(json, "description");
    const std::string scope = textOf(json, "scope");
    if (scope == "local" || scope == "class" || scope == "file") {
        spec.scope = scope;
    }
    spec.keep = flagOf(json, "keep");
    spec.editable = flagOf(json, "editable");
    spec.constant = flagOf(json, "constant");
    return spec;
}

bool variableKey(std::string_view key, std::string& scope, std::string& name)
{
    for (const std::string_view prefix : {"variable:", "local:", "field:"}) {
        if (key.starts_with(prefix)) {
            scope = std::string(prefix.substr(0, prefix.size() - 1));
            name = std::string(key.substr(prefix.size()));
            return !name.empty();
        }
    }
    return false;
}

PinType pinTypeOfVariable(const VariableInfo& info)
{
    if (!info.container.empty()) {
        return PinType::List;
    }
    if (info.kind == "toggle") {
        return PinType::Bool;
    }
    if (info.kind == "integer" || info.kind == "decimal") {
        return PinType::Number;
    }
    if (info.kind == "duration") {
        return PinType::Duration;
    }
    if (info.kind == "text") {
        return PinType::Text;
    }
    if (info.kind == "point") {
        return PinType::Point;
    }
    if (info.kind == "region") {
        return PinType::Region;
    }
    if (info.kind == "color") {
        return PinType::Color;
    }
    if (info.kind == "image") {
        return PinType::Image;
    }
    if (info.kind == "key") {
        return PinType::Key;
    }
    if (info.kind == "component" || info.kind == "class") {
        return PinType::Component;
    }
    return PinType::Any;
}

bool variableExpression(std::string_view text)
{
    if (text.empty()) {
        return false;
    }
    const syntax::Tree tree = syntax::parse("(" + std::string(text) + ")");
    return tree.failures.empty() && tree.program != nullptr && tree.program->items.size() == 1;
}

class Editor
{
public:
    explicit Editor(const Document::Impl& impl) : d(impl) {}

    const Document::Impl& d;

    [[nodiscard]] std::string say(std::string_view key) const { return d.say(key); }

    [[nodiscard]] std::string typeName(PinType type) const { return say("type:" + std::string(pinTypeName(type))); }

    [[nodiscard]] std::string scopeCheck(const Chain& from, std::size_t start, const Chain& to, std::size_t at) const
    {
        if (from.source != to.source || from.source->scopes == nullptr || !from.source->id.empty()) {
            return {};
        }
        const Source& source = *from.source;
        const std::uint32_t runStart = from.statements[start]->start;
        const std::uint32_t runEnd = from.statements.back()->end;
        std::uint32_t destination = 0;
        if (at < to.statements.size()) {
            destination = to.statements[at]->start;
        } else if (!to.statements.empty()) {
            destination = to.statements.back()->end;
        } else if (to.owner != nullptr) {
            destination = to.owner->start + 1;
        } else if (to.parent != nullptr) {
            destination = to.parent->end;
        }
        for (std::size_t i = start; i < from.statements.size(); ++i) {
            for (const Node* id : identifiersIn(from.statements[i])) {
                const Binding* binding = source.scopes->lookup(id->text, id->start);
                if (binding == nullptr || binding->scope == source.scopes->root() || binding->id == id) {
                    continue;
                }
                if (binding->id->start >= runStart && binding->id->end <= runEnd) {
                    continue;
                }
                const Binding* there = source.scopes->lookup(id->text, destination);
                if (there != binding || binding->ready > destination) {
                    return d.say.format("reason:scope", id->text);
                }
            }
        }
        return {};
    }

    [[nodiscard]] std::string execCheck(const Item& from, std::string_view pin, const Item& to) const
    {
        const std::optional<Exit> exit = exitOf(from, pin);
        if (!exit || findPin(to.inputs, "exec") == nullptr || to.chain == nullptr) {
            return say("reason:noPin");
        }
        if (exit->chain == to.chain && exit->index == to.index) {
            return {};
        }
        if (hasIncoming(d, to)) {
            return say("reason:secondExec");
        }
        const Chain& run = *to.chain;
        if (exit->chain == to.chain && exit->index > to.index) {
            return say("reason:loop");
        }
        if (exit->chain->source == run.source && !run.statements.empty()) {
            const std::uint32_t start = run.statements[to.index]->start;
            const std::uint32_t end = run.statements.back()->end;
            if (chainInside(exit->chain, start, end)) {
                return say("reason:loop");
            }
        }
        for (std::size_t i = to.index; i < run.statements.size(); ++i) {
            const Node* statement = run.statements[i];
            if (escapesLoop(statement) && !exit->chain->loop) {
                return say("reason:loopOnly");
            }
            if (returns(statement) && exit->chain->function == nullptr && exit->chain->shape != Shape::Floating) {
                return say("reason:returnOnly");
            }
        }
        if (!execLeaving(d, from, pin).empty()) {
            for (std::size_t i = to.index; i < run.statements.size(); ++i) {
                if (escapesLoop(run.statements[i])) {
                    return say("reason:loopOnly");
                }
            }
        }
        if (exit->chain == to.chain) {
            return {};
        }
        return scopeCheck(run, to.index, *exit->chain, exit->index);
    }

    [[nodiscard]] std::string dataCheck(const Item& from, const Pin& out, const Item& to, const Pin& in) const
    {
        if (!convertible(out.type, in.type)) {
            return d.say.format("reason:type", out.label.empty() ? typeName(out.type) : out.label, typeName(in.type));
        }
        if (from.role == Role::Pure) {
            if (from.consumer == nullptr || (from.consumer == &to && from.consumerPin == in.name)) {
                return {};
            }
            return hoistTarget(from, to) == nullptr ? say("reason:hoist") : std::string();
        }
        const Node* consumer = topStatement(&to);
        if (to.source != from.source) {
            return canAttach(from, out, to) ? std::string() : d.say.format("reason:scope", out.label);
        }
        if (consumer == nullptr || !from.source->id.empty()) {
            return d.say.format("reason:scope", out.label);
        }
        const Source& source = *from.source;
        if (out.binding != nullptr) {
            const Binding* there = source.scopes->lookup(out.local, consumer->start);
            if (there != out.binding || out.binding->ready > consumer->start) {
                return d.say.format("reason:scope", out.label);
            }
            return {};
        }
        if (from.chain == nullptr || from.statement == nullptr) {
            return d.say.format("reason:scope", out.label);
        }
        const Chain& block = *from.chain;
        const std::uint32_t blockStart = block.owner != nullptr ? block.owner->start : 0;
        const std::uint32_t blockEnd = block.owner != nullptr ? block.owner->end : static_cast<std::uint32_t>(source.text.size());
        if (consumer->start < from.statement->end || consumer->start < blockStart || consumer->end > blockEnd) {
            return d.say.format("reason:scope", out.label);
        }
        return {};
    }

    [[nodiscard]] const Item* hoistTarget(const Item& pure, const Item& to) const
    {
        const Item* first = statementOwner(pure.consumer);
        const Item* second = statementOwner(&to);
        if (first == nullptr || second == nullptr || first->chain == nullptr || second->chain == nullptr
            || first->source != second->source) {
            return nullptr;
        }
        const auto within = [](const Item* outer, const Item* inner) -> const Item* {
            for (const Item* current = inner; current != nullptr;) {
                if (current->chain == outer->chain) {
                    return current;
                }
                const Chain* chain = current->chain;
                current = chain != nullptr ? chain->head : nullptr;
                if (current != nullptr && current->chain == nullptr) {
                    return nullptr;
                }
            }
            return nullptr;
        };
        if (const Item* match = within(first, second)) {
            return match->index < first->index ? match : first;
        }
        if (const Item* match = within(second, first)) {
            return match->index < second->index ? match : second;
        }
        return nullptr;
    }

    [[nodiscard]] std::string check(std::string_view fromKey, std::string_view fromPin, std::string_view toKey,
                                    std::string_view toPin) const
    {
        if (!d.ok || d.environment.readOnly) {
            return say("reason:readOnly");
        }
        const Item* from = d.item(fromKey);
        const Item* to = d.item(toKey);
        if (from == nullptr || to == nullptr) {
            return say("reason:notFound");
        }
        const Pin* out = findPin(from->outputs, fromPin);
        const Pin* in = findPin(to->inputs, toPin);
        if (out == nullptr || in == nullptr || (out->type == PinType::Exec) != (in->type == PinType::Exec)) {
            return say("reason:noPin");
        }
        if (to->role == Role::Default) {
            return say("reason:defaultFirst");
        }
        if (out->type == PinType::Exec) {
            return execCheck(*from, fromPin, *to);
        }
        return dataCheck(*from, *out, *to, *in);
    }

    void moveRun(Edit& edit, const Item& to, const Exit& exit) const
    {
        const Chain& run = *to.chain;
        std::string moved = edit.runText(run, to.index);
        const Plan& existing = edit.plan(exit.chain);
        static_cast<void>(existing);
        if (exit.chain == &run) {
            for (std::size_t i = exit.index; i < to.index; ++i) {
                edit.plan(&run).removed.insert(i);
            }
            if (exit.index < to.index) {
                const Item* first = run.items.empty() ? nullptr : itemAt(run, exit.index);
                edit.floatCode(run.page, edit.runText(run, exit.index, to.index), first != nullptr ? first->x : to.x,
                               first != nullptr ? first->y + 200 : to.y + 200);
            }
            return;
        }
        const Chain& target = *exit.chain;
        if (exit.index < target.statements.size()) {
            const Item* first = itemAt(target, exit.index);
            edit.floatCode(target.page, edit.runText(target, exit.index), first != nullptr ? first->x : to.x,
                           first != nullptr ? first->y + 200 : to.y + 200);
            for (std::size_t i = exit.index; i < target.statements.size(); ++i) {
                edit.plan(&target).removed.insert(i);
            }
        }
        if (run.shape == Shape::Floating && to.index == 0) {
            edit.dropped.insert(run.source);
        } else {
            for (std::size_t i = to.index; i < run.statements.size(); ++i) {
                edit.plan(&run).removed.insert(i);
            }
        }
        Plan& destination = edit.plan(&target);
        destination.at = exit.index;
        destination.inserted.push_back(moved);
        if (target.function != nullptr && !target.async) {
            for (std::size_t i = to.index; i < run.statements.size(); ++i) {
                if (awaits(run.statements[i])) {
                    edit.asyncFunctions.insert(target.function);
                }
            }
        }
    }

    [[nodiscard]] static const Item* itemAt(const Chain& chain, std::size_t index)
    {
        for (const Item* item : chain.items) {
            if (item->index <= index && index < item->index + item->count) {
                return item;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::string arrowCode(std::string_view run, std::string_view indent) const
    {
        const std::string& lineBreak = d.style.lineBreak;
        std::string text = "async () => {" + lineBreak;
        text += reindent(run, std::string(indent) + d.style.indent, lineBreak) + lineBreak;
        text += std::string(indent) + "}";
        return text;
    }

    void removeRun(Edit& edit, const Item& target) const
    {
        const Chain& run = *target.chain;
        if (run.shape == Shape::Floating && target.index == 0) {
            edit.dropped.insert(run.source);
            return;
        }
        for (std::size_t i = target.index; i < run.statements.size(); ++i) {
            edit.plan(&run).removed.insert(i);
        }
    }

    [[nodiscard]] std::string removeArrows(Edit& edit, const Item& from, std::string_view pin,
                                           const std::vector<std::size_t>& gone, bool floatRuns) const
    {
        const std::optional<Exit> exit = exitOf(from, pin);
        if (!exit) {
            return say("reason:notFound");
        }
        const Chain& chain = *exit->chain;
        const std::size_t at = exit->index;
        const Node* call = parallelFan(chain, at);
        if (call == nullptr) {
            return say("reason:notFound");
        }
        std::vector<std::size_t> left;
        for (std::size_t i = 0; i < call->items.size(); ++i) {
            if (std::ranges::find(gone, i) == gone.end()) {
                left.push_back(i);
                continue;
            }
            if (!floatRuns) {
                continue;
            }
            const Chain* body = arrowChain(d, call, i);
            if (body == nullptr || body->statements.empty()) {
                continue;
            }
            const Item* first = body->items.empty() ? nullptr : body->items.front();
            edit.floatCode(body->page, edit.runText(*body, 0), first != nullptr ? first->x : from.x + 40,
                           first != nullptr ? first->y : from.y + 120);
        }
        if (left.size() == call->items.size()) {
            return {};
        }
        const Source& source = *chain.source;
        if (left.size() >= 2) {
            for (std::size_t i = 0; i < call->items.size(); ++i) {
                if (std::ranges::find(gone, i) == gone.end()) {
                    continue;
                }
                const Node* first = call->items[i];
                if (i == 0) {
                    edit.change(source, first->start, call->items[1]->start, {});
                } else {
                    edit.change(source, call->items[i - 1]->end, first->end, {});
                }
            }
            return {};
        }
        Plan& plan = edit.plan(&chain);
        plan.removed.insert(at);
        if (left.size() == 1) {
            const Chain* body = arrowChain(d, call, left.front());
            if (body != nullptr && !body->statements.empty()) {
                plan.at = at;
                plan.inserted.push_back(edit.runText(*body, 0));
                if (chain.function != nullptr && !chain.async
                    && std::ranges::any_of(body->statements, [](const Node* statement) { return awaits(statement); })) {
                    edit.asyncFunctions.insert(chain.function);
                }
            }
        }
        return {};
    }

    [[nodiscard]] std::string anchorOf(std::string_view pageKey) const
    {
        const Page* found = d.page(pageKey);
        return found != nullptr ? found->anchor : std::string(pageKey);
    }

    [[nodiscard]] double measuredWidth(const Item& item) const
    {
        for (const Item& old : d.items) {
            if (signatureOf(old) == signatureOf(item)) {
                return d.sizeOf(old).width;
            }
        }
        return d.sizeOf(item).width;
    }

    void gatherFollowers(const Chain& chain, std::size_t from, std::vector<const Chain*>& out) const
    {
        if (std::ranges::find(out, &chain) != out.end()) {
            return;
        }
        out.push_back(&chain);
        for (std::size_t i = from; i < chain.statements.size(); ++i) {
            const Item* item = itemAt(chain, i);
            if (item == nullptr) {
                continue;
            }
            for (const Pin& output : item->outputs) {
                if (output.branch != nullptr && !output.branch->statements.empty()) {
                    gatherFollowers(*output.branch, 0, out);
                }
            }
        }
    }

    [[nodiscard]] std::string placeInChain(Outcome& outcome, const Item& anchor, std::string_view pin,
                                           std::size_t count, bool atEnd = false) const
    {
        const std::string text = applied(d.file().text, outcome.changes);
        const Document probe(text, d.environment, outcome.layout, nullptr, d.sizes);
        const Document::Impl& p = probe.internals();
        const Item* start = p.item(anchor.key);
        if (start == nullptr || signatureOf(*start) != signatureOf(anchor)) {
            start = nullptr;
            for (const Item& candidate : p.items) {
                if (candidate.page == anchor.page && signatureOf(candidate) == signatureOf(anchor)) {
                    start = &candidate;
                    break;
                }
            }
        }
        if (start == nullptr) {
            return {};
        }
        const Chain* chain = start->chain;
        std::size_t index = start->index;
        double x = start->x;
        double y = start->y;
        if (!pin.empty()) {
            const std::optional<Exit> exit = exitOf(*start, pin);
            if (!exit) {
                return {};
            }
            chain = exit->chain;
            index = exit->index;
            x = start->x + measuredWidth(*start) + kChainGap;
            y = start->y;
            if (atEnd) {
                index = chain->statements.size() - std::min(count, chain->statements.size());
                for (std::size_t i = index; i > 0;) {
                    const Item* previous = itemAt(*chain, i - 1);
                    if (previous == nullptr) {
                        break;
                    }
                    if (d.item(previous->key) != nullptr) {
                        x = previous->x + measuredWidth(*previous) + kChainGap;
                        y = previous->y;
                        break;
                    }
                    i = previous->index;
                }
            }
        }
        if (chain == nullptr) {
            return {};
        }
        const Item* first = itemAt(*chain, index);
        if (first == nullptr) {
            return {};
        }
        PageLayout& layout = outcome.layout.pages[anchorOf(chain->page)];
        double dx = 0;
        std::size_t at = index;
        while (at < index + count) {
            const Item* item = itemAt(*chain, at);
            if (item == nullptr) {
                break;
            }
            layout.nodes[item->key] = {x + dx, y};
            dx += measuredWidth(*item) + kChainGap;
            at = item->index + item->count;
        }
        std::vector<const Chain*> followers;
        gatherFollowers(*chain, index + count, followers);
        for (const Chain* follower : followers) {
            const std::size_t from = follower == chain ? std::min(index + count, follower->statements.size()) : 0;
            for (const Item* item : follower->items) {
                if (item->index < from) {
                    continue;
                }
                if (const auto at2 = layout.nodes.find(item->key); at2 != layout.nodes.end()) {
                    at2->second[0] += dx;
                }
            }
        }
        return first->key;
    }

    [[nodiscard]] std::string attachPin(const Item& from, const Pin& out) const
    {
        if (out.binding == nullptr || out.local.empty() || from.source == nullptr || from.source->scopes == nullptr) {
            return {};
        }
        for (const Pin& output : from.outputs) {
            const Chain* branch = output.branch;
            if (branch == nullptr) {
                continue;
            }
            const std::uint32_t at = !branch->statements.empty() ? branch->statements.back()->end
                                    : branch->owner != nullptr       ? branch->owner->end
                                                                     : 0U;
            if (from.source->scopes->lookup(out.local, at) == out.binding && out.binding->ready <= at) {
                return std::string(output.name);
            }
        }
        return {};
    }

    Outcome connectFan(Edit& edit, const Item& from, std::string_view pin, const Exit& exit, const Item& to) const
    {
        const Chain& chain = *exit.chain;
        const std::size_t at = exit.index;
        const Source& source = *chain.source;
        if (to.chain == &chain) {
            moveRun(edit, to, exit);
            return edit.done();
        }
        const Node* call = parallelFan(chain, at);
        if (call != nullptr) {
            const std::string run = edit.runText(*to.chain, to.index);
            if (run.empty()) {
                return edit.done();
            }
            const std::string indent = indentOf(source.text, chain.statements[at]->start);
            const std::size_t close = call->end > 0 ? call->end - 1 : call->end;
            edit.change(source, close, close, ", " + arrowCode(run, indent));
            removeRun(edit, to);
            return edit.done();
        }
        std::string first = edit.runText(chain, at);
        for (std::size_t i = at; i < chain.statements.size(); ++i) {
            if (escapesLoop(chain.statements[i])) {
                return edit.refuse(say("reason:loopOnly"));
            }
        }
        std::string heading = "await ";
        if (chain.function == nullptr && chain.shape != Shape::Floating) {
            heading.clear();
        } else if (chain.function != nullptr && !chain.async) {
            edit.asyncFunctions.insert(chain.function);
        }
        if (first.empty()) {
            const std::vector<const Wire*> wires = execLeaving(d, from, pin);
            const Item* old = !wires.empty() ? d.item(wires.front()->to) : nullptr;
            if (old != nullptr && old->chain != nullptr && old->chain != &chain && old->chain != to.chain) {
                first = edit.runText(*old->chain, old->index);
                if (!first.empty()) {
                    removeRun(edit, *old);
                }
            }
        }
        if (first.empty()) {
            moveRun(edit, to, exit);
            return edit.done();
        }
        const std::string run = edit.runText(*to.chain, to.index);
        if (run.empty()) {
            return edit.done();
        }
        Plan& plan = edit.plan(&chain);
        for (std::size_t i = at; i < chain.statements.size(); ++i) {
            plan.removed.insert(i);
        }
        removeRun(edit, to);
        plan.at = at;
        plan.inserted.push_back(heading + "parallel(" + arrowCode(first, "") + ", " + arrowCode(run, "") + ")");
        return edit.done();
    }

    [[nodiscard]] bool canAttach(const Item& from, const Pin& out, const Item& to) const
    {
        return to.chain != nullptr && out.type != PinType::Exec && to.source != nullptr && !to.source->id.empty()
            && to.source != from.source && to.page == from.page && from.source != nullptr && from.source->id.empty()
            && !attachPin(from, out).empty();
    }

    [[nodiscard]] std::string floatingText(const Item& item, const Pin& pin, const std::string& value) const
    {
        if (item.source == nullptr || item.chain == nullptr) {
            return {};
        }
        Edit probe(d);
        probe.writeInput(item, pin, value);
        const auto found = probe.changes.find(item.source);
        if (found == probe.changes.end()) {
            return {};
        }
        return dedent(applied(item.source->text, found->second));
    }

    Outcome connect(std::string_view fromKey, std::string_view fromPin, std::string_view toKey, std::string_view toPin) const
    {
        Edit edit(d);
        if (const std::string why = check(fromKey, fromPin, toKey, toPin); !why.empty()) {
            return edit.refuse(why);
        }
        const Item& from = *d.item(fromKey);
        const Item& to = *d.item(toKey);
        const Pin& out = *findPin(from.outputs, fromPin);
        const Pin& in = *findPin(to.inputs, toPin);
        if (out.type == PinType::Exec) {
            const Exit exit = *exitOf(from, fromPin);
            if (exit.chain == to.chain && exit.index == to.index) {
                return edit.done();
            }
            if (!execLeaving(d, from, fromPin).empty()) {
                return connectFan(edit, from, fromPin, exit, to);
            }
            moveRun(edit, to, exit);
            return edit.done();
        }
        if (const std::string pin = attachPin(from, out); canAttach(from, out, to) && !pin.empty()) {
            const std::string moved = floatingText(to, in, wrap(out.type, in, out.local));
            const std::optional<Exit> branch = exitOf(from, pin);
            if (moved.empty() || !branch) {
                return edit.refuse(say("reason:noPin"));
            }
            Plan& plan = edit.plan(branch->chain);
            plan.at = kEnd;
            plan.inserted.push_back(moved);
            edit.dropped.insert(to.source);
            Outcome outcome = edit.done();
            if (!outcome.ok()) {
                return outcome;
            }
            outcome.key = placeInChain(outcome, from, pin, to.chain != nullptr ? to.chain->statements.size() : 0, true);
            if (outcome.key.empty()) {
                const std::size_t firstBreak = moved.find('\n');
                outcome.key = locate(outcome, firstBreak == std::string::npos ? moved : moved.substr(0, firstBreak),
                                     from.page, from.x + measuredWidth(from) + kChainGap, from.y);
            }
            return outcome;
        }
        connectData(edit, from, out, to, in);
        return edit.done();
    }

    void connectData(Edit& edit, const Item& from, const Pin& out, const Item& to, const Pin& in) const
    {
        if (from.role == Role::Pure) {
            const std::string text = d.text(*from.source, from.expression);
            if (from.consumer == nullptr) {
                edit.writeInput(to, in, wrap(out.type, in, text));
                edit.dropped.insert(from.source);
                return;
            }
            if (from.consumer == &to && from.consumerPin == in.name) {
                return;
            }
            const Item* target = hoistTarget(from, to);
            const std::string name = freeName(*from.source, identifierFrom(from.title, "value"),
                                              target->statement->start);
            const std::string declaration = "const " + name + " = " + text + edit.semi();
            Plan& plan = edit.plan(target->chain);
            plan.at = target->index;
            plan.inserted.push_back(declaration);
            edit.change(*from.source, from.expression->start, from.expression->end, name);
            edit.writeInput(to, in, wrap(out.type, in, name));
            return;
        }
        std::string name = out.local;
        if (name.empty()) {
            const Node* statement = from.statement;
            name = freeName(*from.source, identifierFrom(out.label, "value"), statement->start);
            const Node* expression = statement->kind == Kind::ExpressionStatement ? statement->target : statement;
            edit.change(*from.source, expression->start, expression->start, "const " + name + " = ");
        }
        edit.writeInput(to, in, wrap(out.type, in, name));
    }

    [[nodiscard]] std::string freeName(const Source& source, std::string base, std::uint32_t at) const
    {
        if (source.scopes == nullptr) {
            return base;
        }
        std::string name = base;
        for (int n = 2; source.scopes->lookup(name, at) != nullptr || !identifierText(name) || nameUsed(source, name); ++n) {
            name = base + std::to_string(n);
        }
        return name;
    }

    [[nodiscard]] static bool nameUsed(const Source& source, std::string_view name)
    {
        bool used = false;
        visit(source.tree.program, [&](const Node* node) {
            used = used || (node->kind == Kind::Identifier && node->text == name);
        });
        return used;
    }

    Outcome removeWire(std::string_view key) const
    {
        Edit edit(d);
        std::string base(key);
        if (base.ends_with("~1") || base.ends_with("~2")) {
            base.resize(base.size() - 2);
        }
        const Wire* found = nullptr;
        for (const Page& page : d.pages) {
            for (const Wire& wire : page.wires) {
                if (wire.key == base) {
                    found = &wire;
                }
            }
        }
        if (found == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        const Item* from = d.item(found->from);
        const Item* to = d.item(found->to);
        if (from == nullptr || to == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        if (found->type == "exec") {
            if (to->chain == nullptr) {
                return edit.done();
            }
            if (const std::optional<Exit> exit = exitOf(*from, found->fromPin)) {
                const Node* call = parallelFan(*exit->chain, exit->index);
                const std::optional<std::size_t> index = call != nullptr ? arrowIndexOf(d, call, to->chain)
                                                                         : std::optional<std::size_t>();
                if (index) {
                    if (const std::string why = removeArrows(edit, *from, found->fromPin, {*index}, true); !why.empty()) {
                        return edit.refuse(why);
                    }
                    return edit.done();
                }
            }
            const Chain& run = *to->chain;
            edit.floatCode(run.page, edit.runText(run, to->index), to->x, to->y);
            if (run.shape == Shape::Floating && to->index == 0) {
                edit.dropped.insert(run.source);
            } else {
                for (std::size_t i = to->index; i < run.statements.size(); ++i) {
                    edit.plan(&run).removed.insert(i);
                }
            }
            return edit.done();
        }
        if (const Pin* in = findPin(to->inputs, found->toPin)) {
            edit.clearInput(*to, *in);
        }
        return edit.done();
    }

    Outcome disconnect(std::string_view node, std::string_view pin) const
    {
        const Item* item = d.item(node);
        if (item == nullptr) {
            return Edit(d).refuse(say("reason:notFound"));
        }
        const Page* page = d.page(item->page);
        std::vector<std::string> outgoing;
        std::vector<std::string> incoming;
        if (page != nullptr) {
            for (const Wire& wire : page->wires) {
                if (wire.from == node && wire.fromPin == pin) {
                    outgoing.push_back(wire.key);
                }
                if (wire.to == node && wire.toPin == pin) {
                    incoming.push_back(wire.key);
                }
            }
        }
        const std::vector<std::string>& wires = !outgoing.empty() ? outgoing : incoming;
        if (wires.empty()) {
            Edit edit(d);
            return edit.done();
        }
        if (wires.size() == 1) {
            return removeWire(wires.front());
        }
        Edit edit(d);
        const Pin* out = findPin(item->outputs, pin);
        if (out != nullptr && out->type == PinType::Exec) {
            const std::optional<Exit> exit = exitOf(*item, pin);
            const Node* call = exit ? parallelFan(*exit->chain, exit->index) : nullptr;
            if (call != nullptr) {
                std::vector<std::size_t> all(call->items.size());
                for (std::size_t i = 0; i < all.size(); ++i) {
                    all[i] = i;
                }
                if (const std::string why = removeArrows(edit, *item, pin, all, true); !why.empty()) {
                    return edit.refuse(why);
                }
                return edit.done();
            }
        }
        for (const std::string& key : wires) {
            for (const Wire& wire : page->wires) {
                if (wire.key != key || wire.type == "exec") {
                    continue;
                }
                const Item* to = d.item(wire.to);
                if (const Pin* in = to != nullptr ? findPin(to->inputs, wire.toPin) : nullptr) {
                    edit.clearInput(*to, *in);
                }
            }
        }
        return edit.done();
    }

    Outcome rewire(std::string_view node, std::string_view pin, std::string_view toNode, std::string_view toPin) const
    {
        const Item* item = d.item(node);
        const Item* other = d.item(toNode);
        if (item == nullptr || other == nullptr) {
            return Edit(d).refuse(say("reason:notFound"));
        }
        const Page* page = d.page(item->page);
        if (page == nullptr) {
            return Edit(d).refuse(say("reason:notFound"));
        }
        const Pin* out = findPin(item->outputs, pin);
        if (out != nullptr) {
            if (out->type == PinType::Exec && execLeaving(d, *item, pin).size() > 1) {
                return Edit(d).refuse(say("reason:noPin"));
            }
            for (const Wire& wire : page->wires) {
                if (wire.from != node || wire.fromPin != pin) {
                    continue;
                }
                if (out->type == PinType::Exec) {
                    const Item* successor = d.item(wire.to);
                    const std::optional<Exit> exit = exitOf(*other, toPin);
                    if (successor == nullptr || !exit) {
                        return Edit(d).refuse(say("reason:noPin"));
                    }
                    Edit edit(d);
                    if (exit->chain == successor->chain && exit->index > successor->index) {
                        return edit.refuse(say("reason:loop"));
                    }
                    if (const std::string why = scopeCheck(*successor->chain, successor->index, *exit->chain, exit->index);
                        !why.empty()) {
                        return edit.refuse(why);
                    }
                    moveRun(edit, *successor, *exit);
                    return edit.done();
                }
            }
            Edit edit(d);
            const Pin* target = findPin(other->outputs, toPin);
            if (target == nullptr) {
                return edit.refuse(say("reason:noPin"));
            }
            for (const Wire& wire : page->wires) {
                if (wire.from != node || wire.fromPin != pin) {
                    continue;
                }
                const Item* consumer = d.item(wire.to);
                const Pin* in = consumer != nullptr ? findPin(consumer->inputs, wire.toPin) : nullptr;
                if (in == nullptr) {
                    continue;
                }
                if (const std::string why = dataCheck(*other, *target, *consumer, *in); !why.empty()) {
                    return edit.refuse(why);
                }
                connectData(edit, *other, *target, *consumer, *in);
            }
            return edit.done();
        }
        for (const Wire& wire : page->wires) {
            if (wire.to != node || wire.toPin != pin) {
                continue;
            }
            const Item* source = d.item(wire.from);
            if (source == nullptr) {
                break;
            }
            const Pin* in = findPin(item->inputs, pin);
            if (in != nullptr && in->type == PinType::Exec) {
                const std::optional<Exit> exit = exitOf(*source, wire.fromPin);
                if (!exit || other->chain == nullptr) {
                    return Edit(d).refuse(say("reason:noPin"));
                }
                Edit edit(d);
                if (other->chain == exit->chain && other->index < exit->index) {
                    return edit.refuse(say("reason:loop"));
                }
                moveRun(edit, *other, *exit);
                return edit.done();
            }
            const Pin* out2 = findPin(source->outputs, wire.fromPin);
            const Pin* in2 = findPin(other->inputs, toPin);
            if (out2 == nullptr || in2 == nullptr || in == nullptr) {
                return Edit(d).refuse(say("reason:noPin"));
            }
            if (const std::string why = dataCheck(*source, *out2, *other, *in2); !why.empty()) {
                return Edit(d).refuse(why);
            }
            if (canAttach(*source, *out2, *other)) {
                return Edit(d).refuse(d.say.format("reason:scope", out2->label));
            }
            Edit edit(d);
            connectData(edit, *source, *out2, *other, *in2);
            edit.clearInput(*item, *in);
            return edit.done();
        }
        return Edit(d).refuse(say("reason:noPin"));
    }

    Outcome setValue(std::string_view node, std::string_view pinName, const nlohmann::json& value) const
    {
        Edit edit(d);
        const Item* item = d.item(node);
        if (item == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        const Pin* pin = findPin(item->inputs, pinName);
        if (pin == nullptr || pin->type == PinType::Exec) {
            return edit.refuse(say("reason:noPin"));
        }
        if (pin->raw == value && !pin->connected) {
            return edit.done();
        }
        const bool empty = value.is_null() || (value.is_string() && value.get<std::string>().empty() && pin->editor != "text");
        if (empty) {
            edit.clearInput(*item, *pin);
            return edit.done();
        }
        for (const Item* pure : edit.feeders(*item, pin->name)) {
            static_cast<void>(pure);
        }
        edit.writeInput(*item, *pin, edit.literal(*item, *pin, value));
        return edit.done();
    }

    struct Made {
        std::string code;
        std::string library;
        bool event = false;
        bool pure = false;
        bool awaitable = false;
        bool declaration = false;
        std::optional<PinType> output;
    };

    using Preset = std::optional<std::pair<PinType, std::string>>;

    [[nodiscard]] PinType paramType(const TypeRef& type, std::string_view name) const
    {
        const Api& api = d.api();
        TypeRef current = withoutEmpty(type);
        for (int depth = 0; depth < 8 && current != nullptr; ++depth) {
            if (current->kind == TypeNode::Kind::Name) {
                const std::string& n = current->name;
                if (n == "number") {
                    return name.ends_with("Ms") || name == "ms" ? PinType::Duration : PinType::Number;
                }
                if (n == "boolean") {
                    return PinType::Bool;
                }
                if (n == "string") {
                    return PinType::Text;
                }
                if (n == "InputName") {
                    return PinType::Key;
                }
                if (n == "PicturePoint") {
                    return PinType::Point;
                }
                if (n == "Region" || n == "RegionName") {
                    return PinType::Region;
                }
                if (n == "TemplateName" || n == "ImagePath") {
                    return PinType::Image;
                }
                if (n == "ColorName" || n == "HexColor") {
                    return PinType::Color;
                }
                if (api.interfaces.contains(n) && api.derives(n, "Component")) {
                    return PinType::Component;
                }
                const TypeRef resolved = api.resolve(current);
                if (resolved == current) {
                    return PinType::Any;
                }
                current = withoutEmpty(resolved);
                continue;
            }
            if (current->kind == TypeNode::Kind::Union) {
                for (const TypeRef& item : current->items) {
                    if (isName(item, "TemplateName") || isName(item, "ImagePath")) {
                        return PinType::Image;
                    }
                    if (isName(item, "RegionName") || isName(item, "Region")) {
                        return PinType::Region;
                    }
                    if (isName(item, "ColorName") || isName(item, "HexColor")) {
                        return PinType::Color;
                    }
                }
                return std::ranges::all_of(current->items, [](const TypeRef& item) {
                    return item != nullptr && item->kind == TypeNode::Kind::Literal;
                }) ? PinType::Text
                   : PinType::Any;
            }
            if (current->kind == TypeNode::Kind::Array || current->kind == TypeNode::Kind::Tuple) {
                return PinType::List;
            }
            return PinType::Any;
        }
        return PinType::Any;
    }

    [[nodiscard]] std::string defaultFor(const TypeParam& param) const
    {
        const PinType type = paramType(param.type, param.name);
        const char quote = d.style.quote;
        switch (type) {
        case PinType::Number:
            return "0";
        case PinType::Duration:
            return "500";
        case PinType::Bool:
            return "false";
        case PinType::Text: {
            const TypeRef plain = withoutEmpty(param.type);
            if (plain != nullptr && plain->kind == TypeNode::Kind::Union && !plain->items.empty()
                && plain->items.front()->kind == TypeNode::Kind::Literal) {
                return stringLiteral(unquote(plain->items.front()->name), quote);
            }
            return stringLiteral("", quote);
        }
        case PinType::Key:
            return stringLiteral("F", quote);
        case PinType::Image:
            return stringLiteral(d.environment.images.empty() ? std::string() : d.environment.images.front().name, quote);
        case PinType::Region:
            return "{ x: 0, y: 0, width: 1, height: 1 }";
        case PinType::Color:
            return stringLiteral("#FFFFFF", quote);
        case PinType::Component:
            return d.environment.controls.empty() ? std::string("undefined")
                                                  : access("controls", d.environment.controls.front().id, quote);
        case PinType::List:
            return "[]";
        case PinType::Point:
            return "{ x: 0.5, y: 0.5 }";
        default:
            return "undefined";
        }
    }

    [[nodiscard]] std::string argumentsFor(const ApiMember& member, bool handlers, const Preset& preset) const
    {
        const Api& api = d.api();
        const std::vector<TypeParam>& params = member.params;
        const auto isPoint = [](const TypeRef& type) {
            if (type == nullptr || type->kind != TypeNode::Kind::Union) {
                return false;
            }
            return std::ranges::any_of(type->items, [](const TypeRef& item) { return isName(item, "PicturePoint"); });
        };
        const auto optionalNumber = [](const TypeParam& param) {
            return param.optional && isName(withoutEmpty(param.type), "number");
        };
        const std::string& id = d.environment.ownControl;
        const bool plain = !id.empty() && std::isdigit(static_cast<unsigned char>(id.front())) == 0 &&
                           id.find_first_of("-. \"'\\") == std::string::npos;
        const std::string own = id.empty() ? std::string()
                                : plain    ? "controls." + id + ".position"
                                           : "controls[\"" + id + "\"].position";
        std::vector<std::string> parts;
        bool used = false;
        for (std::size_t p = 0; p < params.size(); ++p) {
            const TypeParam& param = params[p];
            const bool function = isFunctionType(api, param.type);
            if (param.rest || (param.optional && !function) || (param.optional && function && !handlers)) {
                break;
            }
            if (param.optional && function) {
                break;
            }
            if (isPoint(param.type) && p + 3 < params.size() && isPoint(params[p + 1].type) && optionalNumber(params[p + 2])
                && optionalNumber(params[p + 3])) {
                parts.emplace_back(own.empty() ? "0.5, 0.7, 0.5, 0.3" : own + ", { x: 0.5, y: 0.3 }");
                p += 3;
                continue;
            }
            if (isPoint(param.type) && p + 1 < params.size() && optionalNumber(params[p + 1])) {
                if (preset && !used && (preset->first == PinType::Point || preset->first == PinType::Any)) {
                    parts.push_back(preset->second);
                    used = true;
                } else {
                    parts.emplace_back(own.empty() ? "0.5, 0.5" : own);
                }
                ++p;
                continue;
            }
            if (function) {
                parts.emplace_back(handlers ? "() => {\n}" : "() => {}");
                continue;
            }
            if (preset && !used && convertible(preset->first, paramType(param.type, param.name))) {
                parts.push_back(preset->second);
                used = true;
                continue;
            }
            parts.push_back(defaultFor(param));
        }
        std::string text;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            text += (i > 0 ? ", " : "") + parts[i];
        }
        return text;
    }

    [[nodiscard]] std::optional<Made> generate(std::string_view id, const Preset& preset, std::string_view page) const
    {
        const Api& api = d.api();
        const char quote = d.style.quote;
        const std::string semi = d.style.semicolons ? ";" : "";
        const Source& file = d.file();
        const auto value = [&preset](std::string_view fallback) {
            return preset ? preset->second : std::string(fallback);
        };
        const auto memberOf = [&](std::string_view path, std::string& object) -> const ApiMember* {
            const std::size_t dot = path.rfind('.');
            if (dot == std::string_view::npos) {
                return nullptr;
            }
            const std::string_view head = path.substr(0, dot);
            const std::string member(path.substr(dot + 1));
            if (head.starts_with("controls.")) {
                const std::string id(head.substr(9));
                const ControlEntry* entry = d.control(id);
                object = access("controls", id, quote);
                return api.member(entry != nullptr && api.interfaces.contains(entry->type) ? entry->type : "Component",
                                  member);
            }
            if (head.starts_with("vars.")) {
                object = access("vars", head.substr(5), quote);
                return api.member("Variable", member);
            }
            if (head == "keys") {
                object = "keys.F";
                return api.member("Key", member);
            }
            object = std::string(head);
            return api.find(path);
        };
        Made made;
        if (id == "flow:if" || id == "flow:while") {
            made.code = std::string(id == "flow:if" ? "if" : "while") + " (" + value("false") + ") {\n}";
            return made;
        }
        if (id == "flow:forOf") {
            const std::string name = freeName(file, identifierFrom(say("name:item"), "item"), 0);
            made.code = "for (const " + name + " of " + value("[]") + ") {\n}";
            return made;
        }
        if (id == "flow:repeat") {
            const std::string name = freeName(file, "i", 0);
            made.code = "for (let " + name + " = 0; " + name + " < " + value("3") + "; " + name + "++) {\n}";
            return made;
        }
        if (id == "flow:break" || id == "flow:continue") {
            made.code = std::string(id.substr(5)) + semi;
            return made;
        }
        if (id == "flow:return") {
            made.code = preset ? "return " + preset->second + semi : "return" + semi;
            return made;
        }
        if (id == "flow:code") {
            made.code = ";";
            return made;
        }
        if (id == "flow:machine") {
            const std::string state = identifierFrom(say("name:state"), "start");
            made.code = "await machine({\n" + d.style.indent + "async " + state + "(to) {\n" + d.style.indent + "},\n})" + semi;
            return made;
        }
        if (id == "flow:switch") {
            const std::string& unit = d.style.indent;
            made.code = "switch (" + value("0") + ") {\n" + unit + "case 1: {}\n" + unit + "case 2: {}\n" + unit
                      + "default: {}\n}";
            return made;
        }
        if (id == "flow:sequence") {
            made.code = "sequence(async () => {\n}, async () => {\n})" + semi;
            made.awaitable = true;
            return made;
        }
        if (id == "flow:multiGate") {
            std::string code = "multiGate(" + stringLiteral(say("name:gate"), quote) + ", 3, false, false";
            for (int n = 0; n < 4; ++n) {
                code += ", async () => {\n}";
            }
            made.code = code + ")" + semi;
            made.awaitable = true;
            return made;
        }
        if (id == "flow:forRange") {
            const std::string name = freeName(file, "i", 0);
            made.code = "for (let " + name + " = " + value("0") + "; " + name + " <= " + value("2") + "; " + name
                      + "++) {\n}";
            return made;
        }
        if (id == "flow:forEachIndex") {
            const std::string index = freeName(file, "i", 0);
            const std::string each = freeName(file, identifierFrom(say("name:item"), "item"), 0);
            made.code = "for (const [" + index + ", " + each + "] of " + value("[]") + ".entries()) {\n}";
            return made;
        }
        if (id.starts_with("op:")) {
            const std::string op(id.substr(3));
            made.pure = true;
            if (op == "!") {
                made.code = "!" + value("false");
            } else if (op == "?:") {
                made.code = value("false") + " ? 0 : 0";
            } else if (op == "??") {
                made.code = value("undefined") + " ?? 0";
            } else if (op == "&&" || op == "||") {
                made.code = value("false") + " " + op + " false";
            } else {
                made.code = value("0") + " " + op + " 0";
            }
            return made;
        }
        if (id.starts_with("make:")) {
            const StructInfo* info = structNamed(d.types, id.substr(5));
            if (info == nullptr || info->fields.empty()) {
                return std::nullopt;
            }
            made.pure = true;
            made.output = PinType::Struct;
            std::string object = "{ ";
            for (std::size_t i = 0; i < info->fields.size(); ++i) {
                const StructFieldInfo& field = info->fields[i];
                object += (i > 0 ? ", " : "") + field.name + ": " + defaultValueOfType(field.type);
            }
            object += " }";
            made.code = std::move(object);
            return made;
        }
        if (id.starts_with("break:")) {
            const std::string rest(id.substr(6));
            const std::size_t dot = rest.find('.');
            if (dot == std::string::npos) {
                return std::nullopt;
            }
            const StructInfo* info = structNamed(d.types, rest.substr(0, dot));
            if (info == nullptr) {
                return std::nullopt;
            }
            const StructFieldInfo* field = fieldNamed(*info, rest.substr(dot + 1));
            if (field == nullptr) {
                return std::nullopt;
            }
            made.pure = true;
            made.output = typeOfName(d, field->type);
            made.code = value("undefined") + "." + field->name;
            return made;
        }
        if (id.starts_with("enum:")) {
            const EnumInfo* info = enumNamed(d.types, id.substr(5));
            if (info == nullptr || info->options.empty()) {
                return std::nullopt;
            }
            made.pure = true;
            made.output = PinType::Enum;
            made.code = std::string(id.substr(5)) + "." + info->options.front();
            return made;
        }
        if (id.starts_with("enumvalue:")) {
            const std::string rest(id.substr(10));
            const std::size_t dot = rest.rfind('.');
            if (dot == std::string::npos) {
                return std::nullopt;
            }
            const std::string name(rest.substr(0, dot));
            const std::string option(rest.substr(dot + 1));
            const EnumInfo* info = enumNamed(d.types, name);
            if (info == nullptr || std::ranges::find(info->options, option) == info->options.end()) {
                return std::nullopt;
            }
            made.pure = true;
            made.output = PinType::Enum;
            made.code = name + "." + option;
            return made;
        }
        if (id.starts_with("switch:")) {
            const std::string name(id.substr(7));
            const EnumInfo* info = enumNamed(d.types, name);
            if (info == nullptr || info->options.empty()) {
                return std::nullopt;
            }
            const std::string inner = d.style.indent + d.style.indent;
            std::string text = "switch (" + value("undefined") + ") {\n";
            for (const std::string& option : info->options) {
                text += d.style.indent + "case " + name + "." + option + ":\n";
                text += inner + "break\n";
            }
            text += "}";
            made.code = std::move(text);
            made.declaration = true;
            return made;
        }
        if (id.starts_with("new:")) {
            const std::string name(id.substr(4));
            const auto found = std::ranges::find(d.classes, name, &ClassInfo::name);
            if (found == d.classes.end()) {
                return std::nullopt;
            }
            std::string args;
            std::size_t count = 0;
            if (found->constructor != nullptr) {
                for (const Node* param : found->constructor->items) {
                    const Node* argument = param->kind == Kind::AssignPattern ? param->target : param;
                    if (argument == nullptr || argument->kind != Kind::Identifier) {
                        continue;
                    }
                    args += (count > 0 ? ", " : "") + (count == 0 ? value("undefined") : std::string("undefined"));
                    ++count;
                }
            }
            const std::string local = freeName(file, identifierFrom(say("name:object"), "object"), 0);
            made.code = "const " + local + " = new " + name + "(" + args + ")" + semi;
            made.declaration = true;
            made.output = PinType::Object;
            return made;
        }
        if (id.starts_with("cast:")) {
            const std::string name(id.substr(5));
            if (std::ranges::find(d.classes, name, &ClassInfo::name) == d.classes.end()) {
                return std::nullopt;
            }
            made.code = "if (" + value("undefined") + " instanceof " + name + ") {\n}";
            made.declaration = true;
            made.output = PinType::Object;
            return made;
        }
        if (id.starts_with("bind:this.") || id.starts_with("unbind:this.")) {
            const std::string name(id.substr(id.find(':') + 1));
            if (d.defaultClass() == nullptr) {
                return std::nullopt;
            }
            const std::string event(name.substr(5));
            if (id.starts_with("bind:")) {
                const std::string local = freeName(file, identifierFrom(say("name:subscription"), "subscription"), 0);
                made.code = "const " + local + " = this." + event + ".on(({ value }) => {\n})" + semi;
                made.declaration = true;
                made.output = PinType::Any;
                return made;
            }
            made.code = value("undefined") + ".dispose()" + semi;
            return made;
        }
        if (id.starts_with("getobject:") || id.starts_with("setobject:") || id.starts_with("callobject:")
            || id.starts_with("emit:") || id.starts_with("bind:") || id.starts_with("unbind:")) {
            const std::size_t colon = id.find(':');
            const std::string rest(id.substr(colon + 1));
            const std::size_t dot = rest.find('.');
            if (dot == std::string::npos) {
                return std::nullopt;
            }
            const std::string className(rest.substr(0, dot));
            const std::string member(rest.substr(dot + 1));
            const auto found = std::ranges::find(d.classes, className, &ClassInfo::name);
            if (found == d.classes.end()) {
                return std::nullopt;
            }
            const std::string path = value("undefined") + "." + member;
            const std::string_view kind = id.substr(0, colon);
            if (kind == "getobject") {
                made.pure = true;
                made.output = PinType::Any;
                made.code = path;
                return made;
            }
            if (kind == "setobject") {
                made.code = path + " = " + "undefined" + semi;
                return made;
            }
            if (kind == "emit") {
                made.code = path + ".emit(" + "undefined" + ")" + semi;
                return made;
            }
            if (kind == "bind") {
                const std::string local = freeName(file, identifierFrom(say("name:subscription"), "subscription"), 0);
                made.code = "const " + local + " = " + path + ".on(({ value }) => {\n})" + semi;
                made.declaration = true;
                made.output = PinType::Any;
                return made;
            }
            if (kind == "unbind") {
                made.code = value("undefined") + ".dispose()" + semi;
                return made;
            }
            std::string args;
            std::size_t count = 0;
            if (found->node != nullptr) {
                for (const Node* classMember : found->node->items) {
                    if (classMember->kind != Kind::ClassMember || classMember->target == nullptr
                        || !classMember->has(flag::kMethod) || classMember->value == nullptr
                        || classMember->target->text != member) {
                        continue;
                    }
                    for (const Node* param : classMember->value->items) {
                        const Node* argument = param->kind == Kind::AssignPattern ? param->target : param;
                        if (argument == nullptr || argument->kind != Kind::Identifier) {
                            continue;
                        }
                        args += (count > 0 ? ", " : "") + std::string("undefined");
                        ++count;
                    }
                    break;
                }
            }
            made.code = path + "(" + args + ")" + semi;
            made.output = PinType::Any;
            return made;
        }
        if (id.starts_with("event:")) {
            std::string object;
            const std::string path(id.substr(6));
            const ApiMember* member = path.find('.') == std::string::npos ? api.global(path) : memberOf(path, object);
            if (member == nullptr) {
                return std::nullopt;
            }
            const std::string callee = object.empty() ? path : object + "." + member->name;
            made.code = callee + "(" + argumentsFor(*member, true, preset) + ")" + semi;
            made.event = true;
            return made;
        }
        if (id.starts_with("call:fn:")) {
            const std::string name(id.substr(8));
            const auto found = d.functions.find(name);
            if (found == d.functions.end()) {
                return std::nullopt;
            }
            std::string args;
            for (std::size_t i = 0; i < found->second->items.size(); ++i) {
                if (found->second->items[i]->kind == Kind::AssignPattern) {
                    break;
                }
                args += (i > 0 ? ", " : "") + (i == 0 ? value("undefined") : std::string("undefined"));
            }
            made.code = name + "(" + args + ")" + semi;
            made.awaitable = found->second->has(flag::kAsync);
            made.output = PinType::Any;
            return made;
        }
        if (id.starts_with("call:lib:")) {
            const std::string name(id.substr(9));
            const auto found = std::ranges::find(d.environment.library, name, &LibraryFunction::name);
            if (found == d.environment.library.end() || d.environment.libraryImport.empty()) {
                return std::nullopt;
            }
            std::string args;
            for (std::size_t i = 0; i < found->params.size() && !found->params[i].optional; ++i) {
                args += (i > 0 ? ", " : "") + (i == 0 ? value("undefined") : std::string("undefined"));
            }
            made.code = name + "(" + args + ")" + semi;
            made.awaitable = found->async;
            made.output = PinType::Any;
            made.library = name;
            return made;
        }
        if (id.starts_with("call:this.")) {
            const std::string rest(id.substr(10));
            made.code = "this." + rest + "()" + semi;
            if (const ClassInfo* info = d.defaultClass(); info != nullptr && !rest.ends_with(".emit")) {
                for (const Node* member : info->node->items) {
                    if (member->kind == Kind::ClassMember && member->target != nullptr && member->target->text == rest
                        && member->value != nullptr) {
                        made.awaitable = member->value->has(flag::kAsync);
                    }
                }
            }
            return made;
        }
        if (id.starts_with("job:")) {
            const std::string path(id.substr(4));
            const ApiMember* member = api.find(path);
            if (member == nullptr || !isName(member->returns, "ExtensionJob")) {
                return std::nullopt;
            }
            std::string fields;
            if (!member->params.empty()) {
                for (const TypeProp& prop : propsOf(api, member->params.front().type)) {
                    if (!prop.optional) {
                        fields += (fields.empty() ? "" : ", ") + prop.name + ": "
                                + defaultFor(TypeParam{.name = prop.name, .type = prop.type});
                    }
                }
            }
            const auto named = [&](std::string_view base) { return freeName(file, std::string(base), 0); };
            made.code = path + "(" + (fields.empty() ? std::string("{}") : "{ " + fields + " }") + ").follow(async ("
                      + named("fraction") + ", " + named("text") + ") => {\n}, async (" + named("result")
                      + ") => {\n}, async (" + named("error") + ") => {\n})" + semi;
            made.awaitable = true;
            return made;
        }
        if (id.starts_with("call:") || id.starts_with("pure:")) {
            const bool pure = id.starts_with("pure:");
            const std::string path(id.substr(5));
            std::string object;
            const ApiMember* member = path.starts_with("controls.") || path.starts_with("vars.")
                                        ? memberOf(path, object)
                                        : api.find(path);
            if (member == nullptr) {
                return std::nullopt;
            }
            const std::string callee = object.empty() ? path : object + "." + member->name;
            if (path == "vision.find" || path == "vision.waitFor") {
                const std::string name = freeName(file, identifierFrom(say("name:found"), "found"), 0);
                made.code = "const " + name + " = await " + callee + "(" + argumentsFor(*member, false, preset) + ")"
                          + semi + "\nif (" + name + ") {\n}";
                return made;
            }
            made.code = callee + "(" + argumentsFor(*member, false, preset) + ")" + (pure ? "" : semi);
            made.pure = pure;
            made.awaitable = !pure && awaitable(member->returns);
            const TypeRef returns = withoutEmpty(awaited(member->returns));
            if (returns != nullptr && !isName(returns, "void") && !isName(member->returns, "Subscription")) {
                made.output = paramType(returns, "");
            }
            return made;
        }
        if (id.starts_with("get:controls.") || id.starts_with("set:controls.")) {
            std::string object;
            const ApiMember* member = memberOf(id.substr(4), object);
            if (member == nullptr) {
                return std::nullopt;
            }
            const std::string target = object + "." + member->name;
            if (id.starts_with("get:")) {
                made.code = target;
                made.pure = true;
                return made;
            }
            made.code = target + " = " + value(defaultFor({member->name, member->type, false, false})) + semi;
            return made;
        }
        if (id.starts_with("get:vars.") || id.starts_with("set:vars.")) {
            const std::string variable(id.substr(9));
            const std::string target = access("vars", variable, quote) + ".value";
            if (id.starts_with("get:")) {
                made.code = target;
                made.pure = true;
                return made;
            }
            const VariableEntry* entry = d.variable(variable);
            const std::string fallback = entry == nullptr                 ? "undefined"
                                       : entry->kind == "toggle"          ? "false"
                                       : entry->kind == "number"          ? "0"
                                                                          : stringLiteral("", quote);
            made.code = target + " = " + value(fallback) + semi;
            return made;
        }
        if (id.starts_with("get:global:")) {
            made.code = "globals.get(" + stringLiteral(id.substr(11), quote) + ")";
            made.pure = true;
            return made;
        }
        if (id.starts_with("get:variable:") || id.starts_with("get:local:")) {
            const bool local = id.starts_with("get:local:");
            const std::string name(id.substr(local ? 10 : 13));
            made.code = name;
            made.pure = true;
            if (const VariableInfo* info = d.declaredVariable(local ? page : std::string_view(), name);
                info != nullptr) {
                made.output = pinTypeOfVariable(*info);
            }
            return made;
        }
        if (id.starts_with("set:variable:") || id.starts_with("set:local:")) {
            const bool local = id.starts_with("set:local:");
            const std::string name(id.substr(local ? 10 : 13));
            const VariableInfo* info = d.declaredVariable(local ? page : std::string_view(), name);
            const std::string fallback = info != nullptr ? variableDefault(info->kind, info->type, info->container,
                                                                           info->keyKind, quote)
                                                         : std::string("undefined");
            made.code = name + " = " + value(fallback) + semi;
            if (info != nullptr && info->keep) {
                made.code += "\nstore.set(" + stringLiteral(name, quote) + ", " + name + ")" + semi;
            }
            return made;
        }
        if (id.starts_with("get:this.")) {
            made.code = "this." + std::string(id.substr(9));
            made.pure = true;
            return made;
        }
        if (id.starts_with("set:this.")) {
            made.code = "this." + std::string(id.substr(9)) + " = " + value("undefined") + semi;
            return made;
        }
        if (id.starts_with("ref:controls.")) {
            made.code = access("controls", id.substr(13), quote);
            made.pure = true;
            return made;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string locate(Outcome& outcome, const std::string& code, std::string_view page, double x,
                                     double y) const
    {
        const std::string text = applied(d.file().text, outcome.changes);
        const Document probe(text, d.environment, outcome.layout, nullptr, d.sizes);
        const std::string wanted = "|" + compact(code);
        const Item* found = nullptr;
        for (const Item& item : probe.internals().items) {
            if (!signatureOf(item).ends_with(wanted)) {
                continue;
            }
            if (d.item(item.key) == nullptr) {
                found = &item;
                break;
            }
            if (found == nullptr) {
                found = &item;
            }
        }
        if (found == nullptr) {
            return {};
        }
        const Page* target = probe.internals().page(found->page);
        const std::string anchor = target != nullptr ? target->anchor : std::string(page);
        outcome.layout.pages[anchor].nodes[found->key] = {x, y};
        return found->key;
    }

    [[nodiscard]] std::string withAwait(const Made& made, const Chain* chain, Edit& edit) const
    {
        if (!made.awaitable) {
            return made.code;
        }
        if (chain == nullptr || chain->shape == Shape::Floating) {
            return "await " + made.code;
        }
        if (chain->function == nullptr) {
            return made.code;
        }
        if (!chain->async) {
            edit.asyncFunctions.insert(chain->function);
        }
        return "await " + made.code;
    }

    [[nodiscard]] std::size_t afterLastRegistration(std::string_view id) const
    {
        constexpr std::string_view kPrefix = "event:controls.";
        if (!id.starts_with(kPrefix)) {
            return std::string::npos;
        }
        const std::string rest(id.substr(kPrefix.size()));
        const std::size_t dot = rest.rfind('.');
        if (dot == std::string::npos || dot == 0) {
            return std::string::npos;
        }
        const std::string control = rest.substr(0, dot);
        const std::string member = rest.substr(dot + 1);
        const ControlEntry* entry = d.control(control);
        const Api& api = d.api();
        const ApiMember* found =
            api.member(entry != nullptr && api.interfaces.contains(entry->type) ? entry->type : "Component", member);
        if (found == nullptr || !isEventMember(api, *found)) {
            return std::string::npos;
        }
        std::size_t at = std::string::npos;
        for (const Node* statement : d.file().tree.program->items) {
            if (controlObject(statement) != control) {
                continue;
            }
            at = statement->end;
        }
        return at == std::string::npos ? at : afterLine(d.file().text, at);
    }

    void importFromLibrary(Edit& edit, const std::string& name) const
    {
        const Source& source = d.file();
        if (source.tree.program == nullptr) {
            return;
        }
        const std::string& text = source.text;
        const std::string& from = d.environment.libraryImport;
        std::size_t after = std::string::npos;
        for (const Node* statement : source.tree.program->items) {
            if (statement->kind != Kind::Import || statement->value == nullptr) {
                continue;
            }
            after = statement->end;
            const std::optional<std::string> module = stringValue(statement->value);
            if (!module || (*module != from && *module != from.substr(0, from.size() - 3))) {
                continue;
            }
            if (std::ranges::any_of(statement->items, [&name](const Node* item) { return item->text == name; })) {
                return;
            }
            const std::size_t brace = text.find('{', statement->start);
            if (brace == std::string::npos || brace > statement->value->start || statement->items.empty()
                || statement->items.back()->start < brace) {
                continue;
            }
            edit.change(source, statement->items.back()->end, statement->items.back()->end, ", " + name);
            return;
        }
        const std::string quote(1, d.style.quote);
        const std::string line = "import { " + name + " } from " + quote + from + quote + (d.style.semicolons ? ";" : "");
        if (after != std::string::npos) {
            edit.change(source, after, after, d.style.lineBreak + line);
        } else {
            edit.change(source, 0, 0, line + d.style.lineBreak + (text.empty() ? "" : d.style.lineBreak));
        }
    }

    Outcome add(std::string_view page, std::string_view id, double x, double y, std::string_view fromNode,
                std::string_view fromPin) const
    {
        Edit edit(d);
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        const Item* from = d.item(fromNode);
        const Pin* out = from != nullptr ? findPin(from->outputs, fromPin) : nullptr;
        const Pin* in = from != nullptr && out == nullptr ? findPin(from->inputs, fromPin) : nullptr;
        const Chain* chain = nullptr;
        const Item* anchor = nullptr;
        std::string chainPin;
        std::size_t at = 0;
        Preset preset;
        if (out != nullptr && out->type == PinType::Exec) {
            if (const std::optional<Exit> exit = exitOf(*from, fromPin)) {
                chain = exit->chain;
                at = exit->index;
                anchor = from;
                chainPin = std::string(fromPin);
            }
        } else if (in != nullptr && in->type == PinType::Exec && from->chain != nullptr) {
            chain = from->chain;
            at = from->index;
            anchor = from;
        } else if (out != nullptr) {
            if (!out->local.empty()) {
                preset = std::pair{out->type, out->local};
            }
            const bool body = from->role == Role::Entry || from->role == Role::Event || from->role == Role::ForEach
                           || from->role == Role::Repeat || from->role == Role::Fused
                           || from->role == Role::ForRange;
            if (from->role == Role::Job) {
                const Pin* exit = nullptr;
                for (const Pin& output : from->outputs) {
                    if (&output == out) {
                        break;
                    }
                    if (output.type == PinType::Exec) {
                        exit = &output;
                    }
                }
                if (exit != nullptr && exit->branch != nullptr) {
                    chain = exit->branch;
                    anchor = from;
                    chainPin = exit->name;
                }
            } else if (body) {
                for (const Pin& output : from->outputs) {
                    if (output.branch != nullptr) {
                        chain = output.branch;
                        anchor = from;
                        chainPin = std::string(output.name);
                        break;
                    }
                }
            } else if (from->chain != nullptr) {
                chain = from->chain;
                at = from->index + from->count;
                anchor = from;
                chainPin = "exec";
            }
        }
        const std::optional<Made> made = generate(id, preset, page);
        if (!made) {
            return edit.refuse(say("reason:notFound"));
        }
        std::string code = made->code;
        if (made->event) {
            const std::string& text = d.file().text;
            std::size_t offset = text.size();
            std::string prefix = text.empty() || breakAt(text.back()) ? std::string() : d.style.lineBreak;
            if (const std::size_t after = afterLastRegistration(id); after != std::string::npos) {
                offset = after;
                const bool lastLine = offset >= text.size() && !text.empty() && !breakAt(text.back());
                prefix = lastLine ? d.style.lineBreak : std::string();
            }
            edit.change(d.file(), offset, offset, prefix + reindent(code, "", d.style.lineBreak) + d.style.lineBreak);
        } else if (made->pure) {
            if (in != nullptr && in->type != PinType::Exec) {
                edit.writeInput(*from, *in, wrap(made->output.value_or(PinType::Any), *in, code));
            } else if (id.starts_with("make:")) {
                const std::string name = freeName(d.file(), identifierFrom(say("name:value"), "value"), 0);
                code = "const " + name + " = " + code;
                if (chain != nullptr) {
                    Plan& plan = edit.plan(chain);
                    plan.at = at;
                    plan.inserted.push_back(code);
                } else {
                    edit.floatCode(page, code, x, y);
                }
            } else {
                edit.floatCode(page, code, x, y);
            }
        } else if (in != nullptr && in->type != PinType::Exec && !made->declaration) {
            const Item* owner = statementOwner(from);
            if (owner == nullptr || owner->chain == nullptr) {
                return edit.refuse(say("reason:noPin"));
            }
            const std::string name = freeName(*owner->source, identifierFrom(say("name:value"), "value"),
                                              owner->statement->start);
            code = "const " + name + " = " + withAwait(*made, owner->chain, edit);
            Plan& plan = edit.plan(owner->chain);
            plan.at = owner->index;
            plan.inserted.push_back(code);
            edit.writeInput(*from, *in, wrap(made->output.value_or(PinType::Any), *in, name));
            chain = owner->chain;
            anchor = owner;
            chainPin.clear();
        } else if (chain != nullptr) {
            code = withAwait(*made, chain, edit);
            Plan& plan = edit.plan(chain);
            plan.at = at;
            plan.inserted.push_back(code);
        } else {
            code = withAwait(*made, nullptr, edit);
            edit.floatCode(page, code, x, y);
        }
        if (!made->library.empty()) {
            importFromLibrary(edit, made->library);
        }
        Outcome outcome = edit.done();
        if (!outcome.ok()) {
            return outcome;
        }
        const std::size_t firstBreak = code.find('\n');
        const std::string firstLine = firstBreak == std::string::npos ? code : code.substr(0, firstBreak);
        outcome.key = anchor != nullptr ? placeInChain(outcome, *anchor, chainPin, 1) : std::string();
        if (outcome.key.empty()) {
            outcome.key = locate(outcome, code, page, x, y);
        }
        if (outcome.key.empty() && firstBreak != std::string::npos) {
            outcome.key = locate(outcome, firstLine, page, x, y);
        }
        return outcome;
    }

    Outcome insertOnWire(std::string_view wireKey, std::string_view nodeKey) const
    {
        Edit edit(d);
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        std::string base(wireKey);
        if (base.ends_with("~1") || base.ends_with("~2")) {
            base.resize(base.size() - 2);
        }
        const Wire* wire = nullptr;
        for (const Page& page : d.pages) {
            for (const Wire& candidate : page.wires) {
                if (candidate.key == base) {
                    wire = &candidate;
                }
            }
        }
        if (wire == nullptr || wire->type != "exec") {
            return edit.refuse(say("reason:notFound"));
        }
        const Item* from = d.item(wire->from);
        const Item* to = d.item(wire->to);
        const Item* dropped = d.item(nodeKey);
        if (from == nullptr || to == nullptr || dropped == nullptr || dropped->source == nullptr
            || dropped->page != from->page) {
            return edit.refuse(say("reason:notFound"));
        }
        if (dropped->source->id.empty() || dropped->chain == nullptr || !floatingHead(*dropped)) {
            return edit.refuse(say("reason:wireBlock"));
        }
        const Source* run = dropped->source;
        if (const Page* page = d.page(dropped->page); page != nullptr) {
            for (const Wire& candidate : page->wires) {
                const Item* head = d.item(candidate.from);
                const Item* tail = d.item(candidate.to);
                if (head == nullptr || tail == nullptr) {
                    continue;
                }
                if ((head->source == run) != (tail->source == run)) {
                    return edit.refuse(say("reason:wireBlock"));
                }
            }
        }
        if (to->source == run) {
            return edit.refuse(say("reason:wireBlock"));
        }
        const std::optional<Exit> exit = exitOf(*from, wire->fromPin);
        if (!exit || to->chain != exit->chain || to->index != exit->index) {
            return edit.refuse(say("reason:noPin"));
        }
        const std::string moved = edit.runText(*dropped->chain, 0);
        if (moved.empty()) {
            return edit.refuse(say("reason:noPin"));
        }
        Plan& plan = edit.plan(exit->chain);
        plan.at = exit->index;
        plan.inserted.push_back(moved);
        edit.dropped.insert(dropped->source);
        Outcome outcome = edit.done();
        if (!outcome.ok()) {
            return outcome;
        }
        outcome.key = placeInChain(outcome, *from, wire->fromPin, dropped->chain->statements.size());
        if (outcome.key.empty()) {
            const std::size_t firstBreak = moved.find('\n');
            outcome.key = locate(outcome, firstBreak == std::string::npos ? moved : moved.substr(0, firstBreak),
                                 from->page, dropped->x, dropped->y);
        }
        return outcome;
    }

    Outcome drop(std::string_view page, const nlohmann::json& item, double x, double y, std::string_view node,
                 std::string_view pin) const
    {
        const std::string section = item.value("section", std::string());
        const std::string key = item.value("item", std::string());
        const std::string member = item.value("member", std::string());
        std::string id;
        if (key.starts_with("control:")) {
            const std::string control = key.substr(8);
            if (member.empty()) {
                id = "ref:controls." + control;
            } else {
                const ControlEntry* entry = d.control(control);
                const Api& api = d.api();
                const ApiMember* found = api.member(
                    entry != nullptr && api.interfaces.contains(entry->type) ? entry->type : "Component", member);
                if (found == nullptr) {
                    return Edit(d).refuse(say("reason:notFound"));
                }
                const std::string path = "controls." + control + "." + member;
                id = isEventMember(api, *found) ? "event:" + path : found->property ? "get:" + path : "call:" + path;
            }
        } else if (key.starts_with("variable:") || key.starts_with("local:")) {
            const std::string mode = item.value("mode", std::string());
            const bool local = key.starts_with("local:");
            const std::string name = key.substr(local ? 6 : 9);
            const VariableInfo* info = d.declaredVariable(local ? page : std::string_view(), name);
            if (mode == "set" && info != nullptr && info->constant) {
                return Edit(d).refuse(say("reason:constant"));
            }
            id = (mode == "set" ? "set:" : "get:") + key;
        } else if (key.starts_with("var:")) {
            id = "get:vars." + key.substr(4);
        } else if (key.starts_with("global:")) {
            id = "get:global:" + key.substr(7);
        } else if (key.starts_with("lib:")) {
            id = "call:lib:" + key.substr(4);
        } else if (key.starts_with("fn:")) {
            id = d.functions.contains(key.substr(3)) ? "call:fn:" + key.substr(3)
                                                     : "call:this." + key.substr(key.rfind('.') + 1);
        } else if (key.starts_with("def:")) {
            const std::string typeName(key.substr(4));
            if (structNamed(d.types, typeName) != nullptr) {
                id = member.empty() ? "make:" + typeName : "break:" + typeName + "." + member;
            } else if (enumNamed(d.types, typeName) != nullptr) {
                id = member.empty() ? "enum:" + typeName : "enumvalue:" + typeName + "." + member.substr(member.rfind('.') + 1);
            } else if (std::ranges::find(d.classes, typeName, &ClassInfo::name) != d.classes.end()) {
                id = member.empty()                                 ? "new:" + typeName
                   : member.starts_with("field:")                   ? "getobject:" + typeName + "." + member.substr(6)
                   : member.starts_with("event:")                   ? "emit:" + typeName + "." + member.substr(6)
                   : member.starts_with("fn:" + typeName + ".")     ? "callobject:" + typeName + "." + member.substr(typeName.size() + 4)
                                                                    : "new:" + typeName;
            }
        } else if (key.starts_with("prop:")) {
            id = "get:this.props." + key.substr(5);
        } else if (key.starts_with("event:")) {
            id = "call:this." + key.substr(6) + ".emit";
        } else if (key.starts_with("field:")) {
            id = "get:this." + key.substr(6);
        }
        static_cast<void>(section);
        if (id.empty()) {
            Outcome outcome;
            outcome.layout = d.layout;
            return outcome;
        }
        return add(page, id, x, y, node, pin);
    }

    void floatBranches(Edit& edit, const Item& item) const
    {
        for (const Pin& output : item.outputs) {
            const Chain* chain = output.branch;
            if (chain == nullptr || chain->statements.empty()) {
                continue;
            }
            const Item* first = chain->items.empty() ? nullptr : chain->items.front();
            edit.floatCode(chain->page, edit.runText(*chain, 0), first != nullptr ? first->x : item.x + 40,
                           first != nullptr ? first->y : item.y + 120);
        }
    }

    Outcome remove(std::string_view page, const std::vector<std::string>& keys) const
    {
        Edit edit(d);
        const std::set<std::string, std::less<>> chosen(keys.begin(), keys.end());
        PageLayout& pageLayout = edit.layout.pages[edit.anchorOf(page)];
        std::erase_if(pageLayout.comments, [&chosen](const CommentBox& box) { return chosen.contains(box.key); });
        std::erase_if(pageLayout.reroutes, [&chosen](const Reroute& reroute) { return chosen.contains(reroute.key); });
        std::vector<const Item*> items;
        for (const std::string& key : keys) {
            if (const Item* item = d.item(key)) {
                items.push_back(item);
            }
        }
        if (!items.empty() && (!d.ok || d.environment.readOnly)) {
            return edit.refuse(say("reason:readOnly"));
        }
        const auto nested = [&items](const Item* item) {
            if (item->statement == nullptr) {
                return false;
            }
            return std::ranges::any_of(items, [item](const Item* other) {
                if (other == item || other->source != item->source || other->statement == nullptr
                    || other->role == Role::Pure) {
                    return false;
                }
                const Node* last = other->last != nullptr ? other->last : other->statement;
                return item->statement->start >= other->statement->start && item->statement->end <= last->end
                    && !(item->statement == other->statement && item->chain == other->chain);
            });
        };
        for (const Item* item : items) {
            if (nested(item)) {
                continue;
            }
            switch (item->role) {
            case Role::State:
                removeState(edit, *item);
                continue;
            case Role::Entry:
            case Role::Reroute:
                continue;
            case Role::Pure:
                if (item->consumer != nullptr) {
                    if (const Pin* pin = findPin(item->consumer->inputs, item->consumerPin)) {
                        edit.clearInput(*item->consumer, *pin, false);
                    }
                } else if (item->source != nullptr && !item->source->id.empty()) {
                    edit.dropped.insert(item->source);
                }
                continue;
            case Role::Event:
            case Role::Ready:
            case Role::Run:
            case Role::Override: {
                floatBranches(edit, *item);
                if (item->statement != nullptr) {
                    const Span range = edit.span(*item->source, item->statement, item->statement);
                    edit.change(*item->source, range.start, range.end, {});
                }
                continue;
            }
            case Role::Load:
                floatBranches(edit, *item);
                for (const Pin& output : item->outputs) {
                    const Chain* chain = output.branch;
                    if (chain != nullptr) {
                        for (std::size_t i = 0; i < chain->statements.size(); ++i) {
                            edit.plan(chain).removed.insert(i);
                        }
                    }
                }
                continue;
            default:
                break;
            }
            if (item->chain == nullptr) {
                if (item->role == Role::Code && item->source != nullptr && !item->source->id.empty()) {
                    edit.dropped.insert(item->source);
                }
                continue;
            }
            if (keepMainBranch(edit, *item)) {
                continue;
            }
            floatBranches(edit, *item);
            for (std::size_t i = item->index; i < item->index + item->count; ++i) {
                edit.plan(item->chain).removed.insert(i);
            }
        }
        std::vector<const Chain*> emptied;
        for (const Plan& work : edit.plans) {
            const Chain* chain = work.chain;
            if (chain == nullptr || chain->statements.empty() || work.removed.size() != chain->statements.size()) {
                continue;
            }
            if (fanMemberOf(d, *chain)) {
                emptied.push_back(chain);
            }
        }
        std::vector<const Item*> fanFrom;
        std::vector<std::string> fanPin;
        std::vector<std::vector<std::size_t>> fanGone;
        for (const Chain* chain : emptied) {
            for (Plan& work : edit.plans) {
                if (work.chain == chain) {
                    work.removed.clear();
                }
            }
            const std::optional<FanMember> member = fanMemberOf(d, *chain);
            if (!member) {
                continue;
            }
            auto at = std::ranges::find_if(fanFrom, [&member](const Item* entry) { return entry == member->from; });
            std::size_t index = static_cast<std::size_t>(at - fanFrom.begin());
            if (at == fanFrom.end()) {
                fanFrom.push_back(member->from);
                fanPin.push_back(member->pin);
                fanGone.emplace_back();
            }
            std::vector<std::size_t>& gone = fanGone[index];
            if (std::ranges::find(gone, member->index) == gone.end()) {
                gone.push_back(member->index);
            }
        }
        for (std::size_t i = 0; i < fanFrom.size(); ++i) {
            if (const std::string why = removeArrows(edit, *fanFrom[i], fanPin[i], fanGone[i], false); !why.empty()) {
                return edit.refuse(why);
            }
        }
        return edit.done();
    }

    [[nodiscard]] static std::string_view mainBranchOf(Role role) noexcept
    {
        switch (role) {
        case Role::Branch:
            return "true";
        case Role::Fused:
            return "found";
        case Role::ForEach:
        case Role::Repeat:
        case Role::While:
            return "each";
        default:
            return {};
        }
    }

    [[nodiscard]] static bool leavesLoop(const Node* node, bool inLoop, bool inSwitch)
    {
        if (node == nullptr) {
            return false;
        }
        switch (node->kind) {
        case Kind::Function:
        case Kind::Arrow:
        case Kind::Class:
            return false;
        case Kind::Break:
            return node->target != nullptr || (!inLoop && !inSwitch);
        case Kind::Continue:
            return node->target != nullptr || !inLoop;
        default:
            break;
        }
        const bool loop = inLoop || node->kind == Kind::For || node->kind == Kind::ForIn || node->kind == Kind::ForOf
                       || node->kind == Kind::While || node->kind == Kind::DoWhile;
        const bool choice = inSwitch || node->kind == Kind::Switch;
        for (const Node* child : {node->target, node->value, node->test, node->body, node->other}) {
            if (leavesLoop(child, loop, choice)) {
                return true;
            }
        }
        return std::ranges::any_of(node->items, [loop, choice](const Node* child) {
            return leavesLoop(child, loop, choice);
        });
    }

    [[nodiscard]] std::string keptText(const Chain& body,
                                       const std::vector<std::pair<std::string_view, const syntax::Binding*>>& locals,
                                       const Edit& edit) const
    {
        const Source& source = *body.source;
        std::string text;
        for (const Node* statement : body.statements) {
            const Span range = body.shape == Shape::Expression ? Span{statement->start, statement->end}
                                                                : edit.span(source, statement, statement);
            std::vector<std::pair<std::size_t, std::size_t>> uses;
            visit(statement, [&](const Node* node) {
                if (node->kind != Kind::Identifier || source.scopes == nullptr) {
                    return;
                }
                for (const auto& [name, binding] : locals) {
                    if (node->text == name && source.scopes->lookup(name, node->start) == binding) {
                        uses.emplace_back(node->start - range.start, node->end - node->start);
                    }
                }
            });
            std::string piece = d.text(source, static_cast<std::uint32_t>(range.start), static_cast<std::uint32_t>(range.end));
            std::ranges::sort(uses, std::greater<>());
            for (const auto& [at, length] : uses) {
                piece.replace(at, length, "undefined");
            }
            text += (text.empty() ? "" : "\n") + dedent(piece);
        }
        return text;
    }

    bool keepMainBranch(Edit& edit, const Item& item) const
    {
        const std::string_view mainName = mainBranchOf(item.role);
        const Pin* pin = mainName.empty() ? nullptr : findPin(item.outputs, mainName);
        const Chain* body = pin != nullptr ? pin->branch : nullptr;
        if (body == nullptr || body->statements.empty() || body->source != item.chain->source) {
            return false;
        }
        if (item.role != Role::Branch && item.role != Role::Fused
            && std::ranges::any_of(body->statements, [](const Node* statement) {
                   return leavesLoop(statement, false, false);
               })) {
            return false;
        }
        if (!edit.plan(item.chain).inserted.empty()) {
            return false;
        }
        std::vector<std::pair<std::string_view, const syntax::Binding*>> locals;
        for (const Pin& output : item.outputs) {
            if (!output.local.empty() && output.binding != nullptr) {
                locals.emplace_back(output.local.view(), output.binding);
            }
        }
        const std::string kept = keptText(*body, locals, edit);
        for (const Pin& output : item.outputs) {
            const Chain* chain = output.branch;
            if (chain == nullptr || chain == body || chain->statements.empty()) {
                continue;
            }
            const Item* first = chain->items.empty() ? nullptr : chain->items.front();
            edit.floatCode(chain->page, edit.runText(*chain, 0), first != nullptr ? first->x : item.x + 40,
                           first != nullptr ? first->y : item.y + 120);
        }
        Plan& plan = edit.plan(item.chain);
        for (std::size_t i = item.index; i < item.index + item.count; ++i) {
            plan.removed.insert(i);
        }
        plan.at = item.index;
        plan.inserted.push_back(kept);
        return true;
    }

    [[nodiscard]] std::string copy(const std::vector<std::string>& keys) const
    {
        std::vector<const Item*> items;
        for (const std::string& key : keys) {
            const Item* item = d.item(key);
            if (item != nullptr && item->role != Role::Entry && (item->statement != nullptr || item->expression != nullptr)) {
                items.push_back(item);
            }
        }
        std::ranges::stable_sort(items, [](const Item* a, const Item* b) {
            const Node* first = a->statement != nullptr ? a->statement : a->expression;
            const Node* second = b->statement != nullptr ? b->statement : b->expression;
            if (a->source != b->source) {
                return a->source->id < b->source->id;
            }
            return first->start < second->start;
        });
        Edit edit(d);
        std::string text;
        for (const Item* item : items) {
            const bool inside = std::ranges::any_of(items, [item](const Item* other) {
                if (other == item || other->source != item->source || other->statement == nullptr
                    || item->statement == nullptr || other->role == Role::Pure) {
                    return false;
                }
                const Node* last = other->last != nullptr ? other->last : other->statement;
                return item->statement->start >= other->statement->start && item->statement->end <= last->end
                    && item->statement != other->statement;
            });
            if (inside) {
                continue;
            }
            std::string part;
            if (item->role == Role::Pure || item->statement == nullptr) {
                part = d.text(*item->source, item->expression);
            } else {
                const Node* last = item->last != nullptr ? item->last : item->statement;
                const Span range = edit.span(*item->source, item->statement, last);
                part = dedent(d.text(*item->source, static_cast<std::uint32_t>(range.start),
                                     static_cast<std::uint32_t>(range.end)));
            }
            if (!part.empty()) {
                text += (text.empty() ? "" : "\n") + part;
            }
        }
        return text;
    }

    Outcome paste(std::string_view page, std::string_view text, double x, double y) const
    {
        Edit edit(d);
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        Source probe;
        probe.text = std::string(text);
        if (!parseSource(probe) || probe.tree.program->items.empty()) {
            return edit.refuse(say("reason:paste"));
        }
        const Document reading(probe.text, d.environment, {});
        std::set<std::uint32_t> registrations;
        for (const Item& item : reading.internals().items) {
            if (item.role == Role::Event && item.statement != nullptr && item.chain == nullptr) {
                registrations.insert(item.statement->start);
            }
        }
        std::string rest;
        std::string first;
        const std::string& file = d.file().text;
        std::string appended;
        for (const Node* statement : probe.tree.program->items) {
            const std::string part = dedent(probe.text.substr(statement->start, statement->end - statement->start));
            if (registrations.contains(statement->start)) {
                appended += reindent(part, "", d.style.lineBreak) + d.style.lineBreak;
                if (first.empty()) {
                    first = part;
                }
            } else {
                rest += (rest.empty() ? "" : "\n") + part;
            }
        }
        if (!appended.empty()) {
            const std::string prefix = file.empty() || breakAt(file.back()) ? std::string() : d.style.lineBreak;
            edit.change(d.file(), file.size(), file.size(), prefix + appended);
        }
        if (!rest.empty()) {
            edit.floatCode(page, rest, x, y);
        }
        Outcome outcome = edit.done();
        const std::string located = !rest.empty() ? locate(outcome, rest.substr(0, rest.find('\n')), page, x, y)
                                                  : locate(outcome, first, page, x, y);
        if (!appended.empty() && !rest.empty() && !first.empty()) {
            static_cast<void>(locate(outcome, first, page, x, y + 160));
        }
        outcome.key = located;
        return outcome;
    }

    Outcome duplicate(std::string_view page, const std::vector<std::string>& keys) const
    {
        const std::string text = copy(keys);
        if (text.empty()) {
            return Edit(d).refuse(say("reason:paste"));
        }
        double x = 0;
        double y = 0;
        for (const std::string& key : keys) {
            if (const Item* item = d.item(key)) {
                x = item->x;
                y = item->y;
                break;
            }
        }
        return paste(page, text, x + 24, y + 24);
    }

    Outcome setCode(std::string_view node, std::string_view text) const
    {
        Edit edit(d);
        const Item* item = d.item(node);
        if (item == nullptr || item->role != Role::Code) {
            return edit.refuse(say("reason:notFound"));
        }
        Source probe;
        probe.text = std::string(text);
        if (!parseSource(probe)) {
            return edit.refuse(d.say.format("reason:code", probe.failure));
        }
        if (item->chain == nullptr) {
            if (item->source == nullptr || item->source->id.empty()) {
                return edit.refuse(say("reason:notFound"));
            }
            if (!d.ok || d.environment.readOnly) {
                return edit.refuse(say("reason:readOnly"));
            }
            PageLayout& layout = edit.layout.pages[edit.anchorOf(item->page)];
            for (FloatingCode& entry : layout.floating) {
                if (entry.key == item->source->id) {
                    entry.code = dedent(text);
                }
            }
            return edit.done();
        }
        const Source& source = *item->source;
        const Node* first = item->chain->statements[item->index];
        const Node* last = item->chain->statements[item->index + item->count - 1];
        const std::string indent = blankBefore(source.text, first->start) ? indentOf(source.text, first->start) : std::string();
        edit.change(source, first->start, last->end, firstIndented(dedent(text), indent, d.style.lineBreak));
        return edit.done();
    }

    Outcome replaceControl(std::string_view node, std::string_view from, std::string_view to) const
    {
        Edit edit(d);
        const Item* item = d.item(node);
        if (item == nullptr || item->source == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        const std::uint32_t start = item->statement != nullptr ? item->statement->start : item->sigStart;
        const std::uint32_t end = item->last != nullptr ? item->last->end : item->sigEnd;
        const Source& source = *item->source;
        if (end <= start || end > source.text.size()) {
            return edit.refuse(say("reason:notFound"));
        }
        const std::string before = source.text.substr(start, end - start);
        const std::string after = renamedControl(before, from, to);
        if (after == before) {
            return edit.refuse(say("reason:notFound"));
        }
        edit.change(source, start, end, after);
        return edit.done();
    }

    Outcome collapse(const std::vector<std::string>& keys) const
    {
        Edit edit(d);
        std::vector<const Item*> items;
        for (const std::string& key : keys) {
            const Item* item = d.item(key);
            if (item == nullptr || item->role == Role::Pure) {
                continue;
            }
            if (item->chain == nullptr || item->role == Role::Event || item->role == Role::Entry) {
                return edit.refuse(say("reason:collapseEvent"));
            }
            items.push_back(item);
        }
        if (items.empty()) {
            return edit.refuse(say("reason:contiguous"));
        }
        std::ranges::sort(items, [](const Item* a, const Item* b) { return a->index < b->index; });
        const Chain& chain = *items.front()->chain;
        for (std::size_t i = 1; i < items.size(); ++i) {
            if (items[i]->chain != &chain || items[i]->index != items[i - 1]->index + items[i - 1]->count) {
                return edit.refuse(say("reason:contiguous"));
            }
        }
        if (!chain.source->id.empty() || chain.source->scopes == nullptr) {
            return edit.refuse(say("reason:contiguous"));
        }
        const Source& source = *chain.source;
        const std::size_t from = items.front()->index;
        const std::size_t to = items.back()->index + items.back()->count;
        const std::uint32_t start = chain.statements[from]->start;
        const std::uint32_t end = chain.statements[to - 1]->end;
        bool waits = false;
        bool self = false;
        for (std::size_t i = from; i < to; ++i) {
            const Node* statement = chain.statements[i];
            if (returns(statement) || escapesLoop(statement)) {
                return edit.refuse(say("reason:collapseExit"));
            }
            waits = waits || awaits(statement);
            visit(statement, [&self](const Node* node) { self = self || node->kind == Kind::This; });
        }
        std::vector<std::string> params;
        std::vector<const Binding*> outers;
        std::vector<const Binding*> inner;
        for (std::size_t i = from; i < to; ++i) {
            for (const Node* id : identifiersIn(chain.statements[i])) {
                const Binding* binding = source.scopes->lookup(id->text, id->start);
                if (binding == nullptr || binding->scope == source.scopes->root()) {
                    continue;
                }
                const bool declaredInside = binding->id->start >= start && binding->id->end <= end;
                if (declaredInside) {
                    if (binding->id == id && std::ranges::find(inner, binding) == inner.end()) {
                        inner.push_back(binding);
                    }
                    continue;
                }
                if (binding->kind == BindingKind::Function || binding->kind == BindingKind::Class) {
                    continue;
                }
                if (std::ranges::find(outers, binding) == outers.end()) {
                    outers.push_back(binding);
                    params.emplace_back(binding->name);
                }
            }
        }
        std::vector<std::string> results;
        const std::uint32_t scopeEnd = chain.owner != nullptr ? chain.owner->end : static_cast<std::uint32_t>(source.text.size());
        visit(source.tree.program, [&](const Node* node) {
            if (node->kind != Kind::Identifier || node->start < end || node->start >= scopeEnd) {
                return;
            }
            const Binding* binding = source.scopes->lookup(node->text, node->start);
            if (binding != nullptr && std::ranges::find(inner, binding) != inner.end()
                && std::ranges::find(results, std::string(binding->name)) == results.end()) {
                results.emplace_back(binding->name);
            }
        });
        if (results.size() > 1) {
            return edit.refuse(say("reason:manyOutputs"));
        }
        const bool method = self && chain.function != nullptr && d.defaultClass() != nullptr;
        const std::string name = freeName(source, identifierFrom(say("name:function"), "newFunction"), 0);
        std::string list;
        for (std::size_t i = 0; i < params.size(); ++i) {
            list += (i > 0 ? ", " : "") + params[i];
        }
        const std::string& lineBreak = d.style.lineBreak;
        std::string body = reindent(edit.runText(chain, from, to), d.style.indent, lineBreak);
        if (!results.empty()) {
            body += lineBreak + d.style.indent + "return " + results.front() + edit.semi();
        }
        std::string call = (waits ? "await " : "") + std::string(method ? "this." : "") + name + "(" + list + ")";
        if (!results.empty()) {
            call = "const " + results.front() + " = " + call;
        }
        Plan& plan = edit.plan(&chain);
        for (std::size_t i = from; i < to; ++i) {
            plan.removed.insert(i);
        }
        plan.at = from;
        plan.inserted.push_back(call + edit.semi());
        if (waits && chain.function != nullptr && !chain.async) {
            edit.asyncFunctions.insert(chain.function);
        }
        if (method) {
            const Node* classNode = d.defaultClass()->node;
            const std::string indent = indentOf(source.text, classNode->start) + d.style.indent;
            const std::string text = lineBreak + reindent((waits ? "async " : "") + name + "(" + list + ") {" + lineBreak
                                                              + dedent(body) + lineBreak + "}",
                                                          indent, lineBreak)
                                   + lineBreak;
            const std::size_t close = classNode->body->end > 0 ? classNode->body->end - 1 : classNode->body->end;
            edit.change(source, lineStartOf(source.text, close), lineStartOf(source.text, close),
                        text.substr(lineBreak.size()));
        } else {
            const std::string& text = source.text;
            const std::string prefix = text.empty() || breakAt(text.back()) ? lineBreak : lineBreak + lineBreak;
            const std::string exported = d.environment.fileName == kLibraryFile ? "export " : "";
            edit.change(source, text.size(), text.size(),
                        prefix + exported + (waits ? "async " : "") + "function " + name + "(" + list + ") {" + lineBreak
                            + body + lineBreak + "}" + lineBreak);
        }
        Outcome outcome = edit.done();
        outcome.page = "fn:" + std::string(method ? d.defaultClass()->name + "." : "") + name;
        return outcome;
    }

    void setNote(Edit& edit, const Item& item, std::string_view text) const
    {
        const Source& source = *item.source;
        const Node* first = item.statement;
        const Node* last = item.last != nullptr ? item.last : item.statement;
        if (first == nullptr) {
            return;
        }
        const Span range = edit.span(source, first, last);
        const std::size_t lineStart = lineStartOf(source.text, first->start);
        const std::string indent = indentOf(source.text, first->start);
        std::string comments;
        std::size_t at = 0;
        while (at < text.size()) {
            std::size_t end = text.find('\n', at);
            if (end == std::string_view::npos) {
                end = text.size();
            }
            comments += indent + "// " + std::string(text.substr(at, end - at)) + d.style.lineBreak;
            at = end + 1;
        }
        const std::size_t from = range.lines ? range.start : lineStart;
        if (blankBefore(source.text, first->start)) {
            edit.change(source, std::min(from, lineStart), lineStart, comments);
        } else if (!comments.empty()) {
            edit.change(source, first->start, first->start, "/* " + std::string(text) + " */ ");
        }
        if (range.end > last->end) {
            const std::size_t trailingEnd = range.lines ? lineEndOf(source.text, last->end) : range.end;
            std::size_t trailingStart = last->end;
            const std::string_view gap(source.text.data() + last->end, trailingEnd - last->end);
            const std::size_t comment = std::min(gap.find("//"), gap.find("/*"));
            if (comment != std::string_view::npos) {
                trailingStart = last->end + comment;
                while (trailingStart > last->end && (source.text[trailingStart - 1] == ' ' || source.text[trailingStart - 1] == '\t')) {
                    --trailingStart;
                }
                edit.change(source, trailingStart, trailingEnd, {});
            }
        }
    }

    [[nodiscard]] static std::string trimmed(std::string_view text)
    {
        const std::size_t start = text.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos) {
            return {};
        }
        const std::size_t end = text.find_last_not_of(" \t\r\n");
        return std::string(text.substr(start, end - start + 1));
    }

    [[nodiscard]] static std::vector<std::string> splitCases(std::string_view text)
    {
        std::vector<std::string> parts;
        std::string current;
        char quote = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (quote != 0) {
                current.push_back(c);
                if (c == '\\' && i + 1 < text.size()) {
                    current.push_back(text[++i]);
                } else if (c == quote) {
                    quote = 0;
                }
                continue;
            }
            if (c == '"' || c == '\'' || c == '`') {
                quote = c;
                current.push_back(c);
                continue;
            }
            if (c == ',') {
                parts.push_back(trimmed(current));
                current.clear();
                continue;
            }
            current.push_back(c);
        }
        parts.push_back(trimmed(current));
        return parts;
    }

    Outcome setCases(const Item& item, std::string_view text) const
    {
        Edit edit(d);
        if (item.statement == nullptr || item.source == nullptr || item.statement->kind != Kind::Switch) {
            return edit.refuse(say("reason:notFound"));
        }
        const Source& source = *item.source;
        const std::vector<std::string> values = splitCases(text);
        std::vector<const Node*> cases;
        for (const Node* each : item.statement->items) {
            if (each->kind == Kind::Case && each->test != nullptr) {
                cases.push_back(each->test);
            }
        }
        const bool blank = std::ranges::any_of(values, [](const std::string& value) { return value.empty(); });
        if (values.size() != cases.size() || blank) {
            return edit.refuse(say("reason:cases"));
        }
        for (std::size_t i = 0; i < cases.size(); ++i) {
            if (trimmed(d.text(source, cases[i])) == values[i]) {
                continue;
            }
            edit.change(source, cases[i]->start, cases[i]->end, values[i]);
        }
        return edit.done();
    }

    Outcome editDetail(std::string_view page, std::string_view key, std::string_view name, const nlohmann::json& value) const
    {
        const Item* item = d.item(key);
        if (item == nullptr) {
            Edit edit(d);
            PageLayout& layout = edit.layout.pages[edit.anchorOf(page)];
            for (CommentBox& box : layout.comments) {
                if (box.key != key) {
                    continue;
                }
                if (name == "title" && value.is_string()) {
                    box.title = value.get<std::string>();
                } else if (name == "color" && value.is_string()) {
                    box.color = value.get<std::string>();
                }
            }
            return edit.done();
        }
        Edit edit(d);
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        const std::string text = value.is_string() ? value.get<std::string>() : std::string();
        if (name == "note") {
            setNote(edit, *item, text);
            return edit.done();
        }
        if (name == "code") {
            return setCode(key, text);
        }
        if (item->role == Role::Machine) {
            return machineDetail(*item, name, value);
        }
        if (item->role == Role::Switch && name == "cases") {
            return setCases(*item, text);
        }
        if (item->role == Role::Event && item->expression != nullptr) {
            const Node* call = item->expression;
            const Node* callee = unparen(call->target);
            const auto& args = call->items;
            const Source& source = *item->source;
            const char quote = d.style.quote;
            if (name == "key" && callee->kind == Kind::Member && unparen(callee->target)->kind == Kind::Member) {
                const Node* holder = unparen(callee->target);
                const Node* root = unparen(holder->target);
                if (root->kind != Kind::Identifier || root->text != "keys" || text.empty()) {
                    return edit.refuse(say("reason:noPin"));
                }
                if (holder->has(flag::kComputed)) {
                    edit.change(source, holder->value->start, holder->value->end, stringLiteral(text, quote));
                } else if (identifierText(text)) {
                    edit.change(source, holder->value->start, holder->value->end, text);
                } else {
                    edit.change(source, root->end, holder->end, "[" + stringLiteral(text, quote) + "]");
                }
                return edit.done();
            }
            if (name == "when" && callee->kind == Kind::Member && !text.empty()) {
                const bool wasLong = item->member == "onLongPress";
                const bool isLong = text == "onLongPress";
                edit.change(source, callee->value->start, callee->value->end, text);
                if (isLong && !wasLong && !args.empty()) {
                    edit.change(source, args.front()->start, args.front()->start, "500, ");
                } else if (wasLong && !isLong && args.size() >= 2) {
                    edit.change(source, args[0]->start, args[1]->start, {});
                }
                return edit.done();
            }
            if ((name == "holdMs" || name == "badgeMs") && item->badgeArgument < args.size() && value.is_number()) {
                const Node* duration = args[item->badgeArgument];
                edit.change(source, duration->start, duration->end, numberText(std::round(value.get<double>())));
                return edit.done();
            }
            const auto option = [&](std::string_view optionName, const std::string& wanted) {
                const Node* options = item->optionsArgument < args.size() ? unparen(args[item->optionsArgument]) : nullptr;
                const bool plain = wanted.empty();
                const std::string written = std::string(optionName) + ": " + wanted;
                if (options != nullptr && options->kind == Kind::Object) {
                    const Node* entry = nullptr;
                    for (const Node* property : options->items) {
                        if (property->kind == Kind::Property && property->target != nullptr
                            && property->target->text == optionName) {
                            entry = property;
                        }
                    }
                    if (entry != nullptr && plain) {
                        if (options->items.size() == 1) {
                            edit.change(source, args[item->optionsArgument - 1]->end, options->end, {});
                        } else {
                            const auto at = std::ranges::find(options->items, entry);
                            const std::size_t index = static_cast<std::size_t>(at - options->items.begin());
                            if (index + 1 < options->items.size()) {
                                edit.change(source, entry->start, options->items[index + 1]->start, {});
                            } else {
                                edit.change(source, options->items[index - 1]->end, entry->end, {});
                            }
                        }
                    } else if (entry != nullptr) {
                        edit.change(source, entry->value->start, entry->value->end, wanted);
                    } else if (!plain) {
                        const Node* tail = options->items.empty() ? nullptr : options->items.back();
                        if (tail != nullptr) {
                            edit.change(source, tail->end, tail->end, ", " + written);
                        } else {
                            edit.change(source, options->start, options->end, "{ " + written + " }");
                        }
                    }
                    return edit.done();
                }
                if (!plain && !args.empty()) {
                    edit.change(source, args.back()->end, args.back()->end, ", { " + written + " }");
                }
                return edit.done();
            };
            if (name == "again" && item->optionsArgument != kNone) {
                return option("again", text.empty() || text == "ignore" ? std::string() : stringLiteral(text, quote));
            }
            if (name == "consume" && item->optionsArgument != kNone) {
                return option("consume", text == "both" ? std::string("false") : std::string());
            }
        }
        if (findPin(item->inputs, name) != nullptr) {
            return setValue(key, name, value);
        }
        if (item->token.starts_with("enum:") && name == "option" && item->expression != nullptr
            && item->expression->kind == Kind::Member && item->expression->value != nullptr && item->source != nullptr) {
            const EnumInfo* info = enumNamed(d.types, std::string_view(item->token).substr(5));
            if (info == nullptr || std::ranges::find(info->options, text) == info->options.end()) {
                return edit.refuse(d.say.format("reason:noOption", text));
            }
            edit.change(*item->source, item->expression->value->start, item->expression->value->end, text);
            return edit.done();
        }
        return edit.refuse(say("reason:noPin"));
    }

    [[nodiscard]] std::size_t withDocStart(const Source& source, const Node* declaration) const
    {
        std::size_t boundary = declaration->start;
        std::size_t start = boundary;
        const std::vector<syntax::Comment>& comments = source.tree.comments;
        auto it = std::ranges::lower_bound(comments, boundary, {}, &syntax::Comment::start);
        while (it != comments.begin()) {
            --it;
            if (it->end > boundary) {
                continue;
            }
            const std::string_view gap(source.text.data() + it->end, boundary - it->end);
            if (gap.find_first_not_of(" \t\r\n") != std::string_view::npos
                || std::string_view(source.text).substr(it->start, 3) != "/**") {
                break;
            }
            start = it->start;
            boundary = it->start;
        }
        return start;
    }

    [[nodiscard]] std::vector<const Node*> bindingUses(const Source& source, const Node* id,
                                                       const Node* declaration) const
    {
        std::vector<const Node*> list;
        if (source.scopes == nullptr || id == nullptr) {
            return list;
        }
        const auto inside = [declaration](const Node* node) {
            return declaration != nullptr && node != declaration && node->start >= declaration->start
                && node->end <= declaration->end;
        };
        const std::string name(id->text);
        const Binding* binding = source.scopes->declared(id);
        if (binding == nullptr) {
            visit(source.tree.program, [&](const Node* node) {
                if (node->kind != Kind::Member || node->has(flag::kComputed) || node->value == nullptr
                    || node->value->text != name || inside(node)) {
                    return;
                }
                const Node* target = unparen(node->target);
                const bool onThis = target != nullptr
                                 && (target->kind == Kind::This
                                     || (target->kind == Kind::Member && target->value != nullptr
                                         && target->value->text == "props"
                                         && unparen(target->target) != nullptr
                                         && unparen(target->target)->kind == Kind::This));
                if (onThis) {
                    list.push_back(node->value);
                }
            });
            return list;
        }
        std::set<const Node*> skip;
        visit(source.tree.program, [&skip](const Node* node) {
            if (node->kind == Kind::Member && node->value != nullptr) {
                skip.insert(node->value);
            } else if (node->kind == Kind::ClassMember && node->target != nullptr) {
                skip.insert(node->target);
            } else if (node->kind == Kind::Property && node->target != nullptr && !node->has(flag::kShorthand)) {
                skip.insert(node->target);
            } else if (node->kind == Kind::Labeled && node->target != nullptr) {
                skip.insert(node->target);
            }
        });
        visit(source.tree.program, [&](const Node* node) {
            if (node->kind != Kind::Identifier || node == id || skip.contains(node) || node->text != name
                || inside(node)) {
                return;
            }
            if (source.scopes->lookup(name, node->start) != binding) {
                return;
            }
            list.push_back(node);
        });
        return list;
    }

    void renameBinding(Edit& edit, const Source& source, const Node* id, const Node* declaration,
                       std::string_view wanted) const
    {
        for (const Node* use : bindingUses(source, id, declaration)) {
            edit.change(source, use->start, use->end, std::string(wanted));
        }
    }

    [[nodiscard]] bool nameTaken(std::string_view page, const VariableSpec& spec) const
    {
        if (spec.scope == "class") {
            const ClassInfo* info = d.defaultClass();
            if (info == nullptr) {
                return false;
            }
            return std::ranges::find(info->fields, spec.name) != info->fields.end() || info->props.contains(spec.name);
        }
        return d.declaredVariable(spec.scope == "local" ? page : std::string_view(), spec.name) != nullptr;
    }

    [[nodiscard]] const Node* propsHolder(const ClassInfo& info) const
    {
        const std::string_view wanted = info.task ? "params" : "props";
        for (const Node* member : info.node->items) {
            if (member->kind == Kind::ClassMember && member->has(flag::kStatic) && member->target != nullptr
                && member->target->text == wanted) {
                return member;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const Node* propsEntry(const ClassInfo& info, std::string_view name) const
    {
        const Node* holder = propsHolder(info);
        const Node* object = holder != nullptr ? unparen(holder->value) : nullptr;
        if (object == nullptr || object->kind != Kind::Object) {
            return nullptr;
        }
        for (const Node* entry : object->items) {
            if (entry->kind == Kind::Property && entry->target != nullptr && entry->target->text == name) {
                return entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::string propsEntryText(const ClassInfo& info, const VariableSpec& spec) const
    {
        const std::string kind = variablePropKind(spec.kind);
        const std::string space = info.task ? "param" : "prop";
        const std::string value = variableInitial(spec, d.style.quote);
        return spec.name + ": " + space + "." + kind + "(" + value + ", { label: "
             + stringLiteral(spec.name, d.style.quote) + " })";
    }

    void addPropsEntry(Edit& edit, const ClassInfo& info, const VariableSpec& spec) const
    {
        if (!variableEditableKind(spec.kind)) {
            return;
        }
        const Source& source = d.file();
        const std::string& text = source.text;
        const std::string entryText = propsEntryText(info, spec);
        const Node* holder = propsHolder(info);
        const Node* object = holder != nullptr ? unparen(holder->value) : nullptr;
        if (object != nullptr && object->kind == Kind::Object) {
            const bool multi = lineStartOf(text, object->start) != lineStartOf(text, object->end);
            const std::string indent = object->items.empty() ? indentOf(text, object->start) + d.style.indent
                                                             : indentOf(text, object->items.front()->start);
            const std::string before = multi ? d.style.lineBreak + indent : std::string(" ");
            if (object->items.empty()) {
                if (multi) {
                    edit.change(source, object->start + 1, object->start + 1, before + entryText);
                } else {
                    edit.change(source, object->end - 1, object->end - 1, before + entryText + " ");
                }
                return;
            }
            std::size_t at = object->items.back()->end;
            std::size_t next = at;
            while (next < text.size() && (text[next] == ' ' || text[next] == '\t')) {
                ++next;
            }
            if (next < text.size() && text[next] == ',') {
                at = next + 1;
                edit.change(source, at, at, before + entryText);
            } else {
                edit.change(source, at, at, "," + before + entryText);
            }
            return;
        }
        const Node* node = info.node;
        const std::size_t at = node->body->items.empty() ? afterLine(text, node->body->start)
                                                         : lineStartOf(text, node->body->items.front()->start);
        const std::string indent = node->body->items.empty() ? indentOf(text, node->start) + d.style.indent
                                                             : indentOf(text, node->body->items.front()->start);
        const std::string block = "static " + std::string(info.task ? "params" : "props") + " = { " + entryText + " }"
                                + (d.style.semicolons ? ";" : "");
        edit.change(source, at, at, reindent(block, indent, d.style.lineBreak) + d.style.lineBreak);
    }

    void updatePropsEntry(Edit& edit, const ClassInfo& info, std::string_view oldName, const VariableSpec& spec) const
    {
        const Node* entry = propsEntry(info, oldName);
        if (entry == nullptr) {
            return;
        }
        edit.change(d.file(), entry->start, entry->end, propsEntryText(info, spec));
    }

    void removePropsEntry(Edit& edit, const ClassInfo& info, std::string_view name) const
    {
        const Node* holder = propsHolder(info);
        const Node* object = holder != nullptr ? unparen(holder->value) : nullptr;
        const Node* entry = propsEntry(info, name);
        if (object == nullptr || entry == nullptr) {
            return;
        }
        const auto at = std::ranges::find(object->items, entry);
        const std::size_t index = static_cast<std::size_t>(at - object->items.begin());
        if (index + 1 < object->items.size()) {
            edit.change(d.file(), entry->start, object->items[index + 1]->start, {});
        } else if (index > 0) {
            edit.change(d.file(), object->items[index - 1]->end, entry->end, {});
        } else {
            edit.change(d.file(), entry->start, entry->end, {});
        }
    }

    [[nodiscard]] bool insertVariable(Edit& edit, const VariableSpec& spec, std::string_view page) const
    {
        const Source& source = d.file();
        const std::string& text = source.text;
        const std::string declaration = variableDeclaration(spec, d.style.quote, d.style.semicolons);
        if (spec.scope == "class") {
            const ClassInfo* info = d.defaultClass();
            const Node* node = info != nullptr ? info->node : nullptr;
            if (node == nullptr || node->body == nullptr || node->body->kind != Kind::Block) {
                return false;
            }
            if (spec.editable) {
                addPropsEntry(edit, *info, spec);
            }
            const std::string indent = node->body->items.empty()
                                         ? indentOf(text, node->start) + d.style.indent
                                         : indentOf(text, node->body->items.front()->start);
            const std::size_t at = node->body->items.empty() ? afterLine(text, node->body->start)
                                                             : lineStartOf(text, node->body->items.front()->start);
            edit.change(source, at, at, reindent(declaration, indent, d.style.lineBreak) + d.style.lineBreak);
            return true;
        }
        if (spec.scope == "local") {
            const Page* target = d.page(page);
            const Node* function = target != nullptr ? target->function : nullptr;
            const Node* body = function != nullptr ? function->body : nullptr;
            if (body == nullptr || body->kind != Kind::Block) {
                return false;
            }
            const std::string indent = body->items.empty() ? indentOf(text, body->start) + d.style.indent
                                                           : indentOf(text, body->items.front()->start);
            const std::size_t at = body->items.empty() ? afterLine(text, body->start)
                                                       : lineStartOf(text, body->items.front()->start);
            edit.change(source, at, at, reindent(declaration, indent, d.style.lineBreak) + d.style.lineBreak);
            return true;
        }
        const Node* program = source.tree.program;
        const Node* first = nullptr;
        for (const Node* statement : program->items) {
            const Node* inner = statement->target != nullptr
                                      && (statement->kind == Kind::Export || statement->kind == Kind::ExportDefault)
                                  ? statement->target
                                  : statement;
            if (inner != nullptr && inner->kind == Kind::Import) {
                continue;
            }
            first = statement;
            break;
        }
        if (first != nullptr) {
            const std::size_t at = lineStartOf(text, first->start);
            edit.change(source, at, at, declaration + d.style.lineBreak);
            return true;
        }
        const std::string prefix = text.empty() || breakAt(text.back()) ? std::string() : d.style.lineBreak;
        edit.change(source, text.size(), text.size(), prefix + declaration + d.style.lineBreak);
        return true;
    }

    Outcome createVariable(std::string_view page, const nlohmann::json& specJson) const
    {
        Edit edit(d);
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        VariableSpec spec = specOf(specJson);
        if (!identifierText(spec.name)) {
            return edit.refuse(say("reason:variableName"));
        }
        if (spec.scope == "class") {
            const ClassInfo* own = d.defaultClass();
            if (own == nullptr || own->node == nullptr) {
                spec.scope = "file";
            }
        }
        if (spec.scope != "class") {
            spec.editable = false;
        } else if (spec.editable && !variableEditableKind(spec.kind)) {
            return edit.refuse(say("reason:variableProp"));
        }
        if (!spec.value.empty() && !variableExpression(spec.value)) {
            return edit.refuse(say("reason:variableValue"));
        }
        if (spec.scope == "local") {
            const Page* target = d.page(page);
            const Node* function = target != nullptr ? target->function : nullptr;
            if (function == nullptr || function->body == nullptr || function->body->kind != Kind::Block) {
                return edit.refuse(say("reason:localBody"));
            }
            spec.constant = false;
        }
        if (nameTaken(page, spec)) {
            return edit.refuse(say("reason:variableExists"));
        }
        if (!insertVariable(edit, spec, page)) {
            return edit.refuse(say("reason:localBody"));
        }
        return edit.done();
    }

    Outcome saveVariable(std::string_view page, std::string_view key, const nlohmann::json& specJson) const
    {
        Edit edit(d);
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        std::string scope;
        std::string name;
        if (!variableKey(key, scope, name)) {
            return edit.refuse(say("reason:notFound"));
        }
        const VariableInfo* info = d.declaredVariable(scope == "local" ? page : std::string_view(), name);
        if (info == nullptr || info->declaration == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        VariableSpec spec = specOf(specJson);
        spec.scope = info->local ? "local" : info->inClass ? "class" : "file";
        if (!identifierText(spec.name)) {
            return edit.refuse(say("reason:variableName"));
        }
        if (spec.scope != "class") {
            spec.editable = false;
        } else if (spec.editable && !variableEditableKind(spec.kind)) {
            return edit.refuse(say("reason:variableProp"));
        }
        if (!spec.value.empty() && !variableExpression(spec.value)) {
            return edit.refuse(say("reason:variableValue"));
        }
        const bool renamed = spec.name != name;
        if (renamed && nameTaken(page, spec)) {
            return edit.refuse(say("reason:variableExists"));
        }
        const Source& source = d.file();
        if (renamed && info->id != nullptr) {
            renameBinding(edit, source, info->id, info->declaration, spec.name);
        }
        const std::size_t start = withDocStart(source, info->declaration);
        std::string declaration = variableDeclaration(spec, d.style.quote, d.style.semicolons);
        if (blankBefore(source.text, start)) {
            const std::string indent = indentOf(source.text, start);
            edit.change(source, start, info->declaration->end, reindent(declaration, indent, d.style.lineBreak));
        } else {
            edit.change(source, start, info->declaration->end, std::move(declaration));
        }
        if (info->inClass) {
            const ClassInfo* own = d.defaultClass();
            if (own != nullptr) {
                if (spec.editable && propsEntry(*own, name) != nullptr) {
                    updatePropsEntry(edit, *own, name, spec);
                } else if (spec.editable) {
                    addPropsEntry(edit, *own, spec);
                } else {
                    removePropsEntry(edit, *own, name);
                }
            }
        }
        return edit.done();
    }

    Outcome deleteVariable(std::string_view page, std::string_view key) const
    {
        Edit edit(d);
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        std::string scope;
        std::string name;
        if (!variableKey(key, scope, name)) {
            return edit.refuse(say("reason:notFound"));
        }
        const VariableInfo* info = d.declaredVariable(scope == "local" ? page : std::string_view(), name);
        if (info == nullptr || info->declaration == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        const Source& source = d.file();
        if (!bindingUses(source, info->id, info->declaration).empty()) {
            return edit.refuse(d.say.format("reason:variableUses", name));
        }
        std::size_t start = withDocStart(source, info->declaration);
        std::size_t end = info->declaration->end;
        if (blankBefore(source.text, start)) {
            start = lineStartOf(source.text, start);
            end = afterLine(source.text, end);
        }
        edit.change(source, start, end, {});
        if (info->inClass) {
            if (const ClassInfo* own = d.defaultClass(); own != nullptr) {
                removePropsEntry(edit, *own, name);
            }
        }
        return edit.done();
    }

    Outcome promoteToVariable(std::string_view page, std::string_view nodeKey, std::string_view pinName) const
    {
        Edit edit(d);
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        const Item* item = d.item(nodeKey);
        if (item == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        const Pin* pin = findPin(item->inputs, pinName);
        if (pin == nullptr || pin->type == PinType::Exec) {
            return edit.refuse(say("reason:noPin"));
        }
        if (pin->connected) {
            return edit.refuse(say("reason:connected"));
        }
        VariableSpec spec;
        switch (pin->type) {
        case PinType::Bool:
            spec.kind = "toggle";
            break;
        case PinType::Number:
            spec.kind = "integer";
            break;
        case PinType::Duration:
            spec.kind = "duration";
            break;
        case PinType::Text:
            spec.kind = "text";
            break;
        case PinType::Point:
            spec.kind = "point";
            break;
        case PinType::Region:
            spec.kind = "region";
            break;
        case PinType::Color:
            spec.kind = "color";
            break;
        case PinType::Image:
            spec.kind = "image";
            break;
        case PinType::Key:
            spec.kind = "key";
            break;
        case PinType::Component:
            spec.kind = "component";
            break;
        default:
            return edit.refuse(say("reason:variableType"));
        }
        const ClassInfo* own = d.defaultClass();
        if (own != nullptr && own->node != nullptr) {
            spec.scope = "class";
        } else if (std::string_view(item->page).starts_with("fn:")) {
            spec.scope = "local";
        } else {
            spec.scope = "file";
        }
        const std::string base = pin->local.empty() ? std::string(pin->label) : pin->local.str();
        spec.name = freeName(d.file(), identifierFrom(base, "value"), 0);
        if (nameTaken(page, spec)) {
            return edit.refuse(say("reason:variableExists"));
        }
        if (!pin->raw.is_null()) {
            spec.value = edit.literal(*item, *pin, pin->raw);
        }
        if (!insertVariable(edit, spec, item->page)) {
            return edit.refuse(say("reason:localBody"));
        }
        edit.writeInput(*item, *pin, spec.name);
        return edit.done();
    }

    [[nodiscard]] const Item* machineOf(const Item& state) const
    {
        const Page* page = d.page(state.page);
        return page != nullptr ? page->machine : nullptr;
    }

    [[nodiscard]] std::vector<const Item*> statesOf(const Item& machine) const
    {
        std::vector<const Item*> list;
        if (const Page* page = d.page(machine.target.view())) {
            for (const Item* item : page->items) {
                if (item->role == Role::State) {
                    list.push_back(item);
                }
            }
        }
        return list;
    }

    [[nodiscard]] static std::size_t commaAfter(const std::string& text, std::size_t at)
    {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
            ++at;
        }
        return at < text.size() && text[at] == ',' ? at : kNone;
    }

    [[nodiscard]] Span memberSpan(const Source& source, const Node* member) const
    {
        Edit probe(d);
        const Span inner = probe.span(source, member, member);
        const std::string& text = source.text;
        std::size_t end = std::max<std::size_t>(inner.end, member->end);
        if (const std::size_t comma = commaAfter(text, member->end); comma != kNone) {
            end = std::max(end, comma + 1);
        }
        const std::size_t start = std::min<std::size_t>(inner.start, member->start);
        if (blankBefore(text, start) && blankAfter(text, end)) {
            return {lineStartOf(text, start), afterLine(text, end), true};
        }
        return {start, end, false};
    }

    void removeState(Edit& edit, const Item& state) const
    {
        if (state.statement == nullptr || state.source == nullptr) {
            return;
        }
        const Source& source = *state.source;
        const Span range = memberSpan(source, state.statement);
        edit.change(source, range.start, range.end, {});
        const Page* page = d.page(state.page);
        if (page == nullptr) {
            return;
        }
        for (const Wire& wire : page->wires) {
            if (wire.toItem != &state || wire.call == nullptr || wire.fromItem == &state) {
                continue;
            }
            for (const Item& other : d.items) {
                if (other.role == Role::Goto && other.expression == wire.call && other.chain != nullptr) {
                    edit.plan(other.chain).removed.insert(other.index);
                }
            }
        }
    }

    Outcome addState(std::string_view pageKey, double x, double y) const
    {
        Edit edit(d);
        const Page* page = d.page(pageKey);
        const Item* machine = page != nullptr ? page->machine : nullptr;
        if (machine == nullptr || machine->expression == nullptr || machine->source == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        if (!d.ok || d.environment.readOnly || !machine->problem.empty()) {
            return edit.refuse(say("reason:readOnly"));
        }
        const Source& source = *machine->source;
        const std::string& text = source.text;
        const Node* object = unparen(machine->expression->items[0]);
        const std::vector<const Item*> states = statesOf(*machine);
        const std::string base = identifierFrom(say("name:state"), "state");
        std::string name = base;
        for (int n = 2; std::ranges::any_of(states, [&name](const Item* state) { return state->stateName == name; }); ++n) {
            name = base + std::to_string(n);
        }
        const std::string& br = edit.lineBreak();
        if (!object->items.empty()) {
            const Node* last = object->items.back();
            const std::string indent = indentOf(text, last->start);
            const std::size_t comma = commaAfter(text, last->end);
            const std::size_t at = comma != kNone ? comma + 1 : last->end;
            edit.change(source, at, at,
                        (comma != kNone ? std::string() : std::string(",")) + br + indent + "async " + name + "(to) {" + br
                            + indent + "}" + (comma != kNone ? "," : ""));
        } else {
            const std::string outer = indentOf(text, object->start);
            const std::string indent = outer + edit.unit();
            edit.change(source, object->start, object->end,
                        "{" + br + indent + "async " + name + "(to) {" + br + indent + "}," + br + outer + "}");
        }
        const std::string stateKey = join(machine->key, "/state:", name);
        edit.layout.pages[edit.anchorOf(pageKey)].nodes[stateKey] = {x, y};
        Outcome outcome = edit.done();
        outcome.key = stateKey;
        return outcome;
    }

    Outcome connectStates(std::string_view fromKey, std::string_view toKey) const
    {
        Edit edit(d);
        const Item* from = d.item(fromKey);
        const Item* to = d.item(toKey);
        if (from == nullptr || to == nullptr || from->role != Role::State || to->role != Role::State
            || from->function == nullptr || from->source == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        const Node* body = from->function->body;
        if (body == nullptr || body->kind != Kind::Block) {
            return edit.refuse(say("reason:noPin"));
        }
        if (!body->items.empty()
            && (body->items.back()->kind == Kind::Return || body->items.back()->kind == Kind::Throw)) {
            return edit.refuse(d.say.format("reason:leaves", from->stateName.view()));
        }
        const Source& source = *from->source;
        const std::string& text = source.text;
        std::string param;
        if (!from->function->items.empty() && from->function->items[0]->kind == Kind::Identifier) {
            param = std::string(from->function->items[0]->text);
        } else {
            param = "to";
            const std::size_t open = text.find('(', from->function->start);
            if (open == std::string::npos || open >= body->start) {
                return edit.refuse(say("reason:noPin"));
            }
            edit.change(source, open + 1, open + 1, from->function->items.empty() ? "to" : "to, ");
        }
        const std::string statement = "return " + access(param, to->stateName.view(), edit.quote()) + "()" + edit.semi();
        const std::string& br = edit.lineBreak();
        const std::size_t close = body->end - 1;
        const std::string indent = body->items.empty() ? indentOf(text, body->start) + edit.unit()
                                                       : indentOf(text, body->items.front()->start);
        if (blankBefore(text, close)) {
            const std::size_t at = lineStartOf(text, close);
            edit.change(source, at, at, indent + statement + br);
        } else {
            edit.change(source, close, close, br + indent + statement + br + indentOf(text, body->start));
        }
        return edit.done();
    }

    Outcome setTransitionLabel(std::string_view wireKey, std::string_view label) const
    {
        Edit edit(d);
        for (const Page& page : d.pages) {
            for (const Wire& wire : page.wires) {
                if (wire.key != wireKey || wire.call == nullptr || wire.fromItem == nullptr
                    || wire.fromItem->source == nullptr) {
                    continue;
                }
                if (!d.ok || d.environment.readOnly) {
                    return edit.refuse(say("reason:readOnly"));
                }
                const Node* call = wire.call;
                edit.change(*wire.fromItem->source, call->target->end, call->end,
                            label.empty() ? std::string("()") : "(" + stringLiteral(label, edit.quote()) + ")");
                return edit.done();
            }
        }
        return edit.refuse(say("reason:notFound"));
    }

    Outcome renameState(std::string_view nodeKey, std::string_view wanted) const
    {
        Edit edit(d);
        const Item* state = d.item(nodeKey);
        if (state == nullptr || state->role != Role::State || state->statement == nullptr || state->source == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        std::string name(wanted);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
            name.pop_back();
        }
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
            name.erase(name.begin());
        }
        if (name.empty() || name == state->stateName.view()) {
            return edit.done();
        }
        const Item* machine = machineOf(*state);
        if (machine == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        const std::vector<const Item*> states = statesOf(*machine);
        const std::string base = name;
        for (int n = 2; std::ranges::any_of(states, [&name](const Item* other) { return other->stateName == name; }); ++n) {
            name = base + std::to_string(n);
        }
        const Source& source = *state->source;
        const char quote = edit.quote();
        const Node* key = state->statement->target;
        edit.change(source, key->start, key->end, identifierText(name) ? name : stringLiteral(name, quote));
        if (const Page* page = d.page(state->page)) {
            for (const Wire& wire : page->wires) {
                if (wire.toItem != state || wire.call == nullptr) {
                    continue;
                }
                const Node* callee = unparen(wire.call->target);
                if (callee == nullptr || callee->kind != Kind::Member) {
                    continue;
                }
                if (callee->has(flag::kComputed)) {
                    edit.change(source, callee->value->start, callee->value->end, stringLiteral(name, quote));
                } else if (identifierText(name)) {
                    edit.change(source, callee->value->start, callee->value->end, name);
                } else {
                    edit.change(source, callee->target->end, callee->end, "[" + stringLiteral(name, quote) + "]");
                }
            }
        }
        const Node* options = machine->expression->items.size() > 1 ? unparen(machine->expression->items[1]) : nullptr;
        if (const Node* start = propertyValue(options, "start")) {
            if (const std::optional<std::string> current = stringValue(start); current && *current == state->stateName.view()) {
                edit.change(source, start->start, start->end, stringLiteral(name, quote));
            }
        }
        const std::string oldKey = state->key;
        const std::string newKey = join(machine->key, "/state:", name);
        PageLayout& machineLayout = edit.layout.pages[edit.anchorOf(state->page)];
        if (const auto at = machineLayout.nodes.find(oldKey); at != machineLayout.nodes.end()) {
            const std::array<double, 2> position = at->second;
            machineLayout.nodes.erase(at);
            machineLayout.nodes[newKey] = position;
        }
        if (const auto inner = edit.layout.pages.find("st:" + oldKey); inner != edit.layout.pages.end()) {
            PageLayout moved = std::move(inner->second);
            edit.layout.pages.erase(inner);
            edit.layout.pages["st:" + newKey] = std::move(moved);
        }
        for (Breakpoint& breakpoint : edit.layout.breakpoints) {
            if (breakpoint.anchor == oldKey) {
                breakpoint.anchor = newKey;
            }
        }
        Outcome outcome = edit.done();
        outcome.key = newKey;
        return outcome;
    }

    Outcome setStart(std::string_view nodeKey) const
    {
        Edit edit(d);
        const Item* state = d.item(nodeKey);
        const Item* machine = state != nullptr ? machineOf(*state) : nullptr;
        if (state == nullptr || machine == nullptr || state->role != Role::State || state->statement == nullptr
            || machine->expression == nullptr || state->source == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        const Source& source = *state->source;
        const std::string& text = source.text;
        const char quote = edit.quote();
        const Node* options = machine->expression->items.size() > 1 ? unparen(machine->expression->items[1]) : nullptr;
        if (const Node* start = propertyValue(options, "start"); start != nullptr && stringValue(start)) {
            edit.change(source, start->start, start->end, stringLiteral(state->stateName.view(), quote));
            return edit.done();
        }
        const Node* object = unparen(machine->expression->items[0]);
        if (object->items.empty() || object->items.front() == state->statement) {
            return edit.done();
        }
        const Span range = memberSpan(source, state->statement);
        std::string moved = text.substr(range.start, range.end - range.start);
        if (commaAfter(text, state->statement->end) == kNone) {
            const std::size_t end = state->statement->end - range.start;
            moved.insert(end, ",");
        }
        const Node* first = object->items.front();
        const Span firstRange = memberSpan(source, first);
        const std::size_t at = firstRange.lines ? firstRange.start : first->start;
        if (!range.lines) {
            moved += " ";
        }
        edit.change(source, at, at, moved);
        edit.change(source, range.start, range.end, {});
        return edit.done();
    }

    Outcome machineDetail(const Item& item, std::string_view name, const nlohmann::json& value) const
    {
        Edit edit(d);
        const std::string text = value.is_string() ? value.get<std::string>() : std::string();
        if (name == "start") {
            for (const Item* state : statesOf(item)) {
                if (state->stateName == text) {
                    return setStart(state->key);
                }
            }
            return edit.refuse(say("reason:notFound"));
        }
        if (name != "name" || item.expression == nullptr || item.source == nullptr) {
            return edit.refuse(say("reason:noPin"));
        }
        const Source& source = *item.source;
        const char quote = edit.quote();
        const Node* call = item.expression;
        const Node* options = call->items.size() > 1 ? unparen(call->items[1]) : nullptr;
        if (const Node* current = propertyValue(options, "name")) {
            edit.change(source, current->start, current->end, stringLiteral(text, quote));
        } else if (options != nullptr && options->kind == Kind::Object) {
            if (options->items.empty()) {
                edit.change(source, options->start, options->end, "{ name: " + stringLiteral(text, quote) + " }");
            } else {
                edit.change(source, options->items.back()->end, options->items.back()->end,
                            ", name: " + stringLiteral(text, quote));
            }
        } else if (options == nullptr) {
            edit.change(source, call->items.front()->end, call->items.front()->end,
                        ", { name: " + stringLiteral(text, quote) + " }");
        }
        return edit.done();
    }

    [[nodiscard]] bool typeTaken(std::string_view name, std::string_view except) const
    {
        if (name == except) {
            return false;
        }
        if (structNamed(d.types, name) != nullptr || enumNamed(d.types, name) != nullptr) {
            return true;
        }
        if (std::ranges::find(d.classes, name, &ClassInfo::name) != d.classes.end()) {
            return true;
        }
        return d.functions.contains(name);
    }

    [[nodiscard]] std::string freeFieldName(const StructInfo& info) const
    {
        const std::string base = identifierFrom(say("name:field"), "field");
        std::string name = base;
        for (int n = 2; std::ranges::any_of(info.fields, [&name](const StructFieldInfo& field) {
                 return field.name == name;
             }); ++n) {
            name = base + std::to_string(n);
        }
        return name;
    }

    [[nodiscard]] std::string freeOptionName(const EnumInfo& info) const
    {
        const std::string base = identifierFrom(say("name:option"), "Option");
        std::string name = base;
        for (int n = 2; std::ranges::find(info.options, name) != info.options.end(); ++n) {
            name = base + std::to_string(n);
        }
        return name;
    }

    Outcome addFunction(std::string_view given) const
    {
        Edit edit(d);
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        const Source& source = d.file();
        const std::string name =
            given.empty() ? freeName(source, identifierFrom(say("name:function"), "newFunction"), 0) : std::string(given);
        if (!identifierText(name)) {
            return edit.refuse(d.say.format("reason:typeName", name));
        }
        if (typeTaken(name, {})) {
            return edit.refuse(d.say.format("reason:typeTaken", name));
        }
        const std::string& text = source.text;
        const std::string& br = edit.lineBreak();
        std::string prefix;
        if (!text.empty() && !breakAt(text.back())) {
            prefix = br;
        }
        if (!text.empty()) {
            prefix += br;
        }
        const std::string exported = d.environment.fileName == kLibraryFile ? "export " : "";
        edit.change(source, text.size(), text.size(), prefix + exported + "function " + name + "() {" + br + "}" + br);
        Outcome outcome = edit.done();
        outcome.page = "fn:" + name;
        return outcome;
    }

    Outcome addType(std::string_view kind, std::string_view given) const
    {
        if (kind == "function") {
            return addFunction(given);
        }
        Edit edit(d);
        if (!d.ok || d.environment.readOnly) {
            return edit.refuse(say("reason:readOnly"));
        }
        std::string name(given);
        if (name.empty()) {
            name = identifierFrom(say(std::string("name:") + std::string(kind)), std::string(kind));
        }
        if (!identifierText(name)) {
            return edit.refuse(d.say.format("reason:typeName", name));
        }
        if (typeTaken(name, {})) {
            return edit.refuse(d.say.format("reason:typeTaken", name));
        }
        const Source& source = d.file();
        const std::string& text = source.text;
        const std::string& br = edit.lineBreak();
        const std::string indent = edit.unit();
        std::string block;
        std::string key;
        if (kind == "struct") {
            std::vector<StructFieldInfo> fields;
            fields.push_back({identifierFrom(say("name:field"), "field"), "number", false});
            block = structDeclaration(name, fields, indent, br);
            key = "def:" + name;
        } else if (kind == "enum") {
            std::vector<std::string> options;
            options.push_back(identifierFrom(say("name:option"), "Option"));
            block = enumDeclaration(name, options, indent, br, edit.quote());
            key = "def:" + name;
        } else if (kind == "class") {
            block = "class " + name + " {" + br + "}" + br;
            key = "def:" + name;
        } else {
            return edit.refuse(say("reason:notFound"));
        }
        std::string prefix;
        if (!text.empty() && !breakAt(text.back())) {
            prefix = br;
        }
        if (!text.empty()) {
            prefix += br;
        }
        edit.change(source, text.size(), text.size(), prefix + block);
        Outcome outcome = edit.done();
        outcome.key = key;
        return outcome;
    }

    Outcome editStructType(const StructInfo& info, std::string_view field, const nlohmann::json& value) const
    {
        Edit edit(d);
        const Source& source = d.file();
        const std::string& br = edit.lineBreak();
        const std::string indent = edit.unit();
        const std::string given = value.is_string() ? value.get<std::string>() : std::string();
        std::string name = info.name;
        std::vector<StructFieldInfo> fields = info.fields;
        if (field == "delete") {
            std::size_t end = info.end;
            if (end < source.text.size() && breakAt(source.text[end])) {
                end = afterLine(source.text, end);
            }
            edit.change(source, info.start, end, {});
            return edit.done();
        }
        if (field == "name") {
            if (!identifierText(given)) {
                return edit.refuse(d.say.format("reason:typeName", given));
            }
            if (typeTaken(given, info.name)) {
                return edit.refuse(d.say.format("reason:typeTaken", given));
            }
            name = given;
        } else if (field == "addField") {
            fields.push_back({freeFieldName(info), "number", false});
        } else if (field.starts_with("removeField.")) {
            const std::string key(field.substr(12));
            if (fields.size() <= 1) {
                return edit.refuse(say("reason:lastField"));
            }
            if (std::ranges::find(fields, key, &StructFieldInfo::name) == fields.end()) {
                return edit.refuse(d.say.format("reason:noField", key));
            }
            std::erase_if(fields, [&key](const StructFieldInfo& entry) { return entry.name == key; });
        } else if (field.starts_with("field.")) {
            const std::string rest(field.substr(6));
            const std::size_t dot = rest.rfind('.');
            if (dot == std::string::npos) {
                return edit.refuse(say("reason:noField"));
            }
            const std::string key(rest.substr(0, dot));
            const std::string prop(rest.substr(dot + 1));
            auto found = std::ranges::find(fields, key, &StructFieldInfo::name);
            if (found == fields.end()) {
                return edit.refuse(d.say.format("reason:noField", key));
            }
            if (prop == "name") {
                if (!identifierText(given)) {
                    return edit.refuse(d.say.format("reason:typeName", given));
                }
                if (std::ranges::find(fields, given, &StructFieldInfo::name) != fields.end()
                    && given != found->name) {
                    return edit.refuse(d.say.format("reason:typeTaken", given));
                }
                found->name = given;
            } else if (prop == "type") {
                found->type = given.empty() ? std::string("unknown") : given;
            } else if (prop == "optional") {
                found->optional = value.is_boolean() && value.get<bool>();
            } else {
                return edit.refuse(say("reason:notFound"));
            }
        } else {
            return edit.refuse(say("reason:notFound"));
        }
        std::string code = structDeclaration(name, fields, indent, br);
        const std::string_view replaced(source.text.data() + info.start, info.end - info.start);
        if (code.ends_with(br) && !replaced.ends_with('\n')) {
            code.erase(code.size() - br.size());
        }
        edit.change(source, info.start, info.end, std::move(code));
        Outcome outcome = edit.done();
        outcome.key = "def:" + name;
        return outcome;
    }

    Outcome editEnumType(const EnumInfo& info, std::string_view field, const nlohmann::json& value) const
    {
        Edit edit(d);
        const Source& source = d.file();
        const std::string& br = edit.lineBreak();
        const std::string indent = edit.unit();
        const std::string given = value.is_string() ? value.get<std::string>() : std::string();
        std::string name = info.name;
        std::vector<std::string> options = info.options;
        if (field == "delete") {
            std::size_t end = info.end;
            if (end < source.text.size() && breakAt(source.text[end])) {
                end = afterLine(source.text, end);
            }
            edit.change(source, info.start, end, {});
            return edit.done();
        }
        if (field == "name") {
            if (!identifierText(given)) {
                return edit.refuse(d.say.format("reason:typeName", given));
            }
            if (typeTaken(given, info.name)) {
                return edit.refuse(d.say.format("reason:typeTaken", given));
            }
            name = given;
        } else if (field == "addOption") {
            options.push_back(freeOptionName(info));
        } else if (field.starts_with("removeOption.")) {
            const std::string key(field.substr(13));
            const auto found = std::ranges::find(options, key);
            if (found == options.end()) {
                return edit.refuse(d.say.format("reason:noOption", key));
            }
            if (options.size() <= 1) {
                return edit.refuse(say("reason:lastOption"));
            }
            options.erase(found);
        } else if (field.starts_with("option.")) {
            const std::string key(field.substr(7));
            const auto found = std::ranges::find(options, key);
            if (found == options.end()) {
                return edit.refuse(d.say.format("reason:noOption", key));
            }
            if (!identifierText(given)) {
                return edit.refuse(d.say.format("reason:typeName", given));
            }
            if (given != *found && std::ranges::find(options, given) != options.end()) {
                return edit.refuse(d.say.format("reason:typeTaken", given));
            }
            *found = given;
        } else {
            return edit.refuse(say("reason:notFound"));
        }
        std::string code = enumDeclaration(name, options, indent, br, edit.quote());
        const std::string_view replaced(source.text.data() + info.start, info.end - info.start);
        if (code.ends_with(br) && !replaced.ends_with('\n')) {
            code.erase(code.size() - br.size());
        }
        edit.change(source, info.start, info.end, std::move(code));
        Outcome outcome = edit.done();
        outcome.key = "def:" + name;
        return outcome;
    }

    Outcome editClassType(const ClassInfo& info, std::string_view field, const nlohmann::json& value) const
    {
        Edit edit(d);
        const Source& source = d.file();
        const std::string& text = source.text;
        const std::string& br = edit.lineBreak();
        const std::string indent = edit.unit();
        const std::string given = value.is_string() ? value.get<std::string>() : std::string();
        const Node* node = info.node;
        if (node == nullptr || node->body == nullptr || node->target == nullptr) {
            return edit.refuse(say("reason:notFound"));
        }
        const auto spec = [&value](std::string_view key) {
            if (!value.is_object()) {
                return std::string();
            }
            const auto found = value.find(std::string(key));
            return found != value.end() && found->is_string() ? found->get<std::string>() : std::string();
        };
        const auto flagOf = [&value](std::string_view key) {
            if (!value.is_object()) {
                return false;
            }
            const auto found = value.find(std::string(key));
            return found != value.end() && found->is_boolean() && found->get<bool>();
        };
        const auto memberNamed = [node](std::string_view name) -> const Node* {
            for (const Node* member : node->items) {
                if (member->kind != Kind::ClassMember || member->target == nullptr) {
                    continue;
                }
                const std::string key = member->target->kind == Kind::String ? unquote(member->target->text)
                                                                             : std::string(member->target->text);
                if (key == name) {
                    return member;
                }
            }
            return nullptr;
        };
        const auto insertMember = [&](const std::string& code) {
            const std::size_t close = node->body->end - 1;
            if (blankBefore(text, close)) {
                const std::size_t at = lineStartOf(text, close);
                edit.change(source, at, at, indent + code + br);
            } else {
                edit.change(source, close, close, br + indent + code + br + indentOf(text, node->target->start));
            }
        };
        if (field == "delete") {
            std::size_t end = node->end;
            if (end < text.size() && breakAt(text[end])) {
                end = afterLine(text, end);
            }
            edit.change(source, node->start, end, {});
            return edit.done();
        }
        if (field == "name") {
            if (!identifierText(given) || typeTaken(given, info.name)) {
                return edit.refuse(d.say.format("reason:typeName", given));
            }
            edit.change(source, node->target->start, node->target->end, given);
            Outcome outcome = edit.done();
            outcome.key = "def:" + given;
            return outcome;
        }
        if (field == "base") {
            if (node->other != nullptr) {
                if (given.empty()) {
                    edit.change(source, node->target->end, node->other->end, {});
                } else {
                    edit.change(source, node->other->start, node->other->end, given);
                }
            } else if (!given.empty()) {
                edit.change(source, node->target->end, node->target->end, " extends " + given);
            }
            return edit.done();
        }
        if (field == "addMember" || field == "addFunction" || field == "addEvent") {
            std::string name = spec("name");
            if (name.empty()) {
                const bool function = field == "addFunction";
                const bool event = field == "addEvent";
                name = identifierFrom(say(function ? "name:function" : event ? "name:event" : "name:field"),
                                      function ? "newFunction" : event ? "fired" : "field");
            }
            if (!identifierText(name) || memberNamed(name) != nullptr) {
                return edit.refuse(d.say.format("reason:typeName", name));
            }
            const bool priv = flagOf("private");
            std::string code;
            if (field == "addMember") {
                const std::string type = spec("type");
                code = (priv ? "private " : "") + name + " = "
                     + defaultValueOfType(type.empty() ? std::string("number") : type);
            } else if (field == "addFunction") {
                code = (priv ? "private " : "") + name + "() {" + br + indent + "}";
            } else {
                code = name + " = this.event()";
            }
            insertMember(code);
            return edit.done();
        }
        if (field == "addCtorParam") {
            const Node* ctor = nullptr;
            for (const Node* member : node->items) {
                if (member->kind == Kind::ClassMember && member->has(flag::kConstructor)) {
                    ctor = member;
                }
            }
            std::string name = spec("name");
            if (name.empty()) {
                name = identifierFrom(say("name:param"), "argument");
            }
            if (!identifierText(name)) {
                return edit.refuse(d.say.format("reason:typeName", name));
            }
            const std::string type = spec("type");
            const std::string written = type.empty() ? name : name + ": " + type;
            if (ctor == nullptr) {
                insertMember("constructor(" + written + ") {" + br + indent + "}");
                return edit.done();
            }
            const Node* function = ctor->value;
            if (function == nullptr) {
                return edit.refuse(say("reason:notFound"));
            }
            if (function->items.empty()) {
                const std::size_t paren = text.find('(', ctor->target->end);
                if (paren == std::string::npos) {
                    return edit.refuse(say("reason:notFound"));
                }
                edit.change(source, paren + 1, paren + 1, written);
                return edit.done();
            }
            edit.change(source, function->items.back()->end, function->items.back()->end, ", " + written);
            return edit.done();
        }
        if (field.starts_with("ctor.remove.")) {
            const std::string key(field.substr(11));
            for (const Node* member : node->items) {
                if (member->kind != Kind::ClassMember || !member->has(flag::kConstructor) || member->value == nullptr) {
                    continue;
                }
                for (const Node* param : member->value->items) {
                    const Node* id = param->kind == Kind::AssignPattern ? param->target : param;
                    if (id != nullptr && id->kind == Kind::Identifier && id->text == key) {
                        const Span range = memberSpan(source, param);
                        edit.change(source, range.start, range.end, {});
                        return edit.done();
                    }
                }
            }
            return edit.refuse(d.say.format("reason:noMember", key));
        }
        if (field.starts_with("member.") || field.starts_with("function.") || field.starts_with("event.")) {
            const std::size_t dot = field.find('.');
            const std::string rest(field.substr(dot + 1));
            const std::size_t last = rest.rfind('.');
            const std::string name(last == std::string::npos ? rest : rest.substr(0, last));
            const std::string prop(last == std::string::npos ? std::string("remove") : rest.substr(last + 1));
            const Node* member = memberNamed(name);
            if (member == nullptr) {
                return edit.refuse(d.say.format("reason:noMember", name));
            }
            if (prop == "remove") {
                const Span range = memberSpan(source, member);
                edit.change(source, range.start, range.end, {});
                return edit.done();
            }
            if (prop == "name") {
                if (!identifierText(given) || memberNamed(given) != nullptr) {
                    return edit.refuse(d.say.format("reason:typeName", given));
                }
                edit.change(source, member->target->start, member->target->end, given);
                return edit.done();
            }
            if (prop == "private") {
                const std::string before = text.substr(member->start, member->target->start - member->start);
                const std::size_t at = before.find("private");
                const bool priv = value.is_boolean() && value.get<bool>();
                if (priv && at == std::string::npos) {
                    edit.change(source, member->start, member->start, "private ");
                } else if (!priv && at != std::string::npos) {
                    edit.change(source, member->start + at, member->start + at + 7, {});
                }
                return edit.done();
            }
        }
        return edit.refuse(say("reason:notFound"));
    }

    Outcome editType(std::string_view name, std::string_view field, const nlohmann::json& value) const
    {
        if (!d.ok || d.environment.readOnly) {
            Edit edit(d);
            return edit.refuse(say("reason:readOnly"));
        }
        if (const StructInfo* info = structNamed(d.types, name)) {
            return editStructType(*info, field, value);
        }
        if (const EnumInfo* info = enumNamed(d.types, name)) {
            return editEnumType(*info, field, value);
        }
        const auto found = std::ranges::find(d.classes, name, &ClassInfo::name);
        if (found != d.classes.end() && found->plain) {
            return editClassType(*found, field, value);
        }
        Edit edit(d);
        return edit.refuse(d.say.format("reason:noType", name));
    }
};

}

std::string Document::check(std::string_view from, std::string_view fromPin, std::string_view to,
                            std::string_view toPin) const
{
    return Editor(*m_impl).check(from, fromPin, to, toPin);
}

Outcome Document::connect(std::string_view from, std::string_view fromPin, std::string_view to,
                          std::string_view toPin) const
{
    return Editor(*m_impl).connect(from, fromPin, to, toPin);
}

Outcome Document::rewire(std::string_view node, std::string_view pin, std::string_view toNode,
                         std::string_view toPin) const
{
    return Editor(*m_impl).rewire(node, pin, toNode, toPin);
}

Outcome Document::disconnect(std::string_view node, std::string_view pin) const
{
    return Editor(*m_impl).disconnect(node, pin);
}

Outcome Document::removeWire(std::string_view wire) const
{
    return Editor(*m_impl).removeWire(wire);
}

Outcome Document::setValue(std::string_view node, std::string_view pin, const nlohmann::json& value) const
{
    return Editor(*m_impl).setValue(node, pin, value);
}

Outcome Document::add(std::string_view page, std::string_view id, double x, double y, std::string_view fromNode,
                      std::string_view fromPin) const
{
    return Editor(*m_impl).add(page, id, x, y, fromNode, fromPin);
}

Outcome Document::drop(std::string_view page, const nlohmann::json& item, double x, double y, std::string_view node,
                       std::string_view pin) const
{
    return Editor(*m_impl).drop(page, item, x, y, node, pin);
}

Outcome Document::insertOnWire(std::string_view wire, std::string_view node) const
{
    return Editor(*m_impl).insertOnWire(wire, node);
}

Outcome Document::remove(std::string_view page, const std::vector<std::string>& keys) const
{
    std::vector<std::string> rest;
    std::string pressing;
    for (const std::string& key : keys) {
        const Item* item = m_impl->item(key);
        if (item != nullptr && item->role == Role::Default && item->consumer != nullptr) {
            pressing = item->consumer->key;
            continue;
        }
        rest.push_back(key);
    }
    if (rest.empty() && !pressing.empty()) {
        return Editor(*m_impl).editDetail(page, pressing, "consume", "logic");
    }
    return Editor(*m_impl).remove(page, rest);
}

std::string Document::copy(const std::vector<std::string>& keys) const
{
    std::vector<std::string> kept;
    for (const std::string& key : keys) {
        const Item* item = m_impl->item(key);
        if (item == nullptr || item->role != Role::Default) {
            kept.push_back(key);
        }
    }
    return Editor(*m_impl).copy(kept);
}

Outcome Document::paste(std::string_view page, std::string_view text, double x, double y) const
{
    return Editor(*m_impl).paste(page, text, x, y);
}

Outcome Document::duplicate(std::string_view page, const std::vector<std::string>& keys) const
{
    std::vector<std::string> kept;
    for (const std::string& key : keys) {
        const Item* item = m_impl->item(key);
        if (item == nullptr || item->role != Role::Default) {
            kept.push_back(key);
        }
    }
    return Editor(*m_impl).duplicate(page, kept);
}

Outcome Document::setCode(std::string_view node, std::string_view text) const
{
    return Editor(*m_impl).setCode(node, text);
}

Outcome Document::replaceControl(std::string_view node, std::string_view from, std::string_view to) const
{
    return Editor(*m_impl).replaceControl(node, from, to);
}

Outcome Document::collapse(const std::vector<std::string>& keys) const
{
    return Editor(*m_impl).collapse(keys);
}

Outcome Document::editDetail(std::string_view page, std::string_view key, std::string_view name,
                             const nlohmann::json& value) const
{
    return Editor(*m_impl).editDetail(page, key, name, value);
}

Outcome Document::createVariable(std::string_view page, const nlohmann::json& spec) const
{
    return Editor(*m_impl).createVariable(page, spec);
}

Outcome Document::saveVariable(std::string_view page, std::string_view key, const nlohmann::json& spec) const
{
    return Editor(*m_impl).saveVariable(page, key, spec);
}

Outcome Document::deleteVariable(std::string_view page, std::string_view key) const
{
    return Editor(*m_impl).deleteVariable(page, key);
}

Outcome Document::promoteToVariable(std::string_view page, std::string_view node, std::string_view pin) const
{
    return Editor(*m_impl).promoteToVariable(page, node, pin);
}

Ordered Document::newVariable(std::string_view page) const
{
    const ClassInfo* own = m_impl->defaultClass();
    const std::string scope = own != nullptr && own->node != nullptr ? "class"
                            : std::string_view(page).starts_with("fn:") ? "local"
                                                                        : "file";
    return Ordered{{"key", ""},
                   {"name", ""},
                   {"kind", "integer"},
                   {"type", ""},
                   {"container", ""},
                   {"keyKind", "text"},
                   {"value", ""},
                   {"category", ""},
                   {"description", ""},
                   {"keep", false},
                   {"editable", false},
                   {"constant", false},
                   {"scope", scope},
                   {"local", scope == "local"},
                   {"inClass", scope == "class"},
                   {"page", std::string(page)}};
}

Ordered Document::variable(std::string_view page, std::string_view key) const
{
    std::string scope;
    std::string name;
    if (!variableKey(key, scope, name)) {
        return Ordered();
    }
    const VariableInfo* info = m_impl->declaredVariable(scope == "local" ? page : std::string_view(), name);
    if (info == nullptr) {
        return Ordered();
    }
    return Ordered{{"key", std::string(key)},
                   {"name", info->name},
                   {"kind", info->kind},
                   {"type", info->type},
                   {"container", info->container},
                   {"keyKind", info->keyKind},
                   {"value", info->initial},
                   {"category", info->category},
                   {"description", info->description},
                   {"keep", info->keep},
                   {"editable", info->editable},
                   {"constant", info->constant},
                   {"scope", info->local ? "local" : info->inClass ? "class" : "file"},
                   {"local", info->local},
                   {"inClass", info->inClass}};
}

Outcome Document::addType(std::string_view kind, std::string_view name) const
{
    return Editor(*m_impl).addType(kind, name);
}

Outcome Document::editType(std::string_view name, std::string_view field, const nlohmann::json& value) const
{
    return Editor(*m_impl).editType(name, field, value);
}

Outcome Document::addState(std::string_view page, double x, double y) const
{
    return Editor(*m_impl).addState(page, x, y);
}

Outcome Document::connectStates(std::string_view from, std::string_view to) const
{
    return Editor(*m_impl).connectStates(from, to);
}

Outcome Document::setTransitionLabel(std::string_view wire, std::string_view text) const
{
    return Editor(*m_impl).setTransitionLabel(wire, text);
}

Outcome Document::renameState(std::string_view node, std::string_view name) const
{
    return Editor(*m_impl).renameState(node, name);
}

Outcome Document::setStart(std::string_view node) const
{
    return Editor(*m_impl).setStart(node);
}

}
