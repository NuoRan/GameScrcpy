#include "Api.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace gs::blueprint {

namespace {

struct TypeToken {
    enum class Kind : std::uint8_t { End, Name, String, Number, Template, Punct };

    Kind kind = Kind::End;
    std::string_view text;
};

bool nameChar(char c) noexcept
{
    const auto byte = static_cast<unsigned char>(c);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$'
        || byte >= 0x80;
}

class TypeLexer
{
public:
    explicit TypeLexer(std::string_view text) noexcept : m_text(text) {}

    [[nodiscard]] std::size_t position() const noexcept { return m_at; }
    void seek(std::size_t at) noexcept { m_at = at; }

    TypeToken next()
    {
        while (m_at < m_text.size() && (m_text[m_at] == ' ' || m_text[m_at] == '\n' || m_text[m_at] == '\t')) {
            ++m_at;
        }
        if (m_at >= m_text.size()) {
            return {};
        }
        const std::size_t start = m_at;
        const char c = m_text[m_at];
        if (c == '"' || c == '\'' || c == '`') {
            ++m_at;
            while (m_at < m_text.size() && m_text[m_at] != c) {
                m_at += m_text[m_at] == '\\' ? 2 : 1;
            }
            m_at = std::min(m_at + 1, m_text.size());
            return {c == '`' ? TypeToken::Kind::Template : TypeToken::Kind::String, m_text.substr(start, m_at - start)};
        }
        if ((c >= '0' && c <= '9') || (c == '-' && m_at + 1 < m_text.size() && m_text[m_at + 1] >= '0'
                                       && m_text[m_at + 1] <= '9')) {
            ++m_at;
            while (m_at < m_text.size() && (nameChar(m_text[m_at]) || m_text[m_at] == '.')) {
                ++m_at;
            }
            return {TypeToken::Kind::Number, m_text.substr(start, m_at - start)};
        }
        if (nameChar(c)) {
            while (m_at < m_text.size() && nameChar(m_text[m_at])) {
                ++m_at;
            }
            return {TypeToken::Kind::Name, m_text.substr(start, m_at - start)};
        }
        if (m_text.substr(m_at).starts_with("=>")) {
            m_at += 2;
        } else if (m_text.substr(m_at).starts_with("...")) {
            m_at += 3;
        } else {
            ++m_at;
        }
        return {TypeToken::Kind::Punct, m_text.substr(start, m_at - start)};
    }

private:
    std::string_view m_text;
    std::size_t m_at = 0;
};

class TypeParser
{
public:
    explicit TypeParser(std::string_view text) : m_lexer(text) { advance(); }

    [[nodiscard]] bool is(std::string_view punct) const noexcept
    {
        return m_token.kind == TypeToken::Kind::Punct && m_token.text == punct;
    }

    [[nodiscard]] bool word(std::string_view name) const noexcept
    {
        return m_token.kind == TypeToken::Kind::Name && m_token.text == name;
    }

    [[nodiscard]] const TypeToken& token() const noexcept { return m_token; }

    void advance() { m_token = m_lexer.next(); }

    bool accept(std::string_view punct)
    {
        if (!is(punct)) {
            return false;
        }
        advance();
        return true;
    }

    void skipTypeParameters()
    {
        if (!is("<")) {
            return;
        }
        int depth = 0;
        do {
            if (is("<")) {
                ++depth;
            } else if (is(">")) {
                --depth;
            } else if (m_token.kind == TypeToken::Kind::End) {
                return;
            }
            advance();
        } while (depth > 0);
    }

    TypeRef type()
    {
        if (++m_depth > 64) {
            --m_depth;
            return std::make_shared<TypeNode>();
        }
        accept("|");
        TypeRef first = intersection();
        if (!is("|")) {
            --m_depth;
            return first;
        }
        auto node = std::make_shared<TypeNode>();
        node->kind = TypeNode::Kind::Union;
        node->items.push_back(std::move(first));
        while (accept("|")) {
            node->items.push_back(intersection());
        }
        --m_depth;
        return node;
    }

