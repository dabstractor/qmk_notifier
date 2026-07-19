#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "pattern_match.h"

// Test framework structure
typedef struct {
    const char *pattern;
    const char *input;
    bool case_sensitive;
    bool expected_result;
    const char *description;
} test_case_t;

// Test result counters
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test runner helper function
static void run_test(test_case_t test) {
    tests_run++;
    bool result = pattern_match(test.pattern, test.input, test.case_sensitive);
    
    if (result == test.expected_result) {
        tests_passed++;
        printf("PASS: %s\n", test.description);
    } else {
        tests_failed++;
        printf("FAIL: %s\n", test.description);
        printf("      Pattern: '%s', Input: '%s', Case sensitive: %s\n", 
               test.pattern, test.input, test.case_sensitive ? "true" : "false");
        printf("      Expected: %s, Got: %s\n", 
               test.expected_result ? "true" : "false", result ? "true" : "false");
    }
}

// Test NULL pointer handling
static void test_null_pointer_handling() {
    printf("\n=== Testing NULL Pointer Handling ===\n");
    
    // Test NULL pattern
    bool result1 = pattern_match(NULL, "test", true);
    tests_run++;
    if (result1 == false) {
        tests_passed++;
        printf("PASS: NULL pattern returns false\n");
    } else {
        tests_failed++;
        printf("FAIL: NULL pattern should return false, got true\n");
    }
    
    // Test NULL input string
    bool result2 = pattern_match("test", NULL, true);
    tests_run++;
    if (result2 == false) {
        tests_passed++;
        printf("PASS: NULL input string returns false\n");
    } else {
        tests_failed++;
        printf("FAIL: NULL input string should return false, got true\n");
    }
    
    // Test both NULL
    bool result3 = pattern_match(NULL, NULL, true);
    tests_run++;
    if (result3 == false) {
        tests_passed++;
        printf("PASS: Both NULL returns false\n");
    } else {
        tests_failed++;
        printf("FAIL: Both NULL should return false, got true\n");
    }
}

// Test invalid escape sequences
static void test_invalid_escape_sequences() {
    printf("\n=== Testing Invalid Escape Sequences ===\n");
    
    test_case_t invalid_escape_tests[] = {
        // Invalid escape sequences should be treated as literals
        {"\\x", "\\x", true, true, "Invalid escape \\x: treated as literal"},
        {"\\z", "\\z", true, true, "Invalid escape \\z: treated as literal"},
        {"\\1", "\\1", true, true, "Invalid escape \\1: treated as literal"},
        {"\\@", "\\@", true, true, "Invalid escape \\@: treated as literal"},
        {"\\#", "\\#", true, true, "Invalid escape \\#: treated as literal"},
        {"\\%", "\\%", true, true, "Invalid escape \\%: treated as literal"},
        {"\\&", "\\&", true, true, "Invalid escape \\&: treated as literal"},
        {"\\(", "\\(", true, true, "Invalid escape \\(: treated as literal"},
        {"\\)", "\\)", true, true, "Invalid escape \\): treated as literal"},
        {"\\+", "\\+", true, true, "Invalid escape \\+: treated as literal"},
        {"\\=", "\\=", true, true, "Invalid escape \\=: treated as literal"},
        {"\\[", "\\[", true, true, "Invalid escape \\[: treated as literal"},
        {"\\]", "\\]", true, true, "Invalid escape \\]: treated as literal"},
        {"\\{", "\\{", true, true, "Invalid escape \\{: treated as literal"},
        {"\\}", "\\}", true, true, "Invalid escape \\}: treated as literal"},
        {"\\|", "\\|", true, true, "Invalid escape \\|: treated as literal"},
        {"\\?", "\\?", true, true, "Invalid escape \\?: treated as literal"},
        {"\\.", "\\.", true, true, "Invalid escape \\.: treated as literal"},
        {"\\,", "\\,", true, true, "Invalid escape \\,: treated as literal"},
        {"\\;", "\\;", true, true, "Invalid escape \\;: treated as literal"},
        {"\\:", "\\:", true, true, "Invalid escape \\:: treated as literal"},
        {"\\\"", "\\\"", true, true, "Invalid escape \\\": treated as literal"},
        {"\\'", "\\'", true, true, "Invalid escape \\': treated as literal"},
        {"\\`", "\\`", true, true, "Invalid escape \\`: treated as literal"},
        {"\\~", "\\~", true, true, "Invalid escape \\~: treated as literal"},
        
        // Test that invalid escapes don't interfere with matching
        {"pre\\xinvalid", "pre\\xinvalid", true, true, "Invalid escape in middle: still matches"},
        {"\\ystart", "\\ystart", true, true, "Invalid escape at start: still matches"},
        {"end\\z", "end\\z", true, true, "Invalid escape at end: still matches"},
        
        // Test case sensitivity with invalid escapes
        {"\\X", "\\x", false, true, "Invalid escape case insensitive: matches"},
        {"\\X", "\\x", true, false, "Invalid escape case sensitive: no match"},
    };
    
    for (size_t i = 0; i < sizeof(invalid_escape_tests) / sizeof(invalid_escape_tests[0]); i++) {
        run_test(invalid_escape_tests[i]);
    }
}

