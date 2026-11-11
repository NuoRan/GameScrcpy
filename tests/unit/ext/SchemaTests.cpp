#include "gs/ext/Schema.h"

#include <doctest/doctest.h>

#include <string>

using gs::ext::schemaProblem;
using gs::ext::valueProblem;

namespace {

constexpr const char* kPoint = R"({
    "type": "object",
    "title": "A point",
    "description": "Where to tap.",
    "properties": {
        "x": {"type": "number", "minimum": 0, "maximum": 1},
        "y": {"type": "number", "minimum": 0, "maximum": 1},
        "label": {"type": "string", "maxLength": 3},
        "count": {"type": "integer"},
        "mode": {"enum": ["tap", "hold", 2]},
        "tags": {"type": "array", "items": {"type": "string"}},
        "maybe": {"type": ["string", "null"]}
    },
    "required": ["x", "y"]
})";

}

TEST_CASE("ADR-0112: a service schema uses only the keywords GameScrcpy checks, each with a value of the right kind")
{
    CHECK(schemaProblem(kPoint).empty());
    CHECK(schemaProblem("{}").empty());
    CHECK(schemaProblem(R"({"type": "integer", "minimum": 1, "maximum": 1})").empty());

    CHECK(schemaProblem("[]") == "$ is not an object");
    CHECK(schemaProblem("{") == "the schema is not JSON");
    CHECK(schemaProblem(R"({"pattern": "^a"})").find("$.pattern is not a keyword") == 0);
    CHECK(schemaProblem(R"({"properties": {"a b": {"minLength": 1}}})").find(R"($.properties["a b"].minLength)") == 0);
    CHECK(schemaProblem(R"({"type": "text"})").find("$.type is not one of") == 0);
    CHECK(schemaProblem(R"({"type": ["string", "string"]})").find("$.type lists") == 0);
    CHECK(schemaProblem(R"({"type": []})").find("$.type is not one of") == 0);
    CHECK(schemaProblem(R"({"properties": []})") == "$.properties is not an object");
    CHECK(schemaProblem(R"({"required": ["a", "a"]})") == "$.required is not a list of different names");
    CHECK(schemaProblem(R"({"required": "a"})") == "$.required is not a list");
    CHECK(schemaProblem(R"({"items": 3})") == "$.items is not an object");
    CHECK(schemaProblem(R"({"enum": []})") == "$.enum is not a list of 1 to 64 values");
    CHECK(schemaProblem(R"({"minimum": "1"})") == "$.minimum is not a number");
    CHECK(schemaProblem(R"({"minimum": 2, "maximum": 1})") == "$.minimum is above maximum");
    CHECK(schemaProblem(R"({"maxLength": -1})") == "$.maxLength is not a whole number of at least 0");
    CHECK(schemaProblem(R"({"maxLength": 1.5})") == "$.maxLength is not a whole number of at least 0");
    CHECK(schemaProblem(R"({"title": 7})") == "$.title is not text");

    std::string deep = R"({"type": "string"})";
    for (int level = 0; level < 17; ++level) {
        deep = R"({"items": )" + deep + "}";
    }
    CHECK(schemaProblem(deep).find("nested more than 16 levels") != std::string::npos);
    CHECK(schemaProblem("{\"description\": \"" + std::string(17 * 1024, 'a') + "\"}") ==
          "the schema is larger than 16 KiB");
}

TEST_CASE("ADR-0112: values are checked against a service schema and the first problem names where it is")
{
    CHECK(valueProblem(kPoint, R"({"x": 0.5, "y": 1})").empty());
    CHECK(valueProblem(kPoint, R"({"x": 0, "y": 0, "label": "abc", "count": 3.0, "mode": 2, "tags": ["a"],
                                   "maybe": null, "other": [1, 2]})")
              .empty());
    CHECK(valueProblem(kPoint, R"({"x": 0.5})") == "$.y is missing");
    CHECK(valueProblem(kPoint, R"({"x": "0.5", "y": 0})") == "$.x is not a number");
    CHECK(valueProblem(kPoint, R"({"x": 1.5, "y": 0})") == "$.x is above 1");
    CHECK(valueProblem(kPoint, R"({"x": -1, "y": 0})") == "$.x is below 0");
    CHECK(valueProblem(kPoint, R"({"x": 0, "y": 0, "label": "abcd"})") == "$.label is longer than 3 characters");
    CHECK(valueProblem(kPoint, "{\"x\": 0, \"y\": 0, \"label\": \"\xE4\xBD\xA0\xE5\xA5\xBD\xE5\x95\x8A\"}").empty());
    CHECK(valueProblem(kPoint, R"({"x": 0, "y": 0, "count": 1.5})") == "$.count is not a whole number");
    CHECK(valueProblem(kPoint, R"({"x": 0, "y": 0, "mode": "swipe"})") ==
          "$.mode is not one of the values that the schema lists");
    CHECK(valueProblem(kPoint, R"({"x": 0, "y": 0, "tags": ["a", 2]})") == "$.tags[1] is not text");
    CHECK(valueProblem(kPoint, R"({"x": 0, "y": 0, "maybe": 1})") == "$.maybe is not text or null");
    CHECK(valueProblem(kPoint, "[0.5, 0.5]") == "$ is not an object");
    CHECK(valueProblem(kPoint, "{") == "$ is not JSON");

    CHECK(valueProblem("", "[1, {\"a\": null}]").empty());
    CHECK(valueProblem("", "nope") == "$ is not JSON");
    CHECK(valueProblem(R"({"type": "boolean"})", "true").empty());
    CHECK(valueProblem(R"({"type": "boolean"})", "1") == "$ is not true or false");
    CHECK(valueProblem(R"({"type": "null"})", "null").empty());
    CHECK(valueProblem(R"({"type": "array", "items": {"type": "object"}})", "[[]]") == "$[0] is not an object");
    CHECK(valueProblem(R"({"pattern": "x"})", "\"x\"") == "the schema itself is not valid");
    CHECK(valueProblem("", std::string(65, '[') + std::string(65, ']')) == "$ is nested more than 64 levels deep");
}
