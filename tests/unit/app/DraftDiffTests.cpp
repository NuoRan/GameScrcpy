#include "DraftDiff.h"

#include <gs/blueprint/Document.h>
#include <gs/input/Profile.h>

#include <doctest/doctest.h>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>
#include <span>
#include <string>
#include <vector>

using gs::app::DraftChange;
using gs::app::DraftGraphChanges;
using gs::app::DraftLine;
using gs::app::draftChangeName;
using gs::app::draftControlChanges;
using gs::app::draftDiffList;
using gs::app::draftLinesOf;
using gs::app::draftNodeSignature;
using gs::app::DraftTextDiff;
using gs::app::draftTextDiff;

namespace {

namespace bp = gs::blueprint;

QString changeOf(const gs::input::Profile& live, const gs::input::Profile& draft, const QString& id)
{
    return draftControlChanges(live, draft).value(id);
}

gs::input::Profile profileOf(const char* json)
{
    gs::input::LoadResult loaded = gs::input::loadProfile(json);
    REQUIRE(loaded.ok());
    return loaded.profile;
}

bp::GraphNode nodeOf(const char* key, const char* title, const char* code = "")
{
    bp::GraphNode node;
    node.key = key;
    node.kind = "action";
    node.tone = "flow";
    node.icon = "play";
    node.title = title;
    node.code = code;
    node.inputs = {};
    node.outputs = {};
    return node;
}

std::vector<DraftLine::Kind> kindsOf(const DraftTextDiff& diff)
{
    std::vector<DraftLine::Kind> kinds;
    for (const gs::app::DraftHunk& hunk : diff.hunks) {
        for (const DraftLine& line : hunk.lines) {
            kinds.push_back(line.kind);
        }
    }
    return kinds;
}

}

TEST_CASE("draft text diff: a changed line is one removed and one added, with the lines around it")
{
    const DraftTextDiff diff = draftTextDiff(QStringLiteral("a\nb\nc\nd\ne\n"), QStringLiteral("a\nb\nX\nd\ne\n"));
    CHECK(diff.added == 1);
    CHECK(diff.removed == 1);
    CHECK(diff.leftOut == 0);
    REQUIRE(diff.hunks.size() == 1);
    CHECK(diff.hunks.front().beforeStart == 1);
    CHECK(diff.hunks.front().beforeCount == 5);
    CHECK(diff.hunks.front().afterStart == 1);
    CHECK(diff.hunks.front().afterCount == 5);
    const std::vector<DraftLine::Kind> kinds = kindsOf(diff);
    const std::vector<DraftLine::Kind> wanted{DraftLine::Kind::Context, DraftLine::Kind::Context,
                                              DraftLine::Kind::Removed, DraftLine::Kind::Added,
                                              DraftLine::Kind::Context, DraftLine::Kind::Context};
    CHECK(kinds == wanted);
    REQUIRE(diff.hunks.front().lines.size() == 6);
    CHECK(diff.hunks.front().lines[2].text == QStringLiteral("c"));
    CHECK(diff.hunks.front().lines[3].text == QStringLiteral("X"));
}

TEST_CASE("draft text diff: inserted lines at the end keep the lines before them as context")
{
    const DraftTextDiff diff = draftTextDiff(QStringLiteral("a\nb\nc\nd\n"), QStringLiteral("a\nb\nc\nd\ne\n"));
    CHECK(diff.added == 1);
    CHECK(diff.removed == 0);
    REQUIRE(diff.hunks.size() == 1);
    CHECK(diff.hunks.front().beforeCount == 3);
    CHECK(diff.hunks.front().afterCount == 4);
    const std::vector<DraftLine::Kind> kinds = kindsOf(diff);
    REQUIRE(kinds.size() == 4);
    CHECK(kinds.back() == DraftLine::Kind::Added);
}

