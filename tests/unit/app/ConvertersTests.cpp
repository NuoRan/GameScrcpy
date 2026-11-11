#include "Converters.h"
#include "McpTestSupport.h"
#include "ProfileLibrary.h"
#include "TaskLibrary.h"

#include <gs/input/Profile.h>

#include <doctest/doctest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace gs::app;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view kXyz = "name Racer\n"
                                  "tap jump Space bottomRight -0.2 -0.2\n"
                                  "tap map KeyM topRight -0.2 0.1\n";

constexpr std::string_view kConverter = R"x(export function convert(input: ConvertInput) {
    if (input.from === "task" || input.from === "recording") {
        const told = input.from === "task" ? input.text.slice(0, 40) : input.json().format;
        return { name: "made.txt", text: input.name + "|" + told };
    }
    if (input.to === "profile") {
        let name = "";
        const controls = [];
        for (const line of input.text.split("\n")) {
            const words = line.trim().split(" ").filter((word) => word !== "");
            if (words.length === 0) {
                continue;
            }
            if (words[0] === "name") {
                name = words.slice(1).join(" ");
            } else if (words[0] === "tap") {
                controls.push({ id: words[1], type: "tap", anchor: words[3], offset: [Number(words[4]), Number(words[5])],
                    inputs: { press: words[2] } });
            } else {
                throw new Error("the line " + words[0] + " is not XYZ");
            }
        }
        return { format: "gamescrcpy.profile", version: 3, meta: { name }, controls };
    }
    const profile = input.json();
    const lines = ["name " + profile.meta.name];
    for (const control of profile.controls) {
        lines.push(["tap", control.id, control.inputs.press, control.anchor, control.offset[0], control.offset[1]].join(" "));
    }
    return { name: profile.meta.name + ".xyz", text: lines.join("\n") + "\n" };
}
)x";

class Folder
{
public:
    Folder() { REQUIRE(m_dir.isValid()); }

    QString write(const QString& name, std::string_view contents) const
    {
        REQUIRE(QDir(m_dir.path()).mkpath(QFileInfo(m_dir.filePath(name)).path()));
        QFile file(m_dir.filePath(name));
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(contents.data(), static_cast<qint64>(contents.size())) == static_cast<qint64>(contents.size()));
        return file.fileName();
    }

    [[nodiscard]] QString file(const QString& name) const { return m_dir.filePath(name); }
    [[nodiscard]] std::filesystem::path path() const { return m_dir.path().toStdWString(); }

private:
    QTemporaryDir m_dir;
};

QByteArray readAll(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

ConverterEntry entryOf(const Folder& extension, const char* name, const char* from, const char* to,
                       QStringList extensions, std::string script = "convert/xyz.ts")
{
    return ConverterEntry{.extensionId = QStringLiteral("alice.formats"),
                          .extensionName = QStringLiteral("Formats"),
                          .name = QString::fromLatin1(name),
                          .title = QStringLiteral("XYZ ") + QString::fromLatin1(name),
                          .from = QString::fromLatin1(from),
                          .to = QString::fromLatin1(to),
                          .extensions = std::move(extensions),
                          .folder = extension.path(),
                          .script = std::move(script),
                          .permissions = {}};
}

class Results
{
public:
    explicit Results(Converters& converters)
    {
        QObject::connect(&converters, &Converters::imported, &m_context,
                         [this](int ticket, const QVariantMap& result) { m_imports.insert_or_assign(ticket, result); });
        QObject::connect(&converters, &Converters::exported, &m_context,
                         [this](int ticket, bool ok, const QString& message) {
                             m_exports.insert_or_assign(ticket, std::pair{ok, message});
                         });
    }

    [[nodiscard]] std::optional<QVariantMap> imported(int ticket)
    {
        if (!gs::test::waitUntil([this, ticket] { return m_imports.contains(ticket); }, 90s)) {
            return std::nullopt;
        }
        return m_imports.at(ticket);
    }

    [[nodiscard]] std::optional<std::pair<bool, QString>> exported(int ticket)
    {
        if (!gs::test::waitUntil([this, ticket] { return m_exports.contains(ticket); }, 90s)) {
            return std::nullopt;
        }
        return m_exports.at(ticket);
    }

private:
    QObject m_context;
    std::map<int, QVariantMap> m_imports;
    std::map<int, std::pair<bool, QString>> m_exports;
};

QString reasonOf(const QVariantMap& result)
{
    const QVariantList failed = result.value(QStringLiteral("failed")).toList();
    return failed.isEmpty() ? QString() : failed.front().toMap().value(QStringLiteral("reason")).toString();
}

std::optional<ConvertOutcome> runAndWait(Converters& converters, const QString& key, ConvertInput input)
{
    std::optional<ConvertOutcome> outcome;
    converters.run(key, std::move(input), [&outcome](const ConvertOutcome& made) { outcome = made; });
    if (!gs::test::waitUntil([&outcome] { return outcome.has_value(); }, 90s)) {
        return std::nullopt;
    }
    return outcome;
}

}

