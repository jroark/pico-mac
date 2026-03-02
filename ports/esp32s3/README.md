# ESP32-S3 Port (Waveshare 2.8B)

This ESP-IDF port targets the **Waveshare ESP32-S3-Touch-LCD-2.8B** board.

## Status

- uMac boots and runs
- RGB LCD path works (ST7701, 480x640 panel)
- GT911 touch init/poll works over I2C
- SD mounts over SPI and disk image loads from `/sdcard`
- SD image loading is RAM-first (PSRAM) with file-backed fallback on low heap

RP2040 remains the primary stable target for this project.

## Display Model

- Physical panel: `480x640` (portrait)
- Emulator framebuffer in this port: `640x480` (landscape)
- Blit path rotates 90 degrees into the portrait panel

## Board Mapping

Defined in `main/pinmap_ws28b.h`.

### I2C

- `SDA=GPIO15`
- `SCL=GPIO7`

### RGB LCD Signals

- `PCLK=GPIO41`
- `VSYNC=GPIO39`
- `HSYNC=GPIO38`
- `DE=GPIO40`

RGB565 bus wiring (host D0..D15):

- `D0..D4`: `GPIO5,45,48,47,21`
- `D5..D10`: `GPIO14,13,12,11,10,9`
- `D11..D15`: `GPIO46,3,8,18,17`

### LCD Init SPI (3-wire bit-bang)

- `SCL=GPIO2`
- `SDA=GPIO1`

### Touch / SD

- Touch IRQ: `GPIO16`
- SD SPI: `SCK=GPIO2`, `MOSI=GPIO1`, `MISO=GPIO42`

### TCA9554 Expander (P0..P7)

- `P0`: LCD reset
- `P1`: touch reset
- `P2`: LCD CS (init interface)
- `P3`: SD CS

## Build / Flash / Monitor

```bash
cd ports/esp32s3
source ~/src/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem2101 flash
idf.py -p /dev/cu.usbmodem2101 monitor
```

Exit monitor: `Ctrl+]`

## SD Disk Image

At boot, firmware scans `/sdcard` for:

- `umac0.img`, `UMAC0.IMG`, `disc.img`, `disc.dsk`, `system.img`, `system.dsk`
- then first matching `*.img|*.dsk|*.dc42`

If SD mount/open fails, it falls back to built-in `incbin` disk image.