TEST_CASE("draft text diff: a new file, a removed file and empty files")
{
    const DraftTextDiff fresh = draftTextDiff(std::nullopt, QStringLiteral("one\ntwo\n"));
    CHECK(fresh.added == 2);
    CHECK(fresh.removed == 0);
    REQUIRE(fresh.hunks.size() == 1);
    CHECK(fresh.hunks.front().beforeCount == 0);
    CHECK(fresh.hunks.front().afterStart == 1);

    const DraftTextDiff gone = draftTextDiff(QStringLiteral("one\ntwo\n"), std::nullopt);
    CHECK(gone.added == 0);
    CHECK(gone.removed == 2);
    REQUIRE(gone.hunks.size() == 1);
    CHECK(gone.hunks.front().afterCount == 0);

    CHECK(draftTextDiff(std::nullopt, std::nullopt).empty());
    CHECK(draftTextDiff(std::nullopt, QString()).empty());
    CHECK(draftTextDiff(QString(), QString()).empty());
    CHECK(draftTextDiff(QStringLiteral("a\n"), QStringLiteral("a\n")).empty());
}

TEST_CASE("draft text diff: CRLF and a missing final newline are not changes")
{
    CHECK(draftTextDiff(QStringLiteral("a\r\nb\r\n"), QStringLiteral("a\nb")).empty());
    CHECK(draftLinesOf(QStringLiteral("a\r\nb\r\n")) == QStringList{QStringLiteral("a"), QStringLiteral("b")});
    CHECK(draftLinesOf(QString()) == QStringList{});
}

TEST_CASE("draft text diff: two changes far apart make two hunks")
{
    QString before;
    QString after;
    for (int line = 1; line <= 20; ++line) {
        before += QStringLiteral("line %1\n").arg(line);
        after += line == 2 || line == 18 ? QStringLiteral("line %1 changed\n").arg(line)
                                        : QStringLiteral("line %1\n").arg(line);
    }
    const DraftTextDiff diff = draftTextDiff(before, after);
    CHECK(diff.added == 2);
    CHECK(diff.removed == 2);
    CHECK(diff.hunks.size() == 2);
}

TEST_CASE("draft text diff: a long change stops at the cap and says how many lines are left")
{
    QString before;
    QString after;
    for (int line = 0; line < 400; ++line) {
        before += QStringLiteral("old %1\n").arg(line);
        after += QStringLiteral("new %1\n").arg(line);
    }
    const DraftTextDiff diff = draftTextDiff(before, after, gs::app::kDraftDiffContext, 300);
    CHECK(diff.added == 400);
    CHECK(diff.removed == 400);
    CHECK(diff.leftOut > 0);
    const QVariantList rows = draftDiffList(diff);
    REQUIRE_FALSE(rows.isEmpty());
    CHECK(rows.front().toMap().value(QStringLiteral("kind")).toString() == u"header");
    CHECK(rows.back().toMap().value(QStringLiteral("kind")).toString() == u"note");
    CHECK(rows.back().toMap().value(QStringLiteral("text")).toString().startsWith(QStringLiteral("… ")));
    CHECK(rows.size() < 305);
}

TEST_CASE("draft text diff: a list for the card names every row and marks added and removed lines")
{
    const DraftTextDiff diff = draftTextDiff(QStringLiteral("a\nb\n"), QStringLiteral("a\nc\n"));
    const QVariantList rows = draftDiffList(diff);
    REQUIRE(rows.size() == 4);
    CHECK(rows.at(0).toMap().value(QStringLiteral("kind")).toString() == u"header");
    CHECK(rows.at(0).toMap().value(QStringLiteral("text")).toString() == u"@@ -1,2 +1,2 @@");
    CHECK(rows.at(1).toMap().value(QStringLiteral("kind")).toString() == u"context");
    CHECK(rows.at(2).toMap().value(QStringLiteral("kind")).toString() == u"removed");
    CHECK(rows.at(2).toMap().value(QStringLiteral("text")).toString() == u"b");
    CHECK(rows.at(3).toMap().value(QStringLiteral("kind")).toString() == u"added");
    CHECK(rows.at(3).toMap().value(QStringLiteral("text")).toString() == u"c");
}

