#include "LogicReferences.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace gs::app;

namespace {

void write(const QTemporaryDir& dir, const QString& name, const QByteArray& contents)
{
    REQUIRE(QDir(dir.path()).mkpath(QFileInfo(dir.filePath(name)).path()));
    QFile file(dir.filePath(name));
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(contents) == contents.size());
}

std::string_view slice(std::string_view source, const LogicReference& found)
{
    return source.substr(found.start, found.end - found.start);
}

std::string valueOf(std::string_view literal)
{
    return literalValue(literal).value_or("<none>");
}

}

TEST_CASE("SCR-011: string literals are found by value, not in comments, regular expressions, other strings or templates "
          "with substitutions")
{
    const std::string_view source = "// vision.find(\"chest\")\n"
                                    "/* \"chest\" */ const chest = await vision.find(\"chest\");\n"
                                    "const re = /\"chest\"/g, half = total / 2 / \"chest\".length;\n"
                                    "const other = \"chest box\", plain = `chest`, mixed = `${chest} chest`;\n"
                                    "log.info('chest', controls.chest, chest);\n";
    const QString file = QStringLiteral("logic.ts");

    const std::vector<LogicReference> strings = referencesIn(source, file, {.text = "chest", .names = false});
    REQUIRE(strings.size() == 4);
    CHECK(strings[0].file == file);
    CHECK(strings[0].kind == ReferenceKind::String);
    CHECK(strings[0].line == 2);
    CHECK(strings[0].column == 47);
    CHECK(strings[0].length == 7);
    CHECK(slice(source, strings[0]) == "\"chest\"");
    CHECK(strings[0].lineText == QStringLiteral("/* \"chest\" */ const chest = await vision.find(\"chest\");"));
    CHECK(strings[1].line == 3);
    CHECK(strings[1].column == 43);
    CHECK(slice(source, strings[1]) == "\"chest\"");
    CHECK(strings[2].line == 4);
    CHECK(strings[2].column == 36);
    CHECK(slice(source, strings[2]) == "`chest`");
    CHECK(strings[3].line == 5);
    CHECK(strings[3].column == 10);
    CHECK(slice(source, strings[3]) == "'chest'");

    const std::vector<LogicReference> names = referencesIn(source, file, {.text = "chest", .strings = false});
    REQUIRE(names.size() == 4);
    CHECK(names[0].kind == ReferenceKind::Name);
    CHECK(names[0].line == 2);
    CHECK(names[0].column == 21);
    CHECK(names[1].line == 4);
    CHECK(names[1].column == 56);
    CHECK(names[2].line == 5);
    CHECK(names[2].column == 28);
    CHECK(names[3].line == 5);
    CHECK(names[3].column == 35);
    for (const LogicReference& found : names) {
        CHECK(slice(source, found) == "chest");
        CHECK(found.length == 5);
    }

    CHECK(referencesIn(source, file, {.text = "chest"}).size() == 8);
    CHECK(referencesIn(source, file, {.text = "box"}).empty());
    CHECK(referencesIn(source, file, {.text = ""}).empty());
}

TEST_CASE("SCR-011: places count lines and UTF-16 columns past a byte order mark and Windows line ends")
{
    const std::string_view source = "\xEF\xBB\xBF"
                                    "let x = 1;\r\n"
                                    "let 名 = \"chest\";\r\n"
                                    "\r"
                                    "x = 'chest';";
    const std::vector<LogicReference> found = referencesIn(source, QStringLiteral("bom.ts"), {.text = "chest"});
    REQUIRE(found.size() == 2);
    CHECK(found[0].line == 2);
    CHECK(found[0].column == 9);
    CHECK(found[0].length == 7);
    CHECK(found[0].lineText == QStringLiteral("let 名 = \"chest\";"));
    CHECK(slice(source, found[0]) == "\"chest\"");
    CHECK(found[1].line == 4);
    CHECK(found[1].column == 5);
    CHECK(found[1].lineText == QStringLiteral("x = 'chest';"));

    const std::vector<LogicReference> first = referencesIn(source, QStringLiteral("bom.ts"), {.text = "x"});
    REQUIRE(first.size() == 2);
    CHECK(first[0].line == 1);
    CHECK(first[0].column == 5);
    CHECK(first[0].lineText == QStringLiteral("let x = 1;"));
}

