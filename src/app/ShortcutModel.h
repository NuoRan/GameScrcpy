#pragma once

#include "Commands.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QVariant>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace gs::app {

class ShortcutModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool device READ device WRITE setDevice NOTIFY deviceChanged FINAL)
    Q_PROPERTY(bool filtered READ filtered WRITE setFiltered NOTIFY filterChanged FINAL)
    Q_PROPERTY(QVariantMap hits READ hits WRITE setHits NOTIFY filterChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        CommandIdRole = Qt::UserRole + 1,
        TitleRole,
        KeysRole,
        DefaultTextRole,
        DefaultPortableRole,
        CustomRole,
        ConflictedRole,
    };
    Q_ENUM(Role)

    explicit ShortcutModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool device() const { return m_device; }
    void setDevice(bool device);
    [[nodiscard]] bool filtered() const { return m_filtered; }
    void setFiltered(bool filtered);
    [[nodiscard]] QVariantMap hits() const { return m_hits; }
    void setHits(const QVariantMap& hits);
    [[nodiscard]] int count() const { return static_cast<int>(m_rows.size()); }

signals:
    void deviceChanged();
    void filterChanged();
    void countChanged();

private:
    [[nodiscard]] std::vector<ShortcutInfo> build() const;
    void refresh();

    bool m_device = false;
    bool m_filtered = false;
    QVariantMap m_hits;
    std::vector<ShortcutInfo> m_rows;
};

}
