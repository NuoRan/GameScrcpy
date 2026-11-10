#include "gs/ext/Resolution.h"

#include <algorithm>
#include <functional>
#include <set>
#include <utility>

namespace gs::ext {

namespace {

using Catalog = std::map<std::string, std::vector<const Manifest*>, std::less<>>;
using Choice = std::map<std::string, const Manifest*, std::less<>>;

constexpr int kMaxRounds = 32;

std::string quoted(std::string_view text)
{
    return "\"" + std::string(text) + "\"";
}

std::string memberPath(std::string_view id)
{
    return "$[" + quoted(id) + "]";
}

std::string dependencyPath(std::string_view requester, std::string_view dependency)
{
    return memberPath(requester) + ".dependencies[" + quoted(dependency) + "]";
}

std::string joined(const std::vector<std::string>& items)
{
    std::string text;
    for (const std::string& item : items) {
        text += (text.empty() ? "" : ", ") + item;
    }
    return text;
}

bool covers(std::string_view granted, std::string_view host)
{
    if (granted == host || granted == "*") {
        return true;
    }
    return granted.starts_with("*.") && host.size() > granted.size() - 1 && host.ends_with(granted.substr(1));
}

bool widens(const Permission& before, const Permission& after)
{
    const bool newHost = std::ranges::any_of(after.hosts, [&before](const std::string& host) {
        return std::ranges::none_of(before.hosts, [&host](const std::string& granted) { return covers(granted, host); });
    });
    const bool newPath = std::ranges::any_of(after.paths, [&before](const std::string& path) {
        return std::ranges::find(before.paths, path) == before.paths.end();
    });
    const bool moreDevices =
        before.maxDevices && (!after.maxDevices || *after.maxDevices > *before.maxDevices);
    return newHost || newPath || moreDevices;
}

std::set<std::string, std::less<>> closure(const Catalog& catalog, const Choice& chosen,
                                           const std::vector<std::string>& enabled)
{
    std::set<std::string, std::less<>> members;
    std::vector<std::string> pending(enabled.rbegin(), enabled.rend());
    while (!pending.empty()) {
        std::string id = std::move(pending.back());
        pending.pop_back();
        if (!members.insert(id).second) {
            continue;
        }
        const Manifest* manifest = nullptr;
        if (const auto current = chosen.find(id); current != chosen.end()) {
            manifest = current->second;
        } else if (const auto found = catalog.find(id); found != catalog.end()) {
            manifest = found->second.front();
        }
        if (manifest == nullptr) {
            continue;
        }
        for (const auto& [dependency, spec] : manifest->dependencies) {
            if (!spec.optional) {
                pending.push_back(dependency);
            }
        }
    }
    return members;
}

bool acceptedByAll(const Choice& chosen, std::string_view id, const Version& version)
{
    return std::ranges::all_of(chosen, [id, &version](const auto& entry) {
        const auto spec = entry.second->dependencies.find(id);
        return spec == entry.second->dependencies.end() || spec->second.version.contains(version);
    });
}

Choice choose(const Catalog& catalog, const std::vector<std::string>& enabled)
{
    Choice chosen;
    for (int round = 0; round < kMaxRounds; ++round) {
        Choice next;
        for (const std::string& id : closure(catalog, chosen, enabled)) {
            const auto found = catalog.find(id);
            if (found == catalog.end()) {
                continue;
            }
            const auto accepted = std::ranges::find_if(found->second, [&](const Manifest* candidate) {
                return acceptedByAll(chosen, id, candidate->version);
            });
            next.emplace(id, accepted != found->second.end() ? *accepted : found->second.front());
        }
        if (next == chosen) {
            break;
        }
        chosen = std::move(next);
    }
    return chosen;
}

void markCycles(const Choice& chosen, std::set<std::string, std::less<>>& failed, std::vector<Diagnostic>& problems)
{
    std::map<std::string, int, std::less<>> state;
    std::vector<std::string> stack;
    std::set<std::string, std::less<>> cyclic;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        state[id] = 1;
        stack.push_back(id);
        for (const auto& [dependency, spec] : chosen.at(id)->dependencies) {
            if (!chosen.contains(dependency)) {
                continue;
            }
            const int seen = state.contains(dependency) ? state[dependency] : 0;
            if (seen == 1) {
                const auto start = std::ranges::find(stack, dependency);
                cyclic.insert(start, stack.end());
            } else if (seen == 0) {
                visit(dependency);
            }
        }
        stack.pop_back();
        state[id] = 2;
    };
    for (const auto& [id, manifest] : chosen) {
        if (!state.contains(id)) {
            visit(id);
        }
    }
    const std::vector<std::string> members(cyclic.begin(), cyclic.end());
    for (const std::string& id : members) {
        problems.push_back({Severity::Error, Code::DependencyCycle, memberPath(id), joined(members)});
        failed.insert(id);
    }
}

}

std::vector<Diagnostic> checkCompatibility(const Manifest& manifest, const HostApi& host)
{
    std::vector<Diagnostic> problems;
    for (const auto& [point, range] : manifest.requirements) {
        const std::string path = "$.requires[" + quoted(nameOf(point)) + "]";
        const auto offered = host.versions.find(point);
        if (offered == host.versions.end() || offered->second.empty()) {
            problems.push_back({Severity::Error, Code::HostApiMissing, path, std::string(nameOf(point))});
            continue;
        }
        if (std::ranges::none_of(offered->second, [&range](const Version& version) { return range.contains(version); })) {
            std::vector<std::string> versions;
            for (const Version& version : offered->second) {
                versions.push_back(version.text());
            }
            problems.push_back({Severity::Error, Code::HostApiVersion, path,
                                range.text() + " wanted, offered " + joined(versions)});
        }
    }
    return problems;
}

