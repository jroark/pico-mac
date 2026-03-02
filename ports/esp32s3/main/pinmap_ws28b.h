#ifndef PICO_MAC_ESP32S3_PINMAP_WS28B_H
#define PICO_MAC_ESP32S3_PINMAP_WS28B_H

/* Waveshare ESP32-S3-Touch-LCD-2.8B board map (official wiki/schematic). */

/* Shared internal I2C bus (fixed on board). */
#define PM_I2C_SDA_GPIO         15
#define PM_I2C_SCL_GPIO         7

/* LCD RGB interface (ST7701). */
#define PM_LCD_RGB_PCLK_GPIO    41
#define PM_LCD_RGB_VSYNC_GPIO   39
#define PM_LCD_RGB_HSYNC_GPIO   38
#define PM_LCD_RGB_DE_GPIO      40

/*
 * RGB565 host data bus order:
 * D0..D4  -> panel B[1..5]
 * D5..D10 -> panel G[0..5]
 * D11..D15-> panel R[1..5]
 */
#define PM_LCD_RGB_B0_GPIO      5
#define PM_LCD_RGB_B1_GPIO      45
#define PM_LCD_RGB_B2_GPIO      48
#define PM_LCD_RGB_B3_GPIO      47
#define PM_LCD_RGB_B4_GPIO      21

#define PM_LCD_RGB_G0_GPIO      14
#define PM_LCD_RGB_G1_GPIO      13
#define PM_LCD_RGB_G2_GPIO      12
#define PM_LCD_RGB_G3_GPIO      11
#define PM_LCD_RGB_G4_GPIO      10
#define PM_LCD_RGB_G5_GPIO      9

#define PM_LCD_RGB_R0_GPIO      46
#define PM_LCD_RGB_R1_GPIO      3
#define PM_LCD_RGB_R2_GPIO      8
#define PM_LCD_RGB_R3_GPIO      18
#define PM_LCD_RGB_R4_GPIO      17

/* LCD 3-wire SPI (init interface for ST7701 registers). */
#define PM_LCD_SPI_SCL_GPIO     2
#define PM_LCD_SPI_SDA_GPIO     1

/* LCD backlight. */
#define PM_LCD_BL_GPIO          6

/* TCA9554 GPIO expander pins (P0..P7, 0-indexed). */
#define PM_EXIO_LCD_RST         0
#define PM_EXIO_TP_RST          1
#define PM_EXIO_LCD_CS          2
#define PM_EXIO_SD_CS           3
#define PM_EXIO_IMU_INT1        4
#define PM_EXIO_IMU_INT2        5
#define PM_EXIO_RTC_INT         6
#define PM_EXIO_BUZZER          7

/* Touch (GT911). */
#define PM_TOUCH_IRQ_GPIO       16

/* SD card (SPI mode). */
#define PM_SD_SPI_SCK_GPIO      2
#define PM_SD_SPI_MOSI_GPIO     1
#define PM_SD_SPI_MISO_GPIO     42

#endif /* PICO_MAC_ESP32S3_PINMAP_WS28B_H */
