#include "ScriptHighlighter.h"
#include "ScriptWorkbenchApi.h"
#include "ScriptWorkbenchConsole.h"
#include "ScriptWorkbenchFiles.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTranslator>

#include <algorithm>
#include <array>
#include <memory>
#include <string_view>
#include <vector>

using namespace gs::app;

namespace {

constexpr std::string_view kCompletions = R"json([
  {"label": "sleep", "kind": "function", "detail": "sleep(ms: number): Promise<void>",
   "documentation": "Waits without blocking other tasks.", "insertText": "sleep(${1:ms})"},
  {"label": "nextFrame", "kind": "function", "detail": "nextFrame(): Promise<void>",
   "documentation": "Waits until the next picture frame is shown.", "insertText": "nextFrame()"},
  {"label": "controls", "kind": "variable", "detail": "controls: Controls",
   "documentation": "The controls of the profile in use, by id.", "insertText": "controls"},
  {"label": "vars", "kind": "variable", "detail": "vars: Vars",
   "documentation": "The variables of the profile in use, by id.", "insertText": "vars"},
  {"label": "log", "kind": "namespace", "detail": "log", "documentation": "Lines in the script console.",
   "insertText": "log"},
  {"label": "log.info", "kind": "function", "detail": "log.info(...values: unknown[]): void",
   "documentation": "Writes an information line to the script console.", "insertText": "log.info()"},
  {"label": "time", "kind": "namespace", "detail": "time", "documentation": "The script clock.", "insertText": "time"},
  {"label": "touch", "kind": "namespace", "detail": "touch", "documentation": "Touches on the phone picture.",
   "insertText": "touch"},
  {"label": "touch.tap", "kind": "function",
   "detail": "touch.tap(x: number, y: number, durationMs?: number): Promise<void>",
   "documentation": "Taps the picture at a position.", "insertText": "touch.tap(${1:x}, ${2:y})"},
  {"label": "touch.swipe", "kind": "function",
   "detail": "touch.swipe(fromX: number, fromY: number, toX: number, toY: number, durationMs?: number): Promise<void>",
   "documentation": "Swipes from one position to another.", "insertText": "touch.swipe()"},
  {"label": "Component.name", "kind": "property", "detail": "readonly name: string",
   "documentation": "The name shown in the editor.", "insertText": "name"},
  {"label": "Component.trigger", "kind": "method", "detail": "trigger(): Promise<void>",
   "documentation": "Presses and releases the control.", "insertText": "trigger(${1})"},
  {"label": "Component.onPress", "kind": "method",
   "detail": "onPress(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
   "documentation": "Starts a task each time the control's key goes down.", "insertText": "onPress(${1})"},
  {"label": "Component.onRelease", "kind": "method",
   "detail": "onRelease(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription",
   "documentation": "Starts a task each time the control's key comes up.", "insertText": "onRelease(${1})"},
  {"label": "Variable.value", "kind": "property", "detail": "value: boolean", "documentation": "The current value.",
   "insertText": "value"},
  {"label": "Region", "kind": "type", "detail": "type Region", "documentation": "Part of the picture.",
   "insertText": "Region"}
])json";

constexpr std::string_view kDeclarations = R"ts(/** Part of the picture. */
type Region = { readonly x: number };

/** A control of the profile in use. */
interface Component {
    /**
     * The name shown in the editor.
     * @example log.info(controls.jump.name);
     */
    readonly name: string;

