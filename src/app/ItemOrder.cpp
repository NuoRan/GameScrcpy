#include "ItemOrder.h"

namespace gs::app {

ItemOrder::ItemOrder(QObject* parent)
    : QObject(parent)
{
}

void ItemOrder::placeBefore(QQuickItem* item, QQuickItem* sibling) const
{
    if (item == nullptr || sibling == nullptr || item == sibling || item->parentItem() == nullptr ||
        item->parentItem() != sibling->parentItem()) {
        return;
    }
    item->stackBefore(sibling);
}

}
