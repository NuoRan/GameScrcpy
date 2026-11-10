#include "AiDrafts.h"

#include "AiAssistant.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <utility>

namespace gs::app {

using namespace Qt::StringLiterals;

AiDrafts::AiDrafts(QObject* parent)
    : QObject(parent)
{
    if (AiAssistant* assistant = AiAssistant::instance()) {
        attach(&assistant->drafts());
    }
}

AiDrafts::AiDrafts(AiDraftStore& store, QObject* parent)
    : QObject(parent)
{
    attach(&store);
}

QString AiDrafts::fileDiff(const QString& target, const QString& path) const
{
    return m_store != nullptr ? m_store->fileDiff(target, path) : QString();
}

QString AiDrafts::apply(const QString& target)
{
    const QString failed = QCoreApplication::translate("AiDrafts", "The draft could not be applied.");
    if (m_store == nullptr) {
        return failed;
    }
    QString problem;
    const bool applied = m_store->apply(target, problem);
    refresh();
    if (applied) {
        qInfo("ai draft %s: applied by the user", qUtf8Printable(target));
        return {};
    }
    qInfo("ai draft %s: not applied", qUtf8Printable(target));
    return problem.isEmpty() ? failed : problem;
}

void AiDrafts::discard(const QString& target)
{
    if (m_store == nullptr) {
        return;
    }
    m_store->discard(target);
    qInfo("ai draft %s: discarded by the user", qUtf8Printable(target));
    refresh();
}

void AiDrafts::refresh()
{
    QVariantList next;
    if (m_store != nullptr) {
        for (const QString& target : m_store->targets()) {
            QVariantList files;
            int added = 0;
            int removed = 0;
            int conflicts = 0;
            for (const AiDraftFile& file : m_store->diff(target)) {
                files.append(QVariantMap{{u"path"_s, file.path},
                                         {u"state"_s, file.state},
                                         {u"added"_s, file.added},
                                         {u"removed"_s, file.removed},
                                         {u"conflict"_s, file.conflict}});
                added += file.added;
                removed += file.removed;
                conflicts += file.conflict ? 1 : 0;
            }
            if (files.isEmpty()) {
                continue;
            }
            const qsizetype colon = target.indexOf(u':');
            next.append(QVariantMap{{u"target"_s, target},
                                    {u"kind"_s, colon > 0 ? target.left(colon) : QString()},
                                    {u"id"_s, colon > 0 ? target.mid(colon + 1) : target},
                                    {u"files"_s, files},
                                    {u"added"_s, added},
                                    {u"removed"_s, removed},
                                    {u"conflicts"_s, conflicts}});
        }
    }
    if (next != m_drafts) {
        m_drafts = std::move(next);
        emit draftsChanged();
    }
}

void AiDrafts::attach(AiDraftStore* store)
{
    m_store = store;
    connect(store, &AiDraftStore::changed, this, &AiDrafts::refresh);
    refresh();
}

}
