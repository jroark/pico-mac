set(UMAC_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../external/umac)
set(MUSASHI_ROOT ${UMAC_ROOT}/external/Musashi)

idf_component_register(
    SRCS
        "app_main.c"
        "lcd_ws28b.c"
        "sdcard_disc.c"
        "touch_input.c"
        "tca9554.c"
        ${UMAC_ROOT}/src/disc.c
        ${UMAC_ROOT}/src/main.c
        ${UMAC_ROOT}/src/rom.c
        ${UMAC_ROOT}/src/scc.c
        ${UMAC_ROOT}/src/via.c
        ${MUSASHI_ROOT}/m68kcpu.c
        ${MUSASHI_ROOT}/m68kdasm.c
        ${MUSASHI_ROOT}/m68kops.c
        ${MUSASHI_ROOT}/softfloat/softfloat.c
    INCLUDE_DIRS
        "."
        ${UMAC_ROOT}/include
        ${MUSASHI_ROOT}
        ${CMAKE_CURRENT_LIST_DIR}/../../../incbin
    REQUIRES
        esp_timer
        esp_driver_spi
        esp_driver_sdspi
        esp_driver_i2c
        esp_lcd
        esp_lcd_touch_gt911
        fatfs
        sdmmc
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    UMAC_MEMSIZE=256
    DISP_WIDTH=640
    DISP_HEIGHT=480
    MUSASHI_CNF=\"musashi_conf_esp32.h\"
    USE_WS28B=1
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O2
    -Wno-error=format
)
