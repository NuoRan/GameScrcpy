#include "gs/adb/ForwardList.h"

#include <array>

namespace gs::adb {

namespace {

template <typename Add>
void forEachTriple(std::string_view text, Add add)
{
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

        std::array<std::string_view, 4> fields;
        std::size_t count = 0;
        std::size_t position = 0;
        while (count < fields.size()) {
            const std::size_t start = line.find_first_not_of(" \t\r", position);
            if (start == std::string_view::npos) {
                break;
            }
            const std::size_t end = line.find_first_of(" \t\r", start);
            fields[count++] = line.substr(start, end - start);
            if (end == std::string_view::npos) {
                break;
            }
            position = end;
        }
        if (count == 3) {
            add(fields[0], fields[1], fields[2]);
        }
    }
}

}

std::vector<ForwardEntry> parseForwardList(std::string_view text)
{
    std::vector<ForwardEntry> entries;
    forEachTriple(text, [&](std::string_view serial, std::string_view host, std::string_view device) {
        entries.push_back({std::string(serial), std::string(host), std::string(device)});
    });
    return entries;
}

std::vector<ForwardEntry> parseReverseList(std::string_view text)
{
    std::vector<ForwardEntry> entries;
    forEachTriple(text, [&](std::string_view name, std::string_view device, std::string_view host) {
        entries.push_back({std::string(name), std::string(host), std::string(device)});
    });
    return entries;
}

}
