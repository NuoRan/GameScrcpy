#include "gs_download.h"

#include "esp_system.h"
#include "soc/soc.h"

#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
#include "soc/rtc_cntl_reg.h"
#define GS_FORCE_DOWNLOAD_REGISTER RTC_CNTL_OPTION1_REG
#define GS_FORCE_DOWNLOAD_BIT RTC_CNTL_FORCE_DOWNLOAD_BOOT
#elif CONFIG_IDF_TARGET_ESP32C6
#include "soc/lp_aon_reg.h"
#define GS_FORCE_DOWNLOAD_REGISTER LP_AON_SYS_CFG_REG
#define GS_FORCE_DOWNLOAD_BIT LP_AON_FORCE_DOWNLOAD_BOOT
#elif CONFIG_IDF_TARGET_ESP32P4
#include "soc/lp_system_reg.h"
#define GS_FORCE_DOWNLOAD_REGISTER LP_SYSTEM_REG_SYS_CTRL_REG
#define GS_FORCE_DOWNLOAD_BIT LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT
#endif

bool gs_download_supported(void)
{
#ifdef GS_FORCE_DOWNLOAD_REGISTER
    return true;
#else
    return false;
#endif
}

void gs_download_enter(void)
{
#ifdef GS_FORCE_DOWNLOAD_REGISTER
    REG_SET_BIT(GS_FORCE_DOWNLOAD_REGISTER, GS_FORCE_DOWNLOAD_BIT);
    esp_restart();
#endif
}