TEST_CASE("ADR-0112: a converter script turns a made-up format into a profile and back in the logic sandbox")
{
    gs::test::McpApplication application;
    Folder extension;
    Folder library;
    Folder files;
    Folder out;
    extension.write(QStringLiteral("convert/xyz.ts"), kConverter);
    ProfileLibrary profiles(library.path());
    Converters converters(nullptr);
    converters.setConverters({entryOf(extension, "in", "xyz", "profile", {QStringLiteral("xyz")}),
                              entryOf(extension, "out", "profile", "xyz", {})});
    Results results(converters);

    const QString racer = files.write(QStringLiteral("racer.xyz"), kXyz);
    const int ticket = converters.importProfiles({QUrl::fromLocalFile(racer)});
    CHECK(converters.busy());
    const std::optional<QVariantMap> imported = results.imported(ticket);
    REQUIRE(imported.has_value());
    INFO(reasonOf(*imported).toStdString());
    CHECK(imported->value(QStringLiteral("imported")).toStringList() == QStringList{QStringLiteral("Racer")});
    CHECK(imported->value(QStringLiteral("failed")).toList().isEmpty());
    const QStringList ids = imported->value(QStringLiteral("ids")).toStringList();
    REQUIRE(ids.size() == 1);
    CHECK(ids.front() == u"racer");
    const std::shared_ptr<const gs::input::Profile> profile = profiles.profile(ids.front());
    REQUIRE(profile != nullptr);
    CHECK(profile->meta.name == "Racer");
    REQUIRE(profile->controls.size() == 2);
    CHECK(profile->controls[0].id == "jump");
    CHECK(profile->controls[1].id == "map");
    CHECK(gs::test::waitUntil([&converters] { return !converters.busy(); }));

    const QString target = out.file(QStringLiteral("racer-again.xyz"));
    const int sent = converters.exportTo(QStringLiteral("alice.formats/out"), ids.front(), target);
    const std::optional<std::pair<bool, QString>> exported = results.exported(sent);
    REQUIRE(exported.has_value());
    INFO(exported->second.toStdString());
    CHECK(exported->first);
    CHECK(exported->second == u"Exported as XYZ out: racer-again.xyz");
    CHECK(readAll(target) == QByteArray(kXyz.data(), static_cast<qsizetype>(kXyz.size())));

    const int refused = converters.exportTo(QStringLiteral("alice.formats/in"), ids.front(), target);
    const std::optional<std::pair<bool, QString>> wrong = results.exported(refused);
    REQUIRE(wrong.has_value());
    CHECK_FALSE(wrong->first);
}

