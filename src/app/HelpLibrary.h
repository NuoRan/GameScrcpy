#pragma once

#include "HelpMarkdown.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

class AppSettings;

struct HelpTopic {
    QString slug;
    QString icon;
    QString tile;
    QString hero;
    QString layout;
    std::size_t group = 0;
    bool reference = false;
    std::map<QString, QString> titles;
    std::map<QString, QString> summaries;
    std::map<QString, QString> texts;
    std::map<QString, QString> searchable;
    std::map<QString, HelpDocument> documents;
    QVariantList meta;
    QString extension;
    QString extensionName;
    QString iconPath;
    QUrl base;
};

struct HelpGroup {
    std::map<QString, QString> titles;
};

struct ExtensionHelp {
    QString extension;
    QString extensionName;
    QString id;
    std::map<QString, QString> titles;
    std::map<QString, QString> texts;
    QString icon;
    QString iconPath;
    QUrl base;
};

struct HelpLanguage {
    QString code;
    QString translations;
    QString folder;

    friend bool operator==(const HelpLanguage&, const HelpLanguage&) = default;
};

class HelpLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString language READ language NOTIFY languageChanged FINAL)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    Q_PROPERTY(QVariantList contents READ contents NOTIFY contentsChanged FINAL)
    Q_PROPERTY(QString firstArticle READ firstArticle CONSTANT FINAL)

public:
    static constexpr int kMostSectionHits = 5;

    using Translate = std::function<QString(const QString& language, const QString& text)>;

    explicit HelpLibrary(const AppSettings& settings, QString root = QStringLiteral(":/help"), Translate translate = {},
                         QObject* parent = nullptr);
    ~HelpLibrary() override;

    static HelpLibrary* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] QString language() const { return m_language; }
    [[nodiscard]] QString query() const { return m_query; }
    void setQuery(const QString& query);
    [[nodiscard]] QVariantList contents() const { return m_contents; }
    [[nodiscard]] QString firstArticle() const;
    [[nodiscard]] const std::vector<HelpTopic>& topics() const noexcept { return m_topics; }
    [[nodiscard]] const std::vector<HelpGroup>& groups() const noexcept { return m_groups; }
    [[nodiscard]] const HelpTopic* find(QStringView slug) const;
    [[nodiscard]] QUrl baseUrl() const;
    void setExtensionTopics(std::vector<ExtensionHelp> topics);
    void setLanguagePacks(std::vector<HelpLanguage> packs);

    Q_INVOKABLE bool contains(const QString& slug) const;
    Q_INVOKABLE QVariantMap article(const QString& slug, const QString& language = {}) const;
    Q_INVOKABLE QVariantMap home(const QString& language = {}) const;

signals:
    void languageChanged();
    void queryChanged();
    void contentsChanged();

private:
    void load();
    void loadReference(std::size_t group, const QString& icon);
    [[nodiscard]] std::map<QString, std::map<QString, QString>>
    translatedReference(const std::map<QString, QString>& bundled) const;
    [[nodiscard]] std::map<QString, QString> bundledReference() const;
    [[nodiscard]] static std::map<QString, QString>
    translatedPages(const std::map<QString, QString>& bundled, const std::function<std::string(std::string_view)>& shown);
    void addPack(const HelpLanguage& pack, const std::map<QString, QString>& bundled);
    [[nodiscard]] QStringList languages() const;
    void makeSearchable(HelpTopic& topic) const;
    void followLanguage();
    [[nodiscard]] QVariantList listed() const;
    [[nodiscard]] QVariantMap neighbour(const HelpTopic& topic, int step, const QString& language) const;
    void decorate(QVariantList& blocks) const;
    [[nodiscard]] QString textIn(const QString& language, const char* text) const;

    static inline HelpLibrary* s_instance = nullptr;

    const AppSettings& m_settings;
    QString m_root;
    Translate m_translate;
    QString m_language;
    QString m_query;
    std::vector<HelpGroup> m_groups;
    std::vector<HelpTopic> m_topics;
    QStringList m_popular;
    QVariantList m_contents;
    std::optional<std::size_t> m_extensionGroup;
    std::vector<HelpLanguage> m_packs;
};

}
