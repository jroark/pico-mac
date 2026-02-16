# CMakeLists
#
# MIT License
#
# Copyright (c) 2021, 2024 Matt Evans
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
#
cmake_minimum_required(VERSION 3.13)

# Options that should be defined when initialising the build
# directory with cmake, e.g. "cmake .. -DOPTION=true":
#

option(USE_SD "Build in SD support" OFF)
set(SD_TX 3 CACHE STRING "SD SPI TX pin")
set(SD_RX 4 CACHE STRING "SD SPI RX pin")
set(SD_SCK 2 CACHE STRING "SD SPI SCK pin")
set(SD_CS 5 CACHE STRING "SD SPI CS pin")
set(SD_MHZ 5 CACHE STRING "SD SPI speed in MHz")
set(SD_SPI 0 CACHE STRING "SD SPI controller (0 or 1)")
option(USE_VGA_RES "Video uses VGA (640x480) resolution" OFF)
set(VIDEO_PIN 18 CACHE STRING "Video GPIO base pin (followed by VS, CLK, HS)")
option(USE_WAVESHARE_LCD "Use Waveshare Pico-ResTouch-LCD-2.8 SPI display output" OFF)
set(LCD_SPI 1 CACHE STRING "LCD SPI controller (0 or 1)")
set(LCD_MHZ 16 CACHE STRING "LCD SPI speed in MHz")
set(LCD_PIN_SCK 10 CACHE STRING "LCD SPI SCK pin")
set(LCD_PIN_MOSI 11 CACHE STRING "LCD SPI MOSI pin")
set(LCD_PIN_MISO 12 CACHE STRING "LCD SPI MISO pin (unused by panel, reserved)")
set(LCD_PIN_CS 9 CACHE STRING "LCD CS pin")
set(LCD_PIN_DC 8 CACHE STRING "LCD D/C pin")
set(LCD_PIN_RST 15 CACHE STRING "LCD reset pin")
set(LCD_PIN_BL 13 CACHE STRING "LCD backlight pin")
set(LCD_WIDTH 320 CACHE STRING "LCD width in pixels")
set(LCD_HEIGHT 240 CACHE STRING "LCD height in pixels")
set(LCD_FILTER_MODE 1 CACHE STRING "LCD scaling filter: 0=nearest mono, 1=area grayscale")
option(USE_TOUCH "Enable touchscreen mouse emulation (XPT2046)" ON)
set(TOUCH_MHZ 2 CACHE STRING "Touch SPI speed in MHz")
set(TOUCH_PIN_CS 16 CACHE STRING "Touch controller CS pin")
set(TOUCH_PIN_IRQ 17 CACHE STRING "Touch controller IRQ pin (active low)")
set(TOUCH_USE_IRQ 1 CACHE STRING "Use touch IRQ pin for touch detection (0 or 1)")
set(TOUCH_RAW_MIN_X 200 CACHE STRING "Touch raw minimum X")
set(TOUCH_RAW_MAX_X 3900 CACHE STRING "Touch raw maximum X")
set(TOUCH_RAW_MIN_Y 200 CACHE STRING "Touch raw minimum Y")
set(TOUCH_RAW_MAX_Y 3900 CACHE STRING "Touch raw maximum Y")
set(TOUCH_SWAP_XY 1 CACHE STRING "Swap touch X/Y axes (0 or 1)")
set(TOUCH_INVERT_X 1 CACHE STRING "Invert mapped X axis (0 or 1)")
set(TOUCH_INVERT_Y 0 CACHE STRING "Invert mapped Y axis (0 or 1)")

# See below, -DMEMSIZE=<size in KB> will configure umac's memory size,
# overriding defaults.

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(firmware)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# For TUSB host stuff:
set(FAMILY rp2040)
set(BOARD raspberry_pi_pico)

set(TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)

# umac subproject (and Musashi sub-subproject)
set(UMAC_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/umac)
set(UMAC_MUSASHI_PATH ${UMAC_PATH}/external/Musashi)
set(UMAC_INCLUDE_PATHS ${UMAC_PATH}/include ${UMAC_MUSASHI_PATH})

# This isn't very nice, but hey it's Sunday :p
set(UMAC_SOURCES
  ${UMAC_PATH}/src/disc.c
  ${UMAC_PATH}/src/main.c
  ${UMAC_PATH}/src/rom.c
  ${UMAC_PATH}/src/scc.c
  ${UMAC_PATH}/src/via.c
  ${UMAC_MUSASHI_PATH}/m68kcpu.c
  ${UMAC_MUSASHI_PATH}/m68kdasm.c
  ${UMAC_MUSASHI_PATH}/m68kops.c
  ${UMAC_MUSASHI_PATH}/softfloat/softfloat.c
  )

set(MEMSIZE 128 CACHE STRING "Memory size, in KB")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -DPICO -DMUSASHI_CNF=\\\"../include/m68kconf.h\\\" -DUMAC_MEMSIZE=${MEMSIZE}")

if (USE_WAVESHARE_LCD)
   if (SD_TX STREQUAL "3")
      set(SD_TX 11 CACHE STRING "SD SPI TX pin" FORCE)
   endif()
   if (SD_RX STREQUAL "4")
      set(SD_RX 12 CACHE STRING "SD SPI RX pin" FORCE)
   endif()
   if (SD_SCK STREQUAL "2")
      set(SD_SCK 10 CACHE STRING "SD SPI SCK pin" FORCE)
   endif()
   if (SD_CS STREQUAL "5")
      set(SD_CS 22 CACHE STRING "SD SPI CS pin" FORCE)
   endif()
   if (SD_SPI STREQUAL "0")
      set(SD_SPI 1 CACHE STRING "SD SPI controller (0 or 1)" FORCE)
   endif()
