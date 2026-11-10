#include "McpBlueprint.h"

#include "BlueprintLabels.h"
#include "BlueprintModel.h"
#include "QtConversions.h"
#include "ScriptWorkbenchFiles.h"

#include <gs/blueprint/Document.h>
#include <gs/blueprint/Layout.h>
#include <gs/blueprint/Text.h>
#include <gs/platform/AtomicFile.h>

#include <nlohmann/json.hpp>

#include <QDir>
#include <QJsonDocument>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace gs::app {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMostCodeBytes = std::size_t{8} << 20;
constexpr std::size_t kMostLayoutBytes = std::size_t{8} << 20;
constexpr qsizetype kMostCatalog = 60;
constexpr qsizetype kMostOperations = 200;

class EnglishLabels final : public blueprint::Labels
{
public:
    [[nodiscard]] std::string text(std::string_view key) const override { return m_labels.english(key); }
    [[nodiscard]] std::string english(std::string_view key) const override { return m_labels.english(key); }
    [[nodiscard]] std::string keyName(std::string_view key) const override { return std::string(key); }

private:
    BlueprintLabels m_labels;
};

const EnglishLabels& englishLabels()
{
    static const EnglishLabels labels;
    return labels;
}

QString qs(std::string_view text)
{
    return toQString(text);
}

fs::path toPath(const QString& text)
{
    return {text.toStdWString()};
}

