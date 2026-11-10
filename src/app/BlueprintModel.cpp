#include "BlueprintModel.h"

#include "DraftDiff.h"
#include "KeyLabels.h"
#include "NativeExtensions.h"
#include "QtConversions.h"
#include "ScriptWorkbenchApi.h"

#include <gs/blueprint/Document.h>
#include <gs/blueprint/Instrument.h>
#include <gs/blueprint/Layout.h>
#include <gs/blueprint/Text.h>
#include <gs/input/Profile.h>
#include <gs/platform/AtomicFile.h>
#include <gs/script/ApiFiles.h>
#include <gs/script/Resources.h>

#include <nlohmann/json.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJSValue>
#include <QJsonDocument>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <utility>
#include <variant>

namespace gs::app {

namespace {

constexpr std::size_t kMostProfileBytes = std::size_t{1} << 20;
constexpr std::size_t kMostLayoutBytes = std::size_t{8} << 20;
constexpr std::size_t kMostLibraryBytes = std::size_t{4} << 20;
constexpr std::size_t kMostTransitions = 50;

QString recordingClock(double ms)
{
    const qint64 tenths = std::max<qint64>(0, static_cast<qint64>(std::llround(ms / 100.0)));
    return QStringLiteral("%1:%2.%3")
        .arg(tenths / 600, 2, 10, QChar(u'0'))
        .arg((tenths / 10) % 60, 2, 10, QChar(u'0'))
        .arg(tenths % 10);
}

QString qs(std::string_view text)
{
    return toQString(text);
}

std::string utf8(const QString& text)
{
    return text.toStdString();
}

std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdWString());
}

QString say(const char* english)
{
    return QCoreApplication::translate("Blueprint", english);
}

template <class Json>
QVariant variantOf(const Json& value)
{
    using Type = nlohmann::json::value_t;
    switch (value.type()) {
    case Type::boolean:
        return value.template get<bool>();
    case Type::number_integer:
        return static_cast<double>(value.template get<std::int64_t>());
    case Type::number_unsigned:
        return static_cast<double>(value.template get<std::uint64_t>());
    case Type::number_float:
        return value.template get<double>();
    case Type::string:
        return qs(value.template get_ref<const std::string&>());
    case Type::array: {
        QVariantList list;
        list.reserve(static_cast<qsizetype>(value.size()));
        for (const auto& item : value) {
            list.push_back(variantOf(item));
        }
        return list;
    }
    case Type::object: {
        QVariantMap map;
        for (auto it = value.begin(); it != value.end(); ++it) {
            map.insert(qs(it.key()), variantOf(it.value()));
        }
        return map;
    }
    case Type::null:
    case Type::binary:
    case Type::discarded:
        break;
    }
    return {};
}

nlohmann::json jsonOf(const QVariant& value)
{
    if (value.metaType() == QMetaType::fromType<QJSValue>()) {
        return jsonOf(value.value<QJSValue>().toVariant());
    }
    switch (value.typeId()) {
    case QMetaType::UnknownType:
    case QMetaType::Nullptr:
        return nullptr;
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        return value.toLongLong();
    case QMetaType::Double:
    case QMetaType::Float:
        return value.toDouble();
    case QMetaType::QString:
        return utf8(value.toString());
    case QMetaType::QStringList:
    case QMetaType::QVariantList: {
        nlohmann::json list = nlohmann::json::array();
        for (const QVariant& item : value.toList()) {
            list.push_back(jsonOf(item));
        }
        return list;
    }
    case QMetaType::QVariantMap: {
        nlohmann::json map = nlohmann::json::object();
        const QVariantMap entries = value.toMap();
        for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
            map[utf8(it.key())] = jsonOf(it.value());
        }
        return map;
    }
    default:
        break;
    }
    if (value.canConvert<QVariantMap>()) {
        return jsonOf(QVariant(value.toMap()));
    }
    if (value.canConvert<QVariantList>()) {
        return jsonOf(QVariant(value.toList()));
    }
    return utf8(value.toString());
}

std::vector<std::string> keysOf(const QStringList& keys)
{
    std::vector<std::string> list;
    list.reserve(static_cast<std::size_t>(keys.size()));
    for (const QString& key : keys) {
        list.push_back(utf8(key));
    }
    return list;
}

int utf16Index(const std::string& text, std::size_t byte)
{
    int units = 0;
    const std::size_t end = std::min(byte, text.size());
    for (std::size_t i = 0; i < end;) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        units += length == 4 ? 2 : 1;
        i += length;
    }
    return units;
}

QVariantMap pinVariant(const blueprint::GraphPin& pin)
{
    QVariantMap map;
    map.insert(QStringLiteral("name"), qs(pin.name));
    map.insert(QStringLiteral("label"), qs(pin.label));
    map.insert(QStringLiteral("type"), qs(pin.type));
    map.insert(QStringLiteral("connected"), pin.connected);
    map.insert(QStringLiteral("optional"), pin.optional);
    map.insert(QStringLiteral("value"), qs(pin.value));
    map.insert(QStringLiteral("editor"), qs(pin.editor));
    map.insert(QStringLiteral("raw"), pin.raw != nullptr ? variantOf(*pin.raw) : QVariant());
    QVariantList choices;
    for (const blueprint::GraphChoice& choice : pin.choices) {
        choices.push_back(QVariantMap{{QStringLiteral("value"), qs(choice.value)}, {QStringLiteral("text"), qs(choice.text)}});
    }
    map.insert(QStringLiteral("choices"), choices);
    map.insert(QStringLiteral("preview"), qs(pin.preview));
    if (pin.min) {
        map.insert(QStringLiteral("min"), *pin.min);
    }
    if (pin.max) {
        map.insert(QStringLiteral("max"), *pin.max);
    }
    if (pin.step) {
        map.insert(QStringLiteral("step"), *pin.step);
    }
    if (!pin.unit.empty()) {
        map.insert(QStringLiteral("unit"), qs(pin.unit));
    }
    return map;
}

QVariantList pinsVariant(std::span<const blueprint::GraphPin> pins)
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(pins.size()));
    for (const blueprint::GraphPin& pin : pins) {
        list.push_back(pinVariant(pin));
    }
    return list;
}

struct ApiTexts {
    std::string report;
    std::string declarations;
};

const ApiTexts& apiTexts()
{
    static const ApiTexts texts = [] {
        ApiTexts found;
        for (const script::ApiFile& file : script::apiFiles()) {
            if (file.path.ends_with("api-report.json")) {
                found.report = file.contents;
            } else if (file.path.ends_with("gamescrcpy.d.ts")) {
                found.declarations = file.contents;
            }
        }
        return found;
    }();
    return texts;
}

std::vector<script::NodeInfo> installedNodes()
{
    const NativeExtensions* native = NativeExtensions::instance();
    std::vector<script::NodeInfo> nodes = native != nullptr ? native->nodes() : std::vector<script::NodeInfo>{};
    std::erase_if(nodes, [](const script::NodeInfo& node) { return node.job; });
    return nodes;
}

nlohmann::json nodeSpaces(const std::vector<script::NodeInfo>& nodes)
{
    nlohmann::json spaces = nlohmann::json::array();
    for (const script::NodeInfo& node : nodes) {
        const std::string path = script::nodePath(node.extension, node.name);
        const std::string owner = path.substr(0, path.rfind('.'));
        auto space = std::find_if(spaces.begin(), spaces.end(),
                                  [&owner](const nlohmann::json& each) { return each.at("name") == owner; });
        if (space == spaces.end()) {
            spaces.push_back({{"name", owner}, {"functions", nlohmann::json::array()}});
            space = std::prev(spaces.end());
        }
        (*space)["functions"].push_back({{"name", node.name}, {"signature", script::nodeSignature(node)}});
    }
    return spaces;
}

