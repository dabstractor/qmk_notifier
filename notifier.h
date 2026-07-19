#pragma once
#include <stdbool.h>
#include "os_detection.h"   // header-only: module uses os_variant_t TYPE only,
                          // never calls detected_host_os() (§5.1, §2 F8.2)
typedef void (*callback_t)(void);
typedef struct {
    const char *pattern;
    callback_t on_enable;
    callback_t on_disable;
    const bool case_sensitive;
} command_map_t;
typedef struct {
    const char *pattern;
    const int layer;
    const bool case_sensitive;
} layer_map_t;

// Forward declarations - implementation provided in notifier.c
command_map_t* get_command_map(void);
size_t get_command_map_size(void);
layer_map_t* get_layer_map(void);
size_t get_layer_map_size(void);
// The OS selector — pushed from the keymap (§8.7). os_variant_t comes from
// os_detection.h (included above). ONLY public OS entry point (§2 F8.2).
void notifier_set_os(os_variant_t os);

#define GS_DELIMITER "\x1D"  // ASCII 29 (Group Separator)
#define ETX_TERMINATOR "\x03"  // ASCII 3 (End of Text)
#define WINDOW_TITLE(classname, title) classname GS_DELIMITER title
#define WT(...) WINDOW_TITLE(__VA_ARGS__)

// Define macros to create the maps
#define DEFINE_SERIAL_COMMANDS(...) \
    command_map_t user_command_map[] = __VA_ARGS__; \
    const size_t user_command_map_size = sizeof(user_command_map) / sizeof(user_command_map[0]); \
    command_map_t* get_command_map(void) { return user_command_map; } \
    size_t get_command_map_size(void) { return user_command_map_size; }

#define DEFINE_SERIAL_LAYERS(...) \
    layer_map_t user_layer_map[] = __VA_ARGS__; \
    const size_t user_layer_map_size = sizeof(user_layer_map) / sizeof(user_layer_map[0]); \
    layer_map_t* get_layer_map(void) { return user_layer_map; } \
    size_t get_layer_map_size(void) { return user_layer_map_size; }

// OS-specific map-definition macros (multi-OS overlay, §2 F8 / §5.5).
//
// Naming contract: `os` is an os_variant_t ENUMERATOR token (OS_LINUX,
// OS_WINDOWS, OS_MACOS, OS_IOS). The ##os paste mangles it into the symbols
// _notifier_command_map_OS_MACOS, _notifier_get_command_map_OS_MACOS,
// _notifier_get_command_map_OS_MACOS_size, etc. These EXACT names are what
// notifier.c provides as weak defaults and what its select_*_map_os() switch
// references (§8.3). A typo here = link failure. The keymap never calls these
// directly (internal linkage contract, §5.5).
//
// OS_UNSURE has NO OS-specific map by design (§2 F8.6): do not pass it here.
//
// Row-struct parity: a row has the SAME shape as the default macro
// ({ pattern, on_enable, on_disable, case_sensitive? } for commands;
//  { pattern, layer, case_sensitive? } for layers); an omitted trailing
//  case_sensitive zero-fills to false (§5.4).
//
// Selection rule (§2 F8.4/F8.5): at dispatch, for EACH map type the OS-specific
// map for current_os is scanned FIRST; a match wins and the default map is NOT
// consulted. If no OS map exists (or matches nothing) the default is scanned.
// The command and layer tracks make this decision INDEPENDENTLY. OS_UNSURE ⇒
// default only.
#define DEFINE_SERIAL_COMMANDS_OS(os, ...) \
    command_map_t _notifier_command_map_##os[] = __VA_ARGS__; \
    const size_t  _notifier_command_map_##os##_size = \
        sizeof(_notifier_command_map_##os) / sizeof(_notifier_command_map_##os[0]); \
    command_map_t* _notifier_get_command_map_##os(void) { \
        return _notifier_command_map_##os; \
    } \
    size_t _notifier_get_command_map_##os##_size(void) { \
        return _notifier_command_map_##os##_size; \
    }

#define DEFINE_SERIAL_LAYERS_OS(os, ...) \
    layer_map_t _notifier_layer_map_##os[] = __VA_ARGS__; \
    const size_t  _notifier_layer_map_##os##_size = \
        sizeof(_notifier_layer_map_##os) / sizeof(_notifier_layer_map_##os[0]); \
    layer_map_t* _notifier_get_layer_map_##os(void) { \
        return _notifier_layer_map_##os; \
    } \
    size_t _notifier_get_layer_map_##os##_size(void) { \
        return _notifier_layer_map_##os##_size; \
    }

// From QMK
void raw_hid_receive(uint8_t* data, uint8_t length);
void hid_notify(uint8_t* data, uint8_t length);
