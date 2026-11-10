#pragma once

#include "Declarations.h"
#include "ExtensionContent.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <filesystem>
#include <map>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

[[nodiscard]] std::vector<DeclaredSpec> componentPropsIn(const QString& code);

struct ComponentChild {
    QString name;
    QString type;
};

struct ComponentEntry {
    QString ref;
    QString name;
    QString description;
    QString icon;
    QString category;
    QString folder;
    bool example = false;
    std::vector<DeclaredSpec> props;
    QString type;
    QString parent;
    QString className;
    QString base;
    std::vector<ComponentChild> children;
    QString extensionId;
    QString extensionName;
};

class ComponentLibrary final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList examples READ examples NOTIFY componentsChanged FINAL)
    Q_PROPERTY(QVariantList mine READ mine NOTIFY componentsChanged FINAL)
    Q_PROPERTY(QStringList icons READ icons CONSTANT FINAL)

public:
    ComponentLibrary(std::filesystem::path examples, std::filesystem::path mine, QObject* parent = nullptr);
    ~ComponentLibrary() override;
    ComponentLibrary(const ComponentLibrary&) = delete;
    ComponentLibrary& operator=(const ComponentLibrary&) = delete;

    static ComponentLibrary* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static ComponentLibrary* instance();

    [[nodiscard]] QVariantList examples() const { return m_examples; }
    [[nodiscard]] QVariantList mine() const { return m_mine; }
    [[nodiscard]] static QStringList icons();
    [[nodiscard]] const std::vector<ComponentEntry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] const ComponentEntry* find(const QString& ref) const;
    [[nodiscard]] std::filesystem::path folderOf(const QString& ref) const;
    [[nodiscard]] bool extends(const QString& ref, const QString& base) const;
    [[nodiscard]] const std::filesystem::path& mineDirectory() const noexcept { return m_mineDirectory; }
    [[nodiscard]] static QString entryName() { return QStringLiteral("component.ts"); }

    Q_INVOKABLE QVariantMap info(const QString& ref) const;
    Q_INVOKABLE QString add(const QString& name, const QString& base = {});
    Q_INVOKABLE QString copyToMine(const QString& ref);
    Q_INVOKABLE bool setInfo(const QString& ref, const QString& key, const QString& value);
    Q_INVOKABLE bool remove(const QString& ref);
    Q_INVOKABLE void reveal(const QString& ref) const;
    Q_INVOKABLE void reload();
    QString install(const std::filesystem::path& source);
    void setExtensionComponents(std::vector<ExtensionComponent> components);
    Q_INVOKABLE bool exportPackage(const QString& ref, const QString& fileOrUrl) const;

signals:
    void componentsChanged();
    void componentSaved(const QString& ref);

private:
    void scan(const std::filesystem::path& directory, bool example, std::vector<ComponentEntry>& into) const;
    bool writeInfo(const std::filesystem::path& folder, const ComponentEntry& entry) const;
    QString freeFolder(const QString& wanted) const;
    QString freeName(const QString& wanted) const;
    void refreshLists();
    void watch();

    std::filesystem::path m_examplesDirectory;
    std::filesystem::path m_mineDirectory;
    std::vector<ExtensionComponent> m_extensionComponents;
    std::vector<ComponentEntry> m_entries;
    QVariantList m_examples;
    QVariantList m_mine;
    std::map<QString, QByteArray> m_stamps;
    bool m_loaded = false;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
};

}