std::shared_ptr<const blueprint::Api> sharedApi()
{
    static std::mutex mutex;
    static std::string cachedNodes;
    static std::shared_ptr<const blueprint::Api> cached;
    const nlohmann::json spaces = nodeSpaces(installedNodes());
    const std::string key = spaces.dump();
    const std::scoped_lock lock(mutex);
    if (cached == nullptr || key != cachedNodes) {
        std::string report = apiTexts().report;
        if (!spaces.empty()) {
            nlohmann::json root = nlohmann::json::parse(report, nullptr, false);
            if (root.is_object() && root["namespaces"].is_array()) {
                for (const nlohmann::json& space : spaces) {
                    root["namespaces"].push_back(space);
                }
                report = root.dump();
            }
        }
        cached = blueprint::loadApi(report, apiTexts().declarations);
        cachedNodes = key;
    }
    return cached;
}

std::vector<blueprint::ExtensionEntry> extensionEntries()
{
    std::vector<blueprint::ExtensionEntry> entries;
    for (const script::NodeInfo& node : installedNodes()) {
        const std::string path = script::nodePath(node.extension, node.name);
        std::string owner = path.substr(0, path.rfind('.'));
        if (std::ranges::find(entries, owner, &blueprint::ExtensionEntry::owner) == entries.end()) {
            entries.push_back({std::move(owner), node.extensionName.empty() ? node.extension : node.extensionName});
        }
    }
    return entries;
}

class KeyFinder
{
public:
    QString found;

    template <typename... Args> void identifier(Args&&...) {}
    template <typename... Args> void optionalIdentifier(Args&&...) {}
    template <typename... Args> void text(Args&&...) {}
    template <typename... Args> void optionalText(Args&&...) {}
    template <typename... Args> void requiredText(Args&&...) {}
    template <typename... Args> void valueTable(Args&&...) {}
    template <typename... Args> void optionalVec2(Args&&...) {}
    template <typename... Args> void choice(Args&&...) {}
    template <typename... Args> void integer(Args&&...) {}
    template <typename... Args> void number(Args&&...) {}
    template <typename... Args> void boolean(Args&&...) {}
    template <typename... Args> void vec2(Args&&...) {}
    template <typename... Args> void points(Args&&...) {}
    template <typename... Args> void value(Args&&...) {}
    template <typename... Args> void list(Args&&...) {}
    template <typename... Args> void json(Args&&...) {}
    template <typename... Args> void flag(Args&&...) {}
    template <typename... Args> void condition(Args&&...) {}
    template <typename... Args> void constantText(Args&&...) {}
    template <typename... Args> void constantInteger(Args&&...) {}
    template <typename... Args> void object(Args&&...) {}

    template <typename Fn>
    void group(std::string_view, Fn&& visitFields)
    {
        if (found.isEmpty()) {
            visitFields(*this);
        }
    }

    void binding(std::string_view, const std::optional<input::Binding>& field, input::SlotKind,
                 input::Claim = input::Claim::Lock)
    {
        take(field);
    }

    void optionalChord(std::string_view, const std::optional<input::Chord>& field)
    {
        if (found.isEmpty() && field) {
            found = keyLabel(*field).text;
        }
    }

    void bindingTable(std::string_view, const std::vector<input::NamedBinding>& field)
    {
        for (const input::NamedBinding& entry : field) {
            take(entry.binding);
        }
    }

    void slotTable(std::string_view name, const std::vector<input::NamedBinding>& field) { bindingTable(name, field); }

    void extension(std::string_view, const std::string& component, const std::vector<input::NamedBinding>& keys,
                   const std::vector<input::NamedValue>&)
    {
        if (!component.empty()) {
            bindingTable("inputs", keys);
        }
    }

    template <typename... Alternatives, std::size_t N>
    void variant(std::string_view, const std::variant<Alternatives...>& field, const std::array<std::string_view, N>&)
    {
        std::visit([this](const auto& alternative) { std::decay_t<decltype(alternative)>::visit(*this, alternative); },
                   field);
    }

private:
    void take(const std::optional<input::Binding>& field)
    {
        if (found.isEmpty() && field && field->input) {
            found = keyLabel(*field->input).text;
        }
    }
};

std::string controlKey(const input::Control& control)
{
    KeyFinder finder;
    input::Control::visit(finder, control);
    return finder.found.toStdString();
}

void readProfileEntries(blueprint::Environment& environment, const QString& root)
{
    environment.controls.clear();
    environment.vars.clear();
    environment.images.clear();
    environment.regions.clear();
    environment.points.clear();
    if (root.isEmpty()) {
        return;
    }
    std::string contents;
    if (!platform::readFile(toPath(QDir(root).filePath(QStringLiteral("profile.json"))), kMostProfileBytes, contents,
                            platform::WhenBusy::GiveUp)) {
        const input::LoadResult loaded = input::loadProfile(contents);
        if (loaded.ok()) {
            for (const input::Control& control : loaded.profile.controls) {
                std::string type(input::kControlTypeNames[control.kind.index()]);
                if (!type.empty()) {
                    type[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(type[0])));
                }
                environment.controls.push_back({control.id, control.name, type, controlKey(control)});
            }
            for (const input::Variable& variable : loaded.profile.vars) {
                const char* kind = std::holds_alternative<bool>(variable.initial)     ? "toggle"
                                 : std::holds_alternative<double>(variable.initial) ? "number"
                                                                                     : "text";
                environment.vars.push_back({variable.id, variable.name, kind});
            }
        }
    }
    const script::LoadedResources resources =
        script::loadResources(toPath(QDir(root).filePath(qs(script::kResourcesFile))));
    if (resources) {
        for (const script::TemplateResource& image : resources->templates) {
            const QString file = QDir(QDir(root).filePath(QStringLiteral("res"))).filePath(qs(image.file));
            environment.images.push_back({image.name, utf8(QUrl::fromLocalFile(file).toString())});
        }
        for (const script::RegionResource& region : resources->regions) {
            environment.regions.push_back(region.name);
        }
        for (const script::PointResource& point : resources->points) {
            environment.points.push_back(point.name);
        }
    }
}

std::string ownControlOf(std::string_view fileName)
{
    constexpr std::string_view kFolder = "controls/";
    if (!fileName.starts_with(kFolder)) {
        return {};
    }
    const std::string_view rest = fileName.substr(kFolder.size());
    const std::size_t dot = rest.rfind('.');
    if (rest.find('/') != std::string_view::npos || dot == std::string_view::npos || dot == 0) {
        return {};
    }
    return std::string(rest.substr(0, dot));
}

void readLibrary(blueprint::Environment& environment, const QString& root, const QVariantMap& open)
{
    environment.libraryImport = blueprint::libraryImportFor(environment.fileName);
    environment.library.clear();
    if (environment.libraryImport.empty()) {
        return;
    }
    const QString name = qs(blueprint::kLibraryFile);
    if (const auto found = open.constFind(name); found != open.constEnd()) {
        environment.library = blueprint::libraryFunctions(utf8(found.value().toString()));
        return;
    }
    std::string contents;
    if (root.isEmpty()
        || platform::readFile(toPath(QDir(root).filePath(name)), kMostLibraryBytes, contents, platform::WhenBusy::GiveUp)) {
        return;
    }
    environment.library = blueprint::libraryFunctions(contents);
}

}

