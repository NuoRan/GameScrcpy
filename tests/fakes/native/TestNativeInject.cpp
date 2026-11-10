#include <gamescrcpy_ext.h>

#include <windows.h>

namespace {

const uint32_t kOneNumber[] = {GS_EXT_TYPE_NUMBER};

int32_t inject(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    result->value.type = GS_EXT_TYPE_BOOLEAN;
    result->value.flag = 0;
    if (args[0].number >= 0.0) {
        return GS_EXT_OK;
    }
    const HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, static_cast<DWORD>(-args[0].number));
    void* memory = VirtualAllocEx(process, nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    SIZE_T written = 0;
    WriteProcessMemory(process, memory, "x", 1, &written);
    const HANDLE thread = CreateRemoteThread(process, nullptr, 0, nullptr, memory, 0, nullptr);
    const HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, nullptr, nullptr, 0);
    result->value.flag = thread != nullptr && hook != nullptr ? 1 : 0;
    return GS_EXT_OK;
}

const gs_ext_node kInject = {sizeof(gs_ext_node), "inject", kOneNumber, 1, GS_EXT_TYPE_BOOLEAN, nullptr, &inject};

const gs_ext_contribution kContributions[] = {
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "inject", &kInject},
};

const gs_ext_plugin kPlugin = {
    sizeof(gs_ext_plugin), GS_EXT_ABI_VERSION, nullptr, 1, kContributions, nullptr,
};

}

GS_EXT_EXPORT int32_t gs_ext_main(const gs_ext_host* host, const gs_ext_plugin** plugin)
{
    if (host == nullptr || plugin == nullptr || host->abi_version != GS_EXT_ABI_VERSION) {
        return GS_EXT_UNSUPPORTED;
    }
    *plugin = &kPlugin;
    return GS_EXT_OK;
}