    /**
     * Starts a task each time the control's key goes down.
     * @example controls.jump.onPress(async () => { await controls.crouch.trigger(); });
     */
    onPress(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/**
 * Waits without blocking other tasks.
 * @param ms How long to wait, in milliseconds.
 * @since 1 (stable)
 * @example await sleep(500);
 */
declare function sleep(ms: number): Promise<void>;

/**
 * The controls of the profile in use, by id.
 * @example controls.jump.onPress(() => log.info("jump"));
 */
declare const controls: Controls;

/** Touches on the phone picture. */
declare namespace touch {
    /**
     * Taps the picture at a position.
     * @param x Horizontal position.
     * @since 1 (stable)
     * @example await touch.tap(0.5, 0.8);
     */
    function tap(x: number, y: number, durationMs?: number): Promise<void>;

    /**
     * Swipes from one position to another.
     * @example await touch.swipe(0.5, 0.8, 0.5, 0.2, 250);
     */
    function swipe(fromX: number, fromY: number, toX: number, toY: number, durationMs?: number): Promise<void>;
}
)ts";

ScriptSpan span(int start, int length, ScriptToken token)
{
    return {.start = start, .length = length, .token = token};
}

std::vector<ScriptSpan> spansOf(QStringView text, int state = kScriptPlain)
{
    return scriptLine(text, state).spans;
}

const ScriptApi& fixtureApi()
{
    static const ScriptApi api(kCompletions, kDeclarations);
    return api;
}

ProfileNames fixtureNames()
{
    return {.controls = {{.id = QStringLiteral("jump"), .detail = QStringLiteral("Jump · button")},
                         {.id = QStringLiteral("fire-2"), .detail = QStringLiteral("button")}},
            .states = {{.id = QStringLiteral("aiming"), .detail = QStringLiteral("Aiming")}}};
}

std::vector<CompletionItem> complete(QStringView line, bool asked = false)
{
    return completionsAt(fixtureApi(), fixtureNames(), line, static_cast<int>(line.size()), kScriptPlain, asked);
}

QStringList labelsOf(const std::vector<CompletionItem>& items)
{
    QStringList labels;
    for (const CompletionItem& item : items) {
        labels.append(item.label);
    }
    return labels;
}

void put(const QString& path, const QByteArray& bytes)
{
    REQUIRE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(bytes) == bytes.size());
}

QByteArray contentsOf(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

QStringList pathsOf(const std::vector<ScriptFile>& files)
{
    QStringList paths;
    for (const ScriptFile& file : files) {
        paths.append(file.path);
    }
    return paths;
}

const ScriptFile& fileAt(const std::vector<ScriptFile>& files, const QString& path)
{
    const auto found = std::ranges::find(files, path, &ScriptFile::path);
    REQUIRE(found != files.end());
    return *found;
}

}

TEST_CASE("the script editor colours keywords, strings, numbers, comments and types after a colon (M9)")
{
    CHECK(spansOf(u"const x: number = 42; // done") ==
          std::vector{span(0, 5, ScriptToken::Keyword), span(9, 6, ScriptToken::Type), span(18, 2, ScriptToken::Number),
                      span(22, 7, ScriptToken::Comment)});
    CHECK(spansOf(u"log.info(\"a\", 'b', `c${d}`);") ==
          std::vector{span(9, 3, ScriptToken::String), span(14, 3, ScriptToken::String),
                      span(19, 7, ScriptToken::String)});
    CHECK(spansOf(u"controls.jump.type === \"button\"") == std::vector{span(23, 8, ScriptToken::String)});
    CHECK(spansOf(u"function f(a: string | null): Promise<void> {") ==
          std::vector{span(0, 8, ScriptToken::Keyword), span(14, 6, ScriptToken::Type), span(23, 4, ScriptToken::Type),
                      span(30, 7, ScriptToken::Type), span(38, 4, ScriptToken::Type)});
    CHECK(spansOf(u"const o = { x: found.x, ok: true };") ==
          std::vector{span(0, 5, ScriptToken::Keyword), span(28, 4, ScriptToken::Keyword)});
    CHECK(spansOf(u"x = 1_000 + 2.5e-3 + 10n + .5") ==
          std::vector{span(4, 5, ScriptToken::Number), span(12, 6, ScriptToken::Number),
                      span(21, 3, ScriptToken::Number), span(27, 2, ScriptToken::Number)});

    const ScriptLine opened = scriptLine(u"/* start", kScriptPlain);
    CHECK(opened.spans == std::vector{span(0, 8, ScriptToken::Comment)});
    CHECK(opened.state == kScriptInComment);
    const ScriptLine closed = scriptLine(u"end */ let y = 0x1F;", opened.state);
    CHECK(closed.spans ==
          std::vector{span(0, 6, ScriptToken::Comment), span(7, 3, ScriptToken::Keyword), span(15, 4, ScriptToken::Number)});
    CHECK(closed.state == kScriptPlain);

    const ScriptLine started = scriptLine(u"const t = `one", kScriptPlain);
    CHECK(started.spans == std::vector{span(0, 5, ScriptToken::Keyword), span(10, 4, ScriptToken::String)});
    CHECK(started.state == kScriptInTemplate);
    const ScriptLine ended = scriptLine(u"two` + 1", started.state);
    CHECK(ended.spans == std::vector{span(0, 4, ScriptToken::String), span(7, 1, ScriptToken::Number)});
    CHECK(ended.state == kScriptPlain);

    const ScriptLine unterminated = scriptLine(u"log.info(\"open", kScriptPlain);
    CHECK(unterminated.open);
    CHECK(unterminated.state == kScriptPlain);
}