TEST_CASE("draft component diff: keys are compared by id, so a moved or renamed key is only changed")
{
    const gs::input::Profile live = profileOf(
        R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"jump","name":"Jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.3],"inputs":{"press":"Space"}},
        {"id":"aim","name":"Aim","type":"hold","anchor":"topRight","offset":[-0.1,0.2],"inputs":{"press":"Mouse.Right"}}]})");
    const gs::input::Profile draft = profileOf(
        R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"jump","name":"Jump!","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.3],"inputs":{"press":"Space"}},
        {"id":"fire","name":"Fire","type":"tap","anchor":"bottomLeft","offset":[0.3,-0.2],"inputs":{"press":"Mouse.Left"}}]})");
    CHECK(changeOf(live, draft, QStringLiteral("jump")) == u"changed");
    CHECK(changeOf(live, draft, QStringLiteral("aim")) == u"removed");
    CHECK(changeOf(live, draft, QStringLiteral("fire")) == u"added");
    CHECK(draftControlChanges(live, draft).size() == 3);
    CHECK(draftChangeName(DraftChange::None).isEmpty());
}

TEST_CASE("draft component diff: the same profile has no marks, and an empty profile marks every key")
{
    const gs::input::Profile live = profileOf(
        R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"jump","name":"Jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.3],"inputs":{"press":"Space"}}]})");
    CHECK(draftControlChanges(live, live).isEmpty());
    const QHash<QString, QString> marks = draftControlChanges(live, gs::input::Profile{});
    CHECK(marks.size() == 1);
    CHECK(marks.value(QStringLiteral("jump")) == u"removed");
}

TEST_CASE("draft blueprint diff: blocks are compared by key and signature, and removed blocks are listed")
{
    bp::Graph live;
    live.nodes.push_back(nodeOf("keys.F.onPress/setPosition#0", "Set position"));
    live.nodes.push_back(nodeOf("keys.F.onPress/log#1", "Log"));
    bp::Graph draft;
    draft.nodes.push_back(nodeOf("keys.F.onPress/setPosition#0", "Set position"));
    draft.nodes.push_back(nodeOf("keys.F.onPress/tap#1", "Tap", "await touch.tap(0.5, 0.5);"));
    const DraftGraphChanges changes = gs::app::draftGraphChanges(live, draft);
    CHECK(changes.marks.value(QStringLiteral("keys.F.onPress/setPosition#0")).isEmpty());
    CHECK(changes.marks.value(QStringLiteral("keys.F.onPress/tap#1")) == u"added");
    CHECK(changes.added == 1);
    CHECK(changes.changed == 0);
    CHECK(changes.removedCount == 1);
    REQUIRE(changes.removed.size() == 1);
    CHECK(changes.removed.front().toMap().value(QStringLiteral("key")).toString() == u"keys.F.onPress/log#1");
    CHECK(changes.removed.front().toMap().value(QStringLiteral("title")).toString() == u"Log");
}

TEST_CASE("draft blueprint diff: a block whose value changed is changed, and the same graph has no marks")
{
    bp::GraphPin pin;
    pin.name = "position";
    pin.label = "Position";
    pin.type = "point";
    pin.value = "0.2, 0.3";
    bp::Graph live;
    live.pins.push_back(pin);
    bp::GraphNode node = nodeOf("keys.F.onPress/setPosition#0", "Set position");
    node.inputs = std::span<const bp::GraphPin>(&live.pins.back(), 1);
    live.nodes.push_back(node);
    CHECK(gs::app::draftGraphChanges(live, live).empty());

    bp::GraphPin moved = pin;
    moved.value = "0.6, 0.7";
    bp::Graph draft;
    draft.pins.push_back(moved);
    bp::GraphNode other = nodeOf("keys.F.onPress/setPosition#0", "Set position");
    other.inputs = std::span<const bp::GraphPin>(&draft.pins.back(), 1);
    draft.nodes.push_back(other);
    const DraftGraphChanges changes = gs::app::draftGraphChanges(live, draft);
    CHECK(changes.marks.value(QStringLiteral("keys.F.onPress/setPosition#0")) == u"changed");
    CHECK(changes.changed == 1);
    CHECK(changes.removedCount == 0);
    CHECK(draftNodeSignature(live.nodes.front()) != draftNodeSignature(other));
}
