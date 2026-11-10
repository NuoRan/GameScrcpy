#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::blueprint::syntax {

enum class Kind : std::uint8_t {
    Identifier,
    PrivateName,
    This,
    Super,
    Number,
    String,
    Template,
    Regex,
    True,
    False,
    Null,
    Array,
    Object,
    Property,
    Function,
    Arrow,
    Class,
    ClassMember,
    StaticBlock,
    Call,
    New,
    Member,
    Unary,
    Update,
    Binary,
    Logical,
    Conditional,
    Assign,
    Sequence,
    Spread,
    Await,
    Yield,
    TaggedTemplate,
    Paren,
    MetaProperty,
    ImportCall,
    Elision,
    ObjectPattern,
    ArrayPattern,
    Rest,
    AssignPattern,
    VarDecl,
    Declarator,
    Return,
    If,
    For,
    ForIn,
    ForOf,
    While,
    DoWhile,
    Break,
    Continue,
    Throw,
    Try,
    Catch,
    Switch,
    Case,
    Block,
    ExpressionStatement,
    Labeled,
    Empty,
    Debugger,
    Import,
    Export,
    ExportDefault,
    Opaque,
    Program,
};

namespace flag {
inline constexpr std::uint32_t kAsync = 1u << 0;
inline constexpr std::uint32_t kGenerator = 1u << 1;
inline constexpr std::uint32_t kOptional = 1u << 2;
inline constexpr std::uint32_t kComputed = 1u << 3;
inline constexpr std::uint32_t kShorthand = 1u << 4;
inline constexpr std::uint32_t kMethod = 1u << 5;
inline constexpr std::uint32_t kGetter = 1u << 6;
inline constexpr std::uint32_t kSetter = 1u << 7;
inline constexpr std::uint32_t kStatic = 1u << 8;
inline constexpr std::uint32_t kPrefix = 1u << 9;
inline constexpr std::uint32_t kExpressionBody = 1u << 10;
inline constexpr std::uint32_t kAwait = 1u << 11;
inline constexpr std::uint32_t kDelegate = 1u << 12;
inline constexpr std::uint32_t kDeclaration = 1u << 13;
inline constexpr std::uint32_t kField = 1u << 14;
inline constexpr std::uint32_t kSpread = 1u << 15;
inline constexpr std::uint32_t kConstructor = 1u << 16;
inline constexpr std::uint32_t kChain = 1u << 17;
}

struct Node;

class NodeList
{
public:
    NodeList() noexcept = default;
    NodeList(const NodeList&) = delete;
    NodeList& operator=(const NodeList&) = delete;
    NodeList(NodeList&& other) noexcept
        : m_data(std::exchange(other.m_data, nullptr)),
          m_size(std::exchange(other.m_size, 0)),
          m_capacity(std::exchange(other.m_capacity, 0))
    {
    }
    NodeList& operator=(NodeList&& other) noexcept
    {
        std::swap(m_data, other.m_data);
        std::swap(m_size, other.m_size);
        std::swap(m_capacity, other.m_capacity);
        return *this;
    }
    ~NodeList() { std::free(m_data); }

    void push_back(Node* node)
    {
        if (m_size == m_capacity) {
            const std::uint32_t capacity = m_capacity == 0 ? 4 : m_capacity * 2;
            void* grown = std::realloc(m_data, capacity * sizeof(Node*));
            if (grown == nullptr) {
                throw std::bad_alloc();
            }
            m_data = static_cast<Node**>(grown);
            m_capacity = capacity;
        }
        m_data[m_size++] = node;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] Node* operator[](std::size_t index) const noexcept { return m_data[index]; }
    [[nodiscard]] Node* front() const noexcept { return m_data[0]; }
    [[nodiscard]] Node* back() const noexcept { return m_data[m_size - 1]; }
    [[nodiscard]] Node* const* begin() const noexcept { return m_data; }
    [[nodiscard]] Node* const* end() const noexcept { return m_data + m_size; }

private:
    Node** m_data = nullptr;
    std::uint32_t m_size = 0;
    std::uint32_t m_capacity = 0;
};

struct Node {
    Kind kind = Kind::Empty;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t flags = 0;
    std::string_view text;
    Node* target = nullptr;
    Node* value = nullptr;
    Node* test = nullptr;
    Node* body = nullptr;
    Node* other = nullptr;
    NodeList items;

    [[nodiscard]] bool has(std::uint32_t bit) const noexcept { return (flags & bit) != 0; }
};

struct Comment {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    bool block = false;
};

struct Failure {
    std::uint32_t offset = 0;
    std::string message;
};

class Tree
{
public:
    Tree() = default;
    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;
    Tree(Tree&&) = default;
    Tree& operator=(Tree&&) = default;

    [[nodiscard]] Node* make(Kind kind, std::uint32_t start)
    {
        if (m_chunks.empty() || m_used == kChunk) {
            m_chunks.push_back(std::make_unique<Node[]>(kChunk));
            m_used = 0;
        }
        Node& node = m_chunks.back()[m_used++];
        node.kind = kind;
        node.start = start;
        node.end = start;
        return &node;
    }

    Node* program = nullptr;
    std::vector<Comment> comments;
    std::vector<Failure> failures;

private:
    static constexpr std::size_t kChunk = 512;
    std::vector<std::unique_ptr<Node[]>> m_chunks;
    std::size_t m_used = 0;
};

[[nodiscard]] Tree parse(std::string_view javascript);
[[nodiscard]] bool isFunction(const Node* node) noexcept;
[[nodiscard]] const Node* unparen(const Node* node) noexcept;

}