TEST_CASE("completion stays quiet inside strings and comments (M9)")
{
    CHECK(insideLiteral(u"log.info(\"tou", kScriptPlain, 13));
    CHECK(insideLiteral(u"x // touch.", kScriptPlain, 11));
    CHECK(insideLiteral(u"abc", kScriptInTemplate, 2));
    CHECK(insideLiteral(u"a /* touch.", kScriptPlain, 11));
    CHECK_FALSE(insideLiteral(u"touch.", kScriptPlain, 6));
    CHECK_FALSE(insideLiteral(u"/* a */ touch.", kScriptPlain, 14));
    CHECK_FALSE(insideLiteral(u"\"done\" + to", kScriptPlain, 11));
}

TEST_CASE("the API list carries each entry's signature, description and example from the binding table (M9)")
{
    const ScriptApi& api = fixtureApi();
    const ApiEntry* tap = api.find(u"touch.tap");
    REQUIRE(tap != nullptr);
    CHECK(tap->owner == QStringLiteral("touch"));
    CHECK(tap->name == QStringLiteral("tap"));
    CHECK(tap->kind == QStringLiteral("function"));
    CHECK(tap->documentation == QStringLiteral("Taps the picture at a position."));
    CHECK(tap->example == QStringLiteral("await touch.tap(0.5, 0.8);"));
    CHECK(api.find(u"touch.swipe")->example == QStringLiteral("await touch.swipe(0.5, 0.8, 0.5, 0.2, 250);"));
    CHECK(api.find(u"sleep")->example == QStringLiteral("await sleep(500);"));
    CHECK(api.find(u"controls")->example == QStringLiteral("controls.jump.onPress(() => log.info(\"jump\"));"));
    CHECK(api.find(u"Component.name")->example == QStringLiteral("log.info(controls.jump.name);"));
    CHECK(api.find(u"Component.onPress")->example ==
          QStringLiteral("controls.jump.onPress(async () => { await controls.crouch.trigger(); });"));
    CHECK(api.find(u"touch")->example.isEmpty());
    CHECK(api.find(u"Region")->example.isEmpty());
    CHECK(api.find(u"missing") == nullptr);
    CHECK(api.isNamespace(u"touch"));
    CHECK_FALSE(api.isNamespace(u"sleep"));
    CHECK(api.list().size() == static_cast<qsizetype>(api.entries().size()));

    const ScriptApi& installed = ScriptApi::installed();
    const ApiEntry* real = installed.find(u"touch.tap");
    REQUIRE(real != nullptr);
    CHECK_FALSE(real->documentation.isEmpty());
    CHECK_FALSE(real->example.isEmpty());
    CHECK(installed.isNamespace(u"log"));
    CHECK(installed.find(u"Component.onPress") != nullptr);
}