TEST_CASE("SCR-011: literal values decode escapes and line continuations, and refuse what they cannot read")
{
    CHECK(valueOf("\"chest\"") == "chest");
    CHECK(valueOf("'it\\'s'") == "it's");
    CHECK(valueOf("\"say \\\"hi\\\"\"") == "say \"hi\"");
    CHECK(valueOf("\"a\\\\b\"") == "a\\b");
    CHECK(valueOf("\"\\x63hest\"") == "chest");
    CHECK(valueOf("'\\u0063hest'") == "chest");
    CHECK(valueOf("\"\\u{1F600}\"") == "\xF0\x9F\x98\x80");
    CHECK(valueOf("\"\\uD83D\\uDE00\"") == "\xF0\x9F\x98\x80");
    CHECK(valueOf("\"tab\\there\"") == "tab\there");
    CHECK(valueOf("\"line\\\ncontinued\"") == "linecontinued");
    CHECK(valueOf("\"line\\\r\ncontinued\"") == "linecontinued");
    CHECK(valueOf("`two\r\nlines`") == "two\nlines");
    CHECK(valueOf("`\\${not} code`") == "${not} code");
    CHECK(valueOf("\"开始按钮\"") == "开始按钮");
    CHECK(valueOf("\"\\0\"") == std::string(1, '\0'));
    CHECK(valueOf("\"\"").empty());

    CHECK_FALSE(literalValue("\"\\1\"").has_value());
    CHECK_FALSE(literalValue("\"\\x6\"").has_value());
    CHECK_FALSE(literalValue("\"\\uD83D\"").has_value());
    CHECK_FALSE(literalValue("\"\\uDE00\"").has_value());
    CHECK_FALSE(literalValue("\"\\u{110000}\"").has_value());
    CHECK_FALSE(literalValue("\"open").has_value());
    CHECK_FALSE(literalValue("'mixed\"").has_value());
    CHECK_FALSE(literalValue("chest").has_value());
}

TEST_CASE("SCR-011: new names are quoted in the same style, escaped where needed, and read back the same")
{
    CHECK(quotedLiteral("Door", '"') == "\"Door\"");
    CHECK(quotedLiteral("it's", '\'') == "'it\\'s'");
    CHECK(quotedLiteral("it's", '"') == "\"it's\"");
    CHECK(quotedLiteral("say \"hi\"", '"') == "\"say \\\"hi\\\"\"");
    CHECK(quotedLiteral("a${b}`c$", '`') == "`a\\${b}\\`c$`");
    CHECK(quotedLiteral("开始按钮", '\'') == "'开始按钮'");
    CHECK(quotedLiteral("two\nlines\x01", '"') == "\"two\\nlines\\x01\"");
    for (const std::string_view name : {"Door", "it's", "say \"hi\"", "a${b}`c$", "开始按钮", "back\\slash"}) {
        for (const char quote : {'"', '\'', '`'}) {
            CHECK(valueOf(quotedLiteral(name, quote)) == name);
        }
    }
}

TEST_CASE("SCR-011: renaming literals keeps each quote style, escapes the new name and leaves the rest alone")
{
    const std::string_view source = "// \"Template 1\" stays in comments\r\n"
                                    "const a = await vision.find(\"Template 1\");\r\n"
                                    "const b = await vision.find('Template 1', { region: \"Template 1 area\" });\r\n"
                                    "const c = `Template 1`, d = `${\"Template 1\"}`, e = \"Template 10\";\r\n"
                                    "const Template = /\"Template 1\"/;\r\n";
    const LiteralRename renamed = renameLiterals(source, "Template 1", "Bob's \"door\"");
    CHECK(renamed.changed == 4);
    CHECK(renamed.text == "// \"Template 1\" stays in comments\r\n"
                          "const a = await vision.find(\"Bob's \\\"door\\\"\");\r\n"
                          "const b = await vision.find('Bob\\'s \"door\"', { region: \"Template 1 area\" });\r\n"
                          "const c = `Bob's \"door\"`, d = `${\"Bob's \\\"door\\\"\"}`, e = \"Template 10\";\r\n"
                          "const Template = /\"Template 1\"/;\r\n");

    const LiteralRename untouched = renameLiterals(source, "Template", "Other");
    CHECK(untouched.changed == 0);
    CHECK(untouched.text == source);
}

