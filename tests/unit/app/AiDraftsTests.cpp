#include "AiDrafts.h"
#include "McpTestSupport.h"

#include <doctest/doctest.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <map>
#include <utility>
#include <vector>

using gs::app::AiDraftFile;
using gs::app::AiDrafts;
using namespace Qt::StringLiterals;

namespace {

class FakeStore final : public gs::app::AiDraftStore
{
public:
    [[nodiscard]] QStringList targets() const override { return order; }

    [[nodiscard]] std::vector<AiDraftFile> diff(const QString& target) const override
    {
        const auto found = files.find(target);
        return found != files.end() ? found->second : std::vector<AiDraftFile>();
    }

    [[nodiscard]] QString fileDiff(const QString& target, const QString& path) const override
    {
        return u"--- "_s + target + u"/"_s + path + u"\n+++ draft\n@@ -1 +1 @@\n-old\n+new"_s;
    }

    bool apply(const QString& target, QString& problem) override
    {
        applied.append(target);
        if (refuse) {
            problem = u"The disk is full"_s;
            return false;
        }
        forget(target);
        return true;
    }

    void discard(const QString& target) override
    {
        discarded.append(target);
        forget(target);
    }

    void put(const QString& target, std::vector<AiDraftFile> entries)
    {
        if (!order.contains(target)) {
            order.append(target);
        }
        files[target] = std::move(entries);
        emit changed();
    }

    void forget(const QString& target)
    {
        order.removeAll(target);
        files.erase(target);
        emit changed();
    }

    QStringList order;
    std::map<QString, std::vector<AiDraftFile>> files;
    QStringList applied;
    QStringList discarded;
    bool refuse = false;
};

}

TEST_CASE("the draft card follows the drafts as the store reports them")
{
    gs::test::McpApplication application;
    FakeStore store;
    AiDrafts drafts(store);
    int changes = 0;
    QObject::connect(&drafts, &AiDrafts::draftsChanged, [&changes] { ++changes; });
    CHECK(drafts.count() == 0);

    store.put(u"profile:shooter"_s,
              {AiDraftFile{.path = u"logic.ts"_s, .state = u"modified"_s, .added = 3, .removed = 1},
               AiDraftFile{.path = u"profile.json"_s, .state = u"modified"_s, .added = 1, .conflict = true}});
    CHECK(changes == 1);
    REQUIRE(drafts.count() == 1);
    const QVariantMap card = drafts.drafts().at(0).toMap();
    CHECK(card.value(u"target"_s).toString() == u"profile:shooter");
    CHECK(card.value(u"kind"_s).toString() == u"profile");
    CHECK(card.value(u"id"_s).toString() == u"shooter");
    CHECK(card.value(u"added"_s).toInt() == 4);
    CHECK(card.value(u"removed"_s).toInt() == 1);
    CHECK(card.value(u"conflicts"_s).toInt() == 1);
    const QVariantList files = card.value(u"files"_s).toList();
    REQUIRE(files.size() == 2);
    CHECK(files.at(0).toMap().value(u"path"_s).toString() == u"logic.ts");
    CHECK(files.at(1).toMap().value(u"conflict"_s).toBool());

    store.put(u"component:mine/burst"_s, {});
    CHECK(drafts.count() == 1);
    store.put(u"component:mine/burst"_s, {AiDraftFile{.path = u"main.ts"_s, .state = u"added"_s, .added = 9}});
    REQUIRE(drafts.count() == 2);
    CHECK(drafts.drafts().at(1).toMap().value(u"kind"_s).toString() == u"component");
    CHECK(drafts.drafts().at(1).toMap().value(u"id"_s).toString() == u"mine/burst");

    const int before = changes;
    drafts.refresh();
    CHECK(changes == before);
    CHECK(drafts.fileDiff(u"profile:shooter"_s, u"logic.ts"_s).contains(u"+new"));
}

TEST_CASE("applying and discarding go to the store and the card follows")
{
    gs::test::McpApplication application;
    FakeStore store;
    store.put(u"profile:shooter"_s, {AiDraftFile{.path = u"logic.ts"_s, .state = u"modified"_s, .added = 2}});
    store.put(u"task:daily"_s, {AiDraftFile{.path = u"task.ts"_s, .state = u"modified"_s, .removed = 4}});
    AiDrafts drafts(store);
    REQUIRE(drafts.count() == 2);

    store.refuse = true;
    CHECK(drafts.apply(u"profile:shooter"_s) == u"The disk is full");
    CHECK(drafts.count() == 2);

    store.refuse = false;
    CHECK(drafts.apply(u"profile:shooter"_s).isEmpty());
    CHECK(store.applied == QStringList{u"profile:shooter"_s, u"profile:shooter"_s});
    REQUIRE(drafts.count() == 1);
    CHECK(drafts.drafts().at(0).toMap().value(u"target"_s).toString() == u"task:daily");

    drafts.discard(u"task:daily"_s);
    CHECK(store.discarded == QStringList{u"task:daily"_s});
    CHECK(drafts.count() == 0);
}
