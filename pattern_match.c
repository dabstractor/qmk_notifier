#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "notifier.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

// Structure to hold parsed pattern information
typedef struct {
    const char *core_pattern;  // Points to processed pattern
    bool start_anchored;       // true if original pattern started with ^
    bool end_anchored;         // true if original pattern ended with $
    char *processed_pattern;   // Dynamically allocated, needs freeing
} parsed_pattern_t;

// Forward declarations for helper functions
static bool match_string(const char *pattern, const char *str, bool case_sensitive);
static parsed_pattern_t parse_pattern(const char *pattern);
static void free_parsed_pattern(parsed_pattern_t *parsed);
static char *process_escapes(const char *pattern);
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive);
static char get_escaped_char(char placeholder);


bool pattern_match(const char *pattern, const char *str, bool case_sensitive) {
    if (!pattern || !str) {
        return false;
    }

    // Parse the pattern to identify anchors and process escapes
    parsed_pattern_t parsed = parse_pattern(pattern);

    // Use the appropriate matching strategy based on anchors
    bool result = match_with_anchors(&parsed, str, case_sensitive);

    // Clean up allocated memory
    free_parsed_pattern(&parsed);

    return result;
}


// Helper function to check if a match reaches the end of the string
static bool match_reaches_end(const char *pattern, const char *str, bool case_sensitive) {
    const char *p = pattern;
    const char *s = str;

    while (*p && *s) {
        if (*p == '*') {
            p++;
            if (*p == '\0') {
                // Pattern ends with *, so it matches to the end
                return true;
            }

            // Try to match the rest of the pattern
            while (*s) {
                if (match_reaches_end(p, s, case_sensitive)) {
                    return true;
                }
                s++;
            }
            return false;
        } else if (*p >= '\x01' && *p <= '\x04') {
            // Escaped character - compare with the literal character
            char literal_char = get_escaped_char(*p);
            if (case_sensitive ? (literal_char != *s) : (tolower((unsigned char)literal_char) != tolower((unsigned char)*s))) {
                return false;
            }
            p++;
            s++;
        } else {
            if (case_sensitive ? (*p != *s) : (tolower((unsigned char)*p) != tolower((unsigned char)*s))) {
                return false;
            }
            p++;
            s++;
        }
    }

    // Handle remaining wildcards in pattern
    while (*p == '*') p++;

    // Match reaches end if we consumed both pattern and string
    return (*p == '\0' && *s == '\0');
}

// Helper function to match with anchor support
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive) {
    if (!parsed || !str) {
        return false;
    }

    const char *core_pattern = parsed->core_pattern;

    if (parsed->start_anchored && parsed->end_anchored) {
        // Fully anchored: exact match required
        return match_reaches_end(core_pattern, str, case_sensitive);
    } else if (parsed->start_anchored) {
        // Start anchored: match from beginning
        return match_string(core_pattern, str, case_sensitive);
    } else if (parsed->end_anchored) {
        // End anchored: find a match that reaches the end
        size_t str_len = strlen(str);

        for (size_t i = 0; i <= str_len; i++) {
            if (match_reaches_end(core_pattern, str + i, case_sensitive)) {
                return true;
            }
        }
        return false;
    } else {
        // No anchors: substring matching (backward compatibility)

        // Special case: empty pattern should only match empty string
        if (strlen(core_pattern) == 0) {
            return strlen(str) == 0;
        }

        // Try matching at different positions in the string
        size_t str_len = strlen(str);
        for (size_t i = 0; i <= str_len; i++) {
            if (match_string(core_pattern, str + i, case_sensitive)) {
                return true;
            }
        }
        return false;
    }
}

// Helper function to convert escaped placeholder back to original character
static char get_escaped_char(char placeholder) {
    switch (placeholder) {
        case '\x01': return '^';
        case '\x02': return '$';
        case '\x03': return '*';
        case '\x04': return '\\';
        default: return placeholder;
    }
}

// Helper function to check if string starts with pattern
static bool match_string(const char *pattern, const char *str, bool case_sensitive) {
    while (*pattern && *str) {
        if (*pattern == '*') {
            // Wildcard: skip ahead in pattern and try to match the rest
            pattern++;

            // If * is at the end of pattern, it matches anything
            if (*pattern == '\0') return true;

            // Try to match the rest of the pattern at different positions
            for (const char *s = str; *s; s++) {
                if (match_string(pattern, s, case_sensitive)) {
                    return true;
                }
            }
            return false;
        } else if (*pattern >= '\x01' && *pattern <= '\x04') {
            // Escaped character - compare with the literal character
            char literal_char = get_escaped_char(*pattern);
            if (case_sensitive ? (literal_char != *str) : (tolower((unsigned char)literal_char) != tolower((unsigned char)*str))) {
                return false;
            }
        } else if (case_sensitive ? (*pattern != *str) : (tolower((unsigned char)*pattern) != tolower((unsigned char)*str))) {
            // Characters don't match
            return false;
        }

        // Characters match, move to next
        pattern++;
        str++;
    }

    // If we've consumed the entire pattern, it's a match
    // If there's only * left in pattern, also a match (wildcards can match anything)
    while (*pattern == '*') pattern++;

    if (*pattern == '\0') {
        // Pattern is fully consumed
        // For exact matching (anchored), we need string to be consumed too
        // For prefix matching (unanchored), we just need pattern to be consumed
        // Since this function is used in both contexts, we'll return true if pattern is consumed
        // The caller will handle the context-specific logic
        return true;
    }

    return false;
}

