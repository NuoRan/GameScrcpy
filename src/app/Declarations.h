#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>
#include <utility>
#include <vector>

namespace gs::app {

struct DeclaredSpec {
    QString name;
    QString kind;
    QVariant value;
    QString label;
    std::optional<double> min;
    std::optional<double> max;
    QString of;
};

struct DefaultClass {
    QString name;
    QString base;
    QString from;
    QString body;
};

[[nodiscard]] std::vector<DeclaredSpec> declaredIn(const QString& code, const QString& field, const QString& maker,
                                                    const QStringList& kinds);
[[nodiscard]] QString chordForKey(const QString& name);
[[nodiscard]] DefaultClass defaultClassIn(const QString& code);
[[nodiscard]] std::vector<std::pair<QString, QString>> childCallsIn(const QString& body);
[[nodiscard]] QString builtInType(const QString& className);
[[nodiscard]] QString builtInClass(const QString& type);

}