std::optional<std::string> instrumentModule(const std::filesystem::path& folder, const std::string& module,
                                            const std::string& typescript)
{
    static const BlueprintLabels labels;
    blueprint::Environment environment;
    environment.api = sharedApi();
    environment.labels = &labels;
    environment.extensions = extensionEntries();
    environment.fileName = module;
    environment.ownControl = ownControlOf(module);
    readProfileEntries(environment, QString::fromStdWString(folder.wstring()));
    readLibrary(environment, QString::fromStdWString(folder.wstring()), {});
    const blueprint::Instrumented made = blueprint::instrument(typescript, environment);
    if (!made.problems.empty()) {
        return std::nullopt;
    }
    return "const __gs=__gsFor(" + nlohmann::json(module).dump() + ");" + made.javascript;
}

blueprint::Environment blueprintEnvironment(const std::filesystem::path& folder, const std::string& fileName,
                                            const blueprint::Labels* labels)
{
    blueprint::Environment environment;
    environment.api = sharedApi();
    environment.labels = labels;
    environment.fileName = fileName;
    environment.ownControl = ownControlOf(fileName);
    readProfileEntries(environment, QString::fromStdWString(folder.wstring()));
    readLibrary(environment, QString::fromStdWString(folder.wstring()), {});
    return environment;
}

BlueprintModel::BlueprintModel(QObject* parent)
    : QObject(parent)
{
    m_api = sharedApi();
    m_environment.api = m_api;
    m_environment.labels = &m_labels;
}

BlueprintModel::~BlueprintModel() = default;

BlueprintText* BlueprintModel::text() const
{
    return m_documentObject != nullptr ? qobject_cast<BlueprintText*>(m_documentObject.data()) : nullptr;
}

QString BlueprintModel::documentPath() const
{
    return m_documentObject != nullptr ? m_documentObject->property("path").toString() : QString();
}

QString BlueprintModel::folder() const
{
    return m_bench != nullptr ? m_bench->property("folder").toString() : QString();
}

QString BlueprintModel::documentFolder() const
{
    const QString own = m_documentObject != nullptr ? m_documentObject->property("folder").toString() : QString();
    return own.isEmpty() ? folder() : own;
}

QString BlueprintModel::layoutKey() const
{
    const QString path = documentPath();
    return path.isEmpty() ? QString() : documentFolder() + u'|' + path;
}

QString BlueprintModel::layoutPath() const
{
    const QString path = documentPath();
    const QString root = documentFolder();
    if (path.isEmpty() || root.isEmpty()) {
        return {};
    }
    return QDir(root).filePath(qs(blueprint::layoutFileName(utf8(path))));
}

void BlueprintModel::setBench(QObject* bench)
{
    if (bench == m_bench) {
        return;
    }
    if (m_bench != nullptr) {
        disconnect(m_bench.data(), nullptr, this, nullptr);
    }
    m_bench = bench;
    if (m_bench != nullptr && m_bench->metaObject()->indexOfSignal("filesChanged()") >= 0) {
        connect(m_bench.data(), SIGNAL(filesChanged()), this, SLOT(benchFilesChanged()));
    }
    refreshEnvironment();
    emit benchChanged();
    if (m_documentObject != nullptr) {
        loadLayout();
        m_doc.reset();
        parse();
    }
}

void BlueprintModel::setDocument(QObject* document)
{
    if (document == m_documentObject) {
        return;
    }
    if (m_layoutDirty) {
        saveLayout();
    }
    rememberLayout();
    if (m_documentObject != nullptr) {
        disconnect(m_documentObject.data(), nullptr, this, nullptr);
    }
    if (m_editor != nullptr) {
        disconnect(m_editor.data(), nullptr, this, nullptr);
    }
    m_documentObject = document;
    m_editor = nullptr;
    m_doc.reset();
    m_parsedText.clear();
    m_utf8.clear();
    m_undo.clear();
    m_redo.clear();
    m_selection.clear();
    m_page.clear();
    m_machines.clear();
    if (m_documentObject != nullptr) {
        const QMetaObject* meta = m_documentObject->metaObject();
        if (meta->indexOfSignal("textChanged()") >= 0) {
            connect(m_documentObject.data(), SIGNAL(textChanged()), this, SLOT(textChanged()));
        }
        if (meta->indexOfSignal("saved()") >= 0) {
            connect(m_documentObject.data(), SIGNAL(saved()), this, SLOT(documentSaved()));
        }
    }
    refreshEnvironment();
    loadLayout();
    emit documentChanged();
    emit historyChanged();
    emit selectionChanged();
    emit pageChanged();
    setLayoutDirty(false);
    parse();
    m_cleanLayout = m_doc != nullptr ? currentLayout() : nullptr;
}

void BlueprintModel::refreshEnvironment()
{
    m_api = sharedApi();
    m_environment.api = m_api;
    m_environment.extensions = extensionEntries();
    const QString path = documentPath();
    m_environment.fileName = !path.isEmpty()              ? utf8(path)
                           : m_documentObject != nullptr ? utf8(m_documentObject->property("name").toString())
                                                         : "logic.ts";
    if (m_environment.fileName.empty()) {
        m_environment.fileName = "logic.ts";
    }
    m_environment.ownControl = ownControlOf(utf8(path));
    m_environment.readOnly = m_documentObject != nullptr && m_documentObject->property("readOnly").toBool();
    m_environment.files.clear();
    m_environment.gone.clear();
    if (m_bench != nullptr) {
        for (const QVariant& entry : m_bench->property("files").toList()) {
            const QVariantMap file = entry.toMap();
            if (file.value(QStringLiteral("kind")).toString() == QStringLiteral("code")) {
                m_environment.files.push_back(utf8(file.value(QStringLiteral("path")).toString()));
            }
        }
        const QVariantMap gone = m_bench->property("goneControls").toMap();
        for (auto it = gone.begin(); it != gone.end(); ++it) {
            m_environment.gone.push_back({utf8(it.key()), utf8(it.value().toString()), {}});
        }
    }
    readProfileEntries(m_environment, folder());
    std::erase_if(m_environment.gone, [this](const blueprint::ControlEntry& entry) {
        return std::ranges::any_of(m_environment.controls,
                                   [&entry](const blueprint::ControlEntry& kept) { return kept.id == entry.id; });
    });
    QVariantMap open;
    if (m_bench != nullptr && m_bench->metaObject()->indexOfMethod("openScriptTexts()") >= 0) {
        QMetaObject::invokeMethod(m_bench.data(), "openScriptTexts", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, open));
    }
    readLibrary(m_environment, folder(), open);
}

void BlueprintModel::loadLayout()
{
    const QString key = layoutKey();
    if (key.isEmpty() || m_layouts.contains(key)) {
        return;
    }
    blueprint::Layout layout;
    const QString file = layoutPath();
    std::string contents;
    if (!file.isEmpty()
        && !platform::readFile(toPath(file), kMostLayoutBytes, contents, platform::WhenBusy::GiveUp)) {
        layout = blueprint::parseLayout(contents);
    }
    m_layouts[key] = std::make_shared<const blueprint::Layout>(std::move(layout));
}

void BlueprintModel::rememberLayout()
{
    const QString key = layoutKey();
    if (!key.isEmpty() && m_doc != nullptr) {
        m_layouts[key] = std::make_shared<const blueprint::Layout>(m_doc->layout());
    }
}

