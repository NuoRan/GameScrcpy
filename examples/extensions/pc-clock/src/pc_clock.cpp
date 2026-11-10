#include <gamescrcpy_ext.h>

#include <windows.h>

#include <cstdio>

namespace {

const gs_ext_host* g_host = nullptr;

int32_t now(void*, const gs_ext_value*, uint32_t, gs_ext_result* result)
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char text[16] = {};
    const int length = std::snprintf(text, sizeof(text), "%02u:%02u:%02u", static_cast<unsigned>(time.wHour),
                                     static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond));
    if (length <= 0) {
        constexpr char kFailed[] = "the time could not be written";
        g_host->set_error(result, kFailed, sizeof(kFailed) - 1);
        return GS_EXT_FAILED;
    }
    return g_host->set_text(result, GS_EXT_TYPE_STRING, text, static_cast<size_t>(length));
}

const gs_ext_node kNodes[] = {
    {"now", nullptr, 0, GS_EXT_TYPE_STRING, nullptr, &now},
};

}

GS_EXT_EXPORT int32_t gs_ext_entry(const gs_ext_host* host, gs_ext_module* module)
{
    if (host == nullptr || module == nullptr || host->abi_version != GS_EXT_ABI_VERSION) {
        return GS_EXT_UNSUPPORTED;
    }
    g_host = host;
    module->abi_version = GS_EXT_ABI_VERSION;
    module->node_count = 1;
    module->nodes = kNodes;
    return GS_EXT_OK;
}
