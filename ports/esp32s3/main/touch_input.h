#ifndef PICO_MAC_ESP32_TOUCH_INPUT_H
#define PICO_MAC_ESP32_TOUCH_INPUT_H

#include <stdbool.h>

bool touch_input_init(void);
bool touch_input_poll(int *dx, int *dy, int *button);

#endif /* PICO_MAC_ESP32_TOUCH_INPUT_H */