void BlueprintModel::persistView()
{
    if (m_doc == nullptr) {
        return;
    }
    if (m_documentObject != nullptr && m_documentObject->property("recorded").toBool()) {
        return;
    }
    const QString file = layoutPath();
    if (file.isEmpty()) {
        return;
    }
    if (platform::writeFileAtomically(toPath(file), blueprint::writeLayout(m_doc->layout()))) {
        return;
    }
    m_cleanLayout = currentLayout();
    setLayoutDirty(false);
}

std::shared_ptr<const blueprint::Layout> BlueprintModel::currentLayout() const
{
    if (m_doc != nullptr) {
        return std::make_shared<const blueprint::Layout>(m_doc->layout());
    }
    if (const auto found = m_layouts.find(layoutKey()); found != m_layouts.end()) {
        return found->second;
    }
    return std::make_shared<const blueprint::Layout>();
}

void BlueprintModel::textChanged()
{
    if (m_applying) {
        return;
    }
    syncWithEditor();
    scheduleParse();
}

void BlueprintModel::documentSaved()
{
    if (m_layoutDirty) {
        saveLayout();
    }
}

void BlueprintModel::benchFilesChanged()
{
    refreshEnvironment();
    if (m_doc != nullptr) {
        rebuild(m_parsedText, currentLayout());
    }
}

void BlueprintModel::editorCommandAdded()
{
    if (m_applying || m_editor == nullptr) {
        return;
    }
    Step step;
    step.text = true;
    step.external = true;
    step.undoIndex = m_editor->availableUndoSteps();
    m_undo.push_back(std::move(step));
    m_redo.clear();
    emit historyChanged();
}

void BlueprintModel::syncWithEditor()
{
    BlueprintText* source = text();
    QTextDocument* editor = source != nullptr ? source->editor() : nullptr;
    if (editor != m_editor) {
        if (m_editor != nullptr) {
            disconnect(m_editor.data(), nullptr, this, nullptr);
        }
        m_editor = editor;
        if (m_editor != nullptr) {
            connect(m_editor.data(), &QTextDocument::undoCommandAdded, this, &BlueprintModel::editorCommandAdded);
        }
    }
    if (m_editor == nullptr) {
        return;
    }
    const int available = m_editor->availableUndoSteps();
    bool changed = false;
    while (!m_undo.empty() && m_undo.back().text && m_undo.back().undoIndex > available) {
        m_redo.push_back(std::move(m_undo.back()));
        m_undo.pop_back();
        changed = true;
    }
    while (!m_redo.empty() && m_redo.back().text && m_redo.back().undoIndex >= 0
           && m_redo.back().undoIndex <= available) {
        m_undo.push_back(std::move(m_redo.back()));
        m_redo.pop_back();
        changed = true;
    }
    if (changed) {
        emit historyChanged();
    }
}

void BlueprintModel::scheduleParse()
{
    if (m_parseQueued) {
        return;
    }
    m_parseQueued = true;
    QTimer::singleShot(0, this, [this] { parse(); });
}

blueprint::Document* BlueprintModel::doc()
{
    BlueprintText* source = text();
    if (source != nullptr && (m_parseQueued || m_doc == nullptr || source->text() != m_parsedText)) {
        parse();
    }
    return m_doc.get();
}

void BlueprintModel::parse()
{
    m_parseQueued = false;
    syncWithEditor();
    BlueprintText* source = text();
    if (source == nullptr) {
        m_doc.reset();
        publish();
        return;
    }
    const QString current = source->text();
    if (m_doc != nullptr && current == m_parsedText) {
        return;
    }
    rebuild(current, currentLayout());
}

void BlueprintModel::rebuild(const QString& textValue, std::shared_ptr<const blueprint::Layout> layout)
{
    m_parsedText = textValue;
    m_utf8 = utf8(textValue);
    blueprint::Sizes sizes;
    for (const auto& [key, size] : m_sizes) {
        sizes.emplace(key, blueprint::Size{size.first, size.second});
    }
    auto next = std::make_unique<blueprint::Document>(m_utf8, m_environment,
                                                      layout != nullptr ? *layout : blueprint::Layout{}, m_doc.get(),
                                                      sizes);
    m_doc = std::move(next);
    rememberLayout();
    publish();
}

