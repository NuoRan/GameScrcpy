#include <cstdint>

extern "C" __declspec(dllexport) std::int32_t gs_ext_entry(const void*, void*)
{
    return 0;
}