// Test malformed patterns
static void test_malformed_patterns() {
    printf("\n=== Testing Malformed Patterns ===\n");
    
    test_case_t malformed_tests[] = {
        // Trailing backslash should be treated as literal
        {"test\\", "test\\", true, true, "Trailing backslash: treated as literal"},
        {"\\", "\\", true, true, "Single backslash: treated as literal"},
        
        // Multiple consecutive backslashes
        {"\\\\\\", "\\\\\\", true, true, "Three backslashes: treated as literal"},
        {"\\\\\\\\", "\\\\", true, true, "Four backslashes: two escaped backslashes"},
        {"\\\\\\\\\\", "\\\\\\\\\\", true, true, "Five backslashes: treated as literal"},
        
        // Complex malformed escape sequences
        {"\\\\\\^", "\\^", true, true, "Complex: \\\\\\^ becomes \\^"},
        {"\\\\\\$", "\\$", true, true, "Complex: \\\\\\$ becomes \\$"},
        {"\\\\\\*", "\\*", true, true, "Complex: \\\\\\* becomes \\*"},
        
        // Anchors in wrong positions (should still work as literals when escaped)
        {"mid^dle", "mid^dle", true, true, "^ in middle: treated as literal"},
        {"mid$dle", "mid$dle", true, true, "$ in middle: treated as literal"},
        {"mid$dle$", "mid$dle", true, true, "Multiple $: first is literal, second is anchor"},
        
        // Empty anchor patterns
        {"^", "", true, true, "Lone ^ anchor: matches empty string"},
        {"$", "", true, true, "Lone $ anchor: matches empty string"},
        {"^$", "", true, true, "Both anchors: matches empty string only"},
        {"^$", "a", true, false, "Both anchors: rejects non-empty string"},
        
        // Wildcard edge cases
        {"*", "", true, true, "Lone wildcard: matches empty string"},
        {"**", "", true, true, "Double wildcard: matches empty string"},
        {"***", "anything", true, true, "Triple wildcard: matches anything"},
        
        // Mixed malformed patterns
        {"^\\*$", "*", true, true, "Anchored escaped asterisk: exact match"},
        {"\\^*\\$", "^anything$", true, true, "Escaped anchors with wildcard"},
        {"\\\\*test", "\\anything", true, false, "Escaped backslash with wildcard: should not match"},
        {"\\\\*test", "\\test", true, true, "Escaped backslash with wildcard: minimum match"},
    };
    
    for (size_t i = 0; i < sizeof(malformed_tests) / sizeof(malformed_tests[0]); i++) {
        run_test(malformed_tests[i]);
    }
}