TEST_CASE("completion offers namespaces, functions, values and the profile's controls and states (M9)")
{
    const std::vector<CompletionItem> tap = complete(u"await touch.ta");
    REQUIRE(tap.size() == 1);
    CHECK(tap[0].label == QStringLiteral("tap"));
    CHECK(tap[0].kind == QStringLiteral("function"));
    CHECK(tap[0].detail == QStringLiteral("(x: number, y: number, durationMs?: number): Promise<void>"));
    CHECK(tap[0].documentation == QStringLiteral("Taps the picture at a position."));
    CHECK(tap[0].insert == QStringLiteral("tap()"));
    CHECK(tap[0].from == 12);
    CHECK(tap[0].caret == 4);

    CHECK(labelsOf(complete(u"touch.")) == QStringList{QStringLiteral("swipe"), QStringLiteral("tap")});
    const std::vector<CompletionItem> sleep = complete(u"sl");
    REQUIRE(sleep.size() == 1);
    CHECK(sleep[0].insert == QStringLiteral("sleep()"));
    CHECK(sleep[0].caret == 6);
    CHECK(sleep[0].from == 0);
    const std::vector<CompletionItem> frame = complete(u"    await nextF");
    REQUIRE(frame.size() == 1);
    CHECK(frame[0].insert == QStringLiteral("nextFrame()"));
    CHECK(frame[0].caret == 11);
    CHECK(frame[0].from == 10);
    const std::vector<CompletionItem> log = complete(u"lo");
    REQUIRE(log.size() == 1);
    CHECK(log[0].kind == QStringLiteral("namespace"));
    CHECK(log[0].insert == QStringLiteral("log"));
    CHECK(log[0].detail.isEmpty());
    CHECK(complete(u"log").empty());
    CHECK(labelsOf(complete(u"log", true)) == QStringList{QStringLiteral("log")});
    CHECK(complete(u"t").empty());
    CHECK(labelsOf(complete(u"t", true)) == QStringList{QStringLiteral("time"), QStringLiteral("touch")});

    const std::vector<CompletionItem> jump = complete(u"controls.ju");
    REQUIRE(jump.size() == 1);
    CHECK(jump[0].label == QStringLiteral("jump"));
    CHECK(jump[0].kind == QStringLiteral("control"));
    CHECK(jump[0].detail == QStringLiteral("Jump · button"));
    CHECK(jump[0].insert == QStringLiteral("jump"));
    CHECK(jump[0].from == 9);
    const std::vector<CompletionItem> ids = complete(u"controls.");
    REQUIRE(labelsOf(ids) == QStringList{QStringLiteral("fire-2"), QStringLiteral("jump")});
    CHECK(ids[0].insert == QStringLiteral("[\"fire-2\"]"));
    CHECK(ids[0].from == 8);
    CHECK(ids[0].caret == 10);
    const std::vector<CompletionItem> press = complete(u"controls.jump.on");
    REQUIRE(labelsOf(press) == QStringList{QStringLiteral("onPress"), QStringLiteral("onRelease")});
    CHECK(press[0].insert == QStringLiteral("onPress()"));
    CHECK(press[0].caret == 8);
    CHECK(press[0].from == 14);
    CHECK(labelsOf(complete(u"controls.jump.tr")) == QStringList{QStringLiteral("trigger")});
    CHECK(labelsOf(complete(u"vars.aim")) == QStringList{QStringLiteral("aiming")});
    const std::vector<CompletionItem> value = complete(u"vars.aiming.va");
    REQUIRE(value.size() == 1);
    CHECK(value[0].insert == QStringLiteral("value"));
    CHECK(value[0].caret == 5);

    CHECK(complete(u"log.info(\"touch.ta").empty());
    CHECK(complete(u"// touch.ta").empty());
    CHECK(complete(u"x.ta").empty());
    CHECK(complete(u"a.b.c").empty());
    CHECK(complete(u"Touch.ta").empty());
    CHECK(complete(u"1.5").empty());
    CHECK(labelsOf(complete(u"...lo")) == QStringList{QStringLiteral("log")});
}

TEST_CASE("completion and the API page show the descriptions in the interface language, names and signatures as written (ADR-0065)")
{
    class Translator final : public QTranslator
    {
    public:
        Translator() { QCoreApplication::installTranslator(this); }
        ~Translator() override { QCoreApplication::removeTranslator(this); }

        [[nodiscard]] bool isEmpty() const override { return false; }

        [[nodiscard]] QString translate(const char* context, const char* sourceText, const char*, int) const override
        {
            if (std::string_view(context) != "ScriptApi") {
                return {};
            }
            return QStringLiteral("译：") + QString::fromUtf8(sourceText);
        }
    };

    int argc = 1;
    char name[] = "test";
    std::array<char*, 2> argv{name, nullptr};
    std::unique_ptr<QCoreApplication> app;
    if (QCoreApplication::instance() == nullptr) {
        app = std::make_unique<QCoreApplication>(argc, argv.data());
    }
    const Translator translator;

    const std::vector<CompletionItem> tap = complete(u"await touch.ta");
    REQUIRE(tap.size() == 1);
    CHECK(tap[0].label == QStringLiteral("tap"));
    CHECK(tap[0].detail == QStringLiteral("(x: number, y: number, durationMs?: number): Promise<void>"));
    CHECK(tap[0].documentation == QStringLiteral("译：Taps the picture at a position."));

    const QVariantList list = fixtureApi().list();
    const auto entry = std::ranges::find_if(list, [](const QVariant& value) {
        return value.toMap().value(QStringLiteral("label")).toString() == QStringLiteral("touch.tap");
    });
    REQUIRE(entry != list.end());
    CHECK(entry->toMap().value(QStringLiteral("documentation")).toString() ==
          QStringLiteral("译：Taps the picture at a position."));
    CHECK(entry->toMap().value(QStringLiteral("example")).toString() == QStringLiteral("await touch.tap(0.5, 0.8);"));
    CHECK(fixtureApi().find(u"touch.tap")->documentation == QStringLiteral("Taps the picture at a position."));
}

