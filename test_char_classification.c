#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>

// Copy the character classification functions for testing
// (Since they're static in pattern_match.c, we need to copy them for direct testing)

/**
 * Check if a character is a digit (0-9)
 * @param c The character to check
 * @return true if the character is a digit, false otherwise
 */
static bool is_digit_char(char c) {
    return c >= '0' && c <= '9';
}

/**
 * Check if a character is a word character (alphanumeric + underscore)
 * Word characters are: a-z, A-Z, 0-9, and underscore (_)
 * @param c The character to check
 * @return true if the character is a word character, false otherwise
 */
static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || 
           (c >= 'A' && c <= 'Z') || 
           (c >= '0' && c <= '9') || 
           (c == '_');
}

/**
 * Check if a character is a whitespace character
 * Whitespace characters are: space, tab, newline, carriage return, form feed, vertical tab
 * @param c The character to check
 * @return true if the character is a whitespace character, false otherwise
 */
static bool is_whitespace_char(char c) {
    return c == ' ' ||   // space
           c == '\t' ||  // tab
           c == '\n' ||  // newline
           c == '\r' ||  // carriage return
           c == '\f' ||  // form feed
           c == '\v';    // vertical tab
}

/**
 * Check if a position in a string is a word boundary
 * A word boundary occurs at positions where:
 * - A word character is followed by a non-word character
 * - A non-word character is followed by a word character
 * - At string start if the first character is a word character
 * - At string end if the last character is a word character
 * @param str The string to check
 * @param pos The position to check (0-based index)
 * @return true if the position is a word boundary, false otherwise
 */
static bool is_word_boundary(const char *str, size_t pos) {
    if (!str) {
        return false;
    }
    
    size_t str_len = strlen(str);
    
    // Handle position at string start (pos == 0)
    if (pos == 0) {
        // Word boundary at start if first character is a word character
        return (str_len > 0 && is_word_char(str[0]));
    }
    
    // Handle position at string end (pos == str_len)
    if (pos == str_len) {
        // Word boundary at end if last character is a word character
        return (str_len > 0 && is_word_char(str[str_len - 1]));
    }
    
    // Handle position beyond string end
    if (pos > str_len) {
        return false;
    }
    
    // Handle position within string (between characters)
    // Word boundary exists if character types differ on either side
    bool prev_is_word = is_word_char(str[pos - 1]);
    bool curr_is_word = is_word_char(str[pos]);
    
    return prev_is_word != curr_is_word;
}

// Test framework
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void test_char(char c, bool expected, bool (*func)(char), const char* func_name, const char* description) {
    tests_run++;
    bool result = func(c);
    
    if (result == expected) {
        tests_passed++;
        printf("PASS: %s - %s\n", func_name, description);
    } else {
        tests_failed++;
        printf("FAIL: %s - %s\n", func_name, description);
        printf("      Character: '%c' (0x%02x), Expected: %s, Got: %s\n", 
               c, (unsigned char)c, expected ? "true" : "false", result ? "true" : "false");
    }
}

static void test_word_boundary(const char* str, size_t pos, bool expected, const char* description) {
    tests_run++;
    bool result = is_word_boundary(str, pos);
    
    if (result == expected) {
        tests_passed++;
        printf("PASS: is_word_boundary - %s\n", description);
    } else {
        tests_failed++;
        printf("FAIL: is_word_boundary - %s\n", description);
        printf("      String: \"%s\", Position: %zu, Expected: %s, Got: %s\n", 
               str ? str : "(null)", pos, expected ? "true" : "false", result ? "true" : "false");
    }
}