void BlueprintModel::publish()
{
    m_ready = m_doc != nullptr;
    if (m_doc == nullptr) {
        m_problem.clear();
        m_problemLine = 0;
        m_pages.clear();
        m_crumbs.clear();
        m_graph.clear();
        m_sidebar.clear();
        m_graphText = QStringLiteral("{}");
        m_sidebarText = QStringLiteral("[]");
        m_viewState.clear();
        m_diffRemoved.clear();
        emit graphChanged();
        publishDetails();
        return;
    }
    m_problem = qs(m_doc->problem());
    m_problemLine = static_cast<int>(m_doc->problemLine());
    if (!m_page.isEmpty() && !m_doc->hasPage(utf8(m_page))) {
        m_page.clear();
        emit pageChanged();
    }
    if (m_page.isEmpty()) {
        const QString remembered = qs(m_doc->layout().open);
        if (!remembered.isEmpty() && m_doc->hasPage(utf8(remembered))) {
            m_page = remembered;
            emit pageChanged();
        }
    }
    const std::string page = utf8(m_page);
    m_pages = variantOf(m_doc->pages()).toList();
    m_crumbs = variantOf(m_doc->crumbs(page)).toList();
    const auto sidebar = m_doc->sidebar(page);
    m_sidebar = variantOf(sidebar).toList();
    m_sidebarText = qs(sidebar.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    const blueprint::Graph view = m_doc->view(page);
    DraftGraphChanges changes;
    if (!m_diffBase.isEmpty()) {
        const blueprint::Document base(utf8(m_diffBase), m_environment, blueprint::Layout{});
        changes = draftGraphChanges(base.view(page), view);
    }
    m_diffRemoved = changes.removed;
    QVariantList nodes;
    nodes.reserve(static_cast<qsizetype>(view.nodes.size()));
    std::set<QString> present;
    for (const blueprint::GraphNode& node : view.nodes) {
        QVariantMap map;
        const QString key = qs(node.key);
        present.insert(key);
        map.insert(QStringLiteral("key"), key);
        map.insert(QStringLiteral("kind"), qs(node.kind));
        map.insert(QStringLiteral("tone"), qs(node.tone));
        map.insert(QStringLiteral("icon"), qs(node.icon));
        map.insert(QStringLiteral("title"), qs(node.title));
        map.insert(QStringLiteral("badge"), qs(node.badge));
        map.insert(QStringLiteral("subtitle"), qs(node.subtitle));
        map.insert(QStringLiteral("note"), qs(node.note));
        map.insert(QStringLiteral("x"), node.x);
        map.insert(QStringLiteral("y"), node.y);
        map.insert(QStringLiteral("inputs"), pinsVariant(node.inputs));
        map.insert(QStringLiteral("outputs"), pinsVariant(node.outputs));
        map.insert(QStringLiteral("code"), qs(node.code));
        map.insert(QStringLiteral("floating"), node.floating);
        map.insert(QStringLiteral("breakpoint"), qs(node.breakpoint));
        map.insert(QStringLiteral("breakpointReason"), qs(node.breakpointReason));
        map.insert(QStringLiteral("problem"), qs(node.problem));
        map.insert(QStringLiteral("start"), node.start);
        bool active = node.active;
        double since = node.activeSince;
        bool visited = node.visited;
        for (const auto& [position, run] : m_machines) {
            const QString state = qs(m_doc->stateNode(utf8(position), utf8(run.state)));
            if (state == key) {
                active = true;
                since = run.since;
            }
            for (const QString& seen : run.visited) {
                if (qs(m_doc->stateNode(utf8(position), utf8(seen))) == key) {
                    visited = true;
                }
            }
        }
        map.insert(QStringLiteral("active"), active);
        map.insert(QStringLiteral("activeSince"), since);
        map.insert(QStringLiteral("visited"), visited);
        map.insert(QStringLiteral("diff"), changes.marks.value(key));
        nodes.push_back(map);
    }
    QVariantList wires;
    wires.reserve(static_cast<qsizetype>(view.wires.size()));
    for (const blueprint::GraphWire& wire : view.wires) {
        const QString key = qs(wire.key);
        bool active = wire.active;
        for (const auto& [position, run] : m_machines) {
            static_cast<void>(position);
            active = active || (!run.wire.isEmpty() && run.wire == key);
        }
        wires.push_back(QVariantMap{{QStringLiteral("key"), key},
                                    {QStringLiteral("from"), qs(wire.from)},
                                    {QStringLiteral("fromPin"), qs(wire.fromPin)},
                                    {QStringLiteral("to"), qs(wire.to)},
                                    {QStringLiteral("toPin"), qs(wire.toPin)},
                                    {QStringLiteral("type"), qs(wire.type)},
                                    {QStringLiteral("label"), qs(wire.label)},
                                    {QStringLiteral("active"), active},
                                    {QStringLiteral("dashed"), wire.dashed}});
    }
    QVariantList comments;
    for (const blueprint::CommentBox& box : view.comments) {
        present.insert(qs(box.key));
        comments.push_back(QVariantMap{{QStringLiteral("key"), qs(box.key)},
                                       {QStringLiteral("x"), box.x},
                                       {QStringLiteral("y"), box.y},
                                       {QStringLiteral("width"), box.width},
                                       {QStringLiteral("height"), box.height},
                                       {QStringLiteral("title"), qs(box.title)},
                                       {QStringLiteral("color"), qs(box.color)}});
    }
    m_graph = QVariantMap{{QStringLiteral("key"), qs(view.key)},
                          {QStringLiteral("kind"), qs(view.kind)},
                          {QStringLiteral("title"), qs(view.title)},
                          {QStringLiteral("readOnly"), view.readOnly},
                          {QStringLiteral("nodes"), nodes},
                          {QStringLiteral("wires"), wires},
                          {QStringLiteral("comments"), comments},
                          {QStringLiteral("problem"), qs(view.problem)},
                          {QStringLiteral("problemLine"), static_cast<int>(view.problemLine)}};
    m_graphText = QString::fromUtf8(QJsonDocument::fromVariant(m_graph).toJson(QJsonDocument::Compact));
    const blueprint::ViewState state = view.view.value_or(blueprint::ViewState{});
    m_viewState = QVariantMap{{QStringLiteral("x"), state.x}, {QStringLiteral("y"), state.y},
                              {QStringLiteral("zoom"), state.zoom}};
    QStringList kept;
    for (const QString& key : m_selection) {
        if (present.contains(key)) {
            kept.push_back(key);
        }
    }
    const bool selectionChangedNow = kept != m_selection;
    m_selection = kept;
    emit graphChanged();
    if (selectionChangedNow) {
        emit selectionChanged();
    }
    publishDetails();
}

void BlueprintModel::publishDetails()
{
    QVariantMap made;
    if (m_doc != nullptr && m_selection.size() == 1) {
        nlohmann::ordered_json log = nlohmann::ordered_json::array();
        for (const auto& [position, run] : m_machines) {
            for (const auto& [state, entries] : run.log) {
                if (qs(m_doc->stateNode(utf8(position), utf8(state))) != m_selection.front()) {
                    continue;
                }
                for (const Transition& entry : entries) {
                    log.push_back({{"time", utf8(entry.time)}, {"text", utf8(entry.text)}, {"tone", utf8(entry.tone)}});
                }
            }
        }
        made = variantOf(m_doc->details(utf8(m_page), utf8(m_selection.front()), log)).toMap();
    }
    if (made != m_details) {
        m_details = made;
        emit detailsChanged();
    }
}

void BlueprintModel::setPage(const QString& page)
{
    if (page == m_page) {
        return;
    }
    if (m_doc != nullptr && !m_doc->hasPage(utf8(page))) {
        return;
    }
    m_page = page;
    m_selection.clear();
    emit pageChanged();
    emit selectionChanged();
    if (m_doc != nullptr) {
        m_doc->setOpenPage(utf8(m_page));
    }
    publish();
    persistView();
}

void BlueprintModel::setSelection(const QStringList& keys)
{
    if (keys == m_selection) {
        return;
    }
    m_selection = keys;
    emit selectionChanged();
    publishDetails();
}

void BlueprintModel::setDiffBase(const QString& base)
{
    if (base == m_diffBase) {
        return;
    }
    m_diffBase = base;
    emit diffBaseChanged();
    publish();
}

void BlueprintModel::setDebugging(bool on)
{
    if (on == m_debugging) {
        return;
    }
    m_debugging = on;
    m_reached.clear();
    m_pausedNode.clear();
    m_pinValues.clear();
    m_lastHit.clear();
    m_machines.clear();
    emit debuggingChanged();
    emit reachedChanged();
    emit pausedChanged();
    publish();
}

void BlueprintModel::setLayoutDirty(bool dirty)
{
    if (BlueprintText* target = text()) {
        target->setLayoutDirty(dirty);
    }
    if (dirty == m_layoutDirty) {
        return;
    }
    m_layoutDirty = dirty;
    emit layoutDirtyChanged();
}

QString BlueprintModel::apply(const blueprint::Outcome& outcome)
{
    if (!outcome.ok()) {
        return qs(outcome.reason);
    }
    BlueprintText* target = text();
    if (target == nullptr || m_doc == nullptr) {
        return say(BlueprintLabels::source("reason:readOnly"));
    }
    if (outcome.changes.empty()) {
        return applyLayout(outcome);
    }
    syncWithEditor();
    std::vector<TextChange> changes;
    changes.reserve(outcome.changes.size());
    for (const blueprint::TextChange& change : outcome.changes) {
        changes.push_back({utf16Index(m_utf8, change.start), utf16Index(m_utf8, change.end), qs(change.text)});
    }
    Step step;
    step.text = true;
    step.layoutBefore = currentLayout();
    step.layoutAfter = std::make_shared<const blueprint::Layout>(outcome.layout);
    const QString before = m_parsedText;
    m_applying = true;
    const bool done = target->edit(changes);
    m_applying = false;
    if (!done) {
        return say(BlueprintLabels::source("reason:readOnly"));
    }
    const QString after = target->text();
    if (m_editor != nullptr) {
        step.undoIndex = m_editor->availableUndoSteps();
    } else {
        step.before = before;
        step.after = after;
    }
    std::shared_ptr<const blueprint::Layout> layout = step.layoutAfter;
    m_undo.push_back(std::move(step));
    m_redo.clear();
    emit historyChanged();
    rebuild(after, layout);
    setLayoutDirty(true);
    return {};
}

QString BlueprintModel::applyLayout(const blueprint::Outcome& outcome)
{
    if (!outcome.ok()) {
        return qs(outcome.reason);
    }
    if (m_doc == nullptr) {
        return {};
    }
    Step step;
    step.layoutBefore = currentLayout();
    step.layoutAfter = std::make_shared<const blueprint::Layout>(outcome.layout);
    if (blueprint::sameLayout(*step.layoutBefore, *step.layoutAfter)) {
        return {};
    }
    std::shared_ptr<const blueprint::Layout> layout = step.layoutAfter;
    m_undo.push_back(std::move(step));
    m_redo.clear();
    emit historyChanged();
    rebuild(m_parsedText, layout);
    setLayoutDirty(true);
    return {};
}

void BlueprintModel::restore(const Step& step, bool forward)
{
    const std::shared_ptr<const blueprint::Layout>& layout = forward ? step.layoutAfter : step.layoutBefore;
    const auto markLayout = [this] {
        setLayoutDirty(m_cleanLayout == nullptr || !blueprint::sameLayout(*currentLayout(), *m_cleanLayout));
    };
    if (!step.text) {
        rebuild(m_parsedText, layout);
        markLayout();
        return;
    }
    BlueprintText* target = text();
    if (target == nullptr) {
        return;
    }
    m_applying = true;
    if (step.undoIndex >= 0 && m_editor != nullptr) {
        if (forward) {
            m_editor->redo();
        } else {
            m_editor->undo();
        }
    } else {
        const QString current = target->text();
        const std::array changes{TextChange{0, static_cast<int>(current.size()), forward ? step.after : step.before}};
        target->edit(changes);
    }
    m_applying = false;
    rebuild(target->text(), layout != nullptr ? layout : currentLayout());
    if (layout != nullptr) {
        markLayout();
    }
}

bool BlueprintModel::undo()
{
    syncWithEditor();
    if (m_undo.empty()) {
        return false;
    }
    Step step = std::move(m_undo.back());
    m_undo.pop_back();
    restore(step, false);
    m_redo.push_back(std::move(step));
    emit historyChanged();
    return true;
}

bool BlueprintModel::redo()
{
    syncWithEditor();
    if (m_redo.empty()) {
        return false;
    }
    Step step = std::move(m_redo.back());
    m_redo.pop_back();
    restore(step, true);
    m_undo.push_back(std::move(step));
    emit historyChanged();
    return true;
}

bool BlueprintModel::saveLayout()
{
    if (m_doc == nullptr || (m_documentObject != nullptr && m_documentObject->property("recorded").toBool())) {
        return false;
    }
    const QString file = layoutPath();
    if (file.isEmpty()) {
        return false;
    }
    if (const std::error_code error = platform::writeFileAtomically(toPath(file), blueprint::writeLayout(m_doc->layout()));
        error) {
        emit toast(say(QT_TRANSLATE_NOOP("Blueprint", "The blueprint layout could not be saved")));
        return false;
    }
    setLayoutDirty(false);
    m_cleanLayout = currentLayout();
    return true;
}

QString BlueprintModel::check(const QString& from, const QString& fromPin, const QString& to, const QString& toPin) const
{
    return m_doc != nullptr ? qs(m_doc->check(utf8(from), utf8(fromPin), utf8(to), utf8(toPin))) : QString();
}

QString BlueprintModel::connectPins(const QString& from, const QString& fromPin, const QString& to, const QString& toPin)
{
    return doc() != nullptr ? apply(m_doc->connect(utf8(from), utf8(fromPin), utf8(to), utf8(toPin))) : QString();
}

QString BlueprintModel::rewire(const QString& node, const QString& pin, const QString& toNode, const QString& toPin)
{
    return doc() != nullptr ? apply(m_doc->rewire(utf8(node), utf8(pin), utf8(toNode), utf8(toPin))) : QString();
}

QString BlueprintModel::disconnectPin(const QString& node, const QString& pin)
{
    return doc() != nullptr ? apply(m_doc->disconnect(utf8(node), utf8(pin))) : QString();
}

QString BlueprintModel::removeWire(const QString& wire)
{
    return doc() != nullptr ? apply(m_doc->removeWire(utf8(wire))) : QString();
}

QString BlueprintModel::setValue(const QString& node, const QString& pin, const QVariant& value)
{
    return doc() != nullptr ? apply(m_doc->setValue(utf8(node), utf8(pin), jsonOf(value))) : QString();
}

QVariantList BlueprintModel::catalog(const QString& fromNode, const QString& fromPin, const QString& query) const
{
    if (m_doc == nullptr) {
        return {};
    }
    QVariantList list = variantOf(m_doc->catalog(utf8(m_page), utf8(fromNode), utf8(fromPin), utf8(query))).toList();
    const ScriptApi& api = ScriptApi::installed();
    for (QVariant& value : list) {
        QVariantMap item = value.toMap();
        const QString description = api.documentationOf(item.value(QStringLiteral("api")).toString());
        if (!description.isEmpty()) {
            item.insert(QStringLiteral("description"), description);
            value = item;
        }
    }
    return list;
}

QVariantMap BlueprintModel::add(const QString& id, double x, double y, const QString& fromNode, const QString& fromPin)
{
    if (doc() == nullptr) {
        return {{QStringLiteral("key"), QString()}, {QStringLiteral("problem"), QString()}};
    }
    const blueprint::Outcome outcome = m_doc->add(utf8(m_page), utf8(id), x, y, utf8(fromNode), utf8(fromPin));
    const QString problem = apply(outcome);
    return {{QStringLiteral("key"), problem.isEmpty() ? qs(outcome.key) : QString()}, {QStringLiteral("problem"), problem}};
}

QVariantMap BlueprintModel::drop(const QVariant& item, double x, double y, const QString& node, const QString& pin)
{
    if (doc() == nullptr) {
        return {{QStringLiteral("key"), QString()}, {QStringLiteral("problem"), QString()}};
    }
    const blueprint::Outcome outcome = m_doc->drop(utf8(m_page), jsonOf(item), x, y, utf8(node), utf8(pin));
    const QString problem = apply(outcome);
    return {{QStringLiteral("key"), problem.isEmpty() ? qs(outcome.key) : QString()}, {QStringLiteral("problem"), problem}};
}

QVariantMap BlueprintModel::insertOnWire(const QString& wire, const QString& node)
{
    if (doc() == nullptr) {
        return {{QStringLiteral("key"), QString()}, {QStringLiteral("problem"), QString()}};
    }
    const blueprint::Outcome outcome = m_doc->insertOnWire(utf8(wire), utf8(node));
    const QString problem = apply(outcome);
    return {{QStringLiteral("key"), problem.isEmpty() ? qs(outcome.key) : QString()}, {QStringLiteral("problem"), problem}};
}

QString BlueprintModel::remove(const QStringList& keys)
{
    return doc() != nullptr ? apply(m_doc->remove(utf8(m_page), keysOf(keys))) : QString();
}

QString BlueprintModel::duplicate(const QStringList& keys)
{
    return doc() != nullptr ? apply(m_doc->duplicate(utf8(m_page), keysOf(keys))) : QString();
}

QString BlueprintModel::copyText(const QStringList& keys) const
{
    return m_doc != nullptr ? qs(m_doc->copy(keysOf(keys))) : QString();
}

QString BlueprintModel::paste(const QString& textValue, double x, double y)
{
    return doc() != nullptr ? apply(m_doc->paste(utf8(m_page), utf8(textValue), x, y)) : QString();
}

QString BlueprintModel::setCode(const QString& node, const QString& textValue)
{
    return doc() != nullptr ? apply(m_doc->setCode(utf8(node), utf8(textValue))) : QString();
}

QString BlueprintModel::replaceControl(const QString& node, const QString& from, const QString& to)
{
    return doc() != nullptr ? apply(m_doc->replaceControl(utf8(node), utf8(from), utf8(to))) : QString();
}

QVariantList BlueprintModel::controls() const
{
    QVariantList list;
    for (const blueprint::ControlEntry& entry : m_environment.controls) {
        list.append(QVariantMap{{QStringLiteral("id"), qs(entry.id)},
                                {QStringLiteral("name"), entry.name.empty() ? qs(entry.id) : qs(entry.name)},
                                {QStringLiteral("type"), qs(entry.type)}});
    }
    return list;
}

QString BlueprintModel::collapse(const QStringList& keys)
{
    return doc() != nullptr ? apply(m_doc->collapse(keysOf(keys))) : QString();
}

QString BlueprintModel::move(const QStringList& keys, double dx, double dy)
{
    return doc() != nullptr ? applyLayout(m_doc->move(utf8(m_page), keysOf(keys), dx, dy)) : QString();
}

QString BlueprintModel::addComment(const QStringList& keys)
{
    if (doc() == nullptr) {
        return {};
    }
    const blueprint::Outcome outcome = m_doc->addComment(utf8(m_page), keysOf(keys));
    const QString problem = applyLayout(outcome);
    if (problem.isEmpty() && !outcome.key.empty()) {
        setSelection({qs(outcome.key)});
    }
    return problem;
}

QString BlueprintModel::editComment(const QString& key, const QVariantMap& changes)
{
    return doc() != nullptr ? applyLayout(m_doc->editComment(utf8(m_page), utf8(key), jsonOf(changes))) : QString();
}

QString BlueprintModel::addReroute(const QString& wire, double x, double y)
{
    return doc() != nullptr ? applyLayout(m_doc->addReroute(utf8(m_page), utf8(wire), x, y)) : QString();
}

QString BlueprintModel::arrange()
{
    return doc() != nullptr ? applyLayout(m_doc->arrange(utf8(m_page))) : QString();
}

void BlueprintModel::measured(const QVariantMap& sizes)
{
    for (auto it = sizes.cbegin(); it != sizes.cend(); ++it) {
        const QVariantList size = it.value().toList();
        if (size.size() >= 2) {
            m_sizes[utf8(it.key())] = {size.at(0).toDouble(), size.at(1).toDouble()};
        }
    }
}

void BlueprintModel::setView(double x, double y, double zoom)
{
    if (doc() == nullptr) {
        return;
    }
    const blueprint::Outcome outcome = m_doc->setView(utf8(m_page), x, y, zoom);
    if (outcome.ok()) {
        rebuild(m_parsedText, std::make_shared<const blueprint::Layout>(outcome.layout));
        persistView();
    }
}

QString BlueprintModel::open(const QString& node)
{
    if (m_doc == nullptr) {
        return {};
    }
    const QString page = qs(m_doc->open(utf8(node)));
    if (!page.isEmpty()) {
        if (!page.startsWith(u"file:")) {
            setPage(page);
        }
        return page;
    }
    const QVariantMap target = jumpTarget(node);
    if (target.value(QStringLiteral("line")).toInt() > 0) {
        emit jumpRequested(target.value(QStringLiteral("path")).toString(), target.value(QStringLiteral("line")).toInt(),
                           target.value(QStringLiteral("column")).toInt());
    }
    return {};
}

QVariantMap BlueprintModel::jumpTarget(const QString& node) const
{
    if (m_doc == nullptr) {
        return {};
    }
    const blueprint::Target target = m_doc->jumpTarget(utf8(node));
    if (target.line == 0) {
        return {};
    }
    return {{QStringLiteral("path"), documentPath()},
            {QStringLiteral("line"), static_cast<int>(target.line)},
            {QStringLiteral("column"), static_cast<int>(target.column)}};
}

QString BlueprintModel::toggleBreakpoint(const QString& node)
{
    return doc() != nullptr ? applyLayout(m_doc->toggleBreakpoint(utf8(node))) : QString();
}

QString BlueprintModel::enableBreakpoint(const QString& node, bool on)
{
    return doc() != nullptr ? applyLayout(m_doc->enableBreakpoint(utf8(node), on)) : QString();
}

QString BlueprintModel::editDetail(const QString& name, const QVariant& value)
{
    if (doc() == nullptr || m_selection.size() != 1) {
        return {};
    }
    return apply(m_doc->editDetail(utf8(m_page), utf8(m_selection.front()), utf8(name), jsonOf(value)));
}

QVariantMap BlueprintModel::variableForm(QVariantMap spec) const
{
    QVariantList types;
    for (const std::string& kind : blueprint::variableTypeNames()) {
        const QString text = qs(m_labels.text("variable:type:" + kind));
        types.push_back(QVariantMap{{QStringLiteral("value"), qs(kind)},
                                    {QStringLiteral("text"), text.isEmpty() ? qs(kind) : text}});
    }
    spec.insert(QStringLiteral("types"), types);
    QVariantList containers;
    for (const std::pair<const char*, const char*> pair :
         {std::pair{"single", "variable:single"}, std::pair{"list", "variable:list"},
          std::pair{"dict", "variable:dict"}, std::pair{"set", "variable:set"}}) {
        containers.push_back(QVariantMap{{QStringLiteral("value"), QString::fromLatin1(pair.first)},
                                         {QStringLiteral("text"), qs(m_labels.text(pair.second))}});
    }
    spec.insert(QStringLiteral("containers"), containers);
    QVariantList keyKinds;
    for (const std::pair<const char*, const char*> pair :
         {std::pair{"text", "variable:keyText"}, std::pair{"number", "variable:keyNumber"}}) {
        keyKinds.push_back(QVariantMap{{QStringLiteral("value"), QString::fromLatin1(pair.first)},
                                       {QStringLiteral("text"), qs(m_labels.text(pair.second))}});
    }
    spec.insert(QStringLiteral("keyKinds"), keyKinds);
    spec.insert(QStringLiteral("canBeEditable"),
                spec.value(QStringLiteral("scope")).toString() == QStringLiteral("class"));
    QString pageTitle = qs(m_labels.text("page:main"));
    for (const QVariant& entry : m_pages) {
        const QVariantMap page = entry.toMap();
        if (page.value(QStringLiteral("key")).toString() == m_page) {
            pageTitle = page.value(QStringLiteral("title")).toString();
        }
    }
    spec.insert(QStringLiteral("pageTitle"), pageTitle);
    return spec;
}

void BlueprintModel::newVariable()
{
    if (m_doc == nullptr) {
        return;
    }
    m_variableEditor = variableForm(variantOf(m_doc->newVariable(utf8(m_page))).toMap());
    emit variableEditorChanged();
}

void BlueprintModel::editVariable(const QString& key)
{
    if (m_doc == nullptr) {
        return;
    }
    const QVariantMap spec = variantOf(m_doc->variable(utf8(m_page), utf8(key))).toMap();
    if (spec.isEmpty()) {
        return;
    }
    m_variableEditor = variableForm(spec);
    emit variableEditorChanged();
}

void BlueprintModel::closeVariable()
{
    if (m_variableEditor.isEmpty()) {
        return;
    }
    m_variableEditor.clear();
    emit variableEditorChanged();
}

QString BlueprintModel::saveVariable(const QVariantMap& spec)
{
    if (doc() == nullptr) {
        return {};
    }
    const QString key = spec.value(QStringLiteral("key")).toString();
    const nlohmann::json payload = jsonOf(spec);
    const blueprint::Outcome outcome = key.isEmpty() ? m_doc->createVariable(utf8(m_page), payload)
                                                     : m_doc->saveVariable(utf8(m_page), utf8(key), payload);
    const QString problem = apply(outcome);
    if (problem.isEmpty()) {
        m_variableEditor.clear();
        emit variableEditorChanged();
    }
    return problem;
}

QString BlueprintModel::removeVariable(const QString& key)
{
    if (doc() == nullptr) {
        return {};
    }
    const QString problem = apply(m_doc->deleteVariable(utf8(m_page), utf8(key)));
    if (problem.isEmpty()) {
        m_variableEditor.clear();
        emit variableEditorChanged();
    }
    return problem;
}

QString BlueprintModel::promoteToVariable(const QString& node, const QString& pin)
{
    return doc() != nullptr ? apply(m_doc->promoteToVariable(utf8(m_page), utf8(node), utf8(pin))) : QString();
}

QVariantMap BlueprintModel::addType(const QString& kind, const QString& name)
{
    if (doc() == nullptr) {
        return {{QStringLiteral("key"), QString()}, {QStringLiteral("problem"), QString()}};
    }
    const blueprint::Outcome outcome = m_doc->addType(utf8(kind), utf8(name));
    const QString problem = apply(outcome);
    return {{QStringLiteral("key"), problem.isEmpty() ? qs(outcome.key) : QString()},
            {QStringLiteral("page"), problem.isEmpty() ? qs(outcome.page) : QString()},
            {QStringLiteral("problem"), problem}};
}

QString BlueprintModel::editType(const QString& name, const QString& field, const QVariant& value)
{
    return doc() != nullptr ? apply(m_doc->editType(utf8(name), utf8(field), jsonOf(value))) : QString();
}

QVariantMap BlueprintModel::typeInfo(const QString& name) const
{
    if (m_doc == nullptr) {
        return {};
    }
    return variantOf(m_doc->typeInfo(utf8(name))).toMap();
}

QString BlueprintModel::addState(double x, double y)
{
    return doc() != nullptr ? apply(m_doc->addState(utf8(m_page), x, y)) : QString();
}

QString BlueprintModel::connectStates(const QString& from, const QString& to)
{
    return doc() != nullptr ? apply(m_doc->connectStates(utf8(from), utf8(to))) : QString();
}

QString BlueprintModel::setTransitionLabel(const QString& wire, const QString& textValue)
{
    return doc() != nullptr ? apply(m_doc->setTransitionLabel(utf8(wire), utf8(textValue))) : QString();
}

QString BlueprintModel::renameState(const QString& node, const QString& name)
{
    return doc() != nullptr ? apply(m_doc->renameState(utf8(node), utf8(name))) : QString();
}

QString BlueprintModel::setStart(const QString& node)
{
    return doc() != nullptr ? apply(m_doc->setStart(utf8(node))) : QString();
}

void BlueprintModel::hit(const QString& position)
{
    const QString node = nodeAt(position);
    if (node.isEmpty()) {
        return;
    }
    const QString wire = m_doc != nullptr ? qs(m_doc->wireInto(utf8(node), utf8(m_lastHit))) : QString();
    m_lastHit = node;
    emit pulse(node, wire);
    if (m_debugging && !m_reached.contains(node)) {
        m_reached.push_back(node);
        emit reachedChanged();
    }
}

void BlueprintModel::pausedAt(const QString& position, const QVariantMap& values)
{
    m_pausedNode = nodeAt(position);
    m_pinValues.clear();
    if (m_doc != nullptr) {
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            const QString producer = qs(m_doc->localProducer(utf8(position), utf8(it.key())));
            const qsizetype split = producer.indexOf(u'\n');
            if (split <= 0) {
                continue;
            }
            const QString node = producer.left(split);
            QVariantMap pins = m_pinValues.value(node).toMap();
            const QVariant value = it.value();
            QString shown;
            if (value.typeId() == QMetaType::QVariantMap || value.typeId() == QMetaType::QVariantList) {
                shown = qs(jsonOf(value).dump());
            } else {
                shown = value.toString();
            }
            pins.insert(producer.mid(split + 1), shown);
            m_pinValues.insert(node, pins);
        }
    }
    if (!m_pausedNode.isEmpty() && m_debugging && !m_reached.contains(m_pausedNode)) {
        m_reached.push_back(m_pausedNode);
        emit reachedChanged();
    }
    emit pausedChanged();
}

