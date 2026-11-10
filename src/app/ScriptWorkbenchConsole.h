#pragma once

#include <gs/base/Log.h>
#include <gs/script/Report.h>

#include <QDateTime>
#include <QString>

#include <cstddef>
#include <deque>
#include <map>
#include <vector>

namespace gs::app {

struct LogicReport {
    script::ReportKind kind = script::ReportKind::Log;
    log::Level level = log::Level::Info;
    QString text;
    QString file;
    int line = 0;
    int column = 0;
    QString folder;
    QDateTime time;
};

struct LogicPhone {
    QString serial;
    QString name;
    QString folder;
    bool started = false;
    bool running = false;
    bool halted = false;
    QString error;
};

class LogicConsole
{
public:
    static constexpr std::size_t kMostLines = 1000;

    void append(const QString& serial, LogicReport report);
    [[nodiscard]] std::vector<LogicReport> lines(const QString& serial, const QString& folder) const;
    [[nodiscard]] std::size_t size(const QString& serial) const;
    void clear(const QString& serial, const QString& folder);

private:
    std::map<QString, std::deque<LogicReport>> m_lines;
};

[[nodiscard]] QString folderKey(const QString& folder);
[[nodiscard]] bool sameFolder(const QString& first, const QString& second);

}
