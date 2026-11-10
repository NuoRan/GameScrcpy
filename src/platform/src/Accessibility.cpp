#include "gs/platform/Accessibility.h"

#include <windows.h>

namespace gs::platform {

bool systemAnimationsOn() noexcept
{
    BOOL on = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0) == FALSE) {
        return true;
    }
    return on != FALSE;
}

bool systemTransparencyOn() noexcept
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                        L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status != ERROR_SUCCESS || value != 0;
}

}