endif()

if (USE_SD)
   add_compile_definitions(USE_SD=1)
   set(FF_DISABLE_RTC ${PICO_RP2350})  # RP2350 doesn't have RTC, so disable it
   add_subdirectory(external/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)
   set(EXTRA_SD_SRC src/sd_hw_config.c)
   set(EXTRA_SD_LIB FatFs_SPI)
   add_compile_definitions(SD_TX=${SD_TX} SD_RX=${SD_RX} SD_SCK=${SD_SCK} SD_CS=${SD_CS} SD_MHZ=${SD_MHZ} SD_SPI=${SD_SPI})
endif()

if (USE_VGA_RES)
   add_compile_definitions(USE_VGA_RES=1)
   add_compile_definitions(DISP_WIDTH=640)
   add_compile_definitions(DISP_HEIGHT=480)
else()
   add_compile_definitions(DISP_WIDTH=512)
   add_compile_definitions(DISP_HEIGHT=342)
endif()
add_compile_definitions(GPIO_VID_BASE=${VIDEO_PIN})
if (USE_WAVESHARE_LCD)
   add_compile_definitions(USE_WAVESHARE_LCD=1)
   add_compile_definitions(LCD_SPI=${LCD_SPI} LCD_MHZ=${LCD_MHZ})
   add_compile_definitions(LCD_PIN_SCK=${LCD_PIN_SCK} LCD_PIN_MOSI=${LCD_PIN_MOSI} LCD_PIN_MISO=${LCD_PIN_MISO})
   add_compile_definitions(LCD_PIN_CS=${LCD_PIN_CS} LCD_PIN_DC=${LCD_PIN_DC} LCD_PIN_RST=${LCD_PIN_RST} LCD_PIN_BL=${LCD_PIN_BL})
   add_compile_definitions(LCD_WIDTH=${LCD_WIDTH} LCD_HEIGHT=${LCD_HEIGHT})
   add_compile_definitions(LCD_FILTER_MODE=${LCD_FILTER_MODE})
   if (USE_TOUCH)
      add_compile_definitions(USE_TOUCH=1)
   else()
      add_compile_definitions(USE_TOUCH=0)
   endif()
   add_compile_definitions(TOUCH_PIN_CS=${TOUCH_PIN_CS} TOUCH_PIN_IRQ=${TOUCH_PIN_IRQ})
   add_compile_definitions(TOUCH_MHZ=${TOUCH_MHZ})
   add_compile_definitions(TOUCH_USE_IRQ=${TOUCH_USE_IRQ})
   add_compile_definitions(TOUCH_RAW_MIN_X=${TOUCH_RAW_MIN_X} TOUCH_RAW_MAX_X=${TOUCH_RAW_MAX_X})
   add_compile_definitions(TOUCH_RAW_MIN_Y=${TOUCH_RAW_MIN_Y} TOUCH_RAW_MAX_Y=${TOUCH_RAW_MAX_Y})
   add_compile_definitions(TOUCH_SWAP_XY=${TOUCH_SWAP_XY} TOUCH_INVERT_X=${TOUCH_INVERT_X} TOUCH_INVERT_Y=${TOUCH_INVERT_Y})
endif()

if (USE_WAVESHARE_LCD)
  set(VIDEO_SRC src/video_waveshare_lcd.c)
  set(EXTRA_LCD_LIB hardware_spi)
else()
  set(VIDEO_SRC src/video.c)
endif()

if (TARGET tinyusb_device)
  add_executable(firmware
    src/main.c
    ${VIDEO_SRC}
    src/kbd.c
    src/hid.c
    ${EXTRA_SD_SRC}

    ${UMAC_SOURCES}
    )

  # The umac sources need to prepare Musashi (some sources are generated):
  add_custom_command(OUTPUT ${UMAC_MUSASHI_PATH}/m68kops.c
    COMMAND echo "*** Preparing umac source ***"
    COMMAND make -C ${UMAC_PATH} prepare
    )
  add_custom_target(prepare_umac
    DEPENDS ${UMAC_MUSASHI_PATH}/m68kops.c
    )
  add_dependencies(firmware prepare_umac)

  target_link_libraries(firmware
    pico_stdlib
    pico_multicore
    tinyusb_host
    tinyusb_board
    hardware_dma
    hardware_pio
    hardware_sync
    ${EXTRA_LCD_LIB}
    ${EXTRA_SD_LIB}
    )

  target_include_directories(firmware PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${TINYUSB_PATH}/hw
    ${TINYUSB_PATH}/src
    ${UMAC_INCLUDE_PATHS}
    incbin
    )

  if (NOT USE_WAVESHARE_LCD)
    pico_generate_pio_header(firmware ${CMAKE_CURRENT_LIST_DIR}/src/pio_video.pio)
  endif()

  pico_enable_stdio_uart(firmware 1)

  # Needed for UF2:
  pico_add_extra_outputs(firmware)

elseif(PICO_ON_DEVICE)
   message(WARNING "not building firmware because TinyUSB submodule is not initialized in the SDK")
endif()