TEST_CASE("ADR-0112: tasks and recordings go to export converters as their task.ts and their session.json")
{
    gs::test::McpApplication application;
    Folder extension;
    Folder tasksFolder;
    Folder recordings;
    Folder out;
    extension.write(QStringLiteral("convert/xyz.ts"), kConverter);
    TaskLibrary tasks(tasksFolder.path());
    Converters converters(nullptr);
    converters.setConverters({entryOf(extension, "task", "task", "txt", {}),
                              entryOf(extension, "recording", "recording", "txt", {})});
    Results results(converters);

    const QString task = tasks.add(QStringLiteral("Daily"));
    REQUIRE_FALSE(task.isEmpty());
    const QString taskTarget = out.file(QStringLiteral("task.txt"));
    const std::optional<std::pair<bool, QString>> taskDone =
        results.exported(converters.exportTo(QStringLiteral("alice.formats/task"), task, taskTarget));
    REQUIRE(taskDone.has_value());
    INFO(taskDone->second.toStdString());
    CHECK(taskDone->first);
    const QString taskText = QString::fromUtf8(readAll(taskTarget));
    CHECK(taskText.startsWith(task + QStringLiteral(".ts|")));
    CHECK(taskText.contains(QStringLiteral("export default class")));

    recordings.write(QStringLiteral("Pixel 9 2026-11-11.gsrec/session.json"),
                     R"({"format":"gamescrcpy.session","version":2})");
    const QString recordingTarget = out.file(QStringLiteral("recording.txt"));
    const std::optional<std::pair<bool, QString>> recordingDone = results.exported(converters.exportTo(
        QStringLiteral("alice.formats/recording"), recordings.file(QStringLiteral("Pixel 9 2026-11-11.gsrec")),
        recordingTarget));
    REQUIRE(recordingDone.has_value());
    INFO(recordingDone->second.toStdString());
    CHECK(recordingDone->first);
    CHECK(QString::fromUtf8(readAll(recordingTarget)) == u"Pixel 9 2026-11-11.json|gamescrcpy.session");

    const std::optional<std::pair<bool, QString>> missing = results.exported(converters.exportTo(
        QStringLiteral("alice.formats/recording"), recordings.file(QStringLiteral("gone.gsrec")), recordingTarget));
    REQUIRE(missing.has_value());
    CHECK_FALSE(missing->first);
}

TEST_CASE("ADR-0112: what a converter throws, a script without a converter, a wrong result and a result that is not a "
          "profile reach the import with their message")
{
    gs::test::McpApplication application;
    Folder extension;
    Folder library;
    Folder files;
    extension.write(QStringLiteral("convert/xyz.ts"), kConverter);
    extension.write(QStringLiteral("convert/none.ts"), "export const nothing = 1;\n");
    extension.write(QStringLiteral("convert/wrong.js"), "export default () => ({ bytes: \"nope\" });\n");
    extension.write(QStringLiteral("convert/other.js"), "export default () => ({ format: \"something.else\" });\n");
    ProfileLibrary profiles(library.path());
    Converters converters(nullptr);
    converters.setConverters({entryOf(extension, "xyz", "xyz", "profile", {QStringLiteral("xyz")}),
                              entryOf(extension, "none", "abc", "profile", {QStringLiteral("abc")}, "convert/none.ts"),
                              entryOf(extension, "wrong", "def", "profile", {QStringLiteral("def")}, "convert/wrong.js"),
                              entryOf(extension, "other", "ghi", "profile", {QStringLiteral("ghi")}, "convert/other.js")});
    Results results(converters);

    const auto reasonFor = [&](const QString& name, std::string_view contents) {
        const QString file = files.write(name, contents);
        const std::optional<QVariantMap> result = results.imported(converters.importProfiles({QUrl::fromLocalFile(file)}));
        REQUIRE(result.has_value());
        CHECK(result->value(QStringLiteral("imported")).toStringList().isEmpty());
        const QVariantList failed = result->value(QStringLiteral("failed")).toList();
        REQUIRE(failed.size() == 1);
        CHECK(failed.front().toMap().value(QStringLiteral("file")).toString() == name);
        return reasonOf(*result);
    };
    CHECK(reasonFor(QStringLiteral("broken.xyz"), "name Broken\nswipe left\n") == u"the line swipe is not XYZ");
    CHECK(reasonFor(QStringLiteral("plain.abc"), "anything").startsWith(QStringLiteral("GS-API-165")));
    CHECK(reasonFor(QStringLiteral("plain.def"), "anything").startsWith(QStringLiteral("GS-API-166")));
    CHECK(reasonFor(QStringLiteral("plain.ghi"), "anything") == u"This is not a GameScrcpy key mapping profile.");
    CHECK(profiles.all().empty());
}