fs::path utf8Path(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

QJsonValue jsonValueOf(const nlohmann::json& value)
{
    const std::string text = "[" + value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "]";
    const QJsonArray wrapped = QJsonDocument::fromJson(QByteArray::fromStdString(text)).array();
    return wrapped.isEmpty() ? QJsonValue() : wrapped.at(0);
}

nlohmann::json jsonOf(const QJsonValue& value)
{
    const QByteArray text = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    const nlohmann::json parsed = nlohmann::json::parse(text.toStdString(), nullptr, false);
    return parsed.is_array() && !parsed.empty() ? parsed.front() : nlohmann::json();
}

struct Code {
    std::string text;
    bool bom = false;
    bool found = false;
};

Code readCode(const fs::path& file)
{
    Code code;
    std::string contents;
    if (platform::readFile(file, kMostCodeBytes, contents, platform::WhenBusy::GiveUp)) {
        return code;
    }
    if (contents.starts_with("\xEF\xBB\xBF")) {
        code.bom = true;
        contents.erase(0, 3);
    }
    code.text = std::move(contents);
    code.found = true;
    return code;
}

bool componentFile(const QString& file)
{
    const QStringList segments = file.split(u'/');
    return segments.size() == 2 && segments.front() == u"controls" && segments.back().endsWith(u".ts");
}

Code codeOf(const fs::path& folder, const QString& file)
{
    Code code = readCode(folder / toPath(file));
    std::error_code error;
    if (!code.found && componentFile(file) && !fs::exists(folder / toPath(file), error)) {
        code.found = true;
    }
    return code;
}

blueprint::Layout readLayout(const fs::path& file)
{
    std::string contents;
    if (platform::readFile(file, kMostLayoutBytes, contents, platform::WhenBusy::GiveUp)) {
        return {};
    }
    return blueprint::parseLayout(contents);
}

blueprint::Environment environmentFor(const fs::path& folder, const QString& file)
{
    blueprint::Environment environment = blueprintEnvironment(folder, file.toStdString(), &englishLabels());
    for (const ScriptFile& each : scriptFiles(QString::fromStdWString(folder.wstring()), {})) {
        if (!each.folder && each.kind == ScriptFileKind::Code) {
            environment.files.push_back(each.path.toStdString());
        }
    }
    return environment;
}

QJsonObject pinJson(const blueprint::GraphPin& pin, bool input)
{
    QJsonObject item{{QStringLiteral("name"), qs(pin.name)}, {QStringLiteral("type"), qs(pin.type)}};
    if (!pin.label.empty() && pin.label != pin.name) {
        item.insert(QStringLiteral("label"), qs(pin.label));
    }
    if (pin.connected) {
        item.insert(QStringLiteral("connected"), true);
    }
    if (!input) {
        return item;
    }
    if (pin.raw != nullptr && !pin.raw->is_null()) {
        item.insert(QStringLiteral("value"), jsonValueOf(*pin.raw));
    } else if (!pin.value.empty()) {
        item.insert(QStringLiteral("value"), qs(pin.value));
    }
    if (pin.optional) {
        item.insert(QStringLiteral("optional"), true);
    }
    if (!pin.choices.empty()) {
        QJsonArray choices;
        for (const blueprint::GraphChoice& choice : pin.choices) {
            choices.append(qs(choice.value));
        }
        item.insert(QStringLiteral("choices"), choices);
    }
    return item;
}

QJsonObject pageJson(const blueprint::Document& document, std::string_view page)
{
    const blueprint::Graph graph = document.view(page);
    QJsonArray nodes;
    for (const blueprint::GraphNode& node : graph.nodes) {
        QJsonObject item{{QStringLiteral("key"), qs(node.key)},
                         {QStringLiteral("kind"), qs(node.kind)},
                         {QStringLiteral("title"), qs(node.title)},
                         {QStringLiteral("x"), node.x},
                         {QStringLiteral("y"), node.y}};
        if (!node.badge.empty()) {
            item.insert(QStringLiteral("badge"), qs(node.badge));
        }
        if (!node.subtitle.empty()) {
            item.insert(QStringLiteral("subtitle"), qs(node.subtitle));
        }
        if (!node.note.empty()) {
            item.insert(QStringLiteral("note"), qs(node.note));
        }
        QJsonArray inputs;
        for (const blueprint::GraphPin& pin : node.inputs) {
            inputs.append(pinJson(pin, true));
        }
        QJsonArray outputs;
        for (const blueprint::GraphPin& pin : node.outputs) {
            outputs.append(pinJson(pin, false));
        }
        item.insert(QStringLiteral("inputs"), inputs);
        item.insert(QStringLiteral("outputs"), outputs);
        if (!node.code.empty()) {
            item.insert(QStringLiteral("code"), qs(node.code));
        }
        if (node.floating) {
            item.insert(QStringLiteral("floating"), true);
        }
        if (node.start) {
            item.insert(QStringLiteral("start"), true);
        }
        if (!node.problem.empty()) {
            item.insert(QStringLiteral("problem"), qs(node.problem));
        }
        nodes.append(item);
    }
    QJsonArray wires;
    for (const blueprint::GraphWire& wire : graph.wires) {
        QJsonObject item{{QStringLiteral("key"), qs(wire.key)},
                         {QStringLiteral("from"), qs(wire.from)},
                         {QStringLiteral("fromPin"), qs(wire.fromPin)},
                         {QStringLiteral("to"), qs(wire.to)},
                         {QStringLiteral("toPin"), qs(wire.toPin)},
                         {QStringLiteral("type"), qs(wire.type)}};
        if (!wire.label.empty()) {
            item.insert(QStringLiteral("label"), qs(wire.label));
        }
        if (wire.dashed) {
            item.insert(QStringLiteral("dashed"), true);
        }
        wires.append(item);
    }
    QJsonArray comments;
    for (const blueprint::CommentBox& box : graph.comments) {
        comments.append(QJsonObject{{QStringLiteral("key"), qs(box.key)},
                                    {QStringLiteral("x"), box.x},
                                    {QStringLiteral("y"), box.y},
                                    {QStringLiteral("width"), box.width},
                                    {QStringLiteral("height"), box.height},
                                    {QStringLiteral("title"), qs(box.title)},
                                    {QStringLiteral("color"), qs(box.color)}});
    }
    QJsonObject result{{QStringLiteral("page"), qs(graph.key)},
                       {QStringLiteral("kind"), qs(graph.kind)},
                       {QStringLiteral("title"), qs(graph.title)},
                       {QStringLiteral("nodes"), nodes},
                       {QStringLiteral("wires"), wires},
                       {QStringLiteral("comments"), comments}};
    if (graph.readOnly) {
        result.insert(QStringLiteral("readOnly"), true);
    }
    return result;
}

QJsonArray pagesJson(const blueprint::Document& document)
{
    QJsonArray pages;
    for (const auto& page : document.pages()) {
        pages.append(QJsonObject{{QStringLiteral("page"), qs(page.value("key", std::string()))},
                                 {QStringLiteral("title"), qs(page.value("title", std::string()))},
                                 {QStringLiteral("kind"), qs(page.value("kind", std::string()))}});
    }
    return pages;
}

QString drawProblem(const blueprint::Document& document)
{
    return QStringLiteral("%1 (line %2)").arg(qs(document.problem())).arg(document.problemLine());
}

}

