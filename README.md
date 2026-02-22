# Pico Micro Mac (`pico-umac`)

Embedded classic Mac emulator firmware built around [umac](https://github.com/evansm7/umac), targeting Raspberry Pi Pico / RP2040 boards.

This tree currently has:
- Stable RP2040 firmware path (`TARGET_PLATFORM=pico`)
- VGA and Waveshare SPI LCD display paths
- USB HID keyboard/mouse input
- Optional SD-backed disk images and log file output
- Touchscreen mouse support with persistent pre-boot calibration (Waveshare mode)
- Experimental ESP32-S3 port in `ports/esp32s3/`

## Project Status

- Primary target: **RP2040/Pico**
- ESP32-S3: **experimental** (LCD + touch + SD image path available)

## Quick Start (RP2040)

### 1. Clone with submodules

```bash
git clone --recursive <repo-url>
# or after clone:
git submodule update --init --recursive
```

### 2. Build `umac` helper first

`umac` is used to patch the ROM image for memory/resolution settings.

```bash
cd external/umac
make
```

If you plan to use non-default memory or framebuffer dimensions, build `umac` with matching values, for example:

```bash
make MEMSIZE=208 DISP_WIDTH=320 DISP_HEIGHT=240
```

### 3. Generate ROM/disc include headers

From repo root:

```bash
mkdir -p incbin

# Create patched ROM
./external/umac/main -r '4D1F8172 - MacPlus v3.ROM' -W rom.bin
xxd -i < rom.bin > incbin/umac-rom.h

# Optional in-flash fallback disk image
auto_disc="disc.bin"
if [ -f "$auto_disc" ]; then
  xxd -i < "$auto_disc" > incbin/umac-disc.h
else
  : > incbin/umac-disc.h
fi
```

### 4. Configure and build firmware

```bash
cmake -S . -B build -DTARGET_PLATFORM=pico
cmake --build build -j
```

Output:
- `build/firmware.uf2`

## Common Build Configurations

### Waveshare Pico-ResTouch-LCD-2.8 + SD + touch

```bash
cmake -S . -B build-sd \
  -DTARGET_PLATFORM=pico \
  -DUSE_WAVESHARE_LCD=1 \
  -DUSE_SD=1 \
  -DSD_SPI=1 -DSD_SCK=10 -DSD_TX=11 -DSD_RX=12 -DSD_CS=22

cmake --build build-sd -j
```

### Emulated framebuffer override (example 320x240)

```bash
cmake -S . -B build-sd \
  -DTARGET_PLATFORM=pico \
  -DEMU_DISP_WIDTH=320 -DEMU_DISP_HEIGHT=240
```

## Runtime Behavior (Waveshare + SD)

When `USE_WAVESHARE_LCD=1`, `USE_TOUCH=1`, and `USE_SD=1`:

- Touch calibration is checked before emulator boot.
- If `0:/touch.cal` exists, it is loaded.
- If missing, interactive calibration runs and writes `0:/touch.cal`.
- Logs are mirrored to SD (`umac.log` variants attempted).

Notes:
- LCD, touch, and SD may share SPI; firmware serializes access.
- If SD is not available, firmware falls back to in-flash disk image.

## Configuration Options

### Core options

- `-DTARGET_PLATFORM=pico|esp32s3`
  - top-level build currently supports `pico` only
- `-DMEMSIZE=<KB>`
- `-DEMU_DISP_WIDTH=<px>`
- `-DEMU_DISP_HEIGHT=<px>`
- `-DUSE_VGA_RES=0|1`

### RP2040 VGA path

- `-DVIDEO_PIN=<gpio base>`

### SD options

- `-DUSE_SD=0|1`
- `-DSD_SPI=0|1`
- `-DSD_SCK=<gpio>`
- `-DSD_TX=<gpio>`
- `-DSD_RX=<gpio>`
- `-DSD_CS=<gpio>`
- `-DSD_MHZ=<MHz>`

### Waveshare LCD/touch options

- `-DUSE_WAVESHARE_LCD=0|1`
- `-DLCD_SPI=<0|1>`
- `-DLCD_MHZ=<MHz>`
- `-DLCD_PIN_SCK=<gpio>`
- `-DLCD_PIN_MOSI=<gpio>`
- `-DLCD_PIN_MISO=<gpio>`
- `-DLCD_PIN_CS=<gpio>`
- `-DLCD_PIN_DC=<gpio>`
- `-DLCD_PIN_RST=<gpio>`
- `-DLCD_PIN_BL=<gpio>`
- `-DLCD_WIDTH=<px>`
- `-DLCD_HEIGHT=<px>`
- `-DLCD_FILTER_MODE=0|1`
- `-DLCD_PRESERVE_ASPECT=0|1`

Touch-specific:
- `-DUSE_TOUCH=0|1`
- `-DTOUCH_MHZ=<MHz>`
- `-DTOUCH_PIN_CS=<gpio>`
- `-DTOUCH_PIN_IRQ=<gpio>`
- `-DTOUCH_USE_IRQ=0|1`
- `-DTOUCH_RAW_MIN_X=<n>` / `-DTOUCH_RAW_MAX_X=<n>`
- `-DTOUCH_RAW_MIN_Y=<n>` / `-DTOUCH_RAW_MAX_Y=<n>`
- `-DTOUCH_SWAP_XY=0|1`
- `-DTOUCH_INVERT_X=0|1`
- `-DTOUCH_INVERT_Y=0|1`
- `-DTOUCH_OFFSET_X=<px>`
- `-DTOUCH_OFFSET_Y=<px>`
- `-DTOUCH_EDGE_SNAP_X=<px>`
- `-DTOUCH_EDGE_SNAP_Y=<px>`

Optional capture feature:
- `-DUSE_BOOTSEL_CAPTURE=0|1`

## Disk Images

Supported boot disk names on SD root:
- `umac0.img` (read/write)
- `umac0ro.img` (read-only)

If SD mount/open fails, firmware can boot from embedded image (`incbin/umac-disc.h`).

## Hardware Notes (RP2040)

### VGA mode (default path)

Default Pico GPIO usage:
- `GP18`: video data (through resistor to VGA RGB)
- `GP19`: VSYNC
- `GP21`: HSYNC

See source comments and options for remapping (`VIDEO_PIN`).

### SD default (non-Waveshare)

- `GP2`: SCK
- `GP3`: MOSI
- `GP4`: MISO
- `GP5`: CS

### Waveshare Pico-ResTouch-LCD-2.8 defaults

- LCD SPI shares pins with board wiring defaults in CMake
- Touch defaults tuned for common XPT2046 orientation
- SD defaults are auto-adjusted for common Waveshare wiring when LCD mode is enabled

## ESP32-S3 Port

Port work has started under:
- `ports/esp32s3/`

Current status:
- ESP-IDF app path is available under `ports/esp32s3/`
- Shared SPI LCD + touch + SD stack is implemented for Waveshare 2.8 wiring
- SD disk images are file-backed (no full-image RAM copy required)
- USB monitor logging is available via `idf.py monitor`
- RP2040 remains the primary/most stable target

For details, see:
- `ports/esp32s3/README.md`

## Troubleshooting

- Reconfigure cache issues:
  - delete build directory and rerun CMake
- Touch alignment issues:
  - remove `touch.cal` on SD to force recalibration at next boot
- Cursor jitter on shared SPI:
  - reduce SD traffic and verify touch IRQ pin wiring
- ESP32 USB debug logs:
  - `cd ports/esp32s3 && source ~/src/esp/esp-idf/export.sh && idf.py -p /dev/cu.usbmodem1101 monitor`

## Acknowledgements

- `hid.c` and `include/tusb_config.h` derive from TinyUSB examples (MIT)
- `src/sd_hw_config.c` is based on no-OS-FatFS-SD-SPI-RPi-Pico

## License

MIT License, Copyright (c) 2024 Matt Evans.

See source headers for full text and third-party notices.
