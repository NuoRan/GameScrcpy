#include "SettingsSearch.h"

#include <algorithm>

namespace gs::app {

bool matchesQuery(const QStringList& texts, const QString& query)
{
    const QStringList terms = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return std::ranges::all_of(terms, [&texts](const QString& term) {
        return std::ranges::any_of(texts,
                                   [&term](const QString& text) { return text.contains(term, Qt::CaseInsensitive); });
    });
}

}