    std::vector<TypeParam> parameters()
    {
        std::vector<TypeParam> params;
        if (!accept("(")) {
            return params;
        }
        while (!is(")") && m_token.kind != TypeToken::Kind::End) {
            TypeParam param;
            if (accept("...")) {
                param.rest = true;
            }
            param.name = std::string(m_token.text);
            advance();
            if (accept("?")) {
                param.optional = true;
            }
            if (accept(":")) {
                param.type = type();
            } else {
                param.type = std::make_shared<TypeNode>();
            }
            params.push_back(std::move(param));
            if (!accept(",")) {
                break;
            }
        }
        accept(")");
        return params;
    }

private:
    TypeRef intersection()
    {
        accept("&");
        TypeRef first = postfix();
        if (!is("&")) {
            return first;
        }
        auto node = std::make_shared<TypeNode>();
        node->kind = TypeNode::Kind::Intersection;
        node->items.push_back(std::move(first));
        while (accept("&")) {
            node->items.push_back(postfix());
        }
        return node;
    }

    TypeRef postfix()
    {
        TypeRef base = primary();
        while (is("[")) {
            const std::size_t saved = m_lexer.position();
            const TypeToken savedToken = m_token;
            advance();
            if (!accept("]")) {
                m_lexer.seek(saved);
                m_token = savedToken;
                break;
            }
            auto array = std::make_shared<TypeNode>();
            array->kind = TypeNode::Kind::Array;
            array->items.push_back(std::move(base));
            base = std::move(array);
        }
        return base;
    }

    [[nodiscard]] bool functionAhead()
    {
        const std::size_t saved = m_lexer.position();
        const TypeToken savedToken = m_token;
        advance();
        bool result = is(")") || is("...");
        if (!result && m_token.kind == TypeToken::Kind::Name) {
            advance();
            result = is(":") || is("?") || is(",") || is(")");
        }
        m_lexer.seek(saved);
        m_token = savedToken;
        return result;
    }

    TypeRef function()
    {
        auto node = std::make_shared<TypeNode>();
        node->kind = TypeNode::Kind::Function;
        node->params = parameters();
        if (accept("=>")) {
            node->result = type();
        } else {
            node->result = std::make_shared<TypeNode>();
        }
        return node;
    }

    TypeRef primary()
    {
        auto node = std::make_shared<TypeNode>();
        switch (m_token.kind) {
        case TypeToken::Kind::String:
        case TypeToken::Kind::Number:
            node->kind = TypeNode::Kind::Literal;
            node->name = std::string(m_token.text);
            advance();
            return node;
        case TypeToken::Kind::Template:
            node->kind = TypeNode::Kind::Template;
            node->name = std::string(m_token.text);
            advance();
            return node;
        case TypeToken::Kind::End:
            return node;
        case TypeToken::Kind::Name:
            break;
        case TypeToken::Kind::Punct:
            return punctuated();
        }
        if (word("readonly") || word("unique")) {
            advance();
            return postfix();
        }
        if (word("keyof")) {
            advance();
            node->kind = TypeNode::Kind::Keyof;
            node->items.push_back(postfix());
            return node;
        }
        if (word("new")) {
            advance();
            skipTypeParameters();
            return function();
        }
        node->kind = TypeNode::Kind::Name;
        node->name = std::string(m_token.text);
        advance();
        while (accept(".")) {
            node->name += "." + std::string(m_token.text);
            advance();
        }
        if (accept("<")) {
            while (!is(">") && m_token.kind != TypeToken::Kind::End) {
                node->items.push_back(type());
                if (!accept(",")) {
                    break;
                }
            }
            accept(">");
        }
        return node;
    }

    TypeRef punctuated()
    {
        if (is("(")) {
            if (functionAhead()) {
                return function();
            }
            advance();
            TypeRef inner = type();
            accept(")");
            return inner;
        }
        if (is("<")) {
            skipTypeParameters();
            return function();
        }
        auto node = std::make_shared<TypeNode>();
        if (is("[")) {
            node->kind = TypeNode::Kind::Tuple;
            advance();
            while (!is("]") && m_token.kind != TypeToken::Kind::End) {
                node->items.push_back(type());
                if (!accept(",")) {
                    break;
                }
            }
            accept("]");
            return node;
        }
        if (is("{")) {
            node->kind = TypeNode::Kind::Object;
            advance();
            while (!is("}") && m_token.kind != TypeToken::Kind::End) {
                node->props.push_back(property());
                if (!accept(";") && !accept(",")) {
                    if (!is("}")) {
                        break;
                    }
                }
            }
            accept("}");
            return node;
        }
        advance();
        return node;
    }

