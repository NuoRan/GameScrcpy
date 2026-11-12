#include "HelpLibrary.h"

#include "AppSettings.h"
#include "LanguagePacks.h"

#include <gs/ext/Archive.h>
#include <gs/script/ApiFiles.h>

#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QTranslator>

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<QStringView, 2> kLanguages{u"en", u"zh_CN"};
constexpr QStringView kFallback = u"en";
constexpr qint64 kMostDocumentBytes = 1024 * 1024;

struct Document {
    QString heading;
    QString body;
};

std::optional<QString> readText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.read(kMostDocumentBytes + 1);
    if (bytes.size() > kMostDocumentBytes) {
        qWarning("help: %s is too large", qUtf8Printable(path));
        return std::nullopt;
    }
    QString text = QString::fromUtf8(bytes);
    if (text.startsWith(QChar(0xFEFF))) {
        text.remove(0, 1);
    }
    text.replace(u"\r\n"_s, u"\n"_s);
    return text;
}

Document split(const QString& text)
{
    qsizetype start = 0;
    while (start < text.size() && text[start] == u'\n') {
        ++start;
    }
    if (!QStringView(text).sliced(start).startsWith(u"# ")) {
        return {.heading = {}, .body = text.sliced(start)};
    }
    const qsizetype end = text.indexOf(u'\n', start);
    const qsizetype lineEnd = end < 0 ? text.size() : end;
    qsizetype rest = lineEnd;
    while (rest < text.size() && text[rest] == u'\n') {
        ++rest;
    }
    return {.heading = text.sliced(start + 2, lineEnd - start - 2).trimmed(), .body = text.sliced(rest)};
}

bool validSlug(const QString& slug)
{
    static const QRegularExpression pattern(uR"(^[a-z0-9]+(?:-[a-z0-9]+)*(?:/[a-z0-9]+(?:-[a-z0-9]+)*)?$)"_s);
    return pattern.match(slug).hasMatch();
}

const QRegularExpression& documentLink()
{
    static const QRegularExpression pattern(uR"(\]\(([A-Za-z0-9_-]+)\.md(?:#[^)\s]*)?\))"_s);
    return pattern;
}

QString referenceSlug(const QString& stem)
{
    return stem.compare(u"README"_s, Qt::CaseInsensitive) == 0 ? u"api"_s : u"api/"_s + stem.toLower();
}

QString withHelpLinks(const QString& text)
{
    QString linked;
    qsizetype last = 0;
    for (QRegularExpressionMatchIterator it = documentLink().globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        linked += QStringView(text).sliced(last, match.capturedStart() - last);
        linked += u"](help:"_s + referenceSlug(match.captured(1)) + u')';
        last = match.capturedEnd();
    }
    linked += QStringView(text).sliced(last);
    return linked;
}

QStringList linkedStems(const QString& text)
{
    QStringList stems;
    for (QRegularExpressionMatchIterator it = documentLink().globalMatch(text); it.hasNext();) {
        const QString stem = it.next().captured(1);
        if (!stems.contains(stem)) {
            stems.append(stem);
        }
    }
    return stems;
}

std::map<QString, QString> titlesOf(const QJsonValue& value)
{
    std::map<QString, QString> titles;
    if (value.isString()) {
        titles.emplace(kFallback.toString(), value.toString());
        return titles;
    }
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (const QString title = it.value().toString(); !title.isEmpty()) {
            titles.emplace(it.key(), title);
        }
    }
    return titles;
}

QString pick(const std::map<QString, QString>& values, const QString& language)
{
    for (const QString& candidate : {language, language.section(u'_', 0, 0), kFallback.toString()}) {
        if (const auto found = values.find(candidate); found != values.end()) {
            return found->second;
        }
    }
    return values.empty() ? QString() : values.begin()->second;
}

template <typename Value>
const Value* pickOf(const std::map<QString, Value>& values, const QString& language)
{
    for (const QString& candidate : {language, language.section(u'_', 0, 0), kFallback.toString()}) {
        if (const auto found = values.find(candidate); found != values.end()) {
            return &found->second;
        }
    }
    return values.empty() ? nullptr : &values.begin()->second;
}

QString pickVariant(const QVariant& value, const QString& language)
{
    if (value.typeId() == QMetaType::QString) {
        return value.toString();
    }
    std::map<QString, QString> values;
    const QVariantMap map = value.toMap();
    for (auto it = map.begin(); it != map.end(); ++it) {
        values.emplace(it.key(), it.value().toString());
    }
    return pick(values, language);
}