int main() {
    printf("Testing Character Classification Functions\n");
    printf("=========================================\n");
    
    // Test is_digit_char function
    printf("\n=== Testing is_digit_char ===\n");
    
    // Test digit characters (0-9) - should return true
    for (char c = '0'; c <= '9'; c++) {
        char desc[50];
        snprintf(desc, sizeof(desc), "digit character '%c'", c);
        test_char(c, true, is_digit_char, "is_digit_char", desc);
    }
    
    // Test non-digit characters - should return false
    char non_digits[] = {'a', 'z', 'A', 'Z', ' ', '!', '_', '\t', '\n', '\r', '\f', '\v', '/', ':'};
    for (int i = 0; i < sizeof(non_digits); i++) {
        char desc[50];
        if (non_digits[i] == '\t') {
            snprintf(desc, sizeof(desc), "non-digit character '\\t'");
        } else if (non_digits[i] == '\n') {
            snprintf(desc, sizeof(desc), "non-digit character '\\n'");
        } else if (non_digits[i] == '\r') {
            snprintf(desc, sizeof(desc), "non-digit character '\\r'");
        } else if (non_digits[i] == '\f') {
            snprintf(desc, sizeof(desc), "non-digit character '\\f'");
        } else if (non_digits[i] == '\v') {
            snprintf(desc, sizeof(desc), "non-digit character '\\v'");
        } else {
            snprintf(desc, sizeof(desc), "non-digit character '%c'", non_digits[i]);
        }
        test_char(non_digits[i], false, is_digit_char, "is_digit_char", desc);
    }
    
    // Test is_word_char function
    printf("\n=== Testing is_word_char ===\n");
    
    // Test lowercase letters (a-z) - should return true
    for (char c = 'a'; c <= 'z'; c++) {
        char desc[50];
        snprintf(desc, sizeof(desc), "lowercase letter '%c'", c);
        test_char(c, true, is_word_char, "is_word_char", desc);
    }
    
    // Test uppercase letters (A-Z) - should return true
    for (char c = 'A'; c <= 'Z'; c++) {
        char desc[50];
        snprintf(desc, sizeof(desc), "uppercase letter '%c'", c);
        test_char(c, true, is_word_char, "is_word_char", desc);
    }
    
    // Test digits (0-9) - should return true
    for (char c = '0'; c <= '9'; c++) {
        char desc[50];
        snprintf(desc, sizeof(desc), "digit character '%c'", c);
        test_char(c, true, is_word_char, "is_word_char", desc);
    }
    
    // Test underscore - should return true
    test_char('_', true, is_word_char, "is_word_char", "underscore character '_'");
    
    // Test non-word characters - should return false
    char non_word_chars[] = {' ', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '-', '+', '=', '\t', '\n', '\r', '\f', '\v', '/', ':', ';', '<', '>', '?', '[', ']', '{', '}', '|', '\\', '`', '~'};
    for (int i = 0; i < sizeof(non_word_chars); i++) {
        char desc[50];
        if (non_word_chars[i] == '\t') {
            snprintf(desc, sizeof(desc), "non-word character '\\t'");
        } else if (non_word_chars[i] == '\n') {
            snprintf(desc, sizeof(desc), "non-word character '\\n'");
        } else if (non_word_chars[i] == '\r') {
            snprintf(desc, sizeof(desc), "non-word character '\\r'");
        } else if (non_word_chars[i] == '\f') {
            snprintf(desc, sizeof(desc), "non-word character '\\f'");
        } else if (non_word_chars[i] == '\v') {
            snprintf(desc, sizeof(desc), "non-word character '\\v'");
        } else {
            snprintf(desc, sizeof(desc), "non-word character '%c'", non_word_chars[i]);
        }
        test_char(non_word_chars[i], false, is_word_char, "is_word_char", desc);
    }
    
    // Test is_whitespace_char function
    printf("\n=== Testing is_whitespace_char ===\n");
    
    // Test whitespace characters - should return true
    char whitespace_chars[] = {' ', '\t', '\n', '\r', '\f', '\v'};
    const char* whitespace_names[] = {"space", "tab", "newline", "carriage return", "form feed", "vertical tab"};
    for (int i = 0; i < sizeof(whitespace_chars); i++) {
        char desc[50];
        snprintf(desc, sizeof(desc), "whitespace character '%s'", whitespace_names[i]);
        test_char(whitespace_chars[i], true, is_whitespace_char, "is_whitespace_char", desc);
    }
    
    // Test non-whitespace characters - should return false
    char non_whitespace_chars[] = {'a', 'z', 'A', 'Z', '0', '9', '_', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '-', '+', '='};
    for (int i = 0; i < sizeof(non_whitespace_chars); i++) {
        char desc[50];
        snprintf(desc, sizeof(desc), "non-whitespace character '%c'", non_whitespace_chars[i]);
        test_char(non_whitespace_chars[i], false, is_whitespace_char, "is_whitespace_char", desc);
    }
    
    // Test is_word_boundary function
    printf("\n=== Testing is_word_boundary ===\n");
    
    // Test NULL string
    test_word_boundary(NULL, 0, false, "NULL string");
    
    // Test empty string
    test_word_boundary("", 0, false, "empty string at position 0");
    test_word_boundary("", 1, false, "empty string at position 1");
    
    // Test single word character
    test_word_boundary("a", 0, true, "single word char - start boundary");
    test_word_boundary("a", 1, true, "single word char - end boundary");
    test_word_boundary("a", 2, false, "single word char - beyond end");
    
    // Test single non-word character
    test_word_boundary(" ", 0, false, "single non-word char - start boundary");
    test_word_boundary(" ", 1, false, "single non-word char - end boundary");
    
    // Test word at start of string
    test_word_boundary("hello world", 0, true, "word at start - start boundary");
    test_word_boundary("hello world", 5, true, "word at start - end boundary");
    test_word_boundary("hello world", 6, true, "space after word - boundary");
    
    // Test word at end of string
    test_word_boundary("hello world", 6, true, "word at end - start boundary");
    test_word_boundary("hello world", 11, true, "word at end - end boundary");
    
    // Test word in middle
    test_word_boundary("a word here", 2, true, "middle word - start boundary");
    test_word_boundary("a word here", 6, true, "middle word - end boundary");
    
    // Test transitions between word and non-word characters
    test_word_boundary("abc123", 3, false, "word to word - no boundary");
    test_word_boundary("abc_def", 3, false, "word to underscore - no boundary");
    test_word_boundary("abc def", 3, true, "word to space - boundary");
    test_word_boundary("abc!def", 3, true, "word to punctuation - boundary");
    
    // Test transitions between non-word characters
    test_word_boundary("  !@#", 2, false, "space to punctuation - no boundary");
    test_word_boundary("!@# $%", 3, false, "punctuation to space - no boundary");
    
    // Test complex string with multiple boundaries
    test_word_boundary("hello, world!", 0, true, "complex - start");
    test_word_boundary("hello, world!", 5, true, "complex - after first word");
    test_word_boundary("hello, world!", 6, false, "complex - comma");
    test_word_boundary("hello, world!", 7, true, "complex - before second word");
    test_word_boundary("hello, world!", 12, true, "complex - after second word");
    test_word_boundary("hello, world!", 13, false, "complex - end punctuation");
    
    // Test edge cases with numbers and underscores
    test_word_boundary("test_123", 4, false, "underscore in word - no boundary");
    test_word_boundary("test123", 4, false, "letter to number - no boundary");
    test_word_boundary("123abc", 3, false, "number to letter - no boundary");
    
    // Test boundaries with different whitespace types
    test_word_boundary("word\tword", 4, true, "word to tab - boundary");
    test_word_boundary("word\nword", 4, true, "word to newline - boundary");
    test_word_boundary("word\rword", 4, true, "word to carriage return - boundary");
    
    // Print summary
    printf("\n=== Test Summary ===\n");
    printf("Total tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("All tests PASSED! ✓\n");
    } else {
        printf("Some tests FAILED! ✗\n");
    }
    
    printf("Success rate: %.1f%%\n", tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    
    return tests_failed > 0 ? 1 : 0;
}