TEST_CASE("ADR-0112: converters take at most 32 MiB in and out and stop at their time limit")
{
    gs::test::McpApplication application;
    Folder extension;
    Folder files;
    extension.write(QStringLiteral("convert/big.js"), "export default () => new Uint8Array(33 * 1024 * 1024);\n");
    extension.write(QStringLiteral("convert/stuck.js"), "export default () => new Promise(() => {});\n");
    extension.write(QStringLiteral("convert/echo.js"), "export default (input) => input.bytes;\n");
    Converters converters(nullptr);
    converters.setConverters({entryOf(extension, "big", "profile", "big", {}, "convert/big.js"),
                              entryOf(extension, "stuck", "profile", "stuck", {}, "convert/stuck.js"),
                              entryOf(extension, "echo", "profile", "echo", {}, "convert/echo.js"),
                              entryOf(extension, "read", "xyz", "profile", {QStringLiteral("xyz")}, "convert/echo.js")});

    const std::optional<ConvertOutcome> echoed =
        runAndWait(converters, QStringLiteral("alice.formats/echo"),
                   ConvertInput{.name = QStringLiteral("a.json"), .bytes = QByteArray(3 * 1024 * 1024 + 7, 'e')});
    REQUIRE(echoed.has_value());
    INFO(echoed->problem.toStdString());
    CHECK(echoed->ok);
    CHECK(echoed->bytes == QByteArray(3 * 1024 * 1024 + 7, 'e'));

    const std::optional<ConvertOutcome> big =
        runAndWait(converters, QStringLiteral("alice.formats/big"), ConvertInput{.name = QStringLiteral("a.json"), .bytes = "{}"});
    REQUIRE(big.has_value());
    CHECK_FALSE(big->ok);
    CHECK(big->problem.startsWith(QStringLiteral("GS-API-167")));

    const std::optional<ConvertOutcome> huge = runAndWait(
        converters, QStringLiteral("alice.formats/echo"),
        ConvertInput{.name = QStringLiteral("a.json"), .bytes = QByteArray(Converters::kMostBytes + 1, 'h')});
    REQUIRE(huge.has_value());
    CHECK_FALSE(huge->ok);
    CHECK(huge->problem == u"it is larger than 32 MiB");

    const QByteArray tooLarge(Converters::kMostBytes + 1, 'x');
    Results results(converters);
    const QString large = files.write(QStringLiteral("large.xyz"),
                                      std::string_view(tooLarge.constData(), static_cast<std::size_t>(tooLarge.size())));
    const std::optional<QVariantMap> refused = results.imported(converters.importProfiles({QUrl::fromLocalFile(large)}));
    REQUIRE(refused.has_value());
    CHECK(reasonOf(*refused) == u"it is larger than 32 MiB");

    converters.setTimeLimit(1500ms);
    const auto started = std::chrono::steady_clock::now();
    const std::optional<ConvertOutcome> stuck =
        runAndWait(converters, QStringLiteral("alice.formats/stuck"), ConvertInput{.name = QStringLiteral("a.json"), .bytes = "{}"});
    REQUIRE(stuck.has_value());
    CHECK_FALSE(stuck->ok);
    CHECK(stuck->problem.startsWith(QStringLiteral("it took longer than 2 second")));
    CHECK(std::chrono::steady_clock::now() - started >= 1500ms);
    CHECK_FALSE(converters.busy());

    const std::optional<ConvertOutcome> gone =
        runAndWait(converters, QStringLiteral("alice.formats/missing"), ConvertInput{.name = QStringLiteral("a.json"), .bytes = "{}"});
    REQUIRE(gone.has_value());
    CHECK_FALSE(gone->ok);
}