TEST_CASE("the file tree hides dot-names, node_modules and types and keeps folders first (M9)")
{
    CHECK_FALSE(shownInTree(u".git"));
    CHECK_FALSE(shownInTree(u".hidden.ts"));
    CHECK_FALSE(shownInTree(u"node_modules"));
    CHECK_FALSE(shownInTree(u"types"));
    CHECK_FALSE(shownInTree(u""));
    CHECK_FALSE(shownInTree(u"logic.blueprint.json"));
    CHECK(shownInTree(u"res"));
    CHECK(shownInTree(u"logic.ts"));

    CHECK(fileKindOf(u"logic.ts") == ScriptFileKind::Code);
    CHECK(fileKindOf(u"lib/a.mts") == ScriptFileKind::Code);
    CHECK(fileKindOf(u"old.JS") == ScriptFileKind::Code);
    CHECK(fileKindOf(u"res/chest.PNG") == ScriptFileKind::Image);
    CHECK(fileKindOf(u"profile.json") == ScriptFileKind::Profile);
    CHECK(fileKindOf(u"lib/profile.json") == ScriptFileKind::Text);
    CHECK(fileKindOf(u"README.md") == ScriptFileKind::Text);
    CHECK(fileKindOf(u"data.bin") == ScriptFileKind::Other);
    CHECK(fileKindOf(u"Makefile") == ScriptFileKind::Other);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    for (const char* name : {"profile.json", "logic.ts", "README.md", "tsconfig.json", "lib/helper.ts",
                             "lib/deep/more.ts", "res/chest.png", ".git/config", ".hidden.ts", "logic.blueprint.json",
                             "node_modules/pkg/index.js", "types/gamescrcpy.d.ts"}) {
        put(dir.filePath(QString::fromLatin1(name)), "x");
    }
    const std::vector<ScriptFile> files = scriptFiles(dir.path(), {});
    CHECK(pathsOf(files) == QStringList{QStringLiteral("lib"), QStringLiteral("lib/deep"),
                                        QStringLiteral("lib/deep/more.ts"), QStringLiteral("lib/helper.ts"),
                                        QStringLiteral("res"), QStringLiteral("res/chest.png"),
                                        QStringLiteral("logic.ts"), QStringLiteral("profile.json"),
                                        QStringLiteral("README.md"), QStringLiteral("tsconfig.json")});
    CHECK(fileAt(files, QStringLiteral("lib")).folder);
    CHECK(fileAt(files, QStringLiteral("lib")).open);
    CHECK(fileAt(files, QStringLiteral("lib")).depth == 0);
    CHECK(fileAt(files, QStringLiteral("lib/deep/more.ts")).depth == 2);
    CHECK(fileAt(files, QStringLiteral("lib/helper.ts")).name == QStringLiteral("helper.ts"));
    CHECK(fileAt(files, QStringLiteral("res/chest.png")).kind == ScriptFileKind::Image);
    CHECK(fileAt(files, QStringLiteral("profile.json")).kind == ScriptFileKind::Profile);
    CHECK_FALSE(fileAt(files, QStringLiteral("logic.ts")).folder);

    const std::vector<ScriptFile> folded = scriptFiles(dir.path(), QSet<QString>{QStringLiteral("lib")});
    CHECK(pathsOf(folded) == QStringList{QStringLiteral("lib"), QStringLiteral("res"), QStringLiteral("res/chest.png"),
                                         QStringLiteral("logic.ts"), QStringLiteral("profile.json"),
                                         QStringLiteral("README.md"), QStringLiteral("tsconfig.json")});
    CHECK_FALSE(fileAt(folded, QStringLiteral("lib")).open);

    QStringList watched = watchedFolders(dir.path());
    for (QString& path : watched) {
        path = QDir(dir.path()).relativeFilePath(path);
    }
    watched.sort();
    CHECK(watched == QStringList{QStringLiteral("."), QStringLiteral("lib"), QStringLiteral("lib/deep"),
                                 QStringLiteral("res")});
    CHECK(logicEntryIn(dir.path()) == QStringLiteral("logic.ts"));
}