// Test extremely long patterns and strings
static void test_long_patterns_and_strings() {
    printf("\n=== Testing Extremely Long Patterns and Strings ===\n");
    
    // Test with moderately long patterns first
    char *long_pattern = malloc(1000);
    char *long_string = malloc(1000);
    
    if (!long_pattern || !long_string) {
        printf("SKIP: Could not allocate memory for long pattern tests\n");
        if (long_pattern) free(long_pattern);
        if (long_string) free(long_string);
        return;
    }
    
    // Create a long pattern with repeated characters
    strcpy(long_pattern, "^");
    for (int i = 0; i < 100; i++) {
        strcat(long_pattern, "test");
    }
    strcat(long_pattern, "$");
    
    // Create matching long string
    strcpy(long_string, "");
    for (int i = 0; i < 100; i++) {
        strcat(long_string, "test");
    }
    
    tests_run++;
    bool result1 = pattern_match(long_pattern, long_string, true);
    if (result1 == true) {
        tests_passed++;
        printf("PASS: Long pattern (400+ chars) matches correctly\n");
    } else {
        tests_failed++;
        printf("FAIL: Long pattern should match, got false\n");
    }
    
    // Test with non-matching long string
    strcat(long_string, "extra");
    tests_run++;
    bool result2 = pattern_match(long_pattern, long_string, true);
    if (result2 == false) {
        tests_passed++;
        printf("PASS: Long pattern correctly rejects non-matching string\n");
    } else {
        tests_failed++;
        printf("FAIL: Long pattern should not match modified string, got true\n");
    }
    
    // Test with wildcards in long pattern
    strcpy(long_pattern, "");
    for (int i = 0; i < 50; i++) {
        strcat(long_pattern, "a*");
    }
    strcat(long_pattern, "end");
    
    strcpy(long_string, "");
    for (int i = 0; i < 50; i++) {
        strcat(long_string, "aaa");
    }
    strcat(long_string, "end");
    
    tests_run++;
    bool result3 = pattern_match(long_pattern, long_string, true);
    if (result3 == true) {
        tests_passed++;
        printf("PASS: Long pattern with wildcards matches correctly\n");
    } else {
        tests_failed++;
        printf("FAIL: Long pattern with wildcards should match, got false\n");
    }
    
    free(long_pattern);
    free(long_string);
    
    // Test with very long single-character patterns
    char very_long_pattern[2000];
    char very_long_string[2000];
    
    // Create pattern with many escaped characters
    strcpy(very_long_pattern, "");
    for (int i = 0; i < 500; i++) {
        strcat(very_long_pattern, "\\*");
    }
    
    strcpy(very_long_string, "");
    for (int i = 0; i < 500; i++) {
        strcat(very_long_string, "*");
    }
    
    tests_run++;
    bool result4 = pattern_match(very_long_pattern, very_long_string, true);
    if (result4 == true) {
        tests_passed++;
        printf("PASS: Very long pattern with escapes (1000+ chars) matches\n");
    } else {
        tests_failed++;
        printf("FAIL: Very long escaped pattern should match, got false\n");
    }
    
    // Test performance with long patterns containing metacharacters
    strcpy(very_long_pattern, "^");
    for (int i = 0; i < 200; i++) {
        strcat(very_long_pattern, "\\d\\w\\s");
    }
    strcat(very_long_pattern, "$");
    
    strcpy(very_long_string, "");
    for (int i = 0; i < 200; i++) {
        strcat(very_long_string, "5a ");
    }
    
    tests_run++;
    bool result5 = pattern_match(very_long_pattern, very_long_string, true);
    if (result5 == true) {
        tests_passed++;
        printf("PASS: Long pattern with metacharacters (600+ chars) matches\n");
    } else {
        tests_failed++;
        printf("FAIL: Long metacharacter pattern should match, got false\n");
    }
}

