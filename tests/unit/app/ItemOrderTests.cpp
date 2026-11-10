#include <doctest/doctest.h>

#include "ItemOrder.h"

#include <QList>
#include <QQuickItem>
#include <QQuickWindow>

using gs::app::ItemOrder;

TEST_CASE("UI-013: an item placed before its sibling comes before it in the Tab order")
{
    QQuickWindow window;
    auto* card = new QQuickItem(window.contentItem());
    auto* footer = new QQuickItem(window.contentItem());
    auto* lateCard = new QQuickItem(window.contentItem());
    for (QQuickItem* item : {card, footer, lateCard}) {
        item->setActiveFocusOnTab(true);
    }
    REQUIRE(card->nextItemInFocusChain() == footer);
    REQUIRE(footer->nextItemInFocusChain() == lateCard);

    const ItemOrder order;
    order.placeBefore(lateCard, footer);

    CHECK(window.contentItem()->childItems() == QList<QQuickItem*>{card, lateCard, footer});
    CHECK(card->nextItemInFocusChain() == lateCard);
    CHECK(lateCard->nextItemInFocusChain() == footer);

    order.placeBefore(card, footer);
    order.placeBefore(lateCard, footer);
    CHECK(window.contentItem()->childItems() == QList<QQuickItem*>{card, lateCard, footer});
}

TEST_CASE("UI-013: placing an item before one that is not its sibling leaves every order alone")
{
    QQuickWindow window;
    auto* group = new QQuickItem(window.contentItem());
    auto* inside = new QQuickItem(group);
    auto* outside = new QQuickItem(window.contentItem());
    const QList<QQuickItem*> before = window.contentItem()->childItems();

    const ItemOrder order;
    order.placeBefore(outside, inside);
    order.placeBefore(inside, outside);
    order.placeBefore(outside, outside);
    order.placeBefore(nullptr, outside);
    order.placeBefore(outside, nullptr);

    CHECK(window.contentItem()->childItems() == before);
    CHECK(group->childItems() == QList<QQuickItem*>{inside});
}