TEST_CASE("ADR-0112: files of a converter's types go to the converter, GameScrcpy's own profiles do not, and the "
          "import dialog lists the types")
{
    gs::test::McpApplication application;
    Folder extension;
    Folder files;
    extension.write(QStringLiteral("convert/xyz.ts"), kConverter);
    Converters converters(nullptr);
    std::vector<ConverterEntry> entries{entryOf(extension, "in", "xyz", "profile", {QStringLiteral("xyz"), QStringLiteral("xyzz")}),
                                        entryOf(extension, "json", "json", "profile", {QStringLiteral("json")}),
                                        entryOf(extension, "out", "profile", "xyz", {})};
    entries[1].title = QStringLiteral("Other JSON");
    converters.setConverters(entries);

    const QVariantList listed = converters.converters();
    REQUIRE(listed.size() == 3);
    CHECK(listed[0].toMap().value(QStringLiteral("key")).toString() == u"alice.formats/in");
    CHECK(listed[0].toMap().value(QStringLiteral("imports")).toBool());
    CHECK_FALSE(listed[2].toMap().value(QStringLiteral("imports")).toBool());
    CHECK(listed[2].toMap().value(QStringLiteral("extensionName")).toString() == u"Formats");

    const QUrl xyz = QUrl::fromLocalFile(files.write(QStringLiteral("a.XYZ"), kXyz));
    const QUrl text = QUrl::fromLocalFile(files.write(QStringLiteral("a.txt"), "text"));
    const QUrl own = QUrl::fromLocalFile(files.write(
        QStringLiteral("own.json"), R"({"format":"gamescrcpy.profile","version":3,"meta":{"name":"Own"},"controls":[]})"));
    const QUrl foreign = QUrl::fromLocalFile(files.write(QStringLiteral("foreign.json"), R"({"tool":"other"})"));
    const QUrl package = QUrl::fromLocalFile(files.write(QStringLiteral("a.gsprofile"), "zip"));
    CHECK(converters.accepts(QStringLiteral("profile"), xyz));
    CHECK_FALSE(converters.accepts(QStringLiteral("profile"), text));
    CHECK_FALSE(converters.accepts(QStringLiteral("task"), xyz));
    CHECK_FALSE(converters.accepts(QStringLiteral("profile"), QUrl::fromLocalFile(extension.file(QStringLiteral("convert")))));
    CHECK(converters.importerFor(QStringLiteral("profile"), xyz) == u"alice.formats/in");
    CHECK(converters.importerFor(QStringLiteral("profile"), text).isEmpty());
    CHECK(converters.importerFor(QStringLiteral("profile"), own).isEmpty());
    CHECK(converters.importerFor(QStringLiteral("profile"), foreign) == u"alice.formats/json");
    CHECK(converters.importerFor(QStringLiteral("profile"), package).isEmpty());
    CHECK(converters.importerFor(QStringLiteral("profile"), text, QStringLiteral("alice.formats/in")) == u"alice.formats/in");
    CHECK(converters.importerFor(QStringLiteral("profile"), xyz, QStringLiteral("alice.formats/out")).isEmpty());

    const QString ownFilter = QStringLiteral("Key mapping profiles (*.json *.gsprofile)");
    CHECK(converters.importFilters(QStringLiteral("profile"), ownFilter) ==
          QStringList{QStringLiteral("All files that can be imported (*.json *.gsprofile *.xyz *.xyzz)"), ownFilter,
                      QStringLiteral("XYZ in (*.xyz *.xyzz)"), QStringLiteral("Other JSON (*.json)")});
    CHECK(converters.importFilters(QStringLiteral("task"), ownFilter) == QStringList{ownFilter});
    CHECK(converters.filterConverter(QStringLiteral("profile"), 0).isEmpty());
    CHECK(converters.filterConverter(QStringLiteral("profile"), 1).isEmpty());
    CHECK(converters.filterConverter(QStringLiteral("profile"), 2) == u"alice.formats/in");
    CHECK(converters.filterConverter(QStringLiteral("profile"), 3) == u"alice.formats/json");
    CHECK(converters.filterConverter(QStringLiteral("profile"), 4).isEmpty());
}