TEST_CASE("problems come from the recorded gs check --json output, placed inside the profile folder (M9)")
{
    const CheckOutcome outcome =
        parseCheckOutput(contentsOf(QStringLiteral(GS_APP_TEST_DATA_DIR "/script-check.json")),
                         QStringLiteral("C:/Profiles/demo"));
    REQUIRE(outcome.ran);
    CHECK(outcome.failure.isEmpty());
    REQUIRE(outcome.problems.size() == 6);
    const ScriptProblem& erase = outcome.problems[0];
    CHECK(erase.file == QStringLiteral("src/main.ts"));
    CHECK(erase.inside);
    CHECK(erase.line == 1);
    CHECK(erase.column == 1);
    CHECK(erase.severity == QStringLiteral("error"));
    CHECK(erase.code == QStringLiteral("GS0001"));
    CHECK(erase.source == QStringLiteral("erase"));
    const ScriptProblem& types = outcome.problems[1];
    CHECK(types.line == 2);
    CHECK(types.column == 7);
    CHECK(types.code == QStringLiteral("TS2322"));
    CHECK(types.message == QStringLiteral("Type 'string' is not assignable to type 'number'."));
    const ScriptProblem& loop = outcome.problems[2];
    CHECK(loop.file == QStringLiteral("lib/loop.ts"));
    CHECK(loop.severity == QStringLiteral("warning"));
    CHECK(loop.code == QStringLiteral("GS-CHECK-101"));
    const ScriptProblem& absolute = outcome.problems[3];
    CHECK(absolute.file == QStringLiteral("logic.ts"));
    CHECK(absolute.inside);
    CHECK(absolute.message == QStringLiteral("Property 'tapp' does not exist on type 'typeof touch'. Did you mean 'tap'?"));
    const ScriptProblem& outside = outcome.problems[4];
    CHECK_FALSE(outside.inside);
    CHECK(outside.file == QStringLiteral("../../Temp/gs-check-1234/profile.d.ts"));
    const ScriptProblem& general = outcome.problems[5];
    CHECK_FALSE(general.inside);
    CHECK(general.file.isEmpty());
    CHECK(general.line == 0);
    CHECK(general.code == QStringLiteral("GS-CHECK-001"));

    const CheckOutcome clean = parseCheckOutput(QByteArrayLiteral("{\n  \"ok\": true,\n  \"problems\": []\n}\n"),
                                                QStringLiteral("C:/Profiles/demo"));
    CHECK(clean.ran);
    CHECK(clean.problems.empty());
    const CheckOutcome failed = parseCheckOutput(
        QByteArrayLiteral(R"({"ok": false, "error": "cannot create a temporary folder", "diagnostics": []})"),
        QStringLiteral("C:/Profiles/demo"));
    CHECK_FALSE(failed.ran);
    CHECK(failed.failure == QStringLiteral("cannot create a temporary folder"));
    const CheckOutcome garbled = parseCheckOutput(QByteArrayLiteral("usage: gs check <folder> [--json]\n"),
                                                  QStringLiteral("C:/Profiles/demo"));
    CHECK_FALSE(garbled.ran);
    CHECK(garbled.failure.isEmpty());
}

