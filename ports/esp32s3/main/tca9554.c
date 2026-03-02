#include "tca9554.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "TCA9554";

#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   1000

static uint8_t config_reg = 0xFF;  /* Default: all inputs */
static uint8_t output_reg = 0xFF;  /* Default: all high */

esp_err_t tca9554_init(int sda_io, int scl_io)
{
        i2c_config_t conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = sda_io,
            .scl_io_num = scl_io,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master.clk_speed = I2C_MASTER_FREQ_HZ,
        };

        esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
        if (err != ESP_OK) return err;

        err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

        /* Verify connection by reading config register */
        err = tca9554_read_reg(TCA9554_REG_CONFIG, &config_reg);
        if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to communicate with TCA9554 at 0x%02x", TCA9554_ADDR);
                return err;
        }

        err = tca9554_read_reg(TCA9554_REG_OUTPUT, &output_reg);
        
        ESP_LOGI(TAG, "Initialized TCA9554. Config: 0x%02x, Output: 0x%02x", config_reg, output_reg);
        return err;
}

esp_err_t tca9554_write_reg(uint8_t reg, uint8_t val)
{
        uint8_t data[] = {reg, val};
        return i2c_master_write_to_device(I2C_MASTER_NUM, TCA9554_ADDR, data, sizeof(data), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

esp_err_t tca9554_read_reg(uint8_t reg, uint8_t *val)
{
        return i2c_master_write_read_device(I2C_MASTER_NUM, TCA9554_ADDR, &reg, 1, val, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

esp_err_t tca9554_set_mode(uint8_t pin, bool input)
{
        if (pin > 7) return ESP_ERR_INVALID_ARG;
        
        if (input) {
                config_reg |= (1 << pin);
        } else {
                config_reg &= ~(1 << pin);
        }
        
        return tca9554_write_reg(TCA9554_REG_CONFIG, config_reg);
}

esp_err_t tca9554_set_level(uint8_t pin, bool high)
{
        if (pin > 7) return ESP_ERR_INVALID_ARG;

        if (high) {
                output_reg |= (1 << pin);
        } else {
                output_reg &= ~(1 << pin);
        }

        return tca9554_write_reg(TCA9554_REG_OUTPUT, output_reg);
}

esp_err_t tca9554_get_level(uint8_t pin, bool *high)
{
        if (pin > 7) return ESP_ERR_INVALID_ARG;
        
        uint8_t val = 0;
        esp_err_t err = tca9554_read_reg(TCA9554_REG_INPUT, &val);
        if (err == ESP_OK) {
                *high = (val & (1 << pin)) != 0;
        }
        return err;
}
