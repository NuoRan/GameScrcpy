#pragma once

#include <QString>
#include <QStringView>
#include <QVariantList>
#include <QVariantMap>

#include <vector>

namespace gs::app {

struct HelpSection {
    QString anchor;
    QString title;
    int level = 2;
    QString text;
};

struct HelpDocument {
    QString lead;
    QVariantList blocks;
    QVariantList sections;
    std::vector<HelpSection> searchable;
    int minutes = 1;
};

[[nodiscard]] QString helpInline(QStringView text);
[[nodiscard]] QString helpPlain(QStringView text);
[[nodiscard]] QString helpCodeHtml(const QString& code, const QString& language);
[[nodiscard]] HelpDocument parseHelpMarkdown(const QString& markdown);
[[nodiscard]] QVariantList helpTabs(const QVariantList& blocks);
[[nodiscard]] QVariantList helpFaq(const QVariantList& blocks);
[[nodiscard]] QVariantList helpReference(const QVariantList& blocks);
[[nodiscard]] QVariantList helpReferenceSections(const QVariantList& blocks);

}
