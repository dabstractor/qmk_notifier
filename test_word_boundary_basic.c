#include <stdio.h>
#include <stdbool.h>
#include <string.h>
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

// Test cases for word boundary matching functionality
static void test_word_boundary_basic() {
    printf("\n=== Testing Basic Word Boundary Matching ===\n");
    
    test_case_t word_boundary_tests[] = {
        // \b (word boundary) at start of word - Requirements 2.3, 2.4
        {"\\bword", "word", true, true, "\\b: matches at start of string with word"},
        {"\\bword", "aword", true, false, "\\b: does not match when not at word boundary"},
        {"\\bword", " word", true, true, "\\b: matches after non-word character"},
        {"\\bword", ".word", true, true, "\\b: matches after punctuation"},
        {"\\bword", "123word", true, false, "\\b: does not match after digit (word char)"},
        {"\\bword", "_word", true, false, "\\b: does not match after underscore (word char)"},
        
        // \b (word boundary) at end of word - Requirements 2.3, 2.4
        {"word\\b", "word", true, true, "\\b: matches at end of string with word"},
        {"word\\b", "worda", true, false, "\\b: does not match when not at word boundary"},
        {"word\\b", "word ", true, true, "\\b: matches before non-word character"},
        {"word\\b", "word.", true, true, "\\b: matches before punctuation"},
        {"word\\b", "word123", true, false, "\\b: does not match before digit (word char)"},
        {"word\\b", "word_", true, false, "\\b: does not match before underscore (word char)"},
        
        // \b (word boundary) in middle of pattern - Requirements 2.3, 2.4
        {"\\btest\\b", "test", true, true, "\\b: matches whole word exactly"},
        {"\\btest\\b", "testing", true, false, "\\b: does not match partial word"},
        {"\\btest\\b", "pretest", true, false, "\\b: does not match partial word"},
        {"\\btest\\b", "pretesting", true, false, "\\b: does not match partial word"},
        {"\\btest\\b", " test ", true, true, "\\b: matches whole word with spaces"},
        {"\\btest\\b", ".test.", true, true, "\\b: matches whole word with punctuation"},
        
        // \B (non-word boundary) tests - Requirements 2.3, 2.4
        {"\\Bord", "word", true, true, "\\B: matches inside word (not at boundary)"},
        {"\\Bord", "ord", true, false, "\\B: does not match at word boundary (start)"},
        {"\\Bord", " ord", true, false, "\\B: does not match at word boundary (after space)"},
        {"wor\\B", "word", true, true, "\\B: matches inside word (not at boundary)"},
        {"wor\\B", "wor", true, false, "\\B: does not match at word boundary (end)"},
        {"wor\\B", "wor ", true, false, "\\B: does not match at word boundary (before space)"},
        
        // Edge cases at string boundaries
        {"\\b", "", true, false, "\\b: does not match empty string"},
        {"\\B", "", true, false, "\\B: does not match empty string"},
        {"\\ba", "a", true, true, "\\b: matches single character at start"},
        {"a\\b", "a", true, true, "\\b: matches single character at end"},
        {"\\Ba", "ba", true, true, "\\B: matches inside two-character word"},
        
        // Mixed word and non-word characters
        {"\\btest\\b", "test123", true, false, "\\b: word followed by digits (no boundary)"},
        {"\\btest\\b", "test_var", true, false, "\\b: word followed by underscore (no boundary)"},
        {"\\btest\\b", "test-var", true, true, "\\b: word followed by hyphen (boundary)"},
        {"\\btest\\b", "test.method", true, true, "\\b: word followed by dot (boundary)"},
        
        // Case sensitivity with word boundaries
        {"\\bTest\\b", "test", false, true, "\\b: case insensitive word boundary"},
        {"\\bTest\\b", "test", true, false, "\\b: case sensitive word boundary"},
        {"\\bTEST\\b", "test", false, true, "\\b: case insensitive uppercase pattern"},
        {"\\bTEST\\b", "test", true, false, "\\b: case sensitive uppercase pattern"},
    };
    
    for (int i = 0; i < sizeof(word_boundary_tests) / sizeof(word_boundary_tests[0]); i++) {
        run_test(word_boundary_tests[i]);
    }
}

