# ESP32-S3 Port (Experimental)

This directory contains the ESP-IDF firmware path for ESP32-S3 boards.

## Current scope

- uMac core boots and runs
- ILI9341 SPI LCD output
- XPT2046 touch input
- SD card mount over SPI (`SDSPI`)
- SD disk image load from SD at boot (RAM-first, file-backed fallback on low heap)

RP2040 remains the primary target; this port is still experimental.

## Board/pin mapping (YD-ESP32-23)

Defined in `main/pinmap.h`:

- Shared SPI bus:
  - `SCK=GPIO12`
  - `MOSI=GPIO11`
  - `MISO=GPIO13`
- Chip selects:
  - `LCD_CS=GPIO10`
  - `TOUCH_CS=GPIO9`
  - `SD_CS=GPIO14` (fallback probe also tries GPIO15)
- LCD control:
  - `LCD_DC=GPIO8`
  - `LCD_RST=GPIO7`
  - `LCD_BL=GPIO6`
- Touch IRQ:
  - `TOUCH_IRQ=GPIO5`

## Build / flash / monitor

```bash
cd ports/esp32s3
source ~/src/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
idf.py -p /dev/cu.usbmodem1101 monitor
```

## SD image behavior

- SD root is scanned for common image names and extensions:
  - `umac0.img`, `UMAC0.IMG`, `disc.img`, `disc.dsk`, `system.img`, `system.dsk`
  - first `*.img|*.dsk|*.dc42` found is used
- If open/mount fails, firmware falls back to built-in `incbin` disk image.
