#include "ModulePath.h"
#include "ScriptFixture.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

using gs::script::detail::isTypeScriptModule;
using gs::script::detail::moduleFile;
using gs::script::detail::moduleNameProblem;
using gs::script::detail::resolveImport;

TEST_CASE("relative imports resolve against the importing module's folder")
{
    CHECK(resolveImport("main.js", "./util.js").value() == "util.js");
    CHECK(resolveImport("main.js", "./lib/math.js").value() == "lib/math.js");
    CHECK(resolveImport("lib/math.js", "../shared/base.js").value() == "shared/base.js");
    CHECK(resolveImport("lib/deep/a.js", "./../b.mjs").value() == "lib/b.mjs");
    CHECK(resolveImport("lib/a.js", "./c.JS").value() == "lib/c.JS");
}

TEST_CASE("imports that are not relative, leave the folder or are not modules are refused")
{
    CHECK(resolveImport("main.js", "lodash").error().find("only relative imports") !=
          std::string::npos);
    CHECK(resolveImport("main.js", "/abs.js").error().find("only relative imports") !=
          std::string::npos);
    CHECK(resolveImport("main.js", "C:/abs.js").error().find("only relative imports") !=
          std::string::npos);
    CHECK(resolveImport("main.js", "../outside.js").error().find("leaves the script folder") !=
          std::string::npos);
    CHECK(resolveImport("lib/a.js", "../../outside.js").error().find("leaves the script folder") !=
          std::string::npos);
    CHECK(resolveImport("main.js", "./data.json").error().find(".mjs, .ts or .mts") != std::string::npos);
    CHECK(resolveImport("main.js", "./lib/").error().find("empty path segment") !=
          std::string::npos);
    CHECK(resolveImport("main.js", ".//a.js").error().find("empty path segment") !=
          std::string::npos);
    CHECK(resolveImport("main.js", "./a\\b.js").error().find("file names cannot have") !=
          std::string::npos);
    CHECK(resolveImport("main.js", "./a:b.js").error().find("file names cannot have") !=
          std::string::npos);
    CHECK(resolveImport("main.js", "./dir./a.js").error().find("ends with a dot") !=
          std::string::npos);
}

TEST_CASE("entry module names stay inside the script folder")
{
    CHECK_FALSE(moduleNameProblem("main.js").has_value());
    CHECK_FALSE(moduleNameProblem("src/main.mjs").has_value());
    CHECK(moduleNameProblem("").has_value());
    CHECK(moduleNameProblem("../main.js").has_value());
    CHECK(moduleNameProblem("./main.js").has_value());
    CHECK(moduleNameProblem("/main.js").has_value());
    CHECK(moduleNameProblem("C:/main.js").has_value());
    CHECK_FALSE(moduleNameProblem("main.ts").has_value());
    CHECK_FALSE(moduleNameProblem("lib/util.MTS").has_value());
    CHECK(moduleNameProblem("main.json").has_value());
    CHECK(moduleNameProblem("lib//main.js").has_value());
}

TEST_CASE("TypeScript modules are recognised by their extension")
{
    CHECK(isTypeScriptModule("main.ts"));
    CHECK(isTypeScriptModule("lib/util.MTS"));
    CHECK_FALSE(isTypeScriptModule("main.js"));
    CHECK_FALSE(isTypeScriptModule("types.ts/main.js"));
    CHECK_FALSE(isTypeScriptModule("ts"));
    CHECK(resolveImport("main.ts", "./lib/util.ts").value() == "lib/util.ts");
}

TEST_CASE("module files are found inside the folder only")
{
    const gs::script::test::ScriptFolder folder;
    folder.write("main.js", "export {};");
    folder.write("lib/util.js", "export {};");
    CHECK(moduleFile(folder.path(), "main.js").has_value());
    CHECK(moduleFile(folder.path(), "lib/util.js").has_value());
    CHECK(moduleFile(folder.path(), "missing.js").error() == "cannot be found");
    CHECK(moduleFile(folder.path(), "lib").error() == "cannot be found");
    CHECK_FALSE(moduleFile(folder.path() / "absent", "main.js").has_value());
}