QString layoutOf(const HelpTopic& topic)
{
    if (topic.reference) {
        return u"reference"_s;
    }
    return topic.layout.isEmpty() ? u"guide"_s : topic.layout;
}

QVariantList sectionsOfFaq(const QVariantList& categories)
{
    QVariantList sections;
    for (const QVariant& value : categories) {
        const QVariantMap category = value.toMap();
        sections.append(QVariantMap{{u"title"_s, category.value(u"title"_s)}, {u"anchor"_s, category.value(u"anchor"_s)}});
    }
    return sections;
}

qsizetype questionCount(const QVariantList& categories)
{
    qsizetype count = 0;
    for (const QVariant& value : categories) {
        count += value.toMap().value(u"questions"_s).toList().size();
    }
    return count;
}

QString searchableText(const QString& title, const QString& body)
{
    static const QRegularExpression targets(uR"(\]\([^)]*\))"_s);
    static const QRegularExpression marks(uR"([*`#>|\[\]!])"_s);
    QString plain = body;
    plain.replace(targets, u"]"_s);
    plain.replace(marks, u" "_s);
    return (title + u'\n' + plain).toCaseFolded();
}

QStringList wordsOf(const QString& query)
{
    return query.simplified().toCaseFolded().split(u' ', Qt::SkipEmptyParts);
}

constexpr const char* kExtensionsGroup = QT_TRANSLATE_NOOP("HelpLibrary", "Extensions");

QVariantList withSafePictures(const QVariantList& blocks)
{
    QVariantList kept;
    for (const QVariant& value : blocks) {
        const QVariantMap block = value.toMap();
        if (block.value(u"kind"_s).toString() == u"image"_s) {
            const QString source = block.value(u"source"_s).toString();
            if (source.contains(u':') || !ext::safeArchivePath(source.toStdString())) {
                continue;
            }
        }
        kept.append(value);
    }
    return kept;
}

}

HelpLibrary::HelpLibrary(const AppSettings& settings, QString root, Translate translate, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_root(std::move(root))
    , m_translate(std::move(translate))
    , m_language(settings.interfaceLanguage())
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    load();
    m_contents = listed();
    connect(&settings, &AppSettings::languageChanged, this, &HelpLibrary::followLanguage);
}

HelpLibrary::~HelpLibrary()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

HelpLibrary* HelpLibrary::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void HelpLibrary::setQuery(const QString& query)
{
    if (query == m_query) {
        return;
    }
    const bool refilter = wordsOf(query) != wordsOf(m_query);
    m_query = query;
    emit queryChanged();
    if (refilter) {
        m_contents = listed();
        emit contentsChanged();
    }
}

QString HelpLibrary::firstArticle() const
{
    const auto first = std::ranges::find_if(m_topics, [](const HelpTopic& topic) { return !topic.reference; });
    if (first != m_topics.end()) {
        return first->slug;
    }
    return m_topics.empty() ? QString() : m_topics.front().slug;
}

const HelpTopic* HelpLibrary::find(QStringView slug) const
{
    const auto found = std::ranges::find_if(m_topics, [slug](const HelpTopic& topic) { return topic.slug == slug; });
    return found != m_topics.end() ? &*found : nullptr;
}

QUrl HelpLibrary::baseUrl() const
{
    if (m_root.startsWith(u':')) {
        return QUrl(u"qrc"_s + m_root + u'/');
    }
    return QUrl::fromLocalFile(QDir(m_root).absolutePath() + u'/');
}

