#ifndef LCD_WS28B_H
#define LCD_WS28B_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define LCD_WIDTH_RES   480
#define LCD_HEIGHT_RES  640

esp_err_t lcd_ws28b_init(void);
esp_err_t lcd_ws28b_blit_mono(const uint8_t *src, int src_w, int src_h);
void lcd_ws28b_set_backlight(uint8_t level);

#endif /* LCD_WS28B_H */
