#include "LanHosts.h"

#include <algorithm>

namespace gs::app {

namespace {

constexpr int kNarrowestPrefix = 30;
constexpr int kWidestPrefix = 24;
constexpr std::uint32_t kLoopback = 0x7F000000;
constexpr std::uint32_t kLinkLocal = 0xA9FE0000;

bool inBlock(std::uint32_t ip, std::uint32_t block, int prefix)
{
    return (ip >> (32 - prefix)) == (block >> (32 - prefix));
}

}

std::vector<std::uint32_t> lanHosts(std::span<const LanAddress> own, std::size_t limit)
{
    std::vector<std::uint32_t> hosts;
    for (const LanAddress& address : own) {
        if (address.prefix > kNarrowestPrefix || address.prefix < 1 || inBlock(address.ip, kLoopback, 8)
            || inBlock(address.ip, kLinkLocal, 16)) {
            continue;
        }
        const int prefix = std::max(address.prefix, kWidestPrefix);
        const std::uint32_t mask = ~std::uint32_t{0} << (32 - prefix);
        const std::uint32_t network = address.ip & mask;
        const std::uint32_t broadcast = network | ~mask;
        for (std::uint32_t host = network + 1; host < broadcast && hosts.size() < limit; ++host) {
            if (host != address.ip && std::ranges::find(hosts, host) == hosts.end()) {
                hosts.push_back(host);
            }
        }
    }
    std::erase_if(hosts, [own](std::uint32_t host) {
        return std::ranges::any_of(own, [host](const LanAddress& address) { return address.ip == host; });
    });
    return hosts;
}

}