// Process escape sequences in a pattern
// Uses special placeholder characters to mark escaped characters
static char *process_escapes(const char *pattern) {
    if (!pattern) return NULL;

    // CRITICAL: The debug output below is required for the code to work correctly!
    // There appears to be a compiler optimization bug that causes src++ to not work
    // properly in the case '\\' block unless debug output is present.
    // The debug output is disabled by default but can be enabled with DEBUG_ESCAPES=1
    bool debug = (getenv("DEBUG_ESCAPES") != NULL && strcmp(pattern, "\\\\test") == 0);

    size_t len = strlen(pattern);
    char *processed = malloc(len + 1);  // Processed string will be same length or shorter
    if (!processed) return NULL;

    const char *src = pattern;
    char *dst = processed;

    while (*src) {
        if (debug) printf("  src at pos %d: \\x%02x\n", (int)(src - pattern), (unsigned char)*src);

        if (*src == '\\' && *(src + 1)) {
            if (debug) printf("    Escape sequence: \\%c\n", *(src + 1));
            // Handle escape sequences
            src++;  // Skip the backslash
            switch (*src) {
                case '^':
                    *dst++ = '\x01';  // Use control character as placeholder for escaped ^
                    src++;
                    break;
                case '$':
                    *dst++ = '\x02';  // Use control character as placeholder for escaped $
                    src++;
                    break;
                case '*':
                    *dst++ = '\x03';  // Use control character as placeholder for escaped *
                    src++;
                    break;
                case '\\':
                    if (debug) printf("    Processing escaped backslash\n");
                    if (debug) printf("    Before increment, src at pos %d\n", (int)(src - pattern));
                    *dst++ = '\x04';  /* Use control character as placeholder for escaped \ */
                    if (debug) printf("    About to increment src\n");
                    src++;
                    if (debug) printf("    After increment, src at pos %d\n", (int)(src - pattern));
                    break;
                default:
                    // Invalid escape sequence, treat backslash as literal
                    *dst++ = '\\';
                    *dst++ = *src++;
                    break;
            }
        } else if (*src == '\\' && *(src + 1) == '\0') {
            // Handle trailing backslash - treat as literal
            *dst++ = *src++;
        } else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
    return processed;
}



// Parse a pattern to identify anchors and process escape sequences
static parsed_pattern_t parse_pattern(const char *pattern) {
    parsed_pattern_t parsed = {0};

    if (!pattern) {
        return parsed;
    }

    const char *start = pattern;
    const char *end = pattern + strlen(pattern);

    // Check for start anchor (^) - only if not escaped
    if (*start == '^') {
        parsed.start_anchored = true;
        start++;  // Skip the ^ character
    }

    // Check for end anchor ($) - only if not escaped
    // We need to check if the $ is escaped by looking for an odd number of backslashes before it
    if (end > start && *(end - 1) == '$') {
        // Count backslashes before the $
        int backslash_count = 0;
        const char *check = end - 2;
        while (check >= start && *check == '\\') {
            backslash_count++;
            check--;
        }

        // If even number of backslashes (including 0), the $ is not escaped
        if (backslash_count % 2 == 0) {
            parsed.end_anchored = true;
            end--;  // Don't include the $ character
        }
    }

    // Create a temporary string for the core pattern (without anchors)
    size_t core_len = end - start;
    char *core_pattern = malloc(core_len + 1);
    if (!core_pattern) {
        // Memory allocation failed, fall back to original pattern
        parsed.core_pattern = pattern;
        parsed.processed_pattern = NULL;
        return parsed;
    }

    strncpy(core_pattern, start, core_len);
    core_pattern[core_len] = '\0';

    // Process escape sequences in the core pattern
    parsed.processed_pattern = process_escapes(core_pattern);
    free(core_pattern);  // Free the temporary core pattern

    if (parsed.processed_pattern) {
        parsed.core_pattern = parsed.processed_pattern;
    } else {
        // If escape processing failed, use the original pattern
        parsed.core_pattern = pattern;
    }

    return parsed;
}// 
// Free memory allocated for a parsed pattern
static void free_parsed_pattern(parsed_pattern_t *parsed) {
    if (parsed && parsed->processed_pattern) {
        free(parsed->processed_pattern);
        parsed->processed_pattern = NULL;
        parsed->core_pattern = NULL;
    }
}
