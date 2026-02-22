/* ESP32-S3 platform glue (placeholder).
 *
 * This file is intentionally not wired into the top-level build yet.
 * It documents the API surface that needs ESP-IDF implementations.
 */

#include "platform.h"

void platform_led_init(void)
{
}

void platform_led_set(bool on)
{
        (void)on;
}

uint64_t platform_time_us(void)
{
        return 0;
}

bool platform_bootsel_pressed(void)
{
        return false;
}