void HelpLibrary::setExtensionTopics(std::vector<ExtensionHelp> topics)
{
    std::erase_if(m_topics, [](const HelpTopic& topic) { return !topic.extension.isEmpty(); });
    if (!m_extensionGroup) {
        HelpGroup group;
        for (const QString& code : languages()) {
            group.titles.emplace(code, textIn(code, kExtensionsGroup));
        }
        m_extensionGroup = m_groups.size();
        m_groups.push_back(std::move(group));
    }
    for (ExtensionHelp& wanted : topics) {
        HelpTopic topic{.slug = u"ext/"_s + wanted.extension + u'/' + wanted.id,
                        .icon = wanted.icon.isEmpty() && wanted.iconPath.isEmpty() ? u"puzzle"_s : wanted.icon,
                        .tile = u"azure"_s,
                        .group = *m_extensionGroup,
                        .titles = std::move(wanted.titles),
                        .meta = QVariantList{QVariantMap{{u"icon"_s, u"puzzle"_s}, {u"text"_s, wanted.extensionName}}},
                        .extension = wanted.extension,
                        .extensionName = wanted.extensionName,
                        .iconPath = wanted.iconPath,
                        .base = wanted.base};
        if (find(topic.slug) != nullptr) {
            continue;
        }
        for (const auto& [language, text] : wanted.texts) {
            const QString body = split(text).body;
            topic.texts.emplace(language, body);
            HelpDocument document = parseHelpMarkdown(body);
            document.blocks = withSafePictures(document.blocks);
            topic.documents.emplace(language, std::move(document));
        }
        if (topic.texts.empty()) {
            continue;
        }
        makeSearchable(topic);
        m_topics.push_back(std::move(topic));
    }
    m_contents = listed();
    emit contentsChanged();
}

void HelpLibrary::setLanguagePacks(std::vector<HelpLanguage> packs)
{
    std::erase_if(packs, [](const HelpLanguage& pack) {
        return pack.code.isEmpty() || std::ranges::find(kLanguages, QStringView(pack.code)) != kLanguages.end();
    });
    if (packs == m_packs) {
        return;
    }
    for (HelpTopic& topic : m_topics) {
        if (!topic.extension.isEmpty()) {
            continue;
        }
        for (const HelpLanguage& old : m_packs) {
            topic.titles.erase(old.code);
            topic.texts.erase(old.code);
            topic.documents.erase(old.code);
        }
    }
    if (m_extensionGroup) {
        for (const HelpLanguage& old : m_packs) {
            m_groups[*m_extensionGroup].titles.erase(old.code);
        }
    }
    m_packs = std::move(packs);
    const std::map<QString, QString> bundled = m_packs.empty() ? std::map<QString, QString>() : bundledReference();
    for (const HelpLanguage& pack : m_packs) {
        addPack(pack, bundled);
    }
    for (HelpTopic& topic : m_topics) {
        makeSearchable(topic);
    }
    m_contents = listed();
    emit contentsChanged();
}

void HelpLibrary::addPack(const HelpLanguage& pack, const std::map<QString, QString>& bundled)
{
    QByteArray data;
    QTranslator translator;
    const bool loaded = !m_translate && loadTranslationFile(translator, data, pack.translations);
    const auto translate = [&](const char* context, const QString& english) {
        const QString found = m_translate ? m_translate(pack.code, english)
                              : loaded    ? translator.translate(context, english.toUtf8().constData())
                                          : QString();
        return found.isEmpty() ? english : found;
    };
    if (m_extensionGroup) {
        m_groups[*m_extensionGroup].titles.insert_or_assign(
            pack.code, translate("HelpLibrary", QString::fromUtf8(kExtensionsGroup)));
    }
    if (loaded || m_translate) {
        const QByteArray context(script::kApiTextContext.data(), static_cast<qsizetype>(script::kApiTextContext.size()));
        const std::map<QString, QString> pages = translatedPages(bundled, [&](std::string_view text) {
            return translate(context.constData(), QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())))
                .toStdString();
        });
        for (const auto& [stem, page] : pages) {
            const auto english = bundled.find(stem);
            const QString slug = referenceSlug(stem);
            const auto topic = std::ranges::find_if(
                m_topics, [&slug](const HelpTopic& each) { return each.reference && each.slug == slug; });
            if (topic == m_topics.end() || (english != bundled.end() && english->second == page)) {
                continue;
            }
            const Document shown = split(page);
            const QString body = withHelpLinks(shown.body);
            topic->titles.insert_or_assign(pack.code, shown.heading.isEmpty() ? stem : shown.heading);
            topic->texts.insert_or_assign(pack.code, body);
            topic->documents.insert_or_assign(pack.code, parseHelpMarkdown(body));
        }
    }
    if (pack.folder.isEmpty()) {
        return;
    }
    for (HelpTopic& topic : m_topics) {
        if (topic.reference || !topic.extension.isEmpty()) {
            continue;
        }
        const std::optional<QString> text = readText(pack.folder + u'/' + topic.slug + u".md"_s);
        if (!text) {
            continue;
        }
        const Document shown = split(*text);
        if (!shown.heading.isEmpty()) {
            topic.titles.insert_or_assign(pack.code, shown.heading);
        }
        topic.texts.insert_or_assign(pack.code, shown.body);
        topic.documents.insert_or_assign(pack.code, parseHelpMarkdown(shown.body));
    }
}