void BlueprintModel::resumed()
{
    if (m_pausedNode.isEmpty() && m_pinValues.isEmpty()) {
        return;
    }
    m_pausedNode.clear();
    m_pinValues.clear();
    emit pausedChanged();
}

void BlueprintModel::machineState(const QString& machinePosition, const QString& state, const QString& reason,
                                  double since, double at)
{
    if (m_doc == nullptr) {
        return;
    }
    const QString position = machinePosition.contains(u':') ? machinePosition
                                                             : qs(m_doc->machineOnLine(machinePosition.toInt()));
    if (position.isEmpty()) {
        return;
    }
    MachineRun& run = m_machines[position];
    if (state.isEmpty()) {
        run.state.clear();
        run.wire.clear();
        publish();
        return;
    }
    const QString previous = run.state;
    run.wire = previous.isEmpty() ? QString()
                                  : qs(m_doc->transitionWire(utf8(position), utf8(previous), utf8(state)));
    run.state = state;
    run.since = since;
    run.visited.insert(state);
    if (!previous.isEmpty()) {
        std::vector<Transition>& log = run.log[previous];
        const QString time =
            since > 0 ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(since)).toString(QStringLiteral("HH:mm:ss"))
            : at >= 0 ? recordingClock(at)
                      : QString();
        log.push_back({time, reason.isEmpty() ? state : reason + QStringLiteral(" → ") + state, QStringLiteral("neutral")});
        if (log.size() > kMostTransitions) {
            log.erase(log.begin(), log.begin() + static_cast<std::ptrdiff_t>(log.size() - kMostTransitions));
        }
    }
    publish();
}

void BlueprintModel::resetMachines()
{
    if (m_machines.empty()) {
        return;
    }
    m_machines.clear();
    publish();
}

QVariantList BlueprintModel::breakpointPositions() const
{
    return m_doc != nullptr ? variantOf(m_doc->breakpointPositions()).toList() : QVariantList();
}

QString BlueprintModel::nodeAt(const QString& position) const
{
    return m_doc != nullptr ? qs(m_doc->nodeAt(utf8(position))) : QString();
}

QVariantMap BlueprintModel::instrument(const QString& source) const
{
    const blueprint::Instrumented made = blueprint::instrument(utf8(source), m_environment);
    QVariantList probes;
    for (const blueprint::Probe& probe : made.probes) {
        probes.push_back(QVariantMap{{QStringLiteral("position"), qs(probe.position)},
                                     {QStringLiteral("kind"), qs(probe.kind)},
                                     {QStringLiteral("async"), probe.async}});
    }
    QStringList problems;
    for (const std::string& problem : made.problems) {
        problems.push_back(qs(problem));
    }
    return {{QStringLiteral("javascript"), qs(made.javascript)},
            {QStringLiteral("probes"), probes},
            {QStringLiteral("problems"), problems}};
}

}
