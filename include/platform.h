#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

void platform_led_init(void);
void platform_led_set(bool on);
uint64_t platform_time_us(void);
bool platform_bootsel_pressed(void);

#endif /* PLATFORM_H */