TEST_CASE("Add logic writes a starter logic.ts and the tsconfig.json of gs new --profile, and never overwrites (M9)")
{
    CHECK(starterLogic({}) == QStringLiteral("log.info(\"profile logic loaded\");\n"));
    CHECK(starterLogic({QStringLiteral("jump"), QStringLiteral("fire")}) ==
          QStringLiteral("log.info(\"profile logic loaded\");\n\ncontrols.jump.onPress(async () => {\n"
                         "    log.info(\"pressed\", controls.jump.name);\n});\n"));

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    put(dir.filePath(QStringLiteral("profile.json")), R"({"format":"gamescrcpy.profile","version":3})");
    const QStringList ids{QStringLiteral("dash-move"), QStringLiteral("jump")};
    CHECK(logicEntryIn(dir.path()).isEmpty());
    CHECK(writeStarterLogic(dir.path(), ids) == LogicWrite::Written);
    CHECK(logicEntryIn(dir.path()) == QStringLiteral("logic.ts"));
    const QByteArray logic = contentsOf(dir.filePath(QStringLiteral("logic.ts")));
    CHECK(logic == QByteArrayLiteral("log.info(\"profile logic loaded\");\n\ncontrols[\"dash-move\"].onPress(async () => {\n"
                                     "    log.info(\"pressed\", controls[\"dash-move\"].name);\n});\n"));
    const QByteArray tsconfig = contentsOf(dir.filePath(QStringLiteral("tsconfig.json")));
    CHECK(tsconfig == profileTsconfig());
    const QJsonObject config = QJsonDocument::fromJson(tsconfig).object();
    CHECK(config.value(QStringLiteral("include")).toArray() ==
          QJsonArray{QStringLiteral("**/*.ts"), QStringLiteral("**/*.mts")});
    CHECK(config.value(QStringLiteral("exclude")).toArray() == QJsonArray{QStringLiteral("node_modules")});
    const QJsonObject options = config.value(QStringLiteral("compilerOptions")).toObject();
    CHECK(options.value(QStringLiteral("target")).toString() == QStringLiteral("es2023"));
    CHECK(options.value(QStringLiteral("moduleDetection")).toString() == QStringLiteral("force"));
    CHECK(options.value(QStringLiteral("strict")).toBool());
    CHECK(options.value(QStringLiteral("noEmit")).toBool());
    CHECK(options.value(QStringLiteral("erasableSyntaxOnly")).toBool());
    CHECK(options.value(QStringLiteral("verbatimModuleSyntax")).toBool());
    CHECK(options.value(QStringLiteral("allowImportingTsExtensions")).toBool());
    CHECK(tsconfig.endsWith("}\n"));

    put(dir.filePath(QStringLiteral("logic.ts")), "log.info(\"mine\");\n");
    CHECK(writeStarterLogic(dir.path(), ids) == LogicWrite::Exists);
    CHECK(contentsOf(dir.filePath(QStringLiteral("logic.ts"))) == QByteArrayLiteral("log.info(\"mine\");\n"));

    QTemporaryDir own;
    REQUIRE(own.isValid());
    put(own.filePath(QStringLiteral("tsconfig.json")), "{}\n");
    put(own.filePath(QStringLiteral("logic.js")), "log.info(\"js\");\n");
    CHECK(writeStarterLogic(own.path(), {}) == LogicWrite::Exists);
    CHECK(logicEntryIn(own.path()) == QStringLiteral("logic.js"));
    QFile::remove(own.filePath(QStringLiteral("logic.js")));
    CHECK(writeStarterLogic(own.path(), {}) == LogicWrite::Written);
    CHECK(contentsOf(own.filePath(QStringLiteral("tsconfig.json"))) == QByteArrayLiteral("{}\n"));
    CHECK(contentsOf(own.filePath(QStringLiteral("logic.ts"))) == QByteArrayLiteral("log.info(\"profile logic loaded\");\n"));
}

TEST_CASE("ADR-0091 00:5x a component's new file starts with its key events, each keeping the component's own press")
{
    CHECK(controlStarter(QStringLiteral("jump"), QStringLiteral("Tap")) ==
          QStringLiteral("controls.jump.onPress(() => {\n}, { consume: false });\n"));
    CHECK(controlStarter(QStringLiteral("fire"), QStringLiteral("Hold")) ==
          QStringLiteral("controls.fire.onPress(() => {\n}, { consume: false });\n"
                         "controls.fire.onRelease(() => {\n}, { consume: false });\n"));
    CHECK(controlStarter(QStringLiteral("dash-move"), QStringLiteral("Swipe")) ==
          QStringLiteral("controls[\"dash-move\"].onPress(() => {\n}, { consume: false });\n"));
    CHECK(controlStarter(QStringLiteral("look"), QStringLiteral("Camera")).isEmpty());
    CHECK(controlStarter(QStringLiteral("wheel"), QStringLiteral("ActionWheel")).isEmpty());
}

