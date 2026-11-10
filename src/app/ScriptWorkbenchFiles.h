#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <cstdint>
#include <vector>

namespace gs::app {

enum class ScriptFileKind : std::uint8_t { Code, Text, Image, Profile, Other };

struct ScriptFile {
    QString path;
    QString name;
    int depth = 0;
    bool folder = false;
    bool open = true;
    ScriptFileKind kind = ScriptFileKind::Other;
};

[[nodiscard]] bool shownInTree(QStringView name);
[[nodiscard]] ScriptFileKind fileKindOf(QStringView path);
[[nodiscard]] std::vector<ScriptFile> scriptFiles(const QString& folder, const QSet<QString>& collapsed);
[[nodiscard]] QStringList watchedFolders(const QString& folder);
[[nodiscard]] QString logicEntryIn(const QString& folder);

struct ScriptProblem {
    QString file;
    bool inside = false;
    int line = 0;
    int column = 0;
    QString severity;
    QString code;
    QString message;
    QString source;
};

struct CheckOutcome {
    bool ran = false;
    QString failure;
    std::vector<ScriptProblem> problems;
};

[[nodiscard]] CheckOutcome parseCheckOutput(const QByteArray& output, const QString& folder);

[[nodiscard]] QString starterLogic(const QStringList& controlIds);
[[nodiscard]] QString controlStarter(const QString& id, const QString& shape);
[[nodiscard]] QString controlAccess(const QString& id);
[[nodiscard]] QByteArray profileTsconfig();

struct LogicSnippet {
    QString text;
    int caret = 0;
};

[[nodiscard]] LogicSnippet referenceSnippet(const QString& id);
[[nodiscard]] QString localizedCheckMessage(const QString& code, const QString& message);

enum class LogicWrite : std::uint8_t { Written, Exists, Busy, Failed };

[[nodiscard]] LogicWrite writeStarterLogic(const QString& folder, const QStringList& controlIds);
[[nodiscard]] QString checkedFileName(const QString& typed, QString& problem);

}
