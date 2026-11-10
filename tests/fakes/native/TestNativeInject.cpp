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

const gs_ext_node kNodes[] = {
    {"inject", kOneNumber, 1, GS_EXT_TYPE_BOOLEAN, nullptr, &inject},
};

}

GS_EXT_EXPORT int32_t gs_ext_entry(const gs_ext_host* host, gs_ext_module* module)
{
    if (host == nullptr || module == nullptr || host->abi_version != GS_EXT_ABI_VERSION) {
        return GS_EXT_UNSUPPORTED;
    }
    module->abi_version = GS_EXT_ABI_VERSION;
    module->node_count = 1;
    module->nodes = kNodes;
    return GS_EXT_OK;
}
