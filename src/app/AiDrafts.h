#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace gs::app {

struct AiDraftFile {
    QString path;
    QString state;
    int added = 0;
    int removed = 0;
    bool conflict = false;
};

class AiDraftStore : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    [[nodiscard]] virtual QStringList targets() const = 0;
    [[nodiscard]] virtual std::vector<AiDraftFile> diff(const QString& target) const = 0;
    [[nodiscard]] virtual QString fileDiff(const QString& target, const QString& path) const = 0;
    virtual bool apply(const QString& target, QString& problem) = 0;
    virtual void discard(const QString& target) = 0;

signals:
    void changed();
};

class AiDrafts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList drafts READ drafts NOTIFY draftsChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY draftsChanged FINAL)

public:
    explicit AiDrafts(QObject* parent = nullptr);
    explicit AiDrafts(AiDraftStore& store, QObject* parent = nullptr);

    [[nodiscard]] QVariantList drafts() const { return m_drafts; }
    [[nodiscard]] int count() const { return static_cast<int>(m_drafts.size()); }

    Q_INVOKABLE QString fileDiff(const QString& target, const QString& path) const;
    Q_INVOKABLE QString apply(const QString& target);
    Q_INVOKABLE void discard(const QString& target);
    Q_INVOKABLE void refresh();

signals:
    void draftsChanged();

private:
    void attach(AiDraftStore* store);

    QPointer<AiDraftStore> m_store;
    QVariantList m_drafts;
};

}
