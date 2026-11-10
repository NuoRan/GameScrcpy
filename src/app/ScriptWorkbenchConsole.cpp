#include "ScriptWorkbenchConsole.h"

#include <QDir>

#include <utility>

namespace gs::app {

QString folderKey(const QString& folder)
{
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(folder));
    while (key.size() > 3 && key.endsWith(u'/')) {
        key.chop(1);
    }
    return key.toCaseFolded();
}

bool sameFolder(const QString& first, const QString& second)
{
    return !first.isEmpty() && !second.isEmpty() && folderKey(first) == folderKey(second);
}

void LogicConsole::append(const QString& serial, LogicReport report)
{
    std::deque<LogicReport>& lines = m_lines[serial];
    lines.push_back(std::move(report));
    while (lines.size() > kMostLines) {
        lines.pop_front();
    }
}

std::vector<LogicReport> LogicConsole::lines(const QString& serial, const QString& folder) const
{
    std::vector<LogicReport> shown;
    const auto found = m_lines.find(serial);
    if (found == m_lines.end()) {
        return shown;
    }
    for (const LogicReport& report : found->second) {
        if (sameFolder(report.folder, folder)) {
            shown.push_back(report);
        }
    }
    return shown;
}

std::size_t LogicConsole::size(const QString& serial) const
{
    const auto found = m_lines.find(serial);
    return found == m_lines.end() ? 0 : found->second.size();
}

void LogicConsole::clear(const QString& serial, const QString& folder)
{
    const auto found = m_lines.find(serial);
    if (found == m_lines.end()) {
        return;
    }
    std::erase_if(found->second, [&folder](const LogicReport& report) { return sameFolder(report.folder, folder); });
    if (found->second.empty()) {
        m_lines.erase(found);
    }
}

}