    TypeProp property()
    {
        TypeProp prop;
        if (word("readonly")) {
            const std::size_t saved = m_lexer.position();
            const TypeToken savedToken = m_token;
            advance();
            if (is(":") || is("?") || is("(")) {
                m_lexer.seek(saved);
                m_token = savedToken;
            } else {
                prop.readonly = true;
            }
        }
        if (is("[")) {
            advance();
            while (!is("]") && m_token.kind != TypeToken::Kind::End) {
                advance();
            }
            accept("]");
            prop.name = "[]";
        } else {
            prop.name = std::string(m_token.text);
            if (prop.name.size() >= 2 && (prop.name.front() == '"' || prop.name.front() == '\'')) {
                prop.name = prop.name.substr(1, prop.name.size() - 2);
            }
            advance();
        }
        if (accept("?")) {
            prop.optional = true;
        }
        if (is("(") || is("<")) {
            skipTypeParameters();
            prop.method = true;
            auto method = std::make_shared<TypeNode>();
            method->kind = TypeNode::Kind::Function;
            method->params = parameters();
            method->result = accept(":") ? type() : std::make_shared<TypeNode>();
            prop.type = std::move(method);
            return prop;
        }
        prop.type = accept(":") ? type() : std::make_shared<TypeNode>();
        return prop;
    }

