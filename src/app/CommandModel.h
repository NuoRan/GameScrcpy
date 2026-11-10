#pragma once

#include "CommandMatcher.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace gs::app {

class CommandModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY targetChanged FINAL)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int sectionCount READ sectionCount NOTIFY sectionCountChanged FINAL)

public:
    enum Role {
        CommandIdRole = Qt::UserRole + 1,
        KindRole,
        TitleRole,
        HighlightsRole,
        SubtitleRole,
        IconRole,
        ShortcutRole,
        TargetNameRole,
        CheckedRole,
        SectionRole,
    };
    Q_ENUM(Role)

    explicit CommandModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString query() const { return m_query; }
    void setQuery(const QString& query);
    [[nodiscard]] QString target() const { return m_target; }
    void setTarget(const QString& target);
    [[nodiscard]] bool active() const { return m_active; }
    void setActive(bool active);
    [[nodiscard]] int count() const { return static_cast<int>(m_rows.size()); }
    [[nodiscard]] int sectionCount() const { return m_sectionCount; }

    Q_INVOKABLE bool trigger(int row);
    Q_INVOKABLE bool triggerChoice(const QString& group, const QString& commandId);
    Q_INVOKABLE QVariantList choices(int row) const;

signals:
    void queryChanged();
    void targetChanged();
    void activeChanged();
    void countChanged();
    void sectionCountChanged();

private:
    struct Row {
        QString id;
        bool group = false;
        QString target;
        QString title;
        std::vector<TextRange> highlights;
        QString subtitle;
        QString icon;
        QStringList shortcut;
        QString targetName;
        bool checked = false;
        QStringList members;
        QString section;

        friend bool operator==(const Row&, const Row&) = default;
    };

    [[nodiscard]] std::vector<Row> build() const;
    [[nodiscard]] static int sectionsIn(const std::vector<Row>& rows);
    [[nodiscard]] const Row* rowAt(int row) const;
    void rebuild();

    QString m_query;
    QString m_target;
    bool m_active = true;
    bool m_stale = false;
    int m_sectionCount = 0;
    std::vector<Row> m_rows;
};

}