TEST_CASE("SCR-011: the name under the cursor is the identifier or the string literal there, never a keyword or a "
          "comment")
{
    const QString text = QStringLiteral("// jump here\n"
                                        "const label = \"跳一跳\" + controls.jump;\n"
                                        "if (ready) return 'it\\'s';\n");
    const auto at = [&text](int position) {
        const std::optional<ReferenceTarget> target = referenceTargetAt(text, position);
        return target ? target->text : QStringLiteral("<none>");
    };
    CHECK(at(5) == QStringLiteral("<none>"));
    CHECK(at(19) == QStringLiteral("label"));
    CHECK(at(21) == QStringLiteral("label"));
    CHECK(at(24) == QStringLiteral("label"));
    CHECK(at(29) == QStringLiteral("跳一跳"));
    CHECK(at(33) == QStringLiteral("<none>"));
    CHECK(at(43) == QStringLiteral("controls"));
    CHECK(at(46) == QStringLiteral("jump"));
    CHECK(at(51) == QStringLiteral("<none>"));
    CHECK(at(64) == QStringLiteral("<none>"));
    CHECK(at(70) == QStringLiteral("it's"));
    CHECK(at(-3) == QStringLiteral("<none>"));
    CHECK(at(1000) == QStringLiteral("<none>"));

    REQUIRE(referenceTargetAt(text, 46).has_value());
    CHECK(referenceTargetAt(text, 46)->kind == ReferenceKind::Name);
    REQUIRE(referenceTargetAt(text, 29).has_value());
    CHECK(referenceTargetAt(text, 29)->kind == ReferenceKind::String);
    CHECK_FALSE(referenceTargetAt(QStringLiteral("x = \"\";"), 5).has_value());
}

TEST_CASE("SCR-011: the profile's logic files are searched without types, tests, node_modules or declarations, and an "
          "open editor's text is used instead of the file")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir, QStringLiteral("logic.ts"), QByteArrayLiteral("vision.find(\"chest\");\n"));
    write(dir, QStringLiteral("lib/util.mts"), QByteArrayLiteral("export const chest = 'chest';\n"));
    write(dir, QStringLiteral("lib/old.js"), QByteArrayLiteral("module.exports = \"chest\";\n"));
    write(dir, QStringLiteral("lib/notes.txt"), QByteArrayLiteral("\"chest\"\n"));
    write(dir, QStringLiteral("lib/types/inner.ts"), QByteArrayLiteral("export const x = \"chest\";\n"));
    write(dir, QStringLiteral("types/profile.d.ts"), QByteArrayLiteral("declare const x: \"chest\";\n"));
    write(dir, QStringLiteral("src/extra.d.ts"), QByteArrayLiteral("declare const y: \"chest\";\n"));
    write(dir, QStringLiteral("tests/a.ts"), QByteArrayLiteral("\"chest\"\n"));
    write(dir, QStringLiteral("node_modules/pkg/index.js"), QByteArrayLiteral("\"chest\"\n"));
    write(dir, QStringLiteral(".cache/a.ts"), QByteArrayLiteral("\"chest\"\n"));
    const std::filesystem::path folder = dir.path().toStdWString();

    CHECK(logicFilesIn(folder) == QStringList{QStringLiteral("logic.ts"), QStringLiteral("lib/old.js"),
                                              QStringLiteral("lib/util.mts"), QStringLiteral("lib/types/inner.ts")});

    const ReferenceSearch strings = findLogicReferences(folder, {.text = "chest", .names = false});
    CHECK(strings.unread.isEmpty());
    REQUIRE(strings.found.size() == 4);
    CHECK(strings.found[0].file == QStringLiteral("logic.ts"));
    CHECK(strings.found[1].file == QStringLiteral("lib/old.js"));
    CHECK(strings.found[2].file == QStringLiteral("lib/util.mts"));
    CHECK(strings.found[2].column == 22);
    CHECK(strings.found[3].file == QStringLiteral("lib/types/inner.ts"));

    const ReferenceSearch names = findLogicReferences(folder, {.text = "chest", .strings = false});
    REQUIRE(names.found.size() == 1);
    CHECK(names.found[0].file == QStringLiteral("lib/util.mts"));
    CHECK(names.found[0].column == 14);

    const OpenTexts open{{QStringLiteral("logic.ts"), QByteArrayLiteral("const a = 1;\nconst b = \"chest\", c = 'chest';\n")}};
    const ReferenceSearch edited = findLogicReferences(folder, {.text = "chest", .names = false}, open);
    REQUIRE(edited.found.size() == 5);
    CHECK(edited.found[0].file == QStringLiteral("logic.ts"));
    CHECK(edited.found[0].line == 2);
    CHECK(edited.found[0].column == 11);
    CHECK(edited.found[1].line == 2);
    CHECK(edited.found[1].column == 24);
    CHECK(edited.found[2].file == QStringLiteral("lib/old.js"));

    CHECK(listedPath({QStringLiteral("Lib/Util.mts")}, QStringLiteral("lib/util.mts")));
    CHECK(listedPath({QStringLiteral("lib\\util.mts")}, QStringLiteral("lib/util.mts")));
    CHECK_FALSE(listedPath({QStringLiteral("lib/old.js")}, QStringLiteral("lib/util.mts")));
}