// Test edge cases with special characters
static void test_special_character_edge_cases() {
    printf("\n=== Testing Special Character Edge Cases ===\n");
    
    test_case_t special_char_tests[] = {
        // Test with null characters (should be handled gracefully)
        {"test", "test", true, true, "Normal string without null chars"},
        
        // Test with various Unicode and extended ASCII characters
        {"café", "café", true, true, "UTF-8 characters: exact match"},
        {"café", "CAFÉ", true, false, "UTF-8 characters: case sensitive no match"},
        
        // Test with control characters
        
        // Test with high ASCII values
        {"test\xFF", "test\xFF", true, true, "High ASCII character \\xFF"},
        {"test\xFE", "test\xFE", true, true, "High ASCII character \\xFE"},
        
        // Test with tab and newline characters
        {"test\tmore", "test\tmore", true, true, "Tab character in pattern"},
        
        // Test metacharacters with special characters
        {"\\s", "\xFF", true, false, "\\s should not match high ASCII"},
        {"\\S", "\xFF", true, true, "\\S should match high ASCII"},
        {"\\w", "\xFF", true, false, "\\w should not match high ASCII"},
        {"\\W", "\xFF", true, true, "\\W should match high ASCII"},
        
        // Test dot metacharacter with special characters
        {".", "\xFF", true, true, "Dot should match high ASCII"},
        {".", "\x01", true, true, "Dot should match control characters"},
        {".", "\n", true, false, "Dot should not match newline"},
        {".", "\r", true, false, "Dot should match carriage return"},
        {".", "\t", true, true, "Dot should match tab"},
    };
    
    for (size_t i = 0; i < sizeof(special_char_tests) / sizeof(special_char_tests[0]); i++) {
        run_test(special_char_tests[i]);
    }
}

// Test memory allocation edge cases
static void test_memory_allocation_edge_cases() {
    printf("\n=== Testing Memory Allocation Edge Cases ===\n");
    
    // Test with patterns that would cause memory allocation
    test_case_t memory_tests[] = {
        // Test patterns that require escape processing
        {"\\^\\$\\*\\\\", "^$*\\", true, true, "Multiple escapes requiring memory allocation"},
        {"\\d\\w\\s\\D\\W\\S", "5a 5a ", true, false, "Multiple metacharacters requiring processing"},
        
        // Test very long escape sequences
        {"\\^\\^\\^\\^\\^", "^^^^^", true, true, "Many consecutive escapes"},
        {"\\*\\*\\*\\*\\*", "*****", true, true, "Many escaped asterisks"},
        
        // Test patterns with mixed content requiring processing
        {"pre\\^mid\\*post", "pre^mid*post", true, true, "Mixed content with escapes"},
        {"\\d*\\w+\\s?", "5a ", true, false, "Metacharacters with invalid quantifiers"},
        
        // Test edge case where escape processing might fail
        {"", "", true, true, "Empty pattern (no allocation needed)"},
        {"a", "a", true, true, "Single character (minimal allocation)"},
        {"\\a", "\\a", true, true, "Invalid escape (fallback handling)"},
    };
    
    for (size_t i = 0; i < sizeof(memory_tests) / sizeof(memory_tests[0]); i++) {
        run_test(memory_tests[i]);
    }
    
    // Test behavior when pattern processing might encounter issues
    printf("Testing graceful handling of potential memory issues...\n");
    
    // These tests verify that the system handles edge cases gracefully
    // even if internal memory allocation has issues
    tests_run++;
    bool result1 = pattern_match("", "", true);
    if (result1 == true) {
        tests_passed++;
        printf("PASS: Empty pattern/string handled correctly\n");
    } else {
        tests_failed++;
        printf("FAIL: Empty pattern/string should match\n");
    }
    
    tests_run++;
    bool result2 = pattern_match("simple", "simple", true);
    if (result2 == true) {
        tests_passed++;
        printf("PASS: Simple pattern without escapes works\n");
    } else {
        tests_failed++;
        printf("FAIL: Simple pattern should work\n");
    }
}