QString mcpBlueprintFile(const std::filesystem::path& folder, const QString& given, QString& problem)
{
    QString file = QDir::fromNativeSeparators(given.trimmed());
    if (file.isEmpty()) {
        file = logicEntryIn(QString::fromStdWString(folder.wstring()));
        if (file.isEmpty()) {
            problem = QStringLiteral("the folder has no logic.ts, task.ts or component.ts; give file");
            return {};
        }
    }
    const QStringList segments = file.split(u'/');
    const bool unsafe = QDir::isAbsolutePath(file) || file.contains(u':') || std::ranges::any_of(segments, [](const QString& segment) {
                            return segment.isEmpty() || segment == u"." || segment == u".." || !shownInTree(segment);
                        });
    if (unsafe || fileKindOf(file) != ScriptFileKind::Code) {
        problem = QStringLiteral("%1 is not a code file inside the folder").arg(given);
        return {};
    }
    std::error_code error;
    if (!fs::is_regular_file(folder / toPath(file), error) && !componentFile(file)) {
        problem = QStringLiteral("there is no file %1 in the folder").arg(file);
        return {};
    }
    return file;
}

QJsonObject mcpReadBlueprint(const std::filesystem::path& folder, const QString& file, const QString& page,
                             const std::optional<QString>& catalog, QString& problem)
{
    const Code code = codeOf(folder, file);
    if (!code.found) {
        problem = QStringLiteral("%1 cannot be read").arg(file);
        return {};
    }
    const blueprint::Environment environment = environmentFor(folder, file);
    const blueprint::Document document(code.text, environment,
                                       readLayout(folder / utf8Path(blueprint::layoutFileName(file.toStdString()))));
    const std::string wanted = page.toStdString();
    if (!page.isEmpty() && !document.hasPage(wanted)) {
        problem = QStringLiteral("there is no page %1 in %2; read the blueprint without page to list them").arg(page, file);
        return {};
    }
    QJsonObject result{{QStringLiteral("file"), file}, {QStringLiteral("pages"), pagesJson(document)}};
    if (!document.ok()) {
        result.insert(QStringLiteral("problem"), drawProblem(document));
    }
    QJsonArray graphs;
    if (page.isEmpty()) {
        for (const auto& listed : document.pages()) {
            graphs.append(pageJson(document, listed.value("key", std::string())));
        }
    } else {
        graphs.append(pageJson(document, wanted));
    }
    result.insert(QStringLiteral("graphs"), graphs);
    if (catalog) {
        QJsonArray entries;
        for (const auto& entry : document.catalog(wanted, {}, {}, catalog->toStdString())) {
            if (entries.size() >= kMostCatalog) {
                break;
            }
            entries.append(QJsonObject{{QStringLiteral("id"), qs(entry.value("id", std::string()))},
                                       {QStringLiteral("title"), qs(entry.value("title", std::string()))},
                                       {QStringLiteral("call"), qs(entry.value("subtitle", std::string()))},
                                       {QStringLiteral("group"), qs(entry.value("group", std::string()))}});
        }
        result.insert(QStringLiteral("catalog"), entries);
    }
    return result;
}

