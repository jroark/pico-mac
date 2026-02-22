/* Platform glue for RP2040/Pico SDK.
 *
 * This file is the first step toward splitting hardware-specific code
 * from emulator/application logic so we can add other targets (ESP32-S3).
 */

#include "platform.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#if USE_BOOTSEL_CAPTURE
#include "hardware/regs/io_qspi.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#endif
#include "pico/time.h"

#include "hw.h"

void platform_led_init(void)
{
        gpio_init(GPIO_LED_PIN);
        gpio_set_dir(GPIO_LED_PIN, GPIO_OUT);
}

void platform_led_set(bool on)
{
        gpio_put(GPIO_LED_PIN, on ? 1 : 0);
}

uint64_t platform_time_us(void)
{
        return time_us_64();
}

bool platform_bootsel_pressed(void)
{
#if USE_BOOTSEL_CAPTURE
        const uint CS_PIN_INDEX = 1;
        uint32_t flags = save_and_disable_interrupts();

        hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                        GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
        for (volatile int i = 0; i < 1000; ++i) {
        }

#ifdef __ARM_ARCH_6M__
#define CS_BIT (1u << 1)
#else
#define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#endif
        bool not_pressed = (sio_hw->gpio_hi_in & CS_BIT) != 0;

        hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                        GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
        restore_interrupts(flags);
        return !not_pressed;
#else
        return false;
#endif
}