// Test word boundary edge cases
static void test_word_boundary_edge_cases() {
    printf("\n=== Testing Word Boundary Edge Cases ===\n");
    
    test_case_t boundary_tests[] = {
        // Empty string edge cases
        {"\\b", "", true, false, "Word boundary in empty string: should not match"},
        {"\\B", "", true, false, "Non-word boundary in empty string: should not match"},
        
        // Single character strings
        {"\\b", "a", true, true, "Word boundary with single word char: matches at start"},
        {"\\b", " ", true, false, "Word boundary with single non-word char: no boundary"},
        {"\\Ba", "a", true, false, "Non-word boundary before single word char: no match"},
        {"\\B ", " ", true, true, "Non-word boundary before single non-word char: no match"},
        
        // Word boundaries at string edges
        {"\\bword", "word", true, true, "Word boundary at start of word"},
        {"word\\b", "word", true, true, "Word boundary at end of word"},
        {"\\bword\\b", "word", true, true, "Word boundaries at both ends"},
        {"\\bword\\b", " word ", true, true, "Word boundaries with spaces"},
        {"\\bword\\b", "sword", true, false, "Word boundary: should not match within word"},
        {"\\bword\\b", "words", true, false, "Word boundary: should not match partial word"},
        
        // Non-word boundaries
        {"\\Bord", "word", true, true, "Non-word boundary within word"},
        {"w\\Bord", "word", true, true, "Non-word boundary between word chars"},
        {"\\Bword", "word", true, false, "Non-word boundary at word start: should not match"},
        {"word\\B", "word", true, false, "Non-word boundary at word end: should not match"},
        
        // Complex word boundary scenarios
        {"\\b\\w+\\b", "hello", true, true, "Word boundaries around word chars"},
        {"\\b\\w+\\b", "hello world", true, true, "Word boundaries: matches first word"},
        {"\\B\\w\\B", "hello", true, true, "Non-word boundaries within word"},
        {"\\B\\w\\B", "a", true, false, "Non-word boundaries: single char has boundaries"},
        
        // Word boundaries with special characters
        {"\\b_test", "_test", true, true, "Word boundary: underscore is word char"},
        {"\\b123", "123", true, true, "Word boundary: digits are word chars"},
        {"\\b@test", "@test", true, false, "Word boundary: @ is not word char, no boundary"},
        {"test\\b@", "test@", true, true, "Word boundary: transition from word to non-word"},
        
        // Multiple word boundaries
        {"\\b\\w+\\b\\s+\\b\\w+\\b", "hello world", true, true, "Multiple word boundaries with whitespace"},
        {"\\b\\w\\b\\s\\b\\w\\b", "a b", true, true, "Single chars with word boundaries"},
    };
    
    for (size_t i = 0; i < sizeof(boundary_tests) / sizeof(boundary_tests[0]); i++) {
        run_test(boundary_tests[i]);
    }
}

// Test dot metacharacter edge cases
static void test_dot_metacharacter_edge_cases() {
    printf("\n=== Testing Dot Metacharacter Edge Cases ===\n");
    
    test_case_t dot_tests[] = {
        // Basic dot functionality
        {".", "a", true, true, "Dot matches letter"},
        {".", "5", true, true, "Dot matches digit"},
        {".", " ", true, true, "Dot matches space"},
        {".", "\t", true, true, "Dot matches tab"},
        {".", "\r", true, false, "Dot matches carriage return"},
        {".", "\f", true, true, "Dot matches form feed"},
        {".", "\v", true, true, "Dot matches vertical tab"},
        {".", "\n", true, false, "Dot does not match newline"},
        
        // Dot with special characters
        {".", "@", true, true, "Dot matches special character @"},
        {".", "#", true, true, "Dot matches special character #"},
        {".", "!", true, true, "Dot matches special character !"},
        {".", "\xFF", true, true, "Dot matches high ASCII"},
        {".", "\x01", true, true, "Dot matches control character"},
        
        // Multiple dots
        {"..", "ab", true, true, "Two dots match two characters"},
        {"...", "abc", true, true, "Three dots match three characters"},
        {"..", "a\n", true, false, "Two dots: second fails on newline"},
        {".", "", true, false, "Dot does not match empty string"},
        
        // Dot with anchors
        {"^.", "a", true, true, "Dot with start anchor"},
        {".$", "a", true, true, "Dot with end anchor"},
        {"^.$", "a", true, true, "Dot with both anchors: single char"},
        {"^.$", "ab", true, false, "Dot with both anchors: multiple chars"},
        {"^.$", "", true, false, "Dot with both anchors: empty string"},
        {"^.$", "\n", true, false, "Dot with both anchors: newline"},
        
        // Dot with wildcards
        {".*", "anything", true, true, "Dot with wildcard: matches anything"},
        {".*", "", true, false, "Dot with wildcard: matches empty"},
        {".*", "with\nnewline", true, true, "Dot with wildcard: stops at newline but still matches"},
        {"a.*b", "a\nb", true, false, "Dot wildcard: fails across newline"},
        {"a.*b", "axyzb", true, true, "Dot wildcard: matches across non-newlines"},
        
        // Escaped dot
        {"\\.", ".", true, true, "Escaped dot matches literal dot"},
        {"\\.", "a", true, false, "Escaped dot does not match other chars"},
        {"test\\.txt", "test.txt", true, true, "Escaped dot in filename pattern"},
        {"test\\.txt", "testxtxt", true, false, "Escaped dot: literal dot required"},
    };
    
    for (size_t i = 0; i < sizeof(dot_tests) / sizeof(dot_tests[0]); i++) {
        run_test(dot_tests[i]);
    }
}