McpBlueprintEdit mcpEditBlueprint(const std::filesystem::path& folder, const QString& file,
                                  const QJsonArray& operations)
{
    McpBlueprintEdit edit;
    const auto refuse = [&edit](const QString& problem) {
        edit.code = QStringLiteral("GS-MCP-016");
        edit.problem = problem;
        return edit;
    };
    if (operations.isEmpty() || operations.size() > kMostOperations) {
        return refuse(QStringLiteral("operations must hold from 1 to %1 operations").arg(kMostOperations));
    }
    const Code code = codeOf(folder, file);
    if (!code.found) {
        return refuse(QStringLiteral("%1 cannot be read").arg(file));
    }
    const blueprint::Environment environment = environmentFor(folder, file);
    const std::string layoutName = blueprint::layoutFileName(file.toStdString());
    auto document = std::make_unique<blueprint::Document>(code.text, environment, readLayout(folder / utf8Path(layoutName)));
    if (!document->ok()) {
        return refuse(QStringLiteral("%1 cannot be drawn: %2; fix the code first").arg(file, drawProblem(*document)));
    }
    std::vector<std::string> keys;
    std::vector<std::string> signatures;
    std::string shownPage;
    for (qsizetype index = 0; index < operations.size(); ++index) {
        const QJsonObject operation = operations.at(index).toObject();
        const QString op = operation.value(QStringLiteral("op")).toString();
        const std::string page = operation.value(QStringLiteral("page")).toString().toStdString();
        QString wrong;
        const auto keyFrom = [&](const QString& given) -> std::string {
            if (!given.startsWith(u'$')) {
                return given.toStdString();
            }
            bool number = false;
            const qsizetype at = given.mid(1).toLongLong(&number);
            if (!number || at < 0 || at >= index || keys[static_cast<std::size_t>(at)].empty()) {
                wrong = QStringLiteral("%1 refers to no block made by an earlier operation").arg(given);
                return {};
            }
            const std::string& signature = signatures[static_cast<std::size_t>(at)];
            const std::string found = signature.empty() ? std::string() : document->find(signature);
            return found.empty() ? keys[static_cast<std::size_t>(at)] : found;
        };
        const auto keyOf = [&](const char* name) { return keyFrom(operation.value(QLatin1StringView(name)).toString()); };
        const auto textOf = [&](const char* name) {
            return operation.value(QLatin1StringView(name)).toString().toStdString();
        };
        const auto keysOf = [&](const char* name) {
            std::vector<std::string> list;
            for (const QJsonValue& value : operation.value(QLatin1StringView(name)).toArray()) {
                list.push_back(keyFrom(value.toString()));
            }
            if (list.empty()) {
                wrong = QStringLiteral("%1 must list one or more block keys").arg(QLatin1StringView(name));
            }
            return list;
        };
        blueprint::Outcome outcome;
        bool needsKey = false;
        if (op == u"add") {
            outcome = document->add(page, textOf("id"), operation.value(QStringLiteral("x")).toDouble(40.0),
                                    operation.value(QStringLiteral("y")).toDouble(40.0), keyOf("from"), textOf("fromPin"));
            needsKey = true;
        } else if (op == u"remove") {
            outcome = document->remove(page, keysOf("nodes"));
        } else if (op == u"connect") {
            outcome = document->connect(keyOf("from"), textOf("fromPin"), keyOf("to"), textOf("toPin"));
        } else if (op == u"disconnect") {
            outcome = operation.contains(QStringLiteral("wire")) ? document->removeWire(textOf("wire"))
                                                                 : document->disconnect(keyOf("node"), textOf("pin"));
        } else if (op == u"setValue") {
            outcome = document->setValue(keyOf("node"), textOf("pin"), jsonOf(operation.value(QStringLiteral("value"))));
        } else if (op == u"setCode") {
            outcome = document->setCode(keyOf("node"), textOf("code"));
        } else if (op == u"setDetail") {
            outcome = document->editDetail(page, keyOf("node"), textOf("name"),
                                           jsonOf(operation.value(QStringLiteral("value"))));
        } else if (op == u"move") {
            outcome = document->move(page, keysOf("nodes"), operation.value(QStringLiteral("dx")).toDouble(),
                                     operation.value(QStringLiteral("dy")).toDouble());
        } else if (op == u"comment") {
            outcome = document->addComment(page, keysOf("nodes"));
            needsKey = true;
        } else if (op == u"editComment") {
            nlohmann::json changes = nlohmann::json::object();
            for (const char* name : {"title", "color"}) {
                if (operation.value(QLatin1StringView(name)).isString()) {
                    changes[name] = textOf(name);
                }
            }
            for (const char* name : {"x", "y", "width", "height"}) {
                if (operation.value(QLatin1StringView(name)).isDouble()) {
                    changes[name] = operation.value(QLatin1StringView(name)).toDouble();
                }
            }
            outcome = document->editComment(page, keyOf("comment"), changes);
            needsKey = true;
        } else if (op == u"arrange") {
            outcome = document->arrange(page);
        } else {
            wrong = QStringLiteral("op must be add, remove, connect, disconnect, setValue, setCode, setDetail, move, "
                                   "comment, editComment or arrange");
        }
        if (!wrong.isEmpty()) {
            return refuse(QStringLiteral("operation %1 (%2): %3").arg(index).arg(op, wrong));
        }
        if (!outcome.ok()) {
            return refuse(QStringLiteral("operation %1 (%2) was refused: %3").arg(index).arg(op, qs(outcome.reason)));
        }
        if (needsKey && outcome.key.empty()) {
            return refuse(QStringLiteral("operation %1 (%2) changed nothing; check the id, the keys and the page")
                              .arg(index)
                              .arg(op));
        }
        const std::string source =
            outcome.changes.empty() ? document->source() : blueprint::applied(document->source(), outcome.changes);
        auto next = std::make_unique<blueprint::Document>(source, environment, outcome.layout, document.get());
        document = std::move(next);
        if (!document->ok()) {
            return refuse(QStringLiteral("operation %1 (%2) left code that cannot be drawn: %3")
                              .arg(index)
                              .arg(op, drawProblem(*document)));
        }
        keys.push_back(outcome.key);
        signatures.push_back(outcome.signature);
        if (op == u"comment" && (operation.value(QStringLiteral("title")).isString() ||
                                 operation.value(QStringLiteral("color")).isString())) {
            nlohmann::json changes = nlohmann::json::object();
            for (const char* name : {"title", "color"}) {
                if (operation.value(QLatin1StringView(name)).isString()) {
                    changes[name] = textOf(name);
                }
            }
            const blueprint::Outcome titled = document->editComment(page, outcome.key, changes);
            auto renamed = std::make_unique<blueprint::Document>(document->source(), environment, titled.layout,
                                                                 document.get());
            document = std::move(renamed);
        }
        shownPage = page;
    }
    edit.codeChanged = document->source() != code.text;
    edit.source = QByteArray::fromStdString(document->source());
    if (code.bom) {
        edit.source.prepend("\xEF\xBB\xBF");
    }
    edit.layout = QByteArray::fromStdString(blueprint::writeLayout(document->layout()));
    edit.layoutFile = qs(layoutName);
    QJsonArray made;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const std::string found = signatures[index].empty() ? std::string() : document->find(signatures[index]);
        const std::string& key = found.empty() ? keys[index] : found;
        made.append(key.empty() ? QJsonValue() : QJsonValue(qs(key)));
    }
    edit.summary = QJsonObject{{QStringLiteral("file"), file},
                               {QStringLiteral("operations"), static_cast<int>(operations.size())},
                               {QStringLiteral("codeChanged"), edit.codeChanged},
                               {QStringLiteral("keys"), made},
                               {QStringLiteral("graph"), pageJson(*document, shownPage)}};
    return edit;
}

}
