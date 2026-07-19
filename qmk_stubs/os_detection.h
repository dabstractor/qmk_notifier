#pragma once
#include <stdint.h>

/*
 * Minimal host-test STUB of QMK's quantum/os_detection.h.
 *
 * Provides ONLY the os_variant_t TYPE — the single thing this module consumes.
 * The module uses the type (and the OS_UNSURE/OS_LINUX/OS_WINDOWS/OS_MACOS/OS_IOS
 * enumerator NAMES) but NEVER calls detected_host_os() or any other function
 * declared in the real header (invariant 17 / §13; §2 F8.2). The OS is PUSHED
 * in by the keymap via notifier_set_os(), so there is NO link dependency on the
 * OS-detection .c subsystem.
 *
 * This file deliberately OMITS:
 *   - #include "usb_device_state.h"  (the real header starts with it, but it
 *     would cascade into undefined types in the host harness — architecture F4)
 *   - detected_host_os() / process_detected_host_os_kb() / os_detection_task()
 *     / process_wlength() / etc. declarations (declaring them would require
 *     linking their implementations, which the stub harness does not provide).
 *
 * In a real QMK build, the genuine quantum/os_detection.h is pulled in
 * transitively via #include QMK_KEYBOARD_H and provides the full, richer header.
 * This stub exists solely so notifier.h/notifier.c (which #include
 * "os_detection.h") compile under the host harness with -Iqmk_stubs.
 */
typedef enum {
    OS_UNSURE,   /* = 0  — boot state; no OS-specific map (§2 F8.6) */
    OS_LINUX,    /* = 1 */
    OS_WINDOWS,  /* = 2 */
    OS_MACOS,    /* = 3 */
    OS_IOS,      /* = 4 */
} os_variant_t;