QStringList HelpLibrary::languages() const
{
    QStringList codes;
    for (const QStringView language : kLanguages) {
        codes.append(language.toString());
    }
    for (const HelpLanguage& pack : m_packs) {
        if (!codes.contains(pack.code)) {
            codes.append(pack.code);
        }
    }
    return codes;
}

void HelpLibrary::makeSearchable(HelpTopic& topic) const
{
    topic.searchable.clear();
    for (const QString& code : languages()) {
        topic.searchable.emplace(code, searchableText(pick(topic.titles, code), pick(topic.texts, code)));
    }
}

QString HelpLibrary::textIn(const QString& language, const char* text) const
{
    const QString english = QString::fromUtf8(text);
    if (language == kFallback) {
        return english;
    }
    QString found;
    if (m_translate) {
        found = m_translate(language, english);
    } else if (const auto pack = std::ranges::find(m_packs, language, &HelpLanguage::code); pack != m_packs.end()) {
        QByteArray data;
        QTranslator translator;
        if (loadTranslationFile(translator, data, pack->translations)) {
            found = translator.translate("HelpLibrary", text);
        }
    } else if (QTranslator translator; translator.load(u"gamescrcpy_"_s + language, u":/i18n"_s)) {
        found = translator.translate("HelpLibrary", text);
    }
    return found.isEmpty() ? english : found;
}

bool HelpLibrary::contains(const QString& slug) const
{
    return find(slug) != nullptr;
}

QVariantMap HelpLibrary::article(const QString& slug, const QString& language) const
{
    const HelpTopic* topic = find(slug);
    if (topic == nullptr) {
        return {};
    }
    const QString shown = language.isEmpty() ? m_language : language;
    const QString layout = layoutOf(*topic);
    QVariantMap result{{u"slug"_s, topic->slug},
                       {u"title"_s, pick(topic->titles, shown)},
                       {u"group"_s, pick(m_groups[topic->group].titles, shown)},
                       {u"icon"_s, topic->icon},
                       {u"tile"_s, topic->tile},
                       {u"hero"_s, topic->hero},
                       {u"summary"_s, pick(topic->summaries, shown)},
                       {u"reference"_s, topic->reference},
                       {u"layout"_s, layout},
                       {u"text"_s, pick(topic->texts, shown)},
                       {u"baseUrl"_s, topic->base.isEmpty() ? baseUrl() : topic->base},
                       {u"previous"_s, neighbour(*topic, -1, shown)},
                       {u"next"_s, neighbour(*topic, 1, shown)}};
    QVariantList meta;
    for (const QVariant& entry : topic->meta) {
        const QVariantMap item = entry.toMap();
        meta.append(QVariantMap{{u"icon"_s, item.value(u"icon"_s)},
                                {u"text"_s, pickVariant(item.value(u"text"_s), shown)}});
    }
    result.insert(u"meta"_s, meta);
    const HelpDocument* document = pickOf(topic->documents, shown);
    if (document == nullptr) {
        return result;
    }
    result.insert(u"lead"_s, document->lead);
    result.insert(u"minutes"_s, document->minutes);
    if (layout == u"reference"_s) {
        QVariantList blocks = helpReference(document->blocks);
        decorate(blocks);
        result.insert(u"blocks"_s, blocks);
        result.insert(u"sections"_s, helpReferenceSections(blocks));
    } else if (layout == u"faq"_s) {
        QVariantList categories = helpFaq(document->blocks);
        for (QVariant& category : categories) {
            QVariantMap map = category.toMap();
            QVariantList questions = map.value(u"questions"_s).toList();
            for (QVariant& question : questions) {
                QVariantMap entry = question.toMap();
                QVariantList answer = entry.value(u"blocks"_s).toList();
                decorate(answer);
                entry.insert(u"blocks"_s, answer);
                question = entry;
            }
            map.insert(u"questions"_s, questions);
            category = map;
        }
        result.insert(u"categories"_s, categories);
        result.insert(u"sections"_s, sectionsOfFaq(categories));
        result.insert(u"blocks"_s, QVariantList{});
    } else {
        QVariantList blocks = helpTabs(document->blocks);
        decorate(blocks);
        result.insert(u"blocks"_s, blocks);
        result.insert(u"sections"_s, document->sections);
    }
    return result;
}

