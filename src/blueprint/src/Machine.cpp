#include "Model.h"

#include <charconv>
#include <string_view>

namespace gs::blueprint {

std::string Document::stateNode(std::string_view machinePosition, std::string_view state) const
{
    for (const Item& item : m_impl->items) {
        if (item.role == Role::State && item.machine == machinePosition && item.stateName == state) {
            return item.key;
        }
    }
    return {};
}

std::string Document::machineOnLine(int line) const
{
    std::string best;
    int bestLine = 0;
    for (const Item& item : m_impl->items) {
        if (item.role != Role::State) {
            continue;
        }
        const std::string_view machine = item.machine.view();
        int start = 0;
        std::from_chars(machine.data(), machine.data() + machine.size(), start);
        if (start > 0 && start <= line && start > bestLine) {
            best = std::string(machine);
            bestLine = start;
        }
    }
    return best;
}

std::string Document::transitionWire(std::string_view machinePosition, std::string_view from, std::string_view to) const
{
    const Page* page = m_impl->page(join("machine:", machinePosition));
    if (page == nullptr) {
        return {};
    }
    for (const Wire& wire : page->wires) {
        if (wire.type == "transition" && wire.fromItem != nullptr && wire.toItem != nullptr
            && wire.fromItem->stateName == from && wire.toItem->stateName == to) {
            return wire.key;
        }
    }
    return {};
}

}
