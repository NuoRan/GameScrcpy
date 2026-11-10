#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace feel {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Value {
    enum class Kind { Null, Boolean, Number, Text, List, Object };

    Kind kind = Kind::Null;
    bool flag = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> members;
};

[[nodiscard]] std::optional<Value> parseJson(std::string_view json);

class Params
{
public:
    explicit Params(std::string_view json);

    [[nodiscard]] double number(std::string_view key, double fallback) const;
    [[nodiscard]] bool flag(std::string_view key, bool fallback) const;
    [[nodiscard]] std::optional<Point> point(std::string_view key) const;
    [[nodiscard]] std::vector<Point> points(std::string_view key) const;

private:
    [[nodiscard]] const Value* find(std::string_view key) const;

    Value m_root;
};

}