TEST_CASE("SCR-020: the uses of a component are its member expressions, never a string, a comment or another object")
{
    const std::string_view source = "// controls.hold6.onPress\n"
                                    "controls.hold6.onPress(async () => {\n"
                                    "  const label = \"controls.hold6\";\n"
                                    "  other.controls.hold6.press();\n"
                                    "  controls.hold6.\"hold6\";\n"
                                    "});\n"
                                    "controls[\"hold6\"].press();\n"
                                    "controls['hold61'].press();\n"
                                    "near.hold6();\n";
    const std::vector<ControlUse> found = controlUsesIn(source, QStringLiteral("logic.ts"), "hold6");
    REQUIRE(found.size() == 2);
    CHECK(found[0].line == 2);
    CHECK(found[0].column == 1);
    CHECK(found[0].length == 14);
    CHECK(source.substr(found[0].useStart, found[0].useEnd - found[0].useStart) == "controls.hold6");
    CHECK(source.substr(found[0].start, found[0].end - found[0].start) ==
          "controls.hold6.onPress(async () => {\n"
          "  const label = \"controls.hold6\";\n"
          "  other.controls.hold6.press();\n"
          "  controls.hold6.\"hold6\";\n"
          "});");
    CHECK(found[1].line == 7);
    CHECK(source.substr(found[1].useStart, found[1].useEnd - found[1].useStart) == "controls[\"hold6\"]");
    CHECK(source.substr(found[1].start, found[1].end - found[1].start) == "controls[\"hold6\"].press();");
    CHECK(controlUsesIn(source, QStringLiteral("logic.ts"), "hold61").size() == 1);
    CHECK(controlUsesIn(source, QStringLiteral("logic.ts"), "").empty());
}

TEST_CASE("SCR-020: renaming a component rewrites only its member uses and keeps each quote style")
{
    const std::string_view source = "const a = controls.hold6;\n"
                                    "controls[\"hold6\"].press();\n"
                                    "controls['hold6'].press();\n"
                                    "log(\"controls.hold6\", other.hold6);\n"
                                    "controls.hold61.press();\n";
    const ControlRewrite renamed = renameControlUses(source, "hold6", "holdF");
    CHECK(renamed.changed == 3);
    CHECK(renamed.text == "const a = controls.holdF;\n"
                          "controls[\"holdF\"].press();\n"
                          "controls['holdF'].press();\n"
                          "log(\"controls.hold6\", other.hold6);\n"
                          "controls.hold61.press();\n");
    const ControlRewrite untouched = renameControlUses(source, "hold", "holdF");
    CHECK(untouched.changed == 0);
    CHECK(untouched.text == source);
}

TEST_CASE("SCR-020: removing a component takes the statements that use it and leaves the rest of the file as it was")
{
    const std::string_view source = "keys.F.onPress(async () => {\n"
                                    "  await sleep(10);\n"
                                    "});\n"
                                    "\n"
                                    "const name = \"hold6\";\n"
                                    "controls.hold6.onPress(async () => {\n"
                                    "  await sleep(5);\n"
                                    "});\n"
                                    "if (ready) controls.hold6.press();\n"
                                    "controls.hold6.release(); controls.other.press();\n"
                                    "if (controls.hold6) other();\n"
                                    "log.info(name);\n";
    const ControlRewrite removed = removeControlUses(source, "hold6");
    CHECK(removed.changed == 4);
    CHECK(removed.text == "keys.F.onPress(async () => {\n"
                          "  await sleep(10);\n"
                          "});\n"
                          "\n"
                          "const name = \"hold6\";\n"
                          "controls.other.press();\n"
                          "log.info(name);\n");
    const ControlRewrite none = removeControlUses(source, "hold");
    CHECK(none.changed == 0);
    CHECK(none.text == source);
}

TEST_CASE("SCR-020: the uses of a component in the profile's logic files take the open editor's text")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir, QStringLiteral("logic.ts"), QByteArrayLiteral("controls.hold6.press();\n"));
    write(dir, QStringLiteral("lib/more.mts"), QByteArrayLiteral("controls.hold6.release();\n"));
    write(dir, QStringLiteral("types/profile.d.ts"), QByteArrayLiteral("declare const x: 1;\n"));
    const std::filesystem::path folder = dir.path().toStdWString();

    const ControlUseSearch search = findControlUses(folder, "hold6");
    REQUIRE(search.found.size() == 2);
    CHECK(search.unread.isEmpty());
    CHECK(search.found[0].file == QStringLiteral("logic.ts"));
    CHECK(search.found[1].file == QStringLiteral("lib/more.mts"));

    const OpenTexts open{{QStringLiteral("logic.ts"), QByteArrayLiteral("controls.hold6.press();\n"
                                                                        "controls.hold6.release();\n")}};
    const ControlUseSearch edited = findControlUses(folder, "hold6", open);
    CHECK(edited.found.size() == 3);
    CHECK(findControlUses(folder, "holdF").found.empty());
}
