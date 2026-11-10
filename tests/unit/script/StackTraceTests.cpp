#include "StackTrace.h"

#include <doctest/doctest.h>

using gs::script::SourceLocation;
using gs::script::detail::locationFromStack;

TEST_CASE("the first frame with a module location is the error's location")
{
    CHECK(locationFromStack("    at boom (main.js:3:18)\n    at <anonymous> (main.js:9:5)\n") ==
          SourceLocation{.module = "main.js", .line = 3, .column = 18, .function = "boom"});
    CHECK(locationFromStack("    at lib/bad.js:3:15\n") ==
          SourceLocation{.module = "lib/bad.js", .line = 3, .column = 15});
    CHECK(
        locationFromStack("    at get value (lib/a (b).js:7:2)\n") ==
        SourceLocation{.module = "lib/a (b).js", .line = 7, .column = 2, .function = "get value"});
}

TEST_CASE("native frames and the host prelude are skipped")
{
    CHECK(locationFromStack(
              "    at sleep (native)\n    at fire (<gs>:12:9)\n    at tick (main.js:4:15)\n") ==
          SourceLocation{.module = "main.js", .line = 4, .column = 15, .function = "tick"});
    CHECK(locationFromStack("    at sleep (native)\n") == SourceLocation{});
    CHECK(locationFromStack("") == SourceLocation{});
    CHECK(locationFromStack("Error: not a stack") == SourceLocation{});
    CHECK(locationFromStack("    at f (main.js:x:1)\n") == SourceLocation{});
}
