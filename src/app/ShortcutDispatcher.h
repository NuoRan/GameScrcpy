#pragma once

#include <QKeyEvent>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>
#include <utility>

class QQuickWindow;

namespace gs::app {

class Commands;

class ShortcutDispatcher final : public QObject
{
    Q_OBJECT

public:
    using MirrorOf = std::function<std::optional<QString>(const QQuickWindow&)>;
    using Blocked = std::function<bool(const QString& serial, const QKeyEvent& key)>;
    using TopicOf = std::function<QString(const QQuickWindow&)>;

    ShortcutDispatcher(Commands& commands, MirrorOf mirrorOf, QObject* parent = nullptr);

    void setBlocked(Blocked blocked) { m_blocked = std::move(blocked); }
    void setHelpTopic(TopicOf topicOf) { m_topicOf = std::move(topicOf); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    Commands& m_commands;
    MirrorOf m_mirrorOf;
    Blocked m_blocked;
    TopicOf m_topicOf;
    QSet<quint32> m_held;
};

}
