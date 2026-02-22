#ifndef PICO_MAC_ESP32_SDCARD_DISC_H
#define PICO_MAC_ESP32_SDCARD_DISC_H

#include <stdbool.h>

#include "disc.h"

bool sdcard_disc_try_load(disc_descr_t *out_disc);

#endif /* PICO_MAC_ESP32_SDCARD_DISC_H */
