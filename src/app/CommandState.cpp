#include "CommandState.h"

#include "Commands.h"

namespace gs::app {

CommandState::CommandState(QObject* parent) : QObject(parent)
{
    if (Commands* commands = Commands::instance()) {
        connect(commands, &Commands::stateChanged, this, &CommandState::update);
    }
}

void CommandState::setCommand(const QString& command)
{
    if (command == m_command) {
        return;
    }
    m_command = command;
    emit commandChanged();
    update();
}

void CommandState::setTarget(const QString& target)
{
    if (target == m_target) {
        return;
    }
    m_target = target;
    emit targetChanged();
    update();
}

bool CommandState::trigger()
{
    Commands* commands = Commands::instance();
    return commands != nullptr && commands->trigger(m_command, m_target);
}

void CommandState::update()
{
    const Commands* commands = Commands::instance();
    if (commands == nullptr) {
        return;
    }
    const bool enabled = commands->isEnabled(m_command, m_target);
    const bool checked = commands->isChecked(m_command, m_target);
    const QString title = commands->title(m_command, m_target);
    const QString icon = commands->icon(m_command);
    const QString shortcut = commands->shortcutText(m_command);
    if (enabled == m_enabled && checked == m_checked && title == m_title && icon == m_icon &&
        shortcut == m_shortcut) {
        return;
    }
    m_enabled = enabled;
    m_checked = checked;
    m_title = title;
    m_icon = icon;
    m_shortcut = shortcut;
    emit changed();
}

}
