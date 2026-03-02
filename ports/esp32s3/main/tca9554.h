#ifndef TCA9554_H
#define TCA9554_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define TCA9554_ADDR            0x20

/* Registers */
#define TCA9554_REG_INPUT       0x00
#define TCA9554_REG_OUTPUT      0x01
#define TCA9554_REG_POLARITY    0x02
#define TCA9554_REG_CONFIG      0x03

esp_err_t tca9554_init(int sda_io, int scl_io);
esp_err_t tca9554_write_reg(uint8_t reg, uint8_t val);
esp_err_t tca9554_read_reg(uint8_t reg, uint8_t *val);

/* Set pin mode (0 = output, 1 = input) */
esp_err_t tca9554_set_mode(uint8_t pin, bool input);

/* Set output level */
esp_err_t tca9554_set_level(uint8_t pin, bool high);

/* Read input level */
esp_err_t tca9554_get_level(uint8_t pin, bool *high);

#endif /* TCA9554_H */
