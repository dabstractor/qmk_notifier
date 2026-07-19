#pragma once
#include <stdio.h>

/*
 * Minimal host-test STUB of QMK's quantum/print.h.
 *
 * Provides ONLY uprintf — the single print API this module uses — as a variadic
 * MACRO that forwards to printf. This file is consumed ONLY under
 * -DCONSOLE_ENABLE (notifier.c:20-21 guards the #include "print.h"); the normal
 * host gate (run_notifier_stub_tests.sh) builds WITHOUT -DCONSOLE_ENABLE and
 * never pulls this header in. It exists so the CONSOLE_ENABLE debug path can be
 * compile-checked on a host via a one-off `gcc -DCONSOLE_ENABLE` command.
 *
 * In a real QMK build, the genuine quantum/print.h (pulled in transitively via
 * #include QMK_KEYBOARD_H) provides the full uprintf implementation that writes
 * to the USB console. This stub deliberately OMITS every other QMK print API
 * (uprint, xprintf, print, etc.) — none are used by notifier.c.
 */
#define uprintf(...) printf(__VA_ARGS__)