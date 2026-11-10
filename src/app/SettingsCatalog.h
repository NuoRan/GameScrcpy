#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace gs::app {

struct SettingInfo {
    QString path;
    QString section;
    QString title;
    QString description;
    QString kind;
    QStringList choices;
    QVariant defaultValue;
    qint64 minimum = 0;
    qint64 maximum = 0;
    bool perDevice = false;
};

[[nodiscard]] std::vector<SettingInfo> settingInfos();

class SettingsCatalog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap entries READ entries NOTIFY entriesChanged FINAL)
    Q_PROPERTY(QVariantList extensionGroups READ extensionGroups NOTIFY entriesChanged FINAL)
    Q_PROPERTY(QStringList perDevicePages READ perDevicePages NOTIFY entriesChanged FINAL)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    Q_PROPERTY(bool searching READ searching NOTIFY queryChanged FINAL)
    Q_PROPERTY(QString scopeDevice READ scopeDevice NOTIFY scopeChanged FINAL)
    Q_PROPERTY(QString scopeDeviceName READ scopeDeviceName NOTIFY scopeChanged FINAL)
    Q_PROPERTY(bool scoped READ scoped WRITE setScoped NOTIFY scopeChanged FINAL)
    Q_PROPERTY(QString device READ device NOTIFY scopeChanged FINAL)
    Q_PROPERTY(QVariantMap hits READ hits NOTIFY filterChanged FINAL)
    Q_PROPERTY(QVariantMap pageHits READ pageHits NOTIFY filterChanged FINAL)
    Q_PROPERTY(int hitCount READ hitCount NOTIFY filterChanged FINAL)
    Q_PROPERTY(QVariantMap pageOverrides READ pageOverrides NOTIFY overridesChanged FINAL)

public:
    explicit SettingsCatalog(QObject* parent = nullptr);

    [[nodiscard]] QVariantMap entries() const;
    [[nodiscard]] QVariantList extensionGroups() const;
    [[nodiscard]] QStringList perDevicePages() const;
    [[nodiscard]] QString query() const;
    void setQuery(const QString& query);
    [[nodiscard]] bool searching() const;
    [[nodiscard]] QString scopeDevice() const;
    [[nodiscard]] QString scopeDeviceName() const;
    [[nodiscard]] bool scoped() const;
    void setScoped(bool scoped);
    [[nodiscard]] QString device() const;
    [[nodiscard]] QVariantMap hits() const;
    [[nodiscard]] QVariantMap pageHits() const;
    [[nodiscard]] int hitCount() const;
    [[nodiscard]] QVariantMap pageOverrides() const;

    Q_INVOKABLE void openForDevice(const QString& id, const QString& name);
    void retranslate();

signals:
    void entriesChanged();
    void queryChanged();
    void scopeChanged();
    void filterChanged();
    void openRequested();
    void overridesChanged();

private:
    struct Searchable {
        QString path;
        QString page;
        bool perDevice = false;
        QStringList texts;
    };

    void rebuild();
    void addExtensionSettings();
    void refilter();
    void recount();

    std::vector<Searchable> m_searchable;
    QVariantMap m_entries;
    QVariantList m_extensionGroups;
    QStringList m_perDevicePages;
    QString m_query;
    QString m_scopeDevice;
    QString m_scopeDeviceName;
    bool m_scoped = false;
    QVariantMap m_hits;
    QVariantMap m_pageHits;
    QVariantMap m_pageOverrides;
};

}
