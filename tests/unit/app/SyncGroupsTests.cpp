#include "SyncGroups.h"

#include <doctest/doctest.h>

#include <QString>
#include <QStringList>

using gs::app::SyncGroups;

namespace {

const QString kA = QStringLiteral("A");
const QString kB = QStringLiteral("B");
const QString kC = QStringLiteral("C");
const QString kD = QStringLiteral("D");

}

TEST_CASE("a master keeps the phones that may follow it, and a phone follows one master at a time (ADR-0096)")
{
    SyncGroups groups;
    CHECK(groups.setFollowers(kA, {kB, kC, kA, kB, QString()}));
    REQUIRE(groups.groups().size() == 1);
    CHECK(groups.followersOf(kA) == QStringList{kB, kC});
    CHECK(groups.masterOf(kB) == kA);
    CHECK(groups.masterOf(kA).isEmpty());
    CHECK(groups.leads(kA));
    CHECK_FALSE(groups.leads(kB));
    CHECK_FALSE(groups.setFollowers(kA, {kB, kC}));

    CHECK_FALSE(groups.setFollowers(kD, {kB, kA, kC}));
    CHECK(groups.groups().size() == 1);
    CHECK(groups.masterOf(kB) == kA);
    CHECK_FALSE(groups.setFollowers(kB, {kD}));
    CHECK_FALSE(groups.leads(kB));

    CHECK(groups.canFollow(kD, kA));
    CHECK(groups.canFollow(kB, kA));
    CHECK_FALSE(groups.canFollow(kB, kD));
    CHECK_FALSE(groups.canFollow(kA, kD));
    CHECK_FALSE(groups.canFollow(kA, kA));

    CHECK(groups.setFollowers(kA, {kC, kD}));
    CHECK(groups.followersOf(kA) == QStringList{kC, kD});
    CHECK(groups.masterOf(kB).isEmpty());
}

TEST_CASE("stopping or phones that stop mirroring end a sync, and a new sync takes the first free colour (ADR-0096)")
{
    SyncGroups groups;
    REQUIRE(groups.setFollowers(kA, {kB}));
    REQUIRE(groups.setFollowers(kC, {kD}));
    REQUIRE(groups.groupOf(kA) != nullptr);
    CHECK(groups.groupOf(kA)->tone == 0);
    CHECK(groups.groupOf(kB)->tone == 0);
    CHECK(groups.groupOf(kD)->tone == 1);
    CHECK(groups.groupOf(QStringLiteral("E")) == nullptr);

    CHECK(groups.setFollowers(kA, {}));
    CHECK(groups.groupOf(kB) == nullptr);
    CHECK(groups.masterOf(kB).isEmpty());
    CHECK_FALSE(groups.setFollowers(kA, {}));
    REQUIRE(groups.setFollowers(kB, {kA}));
    CHECK(groups.groupOf(kB)->tone == 0);
    CHECK(groups.groupOf(kC)->tone == 1);

    CHECK_FALSE(groups.keepOnly({kA, kB, kC, kD}));
    CHECK(groups.keepOnly({kB, kC}));
    CHECK(groups.groups().empty());

    REQUIRE(groups.setFollowers(kA, {kB, kC}));
    CHECK(groups.keepOnly({kB, kC}));
    CHECK(groups.groups().empty());

    REQUIRE(groups.setFollowers(kA, {kB, kC}));
    CHECK(groups.keepOnly({kA, kC}));
    CHECK(groups.followersOf(kA) == QStringList{kC});
    CHECK(groups.masterOf(kB).isEmpty());
}
