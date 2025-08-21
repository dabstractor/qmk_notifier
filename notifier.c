// notifier.c
#include QMK_KEYBOARD_H
#include "pattern_match.c"
#include "notifier.h"
#include "raw_hid.h"
#include <string.h>
#ifdef CONSOLE_ENABLE
#include "print.h"
#endif

// Function to sanitize strings by removing non-ASCII characters
// This prevents Unicode decode errors when the data is processed by the Python CLI
static void sanitize_string(char *str) {
    if (!str) return;
    
    char *read_ptr = str;
    char *write_ptr = str;
    
    while (*read_ptr) {
        // Only allow printable ASCII characters (32-126) and essential control characters (9, 10, 13)
        // Also allow our delimiter and terminator characters
        if ((*read_ptr >= 32 && *read_ptr <= 126) || 
            *read_ptr == 9 ||   // tab
            *read_ptr == 10 ||  // newline
            *read_ptr == 13 ||  // carriage return
            *read_ptr == GS_DELIMITER[0] ||  // group separator (our delimiter)
            *read_ptr == ETX_TERMINATOR[0]) { // end of text (our terminator)
            *write_ptr++ = *read_ptr;
        }
        // Skip non-ASCII characters (values > 127) and other control characters
        read_ptr++;
    }
    
    // Null terminate the sanitized string
    *write_ptr = '\0';
}

// Maximum size for the assembled command.
#define MSG_BUFFER_SIZE 256
// Buffer to accumulate incoming data.
static char msg_buffer[MSG_BUFFER_SIZE];
// Current write index into the buffer.
static uint16_t msg_index = 0;

// Default empty map if user doesn't define them
static command_map_t empty_command_map[1] = {0};
static layer_map_t empty_layer_map[1] = {0};

// Default implementations that will be used if the user doesn't call DEFINE_SERIAL_COMMANDS/LAYERS
__attribute__((weak)) command_map_t* get_command_map(void) {
    return empty_command_map;
}

__attribute__((weak)) size_t get_command_map_size(void) {
    return 0;
}

__attribute__((weak)) layer_map_t* get_layer_map(void) {
    return empty_layer_map;
}

__attribute__((weak)) size_t get_layer_map_size(void) {
    return 0;
}

#define LAYER_UNSET 255
uint8_t activated_layer = LAYER_UNSET;
// reference to currently active command:
command_map_t *current_command = {0};

void activate_layer(uint8_t layer) {
    #ifdef CONSOLE_ENABLE
    uprintf("Activating layer %d\n", layer);
    #endif
    layer_on(layer);
    activated_layer = layer;
}

void deactivate_layer(void) {
    if (activated_layer == LAYER_UNSET) {
        return;
    }
    #ifdef CONSOLE_ENABLE
    uprintf("Deactivating layer %d\n", activated_layer);
    #endif
    layer_off(activated_layer);
    activated_layer = LAYER_UNSET;
}

void enable_command(command_map_t *command) {
    current_command = command;
    command->on_enable();
}

void disable_command(void) {
    if (current_command != NULL && current_command->on_disable != NULL) {
        current_command->on_disable();
    }
    current_command = NULL;
}

// Helper function to find unit delimiters in a string
const char* find_first_delimiter(const char *str) {
    for (const char *p = str; *p != '\0'; p++) {
        if (*p == GS_DELIMITER[0]) {  // Add any other delimiters here
            return p;
        }
    }
    return NULL;
}

// Helper function to split string by delimiter
bool split_by_delimiter(const char *source, const char *delimiter_pos,
                         char *left, size_t left_size,
                         char *right, size_t right_size) {
    if (!delimiter_pos) {
        return false;
    }

    // Split source
    size_t left_len = delimiter_pos - source;
    if (left_len >= left_size) {
        return false;
    }

    strncpy(left, source, left_len);
    left[left_len] = '\0';

    // Copy right part
    if (strlen(delimiter_pos + 1) >= right_size) {
        return false;
    }

    strcpy(right, delimiter_pos + 1);
    return true;
}

