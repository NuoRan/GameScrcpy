#pragma once

#include "HelpLibrary.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTranslator>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

class AppSettings;
class ExtensionHost;

struct LanguagePack {
    QString locale;
    QString name;
    QString extension;
    QString extensionName;
    QString translations;
    QString help;

    friend bool operator==(const LanguagePack&, const LanguagePack&) = default;
};

[[nodiscard]] std::vector<LanguagePack> extensionLanguagePacks(const ExtensionHost& host, const QString& language);
[[nodiscard]] bool loadTranslationFile(QTranslator& translator, QByteArray& data, const QString& path);

class LanguagePacks : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList packs READ packList NOTIFY packsChanged FINAL)
    Q_PROPERTY(QVariantMap missing READ missing NOTIFY missingChanged FINAL)

public:
    explicit LanguagePacks(AppSettings& settings, QObject* parent = nullptr);
    ~LanguagePacks() override;
    LanguagePacks(const LanguagePacks&) = delete;
    LanguagePacks& operator=(const LanguagePacks&) = delete;

    static LanguagePacks* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static LanguagePacks* instance() noexcept { return s_instance; }

    void follow(ExtensionHost& extensions);
    void setPacks(std::vector<LanguagePack> packs);
    [[nodiscard]] const std::vector<LanguagePack>& packs() const noexcept { return m_packs; }
    [[nodiscard]] const LanguagePack* find(const QString& locale) const;
    [[nodiscard]] std::vector<HelpLanguage> helpLanguages() const;
    [[nodiscard]] QVariantList packList() const;
    [[nodiscard]] QVariantMap missing() const { return m_missing; }
    [[nodiscard]] QString loaded() const { return m_loaded; }

signals:
    void packsChanged();
    void missingChanged();
    void translationsChanged();

private:
    void apply();
    void settle();
    [[nodiscard]] QVariantMap missingNow() const;

    static inline LanguagePacks* s_instance = nullptr;

    AppSettings& m_settings;
    const ExtensionHost* m_host = nullptr;
    std::vector<LanguagePack> m_packs;
    QVariantMap m_missing;
    QByteArray m_packData;
    QTranslator m_builtin;
    QTranslator m_pack;
    QString m_language;
    QString m_source;
    QString m_loaded;
    bool m_applied = false;
};

void watchLanguageHelp(HelpLibrary& help, LanguagePacks& languages);

}
