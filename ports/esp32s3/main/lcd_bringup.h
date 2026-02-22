#ifndef PICO_MAC_ESP32S3_LCD_BRINGUP_H
#define PICO_MAC_ESP32S3_LCD_BRINGUP_H

#include <stdbool.h>
#include <stdint.h>

bool lcd_bringup_test(void);
bool lcd_panel_init(void);
bool lcd_blit_mono(const uint8_t *fb, int fb_width, int fb_height);

#endif /* PICO_MAC_ESP32S3_LCD_BRINGUP_H */
