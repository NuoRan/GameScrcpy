#pragma once

#include <QKeyCombination>
#include <QString>
#include <QStringView>

#include <cstdint>
#include <functional>
#include <vector>

namespace gs::app {

enum class CommandScope : std::uint8_t { Global, Hub, Mirror };
enum class CommandTarget : std::uint8_t { None, Device, Topic };

struct Command {
    QString id;
    const char* title = "";
    const char* checkedTitle = nullptr;
    const char* description = "";
    const char* keywords = "";
    QString icon;
    QString group;
    const char* groupTitle = nullptr;
    CommandScope scope = CommandScope::Global;
    CommandTarget target = CommandTarget::None;
    QKeyCombination shortcut;
    std::function<bool(const QString& target)> enabled;
    std::function<bool(const QString& target)> checked;
    std::function<void(const QString& target)> run;
};

class CommandTable
{
public:
    static constexpr qsizetype kMaxIdLength = 64;

    [[nodiscard]] static bool validId(QStringView id);

    bool add(Command command);
    [[nodiscard]] const Command* find(QStringView id) const;
    [[nodiscard]] const std::vector<Command>& all() const noexcept { return m_commands; }

private:
    std::vector<Command> m_commands;
};

}