// Test cases for word boundaries with anchors
static void test_word_boundary_with_anchors() {
    printf("\n=== Testing Word Boundaries with Anchors ===\n");
    
    test_case_t anchor_tests[] = {
        // Word boundaries with start anchor (^)
        {"^\\bword", "word", true, true, "^\\b: start anchor with word boundary"},
        {"^\\bword", " word", true, false, "^\\b: start anchor fails with leading space"},
        {"^\\Bord", "word", true, false, "^\\B: start anchor with non-word boundary fails"},
        
        // Word boundaries with end anchor ($)
        {"word\\b$", "word", true, true, "\\b$: word boundary with end anchor"},
        {"word\\b$", "word ", true, false, "\\b$: end anchor fails with trailing space"},
        {"wor\\B$", "word", true, false, "\\B$: non-word boundary with end anchor fails"},
        
        // Word boundaries with full anchors (^...$)
        {"^\\btest\\b$", "test", true, true, "^\\b...\\b$: fully anchored whole word"},
        {"^\\btest\\b$", " test", true, false, "^\\b...\\b$: fails with leading space"},
        {"^\\btest\\b$", "test ", true, false, "^\\b...\\b$: fails with trailing space"},
        {"^\\btest\\b$", "testing", true, false, "^\\b...\\b$: fails with longer word"},
        
        // Complex patterns
        {"^\\b\\w+\\b$", "word", true, true, "^\\b\\w+\\b$: anchored word character pattern"},
        {"^\\b\\w+\\b$", "word123", true, true, "^\\b\\w+\\b$: anchored alphanumeric word"},
        {"^\\b\\w+\\b$", "word-test", true, false, "^\\b\\w+\\b$: fails with hyphenated word"},
    };
    
    for (int i = 0; i < sizeof(anchor_tests) / sizeof(anchor_tests[0]); i++) {
        run_test(anchor_tests[i]);
    }
}

// Test cases for word boundaries with wildcards
static void test_word_boundary_with_wildcards() {
    printf("\n=== Testing Word Boundaries with Wildcards ===\n");
    
    test_case_t wildcard_tests[] = {
        // Word boundaries with wildcards
        {"\\b*test", "test", true, true, "\\b*: word boundary with wildcard"},
        {"\\b*test", "pretest", true, true, "\\b*: word boundary with wildcard matches"},
        {"test*\\b", "test", true, true, "*\\b: wildcard with word boundary"},
        {"test*\\b", "testing", true, true, "*\\b: wildcard with word boundary matches"},
        
        // Complex wildcard patterns
        {"\\b*word*\\b", "word", true, true, "\\b*...*\\b: word boundaries with wildcards"},
        {"\\b*word*\\b", "myword", true, true, "\\b*...*\\b: matches with prefix"},
        {"\\b*word*\\b", "wordy", true, true, "\\b*...*\\b: matches with suffix"},
        {"\\b*word*\\b", "mywordy", true, true, "\\b*...*\\b: matches with both"},
        
        // Non-word boundaries with wildcards
        {"\\B*ord", "word", true, true, "\\B*: non-word boundary with wildcard"},
        {"\\B*ord", "ord", true, false, "\\B*: non-word boundary fails at start"},
        {"wor*\\B", "word", true, true, "*\\B: wildcard with non-word boundary"},
        {"wor*\\B", "wor", true, false, "*\\B: wildcard with non-word boundary fails at end"},
    };
    
    for (int i = 0; i < sizeof(wildcard_tests) / sizeof(wildcard_tests[0]); i++) {
        run_test(wildcard_tests[i]);
    }
}

// Test cases for multiple word boundaries in one pattern
static void test_multiple_word_boundaries() {
    printf("\n=== Testing Multiple Word Boundaries ===\n");
    
    test_case_t multiple_tests[] = {
        // Multiple word boundaries
        {"\\b\\b", "a", true, true, "\\b\\b: double word boundary at start of word"},
        {"\\b\\b", " a", true, true, "\\b\\b: double word boundary matches at a boundary"},
        {"\\B\\B", "ab", true, true, "\\B\\B: double non-word boundary inside word"},
        {"\\B\\B", "a", true, false, "\\B\\B: double non-word boundary fails at boundary"},
        
        // Mixed boundaries
        {"\\b\\Bord", "word", true, false, "\\b\\B: boundary then non-boundary is contradictory"},
        {"\\b\\Bord", "ord", true, false, "\\b\\B: fails when first is not boundary"},
        {"wor\\B\\b", "word", true, false, "\\B\\b: non-boundary then boundary is contradictory"},
        {"wor\\B\\b", "wor", true, false, "\\B\\b: fails when second is not boundary"},
        
        // Boundaries with other metacharacters
        {"\\b\\w\\b", "a", true, true, "\\b\\w\\b: word boundaries around single word char"},
        {"\\b\\w\\b", "ab", true, false, "\\b\\w\\b: fails with multiple word chars"},
        {"\\b\\d\\b", "5", true, true, "\\b\\d\\b: word boundaries around single digit"},
        {"\\b\\s\\b", " ", true, false, "\\b\\s\\b: fails - space is not word char"},
    };
    
    for (int i = 0; i < sizeof(multiple_tests) / sizeof(multiple_tests[0]); i++) {
        run_test(multiple_tests[i]);
    }
}

// Main test function
int main() {
    printf("=== Word Boundary Matching Unit Tests ===\n");
    printf("Testing word boundary matching functionality in pattern_match.c\n");
    
    test_word_boundary_basic();
    test_word_boundary_with_anchors();
    test_word_boundary_with_wildcards();
    test_multiple_word_boundaries();
    
    printf("\n=== Test Summary ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n", tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    
    return tests_failed > 0 ? 1 : 0;
}