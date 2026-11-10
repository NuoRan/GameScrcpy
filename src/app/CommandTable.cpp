#include "CommandTable.h"

#include <algorithm>

namespace gs::app {

namespace {

bool isLower(QChar c)
{
    return c >= u'a' && c <= u'z';
}

bool isWordChar(QChar c)
{
    return isLower(c) || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9');
}

}

bool CommandTable::validId(QStringView id)
{
    if (id.size() > kMaxIdLength) {
        return false;
    }
    int words = 0;
    bool wordStart = true;
    for (const QChar c : id) {
        if (c == u'.') {
            if (wordStart) {
                return false;
            }
            wordStart = true;
            continue;
        }
        if (wordStart) {
            if (!isLower(c)) {
                return false;
            }
            ++words;
            wordStart = false;
        } else if (!isWordChar(c)) {
            return false;
        }
    }
    return !wordStart && words >= 2;
}

bool CommandTable::add(Command command)
{
    if (!validId(command.id) || !command.run || find(command.id) != nullptr) {
        return false;
    }
    if (!command.enabled) {
        command.enabled = [](const QString&) { return true; };
    }
    m_commands.push_back(std::move(command));
    return true;
}

const Command* CommandTable::find(QStringView id) const
{
    const auto found = std::ranges::find_if(m_commands, [id](const Command& command) { return command.id == id; });
    return found == m_commands.end() ? nullptr : &*found;
}

}