    TypeLexer m_lexer;
    TypeToken m_token;
    int m_depth = 0;
};

std::string_view skipName(std::string_view text)
{
    std::size_t at = 0;
    while (at < text.size() && (nameChar(text[at]) || text[at] == '.')) {
        ++at;
    }
    return text.substr(at);
}

ApiMember function(std::string owner, std::string name, std::string_view signature, std::size_t order)
{
    ApiMember member;
    member.owner = std::move(owner);
    member.name = std::move(name);
    member.label = member.owner.empty() ? member.name : member.owner + "." + member.name;
    member.order = order;
    TypeParser parser(skipName(signature));
    parser.skipTypeParameters();
    member.params = parser.parameters();
    member.returns = parser.accept(":") ? parser.type() : std::make_shared<TypeNode>();
    return member;
}

ApiMember declared(const std::string& owner, std::string_view declaration, std::size_t order)
{
    ApiMember member;
    member.owner = owner;
    member.order = order;
    TypeParser parser(declaration);
    if (parser.word("new")) {
        member.construct = true;
        member.name = "new";
        parser.advance();
        member.params = parser.parameters();
        member.returns = parser.accept(":") ? parser.type() : std::make_shared<TypeNode>();
        member.label = owner + ".new";
        return member;
    }
    if (parser.word("readonly")) {
        member.readonly = true;
        parser.advance();
    }
    member.name = std::string(parser.token().text);
    member.label = owner + "." + member.name;
    parser.advance();
    parser.accept("?");
    if (parser.is("(") || parser.is("<")) {
        parser.skipTypeParameters();
        member.params = parser.parameters();
        member.returns = parser.accept(":") ? parser.type() : std::make_shared<TypeNode>();
        return member;
    }
    member.property = true;
    member.type = parser.accept(":") ? parser.type() : std::make_shared<TypeNode>();
    return member;
}

std::string textOf(const nlohmann::json& object, std::string_view key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

void readBases(Api& api, std::string_view declarations)
{
    std::size_t at = 0;
    while (at < declarations.size()) {
        std::size_t end = declarations.find('\n', at);
        if (end == std::string_view::npos) {
            end = declarations.size();
        }
        std::string_view line = declarations.substr(at, end - at);
        at = end + 1;
        if (!line.starts_with("interface ")) {
            continue;
        }
        line.remove_prefix(10);
        std::size_t nameEnd = 0;
        while (nameEnd < line.size() && nameChar(line[nameEnd])) {
            ++nameEnd;
        }
        const std::string_view name = line.substr(0, nameEnd);
        std::string_view rest = line.substr(nameEnd);
        if (!rest.starts_with(" extends ")) {
            continue;
        }
        rest.remove_prefix(9);
        std::size_t baseEnd = 0;
        while (baseEnd < rest.size() && nameChar(rest[baseEnd])) {
            ++baseEnd;
        }
        if (const auto found = api.interfaces.find(name); found != api.interfaces.end()) {
            found->second.base = std::string(rest.substr(0, baseEnd));
        }
    }
}

TypeRef substitute(const TypeRef& type, const std::vector<std::string>& names, const std::vector<TypeRef>& values)
{
    if (type == nullptr) {
        return type;
    }
    if (type->kind == TypeNode::Kind::Name && type->items.empty()) {
        for (std::size_t i = 0; i < names.size() && i < values.size(); ++i) {
            if (names[i] == type->name) {
                return values[i];
            }
        }
        return type;
    }
    auto copy = std::make_shared<TypeNode>(*type);
    for (TypeRef& item : copy->items) {
        item = substitute(item, names, values);
    }
    for (TypeParam& param : copy->params) {
        param.type = substitute(param.type, names, values);
    }
    for (TypeProp& prop : copy->props) {
        prop.type = substitute(prop.type, names, values);
    }
    copy->result = substitute(copy->result, names, values);
    return copy;
}

}

TypeRef parseType(std::string_view text)
{
    TypeParser parser(text);
    return parser.type();
}

std::shared_ptr<const Api> loadApi(std::string_view report, std::string_view declarations)
{
    auto api = std::make_shared<Api>();
    const nlohmann::json root = nlohmann::json::parse(report, nullptr, false);
    if (!root.is_object()) {
        return api;
    }
    std::size_t order = 0;
    if (const auto globals = root.find("globals"); globals != root.end() && globals->is_array()) {
        for (const nlohmann::json& item : *globals) {
            api->globals.push_back(function({}, textOf(item, "name"), textOf(item, "signature"), order++));
        }
    }
    if (const auto values = root.find("values"); values != root.end() && values->is_array()) {
        for (const nlohmann::json& item : *values) {
            api->values.emplace(textOf(item, "name"), parseType(textOf(item, "type")));
        }
    }
    if (const auto spaces = root.find("namespaces"); spaces != root.end() && spaces->is_array()) {
        for (const nlohmann::json& item : *spaces) {
            ApiNamespace space{.name = textOf(item, "name"), .functions = {}};
            if (const auto functions = item.find("functions"); functions != item.end() && functions->is_array()) {
                for (const nlohmann::json& entry : *functions) {
                    space.functions.push_back(function(space.name, textOf(entry, "name"), textOf(entry, "signature"), order++));
                }
            }
            api->namespaces.push_back(std::move(space));
        }
    }
    if (const auto shapes = root.find("interfaces"); shapes != root.end() && shapes->is_array()) {
        for (const nlohmann::json& item : *shapes) {
            ApiInterface shape{.name = textOf(item, "name"), .base = {}, .members = {}};
            if (const auto members = item.find("members"); members != item.end() && members->is_array()) {
                for (const nlohmann::json& entry : *members) {
                    if (entry.is_string()) {
                        shape.members.push_back(declared(shape.name, entry.get<std::string>(), order++));
                    }
                }
            }
            api->interfaceOrder.push_back(shape.name);
            api->interfaces.emplace(shape.name, std::move(shape));
        }
    }
    if (const auto types = root.find("types"); types != root.end() && types->is_array()) {
        for (const nlohmann::json& item : *types) {
            const std::string name = textOf(item, "name");
            ApiAlias alias;
            std::string base = name;
            if (const std::size_t open = name.find('<'); open != std::string::npos) {
                base = name.substr(0, open);
                TypeParser parser(std::string_view(name).substr(open + 1));
                while (parser.token().kind == TypeToken::Kind::Name) {
                    alias.params.emplace_back(parser.token().text);
                    parser.advance();
                    if (!parser.accept(",")) {
                        break;
                    }
                }
            }
            alias.definition = parseType(textOf(item, "definition"));
            api->aliases.emplace(base, std::move(alias));
        }
    }
    readBases(*api, declarations);
    return api;
}

const ApiMember* Api::global(std::string_view name) const
{
    const auto found = std::ranges::find(globals, name, &ApiMember::name);
    return found == globals.end() ? nullptr : &*found;
}

const ApiNamespace* Api::space(std::string_view name) const
{
    const auto found = std::ranges::find(namespaces, name, &ApiNamespace::name);
    return found == namespaces.end() ? nullptr : &*found;
}

const ApiMember* Api::function(std::string_view spaceName, std::string_view name) const
{
    const ApiNamespace* found = space(spaceName);
    if (found == nullptr) {
        return nullptr;
    }
    const auto member = std::ranges::find(found->functions, name, &ApiMember::name);
    return member == found->functions.end() ? nullptr : &*member;
}

const ApiMember* Api::member(std::string_view shape, std::string_view name) const
{
    std::string_view current = shape;
    for (int depth = 0; depth < 16 && !current.empty(); ++depth) {
        const auto found = interfaces.find(current);
        if (found == interfaces.end()) {
            return nullptr;
        }
        const auto member = std::ranges::find(found->second.members, name, &ApiMember::name);
        if (member != found->second.members.end()) {
            return &*member;
        }
        current = found->second.base;
    }
    return nullptr;
}

std::vector<const ApiMember*> Api::members(std::string_view shape) const
{
    std::vector<const ApiMember*> list;
    std::vector<std::string_view> chain;
    std::string_view current = shape;
    for (int depth = 0; depth < 16 && !current.empty(); ++depth) {
        const auto found = interfaces.find(current);
        if (found == interfaces.end()) {
            break;
        }
        chain.push_back(found->first);
        current = found->second.base;
    }
    for (auto level = chain.rbegin(); level != chain.rend(); ++level) {
        for (const ApiMember& member : interfaces.find(*level)->second.members) {
            if (member.construct) {
                continue;
            }
            const auto same = std::ranges::find(list, member.name, &ApiMember::name);
            if (same != list.end()) {
                *same = &member;
            } else {
                list.push_back(&member);
            }
        }
    }
    return list;
}

bool Api::derives(std::string_view shape, std::string_view base) const
{
    std::string_view current = shape;
    for (int depth = 0; depth < 16 && !current.empty(); ++depth) {
        if (current == base) {
            return true;
        }
        const auto found = interfaces.find(current);
        if (found == interfaces.end()) {
            return false;
        }
        current = found->second.base;
    }
    return false;
}

TypeRef Api::resolve(const TypeRef& type) const
{
    if (type == nullptr || type->kind != TypeNode::Kind::Name) {
        return type;
    }
    if (const auto cached = m_resolved.find(type.get()); cached != m_resolved.end()) {
        return cached->second.second;
    }
    TypeRef current = type;
    for (int depth = 0; depth < 16 && current != nullptr && current->kind == TypeNode::Kind::Name; ++depth) {
        const auto found = aliases.find(current->name);
        if (found == aliases.end() || found->second.definition == nullptr) {
            break;
        }
        current = found->second.params.empty() ? found->second.definition
                                                : substitute(found->second.definition, found->second.params,
                                                             current->items);
    }
    m_resolved.emplace(type.get(), std::pair{type, current});
    return current;
}

const ApiMember* Api::find(std::string_view label) const
{
    const std::size_t dot = label.rfind('.');
    if (dot == std::string_view::npos) {
        return global(label);
    }
    const std::string_view owner = label.substr(0, dot);
    const std::string_view name = label.substr(dot + 1);
    if (const ApiMember* found = function(owner, name)) {
        return found;
    }
    return member(owner, name);
}

bool isName(const TypeRef& type, std::string_view name) noexcept
{
    return type != nullptr && type->kind == TypeNode::Kind::Name && type->name == name;
}

TypeRef withoutEmpty(const TypeRef& type)
{
    if (type == nullptr || type->kind != TypeNode::Kind::Union) {
        return type;
    }
    const auto empty = [](const TypeRef& item) {
        return isName(item, "null") || isName(item, "undefined") || isName(item, "void");
    };
    std::size_t kept = 0;
    const TypeRef* only = nullptr;
    for (const TypeRef& item : type->items) {
        if (!empty(item)) {
            ++kept;
            only = &item;
        }
    }
    if (kept == 1) {
        return *only;
    }
    if (kept == type->items.size()) {
        return type;
    }
    auto node = std::make_shared<TypeNode>(*type);
    node->items.clear();
    for (const TypeRef& item : type->items) {
        if (!empty(item)) {
            node->items.push_back(item);
        }
    }
    return node;
}

TypeRef awaited(const TypeRef& type)
{
    if ((isName(type, "Promise") || isName(type, "Action")) && !type->items.empty()) {
        return type->items.front();
    }
    return type;
}

bool awaitable(const TypeRef& type) noexcept
{
    return isName(type, "Promise") || isName(type, "Action");
}

const TypeNode* functionType(const Api& api, const TypeRef& type)
{
    const TypeRef resolved = api.resolve(withoutEmpty(type));
    if (resolved == nullptr) {
        return nullptr;
    }
    if (resolved->kind == TypeNode::Kind::Function) {
        return resolved.get();
    }
    if (resolved->kind == TypeNode::Kind::Union) {
        for (const TypeRef& item : resolved->items) {
            if (item != nullptr && item->kind == TypeNode::Kind::Function) {
                return item.get();
            }
        }
    }
    return nullptr;
}

bool isFunctionType(const Api& api, const TypeRef& type)
{
    return functionType(api, type) != nullptr;
}

std::vector<TypeProp> propsOf(const Api& api, const TypeRef& type)
{
    std::vector<TypeProp> props;
    const TypeRef resolved = api.resolve(withoutEmpty(type));
    if (resolved == nullptr) {
        return props;
    }
    if (resolved->kind == TypeNode::Kind::Object) {
        for (const TypeProp& prop : resolved->props) {
            if (prop.name != "[]") {
                props.push_back(prop);
            }
        }
        return props;
    }
    if (resolved->kind == TypeNode::Kind::Intersection) {
        for (const TypeRef& item : resolved->items) {
            for (TypeProp& prop : propsOf(api, item)) {
                if (std::ranges::find(props, prop.name, &TypeProp::name) == props.end()) {
                    props.push_back(std::move(prop));
                }
            }
        }
    }
    return props;
}

bool isEventMember(const Api& api, const ApiMember& member)
{
    if (member.property || member.construct || !isName(member.returns, "Subscription")) {
        return false;
    }
    return std::ranges::any_of(member.params, [&api](const TypeParam& param) { return isFunctionType(api, param.type); });
}

std::string typeText(const TypeRef& type)
{
    if (type == nullptr) {
        return {};
    }
    switch (type->kind) {
    case TypeNode::Kind::Name: {
        std::string text = type->name;
        if (!type->items.empty()) {
            text += "<";
            for (std::size_t i = 0; i < type->items.size(); ++i) {
                text += (i > 0 ? ", " : "") + typeText(type->items[i]);
            }
            text += ">";
        }
        return text;
    }
    case TypeNode::Kind::Literal:
    case TypeNode::Kind::Template:
        return type->name;
    case TypeNode::Kind::Array:
        return typeText(type->items.empty() ? nullptr : type->items.front()) + "[]";
    case TypeNode::Kind::Union:
    case TypeNode::Kind::Intersection: {
        std::string text;
        for (std::size_t i = 0; i < type->items.size(); ++i) {
            text += (i > 0 ? (type->kind == TypeNode::Kind::Union ? " | " : " & ") : "") + typeText(type->items[i]);
        }
        return text;
    }
    case TypeNode::Kind::Function:
        return "function";
    case TypeNode::Kind::Object:
        return "object";
    default:
        return "unknown";
    }
}

}