void HelpLibrary::decorate(QVariantList& blocks) const
{
    for (QVariant& value : blocks) {
        QVariantMap block = value.toMap();
        const QString kind = block.value(u"kind"_s).toString();
        if (kind == u"link"_s) {
            const QString target = block.value(u"target"_s).toString();
            if (!target.startsWith(u"help:"_s)) {
                continue;
            }
            if (const HelpTopic* topic = find(target.mid(5).section(u'#', 0, 0)); topic != nullptr) {
                block.insert(u"icon"_s, topic->icon);
                block.insert(u"tile"_s, topic->tile);
                value = block;
            }
        } else if (kind == u"tabs"_s) {
            QVariantList tabs = block.value(u"tabs"_s).toList();
            for (QVariant& tab : tabs) {
                QVariantMap entry = tab.toMap();
                QVariantList inner = entry.value(u"blocks"_s).toList();
                decorate(inner);
                entry.insert(u"blocks"_s, inner);
                tab = entry;
            }
            block.insert(u"tabs"_s, tabs);
            value = block;
        }
    }
}

QVariantMap HelpLibrary::home(const QString& language) const
{
    const QString shown = language.isEmpty() ? m_language : language;
    QVariantList guides;
    QVariantList reference;
    for (const HelpTopic& topic : m_topics) {
        if (!topic.extension.isEmpty()) {
            continue;
        }
        if (topic.reference) {
            reference.append(QVariantMap{{u"slug"_s, topic.slug}, {u"title"_s, pick(topic.titles, shown)}});
            continue;
        }
        const HelpDocument* document = pickOf(topic.documents, shown);
        const bool faq = layoutOf(topic) == u"faq"_s;
        const QVariantList categories = faq && document != nullptr ? helpFaq(document->blocks) : QVariantList{};
        guides.append(QVariantMap{{u"slug"_s, topic.slug},
                                  {u"title"_s, pick(topic.titles, shown)},
                                  {u"summary"_s, pick(topic.summaries, shown)},
                                  {u"icon"_s, topic.icon},
                                  {u"tile"_s, topic.tile},
                                  {u"hero"_s, topic.hero},
                                  {u"layout"_s, layoutOf(topic)},
                                  {u"minutes"_s, document != nullptr ? document->minutes : 1},
                                  {u"count"_s, faq ? questionCount(categories)
                                                   : document != nullptr ? document->sections.size() : 0},
                                  {u"groups"_s, categories.size()}});
    }
    QVariantList popular;
    for (const QString& entry : m_popular) {
        const QString slug = entry.section(u'#', 0, 0);
        const QString anchor = entry.section(u'#', 1);
        const HelpTopic* topic = find(slug);
        const HelpDocument* document = topic != nullptr ? pickOf(topic->documents, shown) : nullptr;
        if (document == nullptr) {
            continue;
        }
        const auto section = std::ranges::find_if(document->searchable,
                                                  [&anchor](const HelpSection& found) { return found.anchor == anchor; });
        if (section != document->searchable.end()) {
            popular.append(QVariantMap{{u"slug"_s, slug}, {u"anchor"_s, anchor}, {u"title"_s, section->title}});
        }
    }
    return {{u"guides"_s, guides}, {u"reference"_s, reference}, {u"popular"_s, popular}};
}

QVariantMap HelpLibrary::neighbour(const HelpTopic& topic, int step, const QString& language) const
{
    const auto at = std::ranges::find_if(m_topics, [&topic](const HelpTopic& entry) { return entry.slug == topic.slug; });
    if (at == m_topics.end()) {
        return {};
    }
    const auto index = static_cast<std::ptrdiff_t>(std::distance(m_topics.begin(), at)) + step;
    if (index < 0 || index >= static_cast<std::ptrdiff_t>(m_topics.size())) {
        return {};
    }
    const HelpTopic& next = m_topics[static_cast<std::size_t>(index)];
    if (next.reference != topic.reference || next.extension != topic.extension) {
        return {};
    }
    return {{u"slug"_s, next.slug}, {u"title"_s, pick(next.titles, language)}};
}

