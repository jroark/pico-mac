#ifndef PICO_MAC_ESP32S3_PINMAP_H
#define PICO_MAC_ESP32S3_PINMAP_H

/* Shared SPI bus for LCD panel, XPT2046 touch, and SD card */
#define PM_SPI_SCK_GPIO         12
#define PM_SPI_MOSI_GPIO        11
#define PM_SPI_MISO_GPIO        13

/* Chip selects on shared SPI bus */
#define PM_LCD_CS_GPIO          10
#define PM_TOUCH_CS_GPIO        9
#define PM_SD_CS_GPIO           14

/* LCD control lines */
#define PM_LCD_DC_GPIO          8
#define PM_LCD_RST_GPIO         7
#define PM_LCD_BL_GPIO          6

/* Touch IRQ (active low) */
#define PM_TOUCH_IRQ_GPIO       5

/* Optional status LED for debug blink/error patterns */
#define PM_STATUS_LED_GPIO      48

#endif /* PICO_MAC_ESP32S3_PINMAP_H */
