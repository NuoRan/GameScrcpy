#include "gs/script/Permissions.h"

#include <algorithm>
#include <format>
#include <iterator>

namespace gs::script {

namespace {

constexpr std::string_view kInput = "device.input";
constexpr std::string_view kScreen = "device.screen";
constexpr std::string_view kClipboard = "device.clipboard";
constexpr std::string_view kApps = "device.apps";
constexpr std::string_view kMapping = "mapping";
constexpr std::string_view kRead = "input.read";
constexpr std::string_view kNotify = "notify";
constexpr std::string_view kAi = "ai";

struct Named {
    std::string_view name;
    ServiceRule rule;
};

constexpr Named kServices[] = {
    {"device", {{}, true}},
    {"tap", {kInput, true}},
    {"swipe", {kInput, true}},
    {"lift", {{}, true}},
    {"key", {kInput, true}},
    {"controls", {kMapping, true}},
    {"pressControl", {kMapping, true}},
    {"enableControl", {kMapping, true}},
    {"states", {kMapping, true}},
    {"stateValue", {kMapping, true}},
    {"setState", {kMapping, true}},
    {"loadStore", {{}, false}},
    {"saveStore", {{}, false}},
    {"toast", {{}, true}},
    {"ask", {{}, false}},
    {"withdraw", {{}, true}},
    {"draw", {{}, true}},
    {"erase", {{}, true}},
    {"find", {kScreen, true}},
    {"findColor", {kScreen, true}},
    {"pickColor", {kScreen, true}},
    {"capture", {kScreen, true}},
    {"changed", {kScreen, true}},
    {"readText", {kScreen, true}},
    {"saveCapture", {kNeverGranted, true}},
    {"cancelFind", {{}, true}},
    {"cursor", {kRead, true}},
    {"keyDown", {kRead, true}},
    {"controlAction", {kMapping, true}},
    {"tuneControl", {kMapping, true}},
    {"saveProfile", {kMapping, true}},
    {"controlStatus", {kMapping, true}},
    {"cameraLocked", {kMapping, true}},
    {"claimKey", {kRead, true}},
    {"handOver", {{}, true}},
    {"type", {kInput, true}},
    {"hold", {kInput, true}},
    {"move", {kInput, true}},
    {"liftAll", {{}, true}},
    {"globalValue", {kMapping, true}},
    {"setGlobal", {kMapping, true}},
    {"app", {{}, false}},
    {"connected", {{}, false}},
    {"awaitConnection", {{}, true}},
    {"readClipboard", {kClipboard, true}},
    {"writeClipboard", {kClipboard, true}},
    {"nodes", {{}, false}},
    {"callNode", {{}, false}},
    {"offersService", {{}, false}},
    {"callService", {{}, false}},
    {"requestService", {{}, false}},
    {"dropService", {{}, false}},
};

constexpr Named kAppRequests[] = {
    {"keymap", {kMapping, true}},
    {"hints", {kMapping, true}},
    {"hint", {kMapping, true}},
    {"profile", {kMapping, true}},
    {"foreground", {kApps, true}},
    {"launch", {kApps, true}},
    {"screen", {kInput, true}},
    {"rotate", {kInput, true}},
    {"focused", {{}, true}},
    {"progress", {{}, true}},
    {"notify", {kNotify, false}},
    {"taskParams", {{}, false}},
    {"taskProgress", {{}, false}},
    {"machineState", {{}, false}},
};

constexpr Named kAppEvents[] = {
    {"foreground", {kApps, false}},
    {"inputBox", {kInput, false}},
};

constexpr std::string_view kMappingEvents[] = {"control", "state", "enabled", "status", "lock", "global"};

constexpr Named kChannelServices[] = {
    {"ai.ask", {kAi, false}},
};

}

ServiceRule serviceRule(std::string_view method) noexcept
{
    const auto found = std::ranges::find(kServices, method, &Named::name);
    return found != std::end(kServices) ? found->rule : ServiceRule{kNeverGranted, true};
}

ServiceRule appRule(std::string_view request) noexcept
{
    const auto found = std::ranges::find(kAppRequests, request, &Named::name);
    return found != std::end(kAppRequests) ? found->rule : ServiceRule{kInput, true};
}

ServiceRule eventRule(std::string_view kind, std::string_view id) noexcept
{
    if (std::ranges::find(kMappingEvents, kind) != std::end(kMappingEvents)) {
        return {kMapping, false};
    }
    if (kind == "key") {
        return {kRead, false};
    }
    if (kind == "app") {
        const auto found = std::ranges::find(kAppEvents, id, &Named::name);
        return found != std::end(kAppEvents) ? found->rule : ServiceRule{{}, false};
    }
    return {{}, false};
}

ServiceRule channelRule(std::string_view service) noexcept
{
    const auto found = std::ranges::find(kChannelServices, service, &Named::name);
    return found != std::end(kChannelServices) ? found->rule : ServiceRule{{}, false};
}

bool permits(const std::vector<std::string>& granted, std::string_view permission)
{
    if (permission.empty()) {
        return true;
    }
    return permission != kNeverGranted && std::ranges::find(granted, permission) != granted.end();
}

std::string permissionProblem(std::string_view permission)
{
    if (permission == kNeverGranted) {
        return "GS-API-072 the logic of an extension cannot do this";
    }
    return std::format("GS-API-072 this needs the permission \"{}\": list it under permissions in the extension's "
                       "manifest.json with a reason, then turn the extension on again so that it can be allowed",
                       permission);
}

std::string phoneProblem()
{
    return "GS-API-073 this logic has no phone: it is an app script of an extension, so act on phones from a "
           "session script";
}

}
