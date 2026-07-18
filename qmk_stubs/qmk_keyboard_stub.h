#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Minimal QMK surface consumed by notifier.c when stub-compiled on a host.
 * In a real QMK build these come transitively from #include QMK_KEYBOARD_H. */
void layer_on(uint8_t layer);
void layer_off(uint8_t layer);

#ifndef RAW_EPSIZE
#define RAW_EPSIZE 32
#endif
