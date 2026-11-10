#include <gamescrcpy_ext.h>

#include <cstddef>

namespace {

int32_t answer(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    result->value.type = GS_EXT_TYPE_NUMBER;
    result->value.number = 42.0;
    return GS_EXT_OK;
}

int32_t openTouch(void*, const gs_ext_touch_open*, void** session)
{
    *session = nullptr;
    return GS_EXT_OK;
}

void closeTouch(void*, void*) {}

int32_t frameTouch(void*, void*, const gs_ext_touch_frame*)
{
    return GS_EXT_OK;
}

int32_t keyTouch(void*, void*, const gs_ext_key*)
{
    return GS_EXT_OK;
}

int32_t textTouch(void*, void*, const char*, size_t)
{
    return GS_EXT_OK;
}

struct LaterNode {
    gs_ext_node node;
    const void* later[4];
};

const LaterNode kLater = {
    {sizeof(LaterNode), "later", nullptr, 0, GS_EXT_TYPE_NUMBER, nullptr, &answer},
    {nullptr, nullptr, nullptr, nullptr},
};

const gs_ext_touch_method kOlder = {
    offsetof(gs_ext_touch_method, text), "older", 2,      0,       1,
    nullptr,                             &openTouch, &closeTouch, &frameTouch, &keyTouch,
    &textTouch,                          nullptr,    nullptr,
};

struct LaterContribution {
    gs_ext_contribution contribution;
    const void* later;
};

const LaterContribution kContributions[] = {
    {{sizeof(LaterContribution), GS_EXT_KIND_NODE, "later", &kLater}, nullptr},
    {{sizeof(LaterContribution), "telepathy", "mind", nullptr}, nullptr},
    {{sizeof(LaterContribution), GS_EXT_KIND_TOUCH_METHOD, "older", &kOlder}, nullptr},
};

struct LaterPlugin {
    gs_ext_plugin plugin;
    const void* later;
};

const LaterPlugin kPlugin = {
    {sizeof(LaterPlugin), GS_EXT_ABI_VERSION, nullptr, 3, &kContributions[0].contribution, nullptr},
    nullptr,
};

}

GS_EXT_EXPORT int32_t gs_ext_main(const gs_ext_host* host, const gs_ext_plugin** plugin)
{
    if (host == nullptr || plugin == nullptr || host->abi_version != GS_EXT_ABI_VERSION) {
        return GS_EXT_UNSUPPORTED;
    }
    *plugin = &kPlugin.plugin;
    return GS_EXT_OK;
}
