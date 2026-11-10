#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace gs::app {

class CommandState : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString command READ command WRITE setCommand NOTIFY commandChanged FINAL)
    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY targetChanged FINAL)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed FINAL)
    Q_PROPERTY(bool checked READ checked NOTIFY changed FINAL)
    Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
    Q_PROPERTY(QString icon READ icon NOTIFY changed FINAL)
    Q_PROPERTY(QString shortcut READ shortcut NOTIFY changed FINAL)

public:
    explicit CommandState(QObject* parent = nullptr);

    [[nodiscard]] QString command() const { return m_command; }
    void setCommand(const QString& command);
    [[nodiscard]] QString target() const { return m_target; }
    void setTarget(const QString& target);
    [[nodiscard]] bool enabled() const { return m_enabled; }
    [[nodiscard]] bool checked() const { return m_checked; }
    [[nodiscard]] QString title() const { return m_title; }
    [[nodiscard]] QString icon() const { return m_icon; }
    [[nodiscard]] QString shortcut() const { return m_shortcut; }

    Q_INVOKABLE bool trigger();

signals:
    void commandChanged();
    void targetChanged();
    void changed();

private:
    void update();

    QString m_command;
    QString m_target;
    bool m_enabled = false;
    bool m_checked = false;
    QString m_title;
    QString m_icon;
    QString m_shortcut;
};

}