Resolution resolveDependencies(const std::vector<Manifest>& installed, const std::vector<std::string>& enabled)
{
    Resolution result;
    Catalog catalog;
    for (const Manifest& manifest : installed) {
        catalog[manifest.id].push_back(&manifest);
    }
    for (auto& [id, versions] : catalog) {
        std::ranges::stable_sort(versions, std::greater<>{}, [](const Manifest* manifest) { return manifest->version; });
    }

    const Choice chosen = choose(catalog, enabled);
    std::set<std::string, std::less<>> failed;
    std::set<std::string, std::less<>> reported;
    for (const std::string& id : enabled) {
        if (!catalog.contains(id) && reported.insert(id).second) {
            result.problems.push_back({Severity::Error, Code::DependencyMissing, memberPath(id), id});
        }
    }

    for (const auto& [id, manifest] : chosen) {
        for (const auto& [dependency, spec] : manifest->dependencies) {
            const std::string path = dependencyPath(id, dependency);
            const auto found = catalog.find(dependency);
            const bool installedAtAll = found != catalog.end();
            const bool versionExists =
                installedAtAll && std::ranges::any_of(found->second, [&spec](const Manifest* candidate) {
                    return spec.version.contains(candidate->version);
                });
            if (versionExists) {
                continue;
            }
            if (spec.optional) {
                result.problems.push_back({Severity::Notice, Code::DependencyOptionalMissing, path,
                                           installedAtAll ? spec.version.text() : dependency});
                continue;
            }
            result.problems.push_back({Severity::Error, installedAtAll ? Code::DependencyVersion : Code::DependencyMissing,
                                       path, installedAtAll ? spec.version.text() : dependency});
            failed.insert(id);
        }
    }

    for (const auto& [id, manifest] : chosen) {
        std::vector<std::string> unhappy;
        for (const auto& [requester, other] : chosen) {
            const auto spec = other->dependencies.find(id);
            if (spec == other->dependencies.end() || spec->second.version.contains(manifest->version)) {
                continue;
            }
            const bool satisfiable = std::ranges::any_of(catalog.at(id), [&spec](const Manifest* candidate) {
                return spec->second.version.contains(candidate->version);
            });
            if (!satisfiable) {
                continue;
            }
            if (spec->second.optional) {
                result.problems.push_back({Severity::Notice, Code::DependencyOptionalMissing,
                                           dependencyPath(requester, id), spec->second.version.text()});
                continue;
            }
            unhappy.push_back(requester);
        }
        if (!unhappy.empty()) {
            result.problems.push_back({Severity::Error, Code::DependencyConflict, memberPath(id),
                                       joined(unhappy) + " need a version of " + id + " other than " +
                                           manifest->version.text()});
            failed.insert(unhappy.begin(), unhappy.end());
        }
    }

    markCycles(chosen, failed, result.problems);

    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& [id, manifest] : chosen) {
            if (failed.contains(id)) {
                continue;
            }
            const bool broken = std::ranges::any_of(manifest->dependencies, [&](const auto& entry) {
                return !entry.second.optional && (failed.contains(entry.first) || !chosen.contains(entry.first));
            });
            if (broken) {
                failed.insert(id);
                changed = true;
            }
        }
    }

    std::map<std::string, int, std::less<>> waiting;
    std::map<std::string, std::vector<std::string>, std::less<>> dependents;
    for (const auto& [id, manifest] : chosen) {
        if (!failed.contains(id)) {
            waiting[id] = 0;
        }
    }
    for (const auto& [id, count] : waiting) {
        for (const auto& [dependency, spec] : chosen.at(id)->dependencies) {
            if (waiting.contains(dependency)) {
                ++waiting[id];
                dependents[dependency].push_back(id);
            }
        }
    }
    std::set<std::string, std::less<>> ready;
    for (const auto& [id, count] : waiting) {
        if (count == 0) {
            ready.insert(id);
        }
    }
    while (!ready.empty()) {
        const std::string id = *ready.begin();
        ready.erase(ready.begin());
        result.order.push_back({id, chosen.at(id)->version});
        for (const std::string& dependent : dependents[id]) {
            if (--waiting[dependent] == 0) {
                ready.insert(dependent);
            }
        }
    }
    return result;
}

UpdateChanges compareForUpdate(const Manifest& before, const Manifest& after)
{
    UpdateChanges changes;
    for (const Permission& permission : after.permissions) {
        const auto previous = std::ranges::find(before.permissions, permission.name, &Permission::name);
        if (previous == before.permissions.end() || widens(*previous, permission)) {
            changes.addedPermissions.push_back(permission);
        }
    }
    for (const auto& [id, dependency] : after.dependencies) {
        const auto previous = before.dependencies.find(id);
        if (!dependency.optional && (previous == before.dependencies.end() || previous->second.optional)) {
            changes.addedDependencies.push_back(id);
        }
    }
    for (const auto& [point, range] : after.requirements) {
        const auto previous = before.requirements.find(point);
        if (previous == before.requirements.end() || previous->second.text() != range.text()) {
            changes.changedRequirements.push_back(point);
        }
    }
    for (const auto& [point, range] : before.requirements) {
        if (!after.requirements.contains(point)) {
            changes.changedRequirements.push_back(point);
        }
    }
    std::ranges::sort(changes.changedRequirements);
    return changes;
}

}