void HelpLibrary::load()
{
    const std::optional<QString> indexText = readText(m_root + u"/index.json"_s);
    if (!indexText) {
        qWarning("help: %s/index.json cannot be read", qUtf8Printable(m_root));
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(indexText->toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning("help: %s/index.json is not valid: %s", qUtf8Printable(m_root), qUtf8Printable(error.errorString()));
        return;
    }
    const QJsonObject index = document.object();
    for (const QJsonValue& popular : index.value(u"popular"_s).toArray()) {
        m_popular.append(popular.toString());
    }
    for (const QJsonValue& groupValue : index.value(u"groups"_s).toArray()) {
        const QJsonObject group = groupValue.toObject();
        const std::size_t groupIndex = m_groups.size();
        m_groups.push_back({.titles = titlesOf(group.value(u"title"_s))});
        if (group.value(u"reference"_s).toBool()) {
            loadReference(groupIndex, group.value(u"icon"_s).toString());
            continue;
        }
        for (const QJsonValue& articleValue : group.value(u"articles"_s).toArray()) {
            const QJsonObject entry = articleValue.toObject();
            HelpTopic topic{.slug = entry.value(u"slug"_s).toString(),
                            .icon = entry.value(u"icon"_s).toString(),
                            .tile = entry.value(u"tile"_s).toString(),
                            .hero = entry.value(u"hero"_s).toString(),
                            .layout = entry.value(u"layout"_s).toString(),
                            .group = groupIndex,
                            .titles = titlesOf(entry.value(u"title"_s)),
                            .summaries = titlesOf(entry.value(u"summary"_s)),
                            .meta = entry.value(u"meta"_s).toArray().toVariantList()};
            if (!validSlug(topic.slug) || find(topic.slug) != nullptr) {
                qWarning("help: the article \"%s\" has no usable slug", qUtf8Printable(topic.slug));
                continue;
            }
            for (const QStringView language : kLanguages) {
                const QString code = language.toString();
                if (const std::optional<QString> text = readText(m_root + u'/' + code + u'/' + topic.slug + u".md"_s)) {
                    const QString body = split(*text).body;
                    topic.texts.emplace(code, body);
                    topic.documents.emplace(code, parseHelpMarkdown(body));
                }
            }
            if (topic.texts.empty()) {
                qWarning("help: the article %s has no text", qUtf8Printable(topic.slug));
                continue;
            }
            m_topics.push_back(std::move(topic));
        }
    }
    for (HelpTopic& topic : m_topics) {
        makeSearchable(topic);
    }
}

std::map<QString, QString> HelpLibrary::bundledReference() const
{
    const QDir folder(m_root + u"/api"_s);
    std::map<QString, QString> bundled;
    for (const QString& file : folder.entryList({u"*.md"_s}, QDir::Files, QDir::Name)) {
        if (const std::optional<QString> text = readText(folder.filePath(file))) {
            bundled.emplace(file.chopped(3), *text);
        }
    }
    return bundled;
}

void HelpLibrary::loadReference(std::size_t group, const QString& icon)
{
    const std::map<QString, QString> bundled = bundledReference();
    std::map<QString, Document> documents;
    for (const auto& [stem, text] : bundled) {
        documents.emplace(stem, split(text));
    }
    const std::map<QString, std::map<QString, QString>> translations = translatedReference(bundled);
    QStringList order;
    const auto overview =
        std::ranges::find_if(documents, [](const auto& entry) { return referenceSlug(entry.first) == u"api"; });
    if (overview != documents.end()) {
        order.append(overview->first);
        for (const QString& stem : linkedStems(overview->second.body)) {
            if (documents.contains(stem) && !order.contains(stem)) {
                order.append(stem);
            }
        }
    }
    for (const auto& entry : documents) {
        if (!order.contains(entry.first)) {
            order.append(entry.first);
        }
    }
    for (const QString& stem : order) {
        const Document& document = documents.at(stem);
        HelpTopic topic{.slug = referenceSlug(stem), .icon = icon, .group = group, .reference = true};
        if (!validSlug(topic.slug) || find(topic.slug) != nullptr) {
            qWarning("help: the reference page %s has no usable name", qUtf8Printable(stem));
            continue;
        }
        topic.titles.emplace(kFallback.toString(), document.heading.isEmpty() ? stem : document.heading);
        const QString body = withHelpLinks(document.body);
        topic.texts.emplace(kFallback.toString(), body);
        topic.documents.emplace(kFallback.toString(), parseHelpMarkdown(body));
        for (const auto& [language, pages] : translations) {
            const auto page = pages.find(stem);
            if (page == pages.end()) {
                continue;
            }
            const Document shown = split(page->second);
            topic.titles.emplace(language, shown.heading.isEmpty() ? stem : shown.heading);
            const QString shownBody = withHelpLinks(shown.body);
            topic.texts.emplace(language, shownBody);
            topic.documents.emplace(language, parseHelpMarkdown(shownBody));
        }
        m_topics.push_back(std::move(topic));
    }
}

std::map<QString, std::map<QString, QString>>
HelpLibrary::translatedReference(const std::map<QString, QString>& bundled) const
{
    const QByteArray context(script::kApiTextContext.data(), static_cast<qsizetype>(script::kApiTextContext.size()));
    std::map<QString, std::map<QString, QString>> translations;
    for (const QStringView language : kLanguages) {
        if (language == kFallback) {
            continue;
        }
        const QString code = language.toString();
        QTranslator translator;
        if (!m_translate && !translator.load(u"gamescrcpy_"_s + code, u":/i18n"_s)) {
            continue;
        }
        const auto shown = [&](std::string_view text) {
            const std::string source(text);
            const QString english = QString::fromStdString(source);
            const QString target =
                m_translate ? m_translate(code, english) : translator.translate(context.constData(), source.c_str());
            return (target.isEmpty() ? english : target).toStdString();
        };
        translations.emplace(code, translatedPages(bundled, shown));
    }
    return translations;
}

std::map<QString, QString> HelpLibrary::translatedPages(const std::map<QString, QString>& bundled,
                                                       const std::function<std::string(std::string_view)>& shown)
{
    const auto stemOf = [](const std::string& path) {
        const QString name = QString::fromStdString(path).section(u'/', -1);
        return name.endsWith(u".md"_s) ? name.chopped(3) : name;
    };
    std::map<QString, QString> generated;
    for (const script::ApiFile& page : script::apiDocs({})) {
        generated.emplace(stemOf(page.path), QString::fromStdString(page.contents));
    }
    std::map<QString, QString> pages;
    for (const script::ApiFile& page : script::apiDocs(shown)) {
        const QString stem = stemOf(page.path);
        const auto english = generated.find(stem);
        const auto file = bundled.find(stem);
        if (english != generated.end() && file != bundled.end() && file->second == english->second) {
            pages.emplace(stem, QString::fromStdString(page.contents));
        }
    }
    return pages;
}

void HelpLibrary::followLanguage()
{
    const QString language = m_settings.interfaceLanguage();
    if (language == m_language) {
        return;
    }
    m_language = language;
    m_contents = listed();
    emit languageChanged();
    emit contentsChanged();
}

QVariantList HelpLibrary::listed() const
{
    const QStringList words = wordsOf(m_query);
    const auto matches = [&words](const QString& text) {
        return std::ranges::all_of(words, [&text](const QString& word) { return text.contains(word); });
    };
    QVariantList contents;
    std::optional<std::size_t> heading;
    for (const HelpTopic& topic : m_topics) {
        if (!matches(pick(topic.searchable, m_language))) {
            continue;
        }
        if (heading != topic.group) {
            contents.append(QVariantMap{{u"kind"_s, u"group"_s},
                                        {u"label"_s, pick(m_groups[topic.group].titles, m_language)}});
            heading = topic.group;
        }
        contents.append(QVariantMap{{u"kind"_s, topic.reference ? u"reference"_s : u"article"_s},
                                    {u"slug"_s, topic.slug},
                                    {u"title"_s, pick(topic.titles, m_language)},
                                    {u"icon"_s, topic.icon},
                                    {u"iconPath"_s, topic.iconPath},
                                    {u"tile"_s, topic.tile}});
        const HelpDocument* document = pickOf(topic.documents, m_language);
        if (words.isEmpty() || document == nullptr) {
            continue;
        }
        int hits = 0;
        for (const HelpSection& section : document->searchable) {
            if (!matches((section.title + u'\n' + section.text).toCaseFolded())) {
                continue;
            }
            contents.append(QVariantMap{{u"kind"_s, u"section"_s},
                                        {u"of"_s, topic.slug},
                                        {u"anchor"_s, section.anchor},
                                        {u"title"_s, section.title}});
            if (++hits == kMostSectionHits) {
                break;
            }
        }
    }
    return contents;
}

}