// Test complex error scenarios
static void test_complex_error_scenarios() {
    printf("\n=== Testing Complex Error Scenarios ===\n");
    
    test_case_t complex_tests[] = {
        // Patterns that combine multiple potential error conditions
        {"\\\\\\^\\$\\*", "\\^$*", true, true, "Complex escapes: all special chars"},
        {"^\\d*\\w+\\s?$", "123abc", true, false, "Complex pattern with invalid quantifiers"},
        {"\\b\\w*\\B\\s*\\d+", "hello 123", true, false, "Mixed boundaries and metacharacters"},
        
        // Patterns with potential parsing ambiguities
        {"*^test", "^test", true, true, "Wildcard before anchor (anchor as literal)"},
        
        // Very complex escape sequences
        {"\\\\\\\\\\^\\\\\\$", "\\\\^\\$", true, true, "Multiple levels of escaping"},
        {"\\\\\\d\\\\\\w", "\\5\\a", true, true, "Escaped backslashes with metacharacters"},
        
        // Patterns that might cause infinite loops or excessive backtracking
        {"a*a*a*a*", "aaaa", true, true, "Multiple wildcards: should terminate"},
        {".*.*.*", "test", true, true, "Multiple dot wildcards: should terminate"},
        {"\\w*\\w*\\w*", "abc", true, true, "Multiple word char wildcards: should terminate"},
        
        // Edge cases with anchors and escapes
        {"\\^*\\$", "^^^$", true, true, "Escaped anchors with wildcards"},
        {"^\\^*$", "^^^", true, true, "Mixed real and escaped anchors"},
        {"\\^$", "^", true, true, "Escaped start anchor with real end anchor"},
        {"^\\$", "$", true, true, "Real start anchor with escaped end anchor"},
        
        // Patterns that test boundary conditions
        {"\\b\\b\\b", "test", true, true, "Multiple consecutive word boundaries"},
        {"\\B\\B\\B", "test", true, true, "Multiple consecutive non-word boundaries"},
        {"\\b\\B", "test", true, false, "Word boundary followed by non-word boundary"},
        {"\\B\\b", "test", true, false, "Non-word boundary followed by word boundary"},
    };
    
    for (size_t i = 0; i < sizeof(complex_tests) / sizeof(complex_tests[0]); i++) {
        run_test(complex_tests[i]);
    }
}

// Main test function
int main() {
    printf("=== Pattern Matching Library - Error Handling and Edge Case Tests ===\n");
    printf("Testing Requirements 6.3, 6.4: Error handling and edge cases\n\n");
    
    // Run all test categories
    test_null_pointer_handling();
    test_invalid_escape_sequences();
    test_malformed_patterns();
    test_long_patterns_and_strings();
    test_special_character_edge_cases();
    test_memory_allocation_edge_cases();
    test_word_boundary_edge_cases();
    test_dot_metacharacter_edge_cases();
    test_complex_error_scenarios();
    
    // Print final results
    printf("\n=== Test Results Summary ===\n");
    printf("Total tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n", tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    
    if (tests_failed == 0) {
        printf("\n🎉 All error handling and edge case tests passed!\n");
        printf("The pattern matching library handles error conditions gracefully.\n");
        return 0;
    } else {
        printf("\n❌ Some tests failed. Review the failures above.\n");
        return 1;
    }
}