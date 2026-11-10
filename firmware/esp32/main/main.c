#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "gs_board.h"
#include "gs_link.h"

static const char *gs_tag = "gs";

void app_main(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        (void)nvs_flash_erase();
        result = nvs_flash_init();
    }
    if (result != ESP_OK) {
        ESP_LOGE(gs_tag, "settings could not be read (%s); the board keeps running with its defaults",
                 esp_err_to_name(result));
    }
    gs_board_init();
    gs_link_start();
    gs_board_start();
    gs_board_start_parts();
}
