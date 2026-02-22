# ESP32-S3 Port (Work In Progress)

This directory tracks the ESP32-S3 port effort for `pico-mac`.

## Current status

- Platform abstraction started in main firmware:
  - `include/platform.h`
  - `src/platform_pico.c`
- `src/main.c` now uses the platform abstraction for:
  - LED heartbeat
  - microsecond timing
  - BOOTSEL screenshot button polling

## Goal for first bring-up (Phase 1)

Boot the emulator core with:

- SPI LCD output (`video_waveshare_lcd.c` equivalent on ESP-IDF SPI)
- Touch input (XPT2046)
- SD card disk/log file support (SDSPI + FatFS)
- USB HID host keyboard/mouse via TinyUSB host

## Initial pin mapping (YD-ESP32-23)

This mapping is now defined in `main/pinmap.h`:

- SPI bus:
  - `SCK=GPIO12`
  - `MOSI=GPIO11`
  - `MISO=GPIO13`
- Chip selects:
  - `LCD_CS=GPIO10`
  - `TOUCH_CS=GPIO9`
  - `SD_CS=GPIO14`
- LCD control:
  - `LCD_DC=GPIO8`
  - `LCD_RST=GPIO7`
  - `LCD_BL=GPIO6`
- Touch:
  - `TOUCH_IRQ=GPIO5`
- Debug LED:
  - `STATUS_LED=GPIO48`

Reserved/avoid:

- Keep `GPIO19/GPIO20` free for native USB D-/D+.
- Avoid boot strap pins for peripheral wiring (`GPIO0`, `GPIO45`, `GPIO46`).

## Recommended work order

1. Create an ESP-IDF app skeleton with `app_main()`.
2. Implement `platform_esp32s3.c` for timing/LED/button primitives.
3. Add ESP-IDF SPI bus manager with shared LCD/touch/SD arbitration.
4. Port `video_waveshare_lcd.c` hardware calls from Pico SDK to ESP-IDF.
5. Port SD mount + file I/O glue from FatFs_SPI to `esp_vfs_fat`.
6. Port USB HID host glue (`hid.c`) to TinyUSB host under ESP-IDF.
7. Replace Pico multicore assumptions with FreeRTOS task model.

## Notes

- RP2040 VGA path (`src/video.c`, `src/pio_video.pio`) is intentionally out of scope for the ESP32-S3 path.
- Top-level CMake currently builds only `TARGET_PLATFORM=pico`.