// Generic function for pattern matching with delimiter support
bool match_pattern(const char *pattern, const char *message, bool case_sensitive) {
    const char *pattern_delimiter = find_first_delimiter(pattern);

    if (message == NULL || pattern == NULL) {
        return false;
    }

    if (pattern_delimiter == NULL) {
        // No delimiter in pattern
        // But check if message has a delimiter
        const char *msg_delimiter_pos = find_first_delimiter(message);

        if (msg_delimiter_pos != NULL) {
            // Message has a delimiter but pattern doesn't
            // Match only against first part of message
            char msg_left[256] = {0};
            size_t left_len = msg_delimiter_pos - message;

            if (left_len >= sizeof(msg_left)) {
                return false;
            }

            strncpy(msg_left, message, left_len);
            msg_left[left_len] = '\0';

            // Match pattern against only first part of message
            bool result = pattern_match(pattern, msg_left, case_sensitive);
            return result;
        }

        // No delimiter in either string, use direct pattern matching
        bool result = pattern_match(pattern, message, case_sensitive);
        return result;
    }

    // Pattern contains a delimiter, check if message has the same delimiter
    char delimiter = *pattern_delimiter;
    char *msg_delimiter_pos = NULL;

    for (char *p = (char*)message; *p != '\0'; p++) {
        if (*p == delimiter) {
            msg_delimiter_pos = p;
            break;
        }
    }

    if (msg_delimiter_pos == NULL) {
        // Message doesn't have the delimiter
        // But we should still try to match the part before the delimiter
        char pattern_left[256] = {0};
        size_t left_len = pattern_delimiter - pattern;

        if (left_len >= sizeof(pattern_left)) {
            return false;
        }

        strncpy(pattern_left, pattern, left_len);
        pattern_left[left_len] = '\0';

        // Only match the first part of the pattern against the entire message
        return pattern_match(pattern_left, message, case_sensitive);
    }

    // Split both pattern and message
    char pattern_left[256] = {0};
    char pattern_right[256] = {0};
    char msg_left[256] = {0};
    char msg_right[256] = {0};

    if (!split_by_delimiter(pattern, pattern_delimiter,
                           pattern_left, sizeof(pattern_left),
                           pattern_right, sizeof(pattern_right))) {
        return false;
    }

    if (!split_by_delimiter(message, msg_delimiter_pos,
                          msg_left, sizeof(msg_left),
                          msg_right, sizeof(msg_right))) {
        return false;
    }

    // Match both sides
    return pattern_match(pattern_left, msg_left, case_sensitive) &&
           pattern_match(pattern_right, msg_right, case_sensitive);
}

bool process_full_message(char *data) {
    char received_command[256] = {0};
    int length = strlen(data);
    command_map_t *command_found = NULL;
    uint8_t layer_found = LAYER_UNSET;
    signed int found_command_match = -1;
    signed int found_layer_match = -1;

    if (length >= sizeof(received_command)) {
        return false;
    }

    memcpy(received_command, data, length);
    received_command[length] = '\0';

    // Always disable current command first
    disable_command();

    // Get the current command map and size
    command_map_t *cmd_map = get_command_map();
    size_t cmd_map_size = get_command_map_size();

    // Get the current layer map and size
    layer_map_t *lyr_map = get_layer_map();
    size_t lyr_map_size = get_layer_map_size();

    // Command map checks
    for (int i = 0; i < cmd_map_size; i++) {
        if (match_pattern(cmd_map[i].pattern, received_command, cmd_map[i].case_sensitive)) {
            found_command_match = i;
            command_found = &cmd_map[i];
            break;
        }
    }

    // Layer map checks
    for (int i = 0; i < lyr_map_size; i++) {
        if (match_pattern(lyr_map[i].pattern, received_command, lyr_map[i].case_sensitive)) {
            found_layer_match = i;
            layer_found = lyr_map[i].layer;
            break;
        }
    }

    // Always deactivate the current layer first
    deactivate_layer();

    // Enable new command if found
    if (found_command_match != -1 && command_found != NULL) {
        enable_command(command_found);
    }

    // Activate new layer if found
    if (found_layer_match != -1 && layer_found != LAYER_UNSET) {
        activate_layer(layer_found);
    }

    #ifdef CONSOLE_ENABLE
    // replace all unit delimeters with |
    for (int i = 0; i < strlen(received_command); i++) {
        if (received_command[i] == GS_DELIMITER[0]) {
            received_command[i] = '|';
        }
    }

    if (found_command_match != -1) {
        uprintf("Matched message %s on command: %s\n", received_command, cmd_map[found_command_match].pattern);
    } else {
        uprintf("Did not match message %s on any command\n", received_command);
    }
    #endif

    return found_command_match != -1 || found_layer_match != -1;
}


void hid_notify(uint8_t *data, uint8_t length) {
    // Check for our identifiers to ensure no conflicts with other libraries
    if (length < 2 || data[0] != 0x81 || data[1] != 0x9F) {
        return; // Discard the message if it doesn't match
    }

    // Strip off those 2 identifying characters
    data += 2;
    length -= 2;

    // Process each byte of the incoming packet.
    bool match = false;
    for (uint8_t i = 0; i < length; i++) {
        char c = (char)data[i];
        // End of text (ASCII 3) indicates the end of the message.
        if (c == ETX_TERMINATOR[0]) {
            msg_buffer[msg_index] = '\0'; // Ensure the buffer is properly terminated
            
            // Sanitize the buffer to remove non-ASCII characters that could cause decode errors
            sanitize_string(msg_buffer);
            
            msg_index = 0; // Reset the buffer for the next message
            match = process_full_message(msg_buffer);
            break;
        } else {
            // Append character if space is available.
            if (msg_index < (MSG_BUFFER_SIZE - 1)) {
                msg_buffer[msg_index++] = c;
            } else {
                // Buffer overflow – reset the buffer to prevent corruption.
                msg_index = 0;
            }
        }
    }
    uint8_t response[length] = {};
    response[0] = match;
    for (uint8_t i = 1; i < length; i++) {
        response[i] = 0;
    }
    raw_hid_send(response, length);
}