TEST_CASE("a new file is a .ts module with a safe name inside the profile folder (M9)")
{
    QString problem;
    CHECK(checkedFileName(QStringLiteral("helper"), problem) == QStringLiteral("helper.ts"));
    CHECK(problem.isEmpty());
    CHECK(checkedFileName(QStringLiteral("lib/util.ts"), problem) == QStringLiteral("lib/util.ts"));
    CHECK(checkedFileName(QStringLiteral("  shared.mts "), problem) == QStringLiteral("shared.mts"));
    CHECK(checkedFileName(QStringLiteral("lib\\Tools"), problem) == QStringLiteral("lib/Tools.ts"));
    for (const char* refused : {"", "   ", "/abs", "../outside", "lib//x", "a:b", "what?", "dot.", ".hidden",
                                "types/x", "node_modules/x"}) {
        problem.clear();
        CHECK(checkedFileName(QString::fromLatin1(refused), problem).isEmpty());
        CHECK_FALSE(problem.isEmpty());
    }
}

TEST_CASE("the console keeps the last 1000 lines of each phone and clears only the shown logic's lines (M9)")
{
    LogicConsole console;
    const QString folder = QStringLiteral("C:/Profiles/Demo");
    const QString other = QStringLiteral("C:/Profiles/Other");
    for (int i = 0; i < 1005; ++i) {
        console.append(QStringLiteral("phone-a"), LogicReport{.text = QString::number(i), .folder = folder});
    }
    console.append(QStringLiteral("phone-b"), LogicReport{.text = QStringLiteral("elsewhere"), .folder = folder});
    const std::vector<LogicReport> lines = console.lines(QStringLiteral("phone-a"), QStringLiteral("c:\\profiles\\demo\\"));
    REQUIRE(lines.size() == LogicConsole::kMostLines);
    CHECK(lines.front().text == QStringLiteral("5"));
    CHECK(lines.back().text == QStringLiteral("1004"));

    console.append(QStringLiteral("phone-a"), LogicReport{.text = QStringLiteral("other logic"), .folder = other});
    CHECK(console.size(QStringLiteral("phone-a")) == LogicConsole::kMostLines);
    CHECK(console.lines(QStringLiteral("phone-a"), folder).size() == LogicConsole::kMostLines - 1);
    CHECK(console.lines(QStringLiteral("phone-a"), other).size() == 1);

    console.clear(QStringLiteral("phone-a"), folder);
    CHECK(console.lines(QStringLiteral("phone-a"), folder).empty());
    CHECK(console.lines(QStringLiteral("phone-a"), other).size() == 1);
    CHECK(console.lines(QStringLiteral("phone-b"), folder).size() == 1);
    CHECK(console.lines(QStringLiteral("phone-c"), folder).empty());
    CHECK(console.size(QStringLiteral("phone-c")) == 0);

    CHECK(sameFolder(QStringLiteral("D:/a/B"), QStringLiteral("d:\\A\\b")));
    CHECK_FALSE(sameFolder(QStringLiteral("D:/a/B"), QStringLiteral("D:/a/C")));
    CHECK_FALSE(sameFolder(QString(), QString()));
}

TEST_CASE("ADR-0091: the code that uses a component in logic names it the way the logic reaches it, with the cursor inside")
{
    const gs::app::LogicSnippet plain = gs::app::referenceSnippet(QStringLiteral("jump"));
    CHECK(plain.text == QStringLiteral("controls.jump.onPress(async () => {\n    \n});\n"));
    CHECK(plain.text.left(plain.caret).endsWith(QStringLiteral("{\n    ")));
    CHECK(gs::app::referenceSnippet(QStringLiteral("fire-2")).text.startsWith(QStringLiteral("controls[\"fire-2\"].onPress(")));
}

TEST_CASE("ADR-0091: a task folder's task.ts is its logic entry")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QFile task(QDir(dir.path()).filePath(QStringLiteral("task.ts")));
    REQUIRE(task.open(QIODevice::WriteOnly));
    task.write("export default class A extends Task { async run() {} }\n");
    task.close();
    CHECK(gs::app::logicEntryIn(dir.path()) == QStringLiteral("task.ts"));
}

TEST_CASE("check messages that the program knows are translated, others are kept (issue 28)")
{
    CHECK(gs::app::localizedCheckMessage(QStringLiteral("GS-CHECK-004"), QStringLiteral("the file cannot be read")) ==
          QStringLiteral("the file cannot be read"));
    CHECK(gs::app::localizedCheckMessage(QStringLiteral("TS2322"), QStringLiteral("anything")) ==
          QStringLiteral("anything"));
    CHECK(gs::app::localizedCheckMessage(QStringLiteral("GS0100"), QStringLiteral("expected ')'")) ==
          QStringLiteral("Syntax error: expected ')'"));
}
