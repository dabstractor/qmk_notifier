#pragma once
#include <stdbool.h>
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

#define GS_DELIMITER "\x1D"  // ASCII 31 (Unit Separator)
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

// From QMK
void raw_hid_receive(uint8_t* data, uint8_t length);
void hid_notify(uint8_t* data, uint8_t length);
