idf_build_get_property(gs_target IDF_TARGET)

set(gs_sources
    main.c
    gs_board.c
    gs_download.c
    gs_hid_reports.c
    gs_link.c
    gs_wifi.c
)
set(gs_requires
    esp_app_format
    esp_driver_uart
    esp_hw_support
    esp_system
    esp_timer
    freertos
    log
    nvs_flash
)

if(gs_target STREQUAL "esp32s2" OR gs_target STREQUAL "esp32s3" OR gs_target STREQUAL "esp32p4")
    list(APPEND gs_sources gs_hid_usb.c)
else()
    list(APPEND gs_sources gs_hid_ble.c)
    list(APPEND gs_requires bt esp_hid)
endif()

if(NOT gs_target STREQUAL "esp32p4")
    list(APPEND gs_requires esp_event esp_netif esp_wifi lwip)
endif()

if(gs_target STREQUAL "esp32c3" OR gs_target STREQUAL "esp32c6" OR gs_target STREQUAL "esp32p4")
    list(APPEND gs_requires esp_driver_usb_serial_jtag)
endif()

idf_component_register(
    SRCS ${gs_sources}
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../../protocol/esp32"
    REQUIRES ${gs_requires}
)
