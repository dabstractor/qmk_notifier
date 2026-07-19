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

// Test cases for word boundaries with anchor characters (^, $)
static void test_word_boundaries_with_anchors() {
    printf("\n=== Testing Word Boundaries with Anchors ===\n");
    
    test_case_t anchor_tests[] = {
        // Requirements 2.3, 2.4, 6.1 - Word boundaries with start anchor (^)
        {"^\\bword", "word", true, true, "^\\b: start anchor + word boundary matches word at beginning"},
        {"^\\bword", "word123", true, true, "^\\b: start anchor + word boundary matches word at beginning with trailing"},
        {"^\\bword", " word", true, false, "^\\b: start anchor + word boundary fails with leading space"},
        {"^\\bword", "aword", true, false, "^\\b: start anchor + word boundary fails when not at word boundary"},
        {"^\\btest", "test", true, true, "^\\b: start anchor + word boundary exact match"},
        {"^\\btest", "testing", true, true, "^\\b: start anchor + word boundary with suffix"},
        
        // Word boundaries with end anchor ($)
        {"word\\b$", "word", true, true, "\\b$: word boundary + end anchor matches word at end"},
        {"word\\b$", "123word", true, true, "\\b$: word boundary + end anchor matches word at end with leading"},
        {"word\\b$", "word ", true, false, "\\b$: word boundary + end anchor fails with trailing space"},
        {"word\\b$", "worda", true, false, "\\b$: word boundary + end anchor fails when not at word boundary"},
        {"test\\b$", "test", true, true, "\\b$: word boundary + end anchor exact match"},
        {"test\\b$", "pretest", true, true, "\\b$: word boundary + end anchor with prefix"},
        
        // Word boundaries with full anchors (^...$)
        {"^\\btest\\b$", "test", true, true, "^\\b...\\b$: fully anchored whole word exact match"},
        {"^\\btest\\b$", " test", true, false, "^\\b...\\b$: fully anchored fails with leading space"},
        {"^\\btest\\b$", "test ", true, false, "^\\b...\\b$: fully anchored fails with trailing space"},
        {"^\\btest\\b$", "testing", true, false, "^\\b...\\b$: fully anchored fails with suffix"},
        {"^\\btest\\b$", "pretest", true, false, "^\\b...\\b$: fully anchored fails with prefix"},
        {"^\\btest\\b$", "pretesting", true, false, "^\\b...\\b$: fully anchored fails with both prefix and suffix"},
        
        // Non-word boundaries with anchors
        {"^\\Bord", "word", true, false, "^\\B: start anchor + non-word boundary fails at string start"},
        {"^\\Bord", "sword", true, false, "^\\B: start anchor + non-word boundary fails (\\B not at start)"},
        {"wor\\B$", "word", true, false, "\\B$: non-word boundary + end anchor fails at string end"},
        {"wor\\B$", "words", true, false, "\\B$: non-word boundary + end anchor fails (\\B not at end)"},
        
        // Simple patterns with anchors (avoiding unsupported + quantifier)
        {"^\\b\\w\\b$", "a", true, true, "^\\b\\w\\b$: anchored single word char"},
        {"^\\b\\w\\w\\b$", "ab", true, true, "^\\b\\w\\w\\b$: anchored two word chars"},
        {"^\\b\\w\\w\\w\\b$", "abc", true, true, "^\\b\\w\\w\\w\\b$: anchored three word chars"},
        {"^\\b\\w\\w\\w\\w\\b$", "word", true, true, "^\\b\\w\\w\\w\\w\\b$: anchored four word chars"},
        {"^\\b\\w\\w\\w\\w\\b$", " word", true, false, "^\\b\\w\\w\\w\\w\\b$: anchored word pattern fails with leading space"},
        {"^\\b\\w\\w\\w\\w\\b$", "word ", true, false, "^\\b\\w\\w\\w\\w\\b$: anchored word pattern fails with trailing space"},
        
        // Edge cases with anchors
        {"^\\b", "word", true, true, "^\\b: start anchor + word boundary at beginning of word"},
        {"^\\b", " word", true, false, "^\\b: start anchor + word boundary fails with leading space"},
        {"^\\b", "", true, false, "^\\b: start anchor + word boundary fails with empty string"},
        {"\\b$", "word", true, true, "\\b$: word boundary + end anchor at end of word"},
        {"\\b$", "word ", true, false, "\\b$: word boundary + end anchor fails with trailing space"},
        {"\\b$", "", true, false, "\\b$: word boundary + end anchor fails with empty string"},
    };
    
    for (size_t i = 0; i < sizeof(anchor_tests) / sizeof(anchor_tests[0]); i++) {
        run_test(anchor_tests[i]);
    }
}

// Test cases for word boundaries with wildcards (*)
static void test_word_boundaries_with_wildcards() {
    printf("\n=== Testing Word Boundaries with Wildcards ===\n");
    
    test_case_t wildcard_tests[] = {
        // Requirements 2.3, 2.4, 6.2 - Word boundaries with wildcards
        {"\\b*test", "test", true, true, "\\b*: word boundary + wildcard matches at word start"},
        {"\\b*test", "pretest", true, true, "\\b*: word boundary + wildcard matches with prefix"},
        {"\\b*test", " test", true, true, "\\b*: word boundary + wildcard matches after space"},
        {"\\b*test", "atest", true, true, "\\b*: word boundary + wildcard matches (wildcard allows any prefix)"},
        
        {"test*\\b", "test", true, true, "*\\b: wildcard + word boundary matches"},
        {"test*\\b", "testing", true, true, "*\\b: wildcard + word boundary matches"},
        {"test*\\b", "test ", true, true, "*\\b: wildcard + word boundary matches before space"},
        {"test*\\b", "testa", true, true, "*\\b: wildcard + word boundary matches (boundary at end)"},
        
        // Word boundaries on both sides with wildcards (simplified expectations)
        {"\\b*word*\\b", "word", true, true, "\\b*...*\\b: word boundaries with wildcards"},
        {"\\b*word*\\b", "myword", true, true, "\\b*...*\\b: word boundaries with wildcards"},
        {"\\b*word*\\b", "wordy", true, true, "\\b*...*\\b: word boundaries with wildcards"},
        {"\\b*word*\\b", "mywordy", true, true, "\\b*...*\\b: word boundaries with wildcards"},
        {"\\b*word*\\b", " word ", true, true, "\\b*...*\\b: word boundaries with wildcards matches with spaces"},
        {"\\b*word*\\b", "password", true, true, "\\b*...*\\b: word boundaries with wildcards matches (substring via wildcard)"},
        
        // Non-word boundaries with wildcards
        {"\\B*ord", "word", true, true, "\\B*: non-word boundary + wildcard matches inside word"},
        {"\\B*ord", "ord", true, false, "\\B*: non-word boundary + wildcard fails at word start"},
        {"\\B*ord", " ord", true, true, "\\B*: non-word boundary + wildcard matches (wildcard allows prefix)"},
        
        {"wor*\\B", "word", true, true, "*\\B: wildcard + non-word boundary matches inside word"},
        {"wor*\\B", "wor", true, false, "*\\B: wildcard + non-word boundary fails at word end"},
        {"wor*\\B", "wor ", true, true, "*\\B: wildcard + non-word boundary matches (non-boundary via wildcard)"},
        
        // Complex wildcard patterns with boundaries
        {"\\b*\\w*\\b", "word", true, true, "\\b*\\w*\\b: boundaries with word chars and wildcards (matches via wildcard)"},
        {"\\b*\\w*\\b", "word123", true, true, "\\b*\\w*\\b: boundaries with alphanumeric (matches via wildcard)"},
        {"\\b*\\w*\\b", " word ", true, true, "\\b*\\w*\\b: boundaries with spaces around"},
        {"\\b*\\w*\\b", "word-test", true, true, "\\b*\\w*\\b: boundaries with hyphen (wildcard matches)"},
        
        // Multiple wildcards with boundaries (simplified expectations)
        {"\\b*test*case*\\b", "testcase", true, true, "\\b*...*...*\\b: multiple wildcards with boundaries (matches via wildcard)"},
        {"\\b*test*case*\\b", "mytestcase", true, true, "\\b*...*...*\\b: multiple wildcards with prefix (matches via wildcard)"},
        {"\\b*test*case*\\b", "testcasemy", true, true, "\\b*...*...*\\b: multiple wildcards with suffix (matches via wildcard)"},
        {"\\b*test*case*\\b", "mytestcasemy", true, true, "\\b*...*...*\\b: multiple wildcards with both (matches via wildcard)"},
        
        // Edge cases with wildcards and boundaries
        {"\\b*", "word", true, true, "\\b*: word boundary + wildcard matches any word start"},
        {"\\b*", " word", true, true, "\\b*: word boundary + wildcard matches after space"},
        {"\\b*", "", true, false, "\\b*: word boundary + wildcard fails with empty string"},
        {"*\\b", "word", true, true, "*\\b: wildcard + word boundary matches any word end"},
        {"*\\b", "word ", true, true, "*\\b: wildcard + word boundary matches before space"},
        {"*\\b", "", true, false, "*\\b: wildcard + word boundary fails with empty string"},
    };
    
    for (size_t i = 0; i < sizeof(wildcard_tests) / sizeof(wildcard_tests[0]); i++) {
        run_test(wildcard_tests[i]);
    }
}

// Test cases for word boundaries with other metacharacters
static void test_word_boundaries_with_metacharacters() {
    printf("\n=== Testing Word Boundaries with Other Metacharacters ===\n");
    
    test_case_t metachar_tests[] = {
        // Requirements 2.3, 2.4, 6.1 - Word boundaries with digit metacharacters
        {"\\b\\d", "5", true, true, "\\b\\d: word boundary + digit matches digit at start"},
        {"\\b\\d", "a5", true, false, "\\b\\d: word boundary + digit fails when digit not at boundary"},
        {"\\b\\d", " 5", true, true, "\\b\\d: word boundary + digit matches digit after space"},
        {"\\b\\d", ".5", true, true, "\\b\\d: word boundary + digit matches digit after punctuation"},
        
        {"\\d\\b", "5", true, true, "\\d\\b: digit + word boundary matches digit at end"},
        {"\\d\\b", "5a", true, false, "\\d\\b: digit + word boundary fails when digit not at boundary"},
        {"\\d\\b", "5 ", true, true, "\\d\\b: digit + word boundary matches digit before space"},
        {"\\d\\b", "5.", true, true, "\\d\\b: digit + word boundary matches digit before punctuation"},
        
        {"\\b\\d\\b", "5", true, true, "\\b\\d\\b: word boundaries around single digit"},
        {"\\b\\d\\b", "a5", true, false, "\\b\\d\\b: word boundaries around digit fails inside word"},
        {"\\b\\d\\b", "5a", true, false, "\\b\\d\\b: word boundaries around digit fails inside word"},
        {"\\b\\d\\b", " 5 ", true, true, "\\b\\d\\b: word boundaries around digit with spaces"},
        
        // Word boundaries with non-digit metacharacters
        {"\\b\\D", "a", true, true, "\\b\\D: word boundary + non-digit matches letter at start"},
        {"\\b\\D", "5a", true, false, "\\b\\D: word boundary + non-digit fails when letter not at boundary"},
        {"\\b\\D", " a", true, true, "\\b\\D: word boundary + non-digit matches letter after space"},
        
        {"\\D\\b", "a", true, true, "\\D\\b: non-digit + word boundary matches letter at end"},
        {"\\D\\b", "a5", true, false, "\\D\\b: non-digit + word boundary fails when letter not at boundary"},
        {"\\D\\b", "a ", true, true, "\\D\\b: non-digit + word boundary matches letter before space"},
        
        // Word boundaries with word character metacharacters
        {"\\b\\w", "a", true, true, "\\b\\w: word boundary + word char matches at start"},
        {"\\b\\w", " a", true, true, "\\b\\w: word boundary + word char matches after space"},
        {"\\b\\w", "ba", true, true, "\\b\\w: word boundary + word char matches (b is at word boundary)"},
        
        {"\\w\\b", "a", true, true, "\\w\\b: word char + word boundary matches at end"},
        {"\\w\\b", "a ", true, true, "\\w\\b: word char + word boundary matches before space"},
        {"\\w\\b", "ab", true, true, "\\w\\b: word char + word boundary matches (b is at word boundary)"},
        
        {"\\b\\w\\b", "a", true, true, "\\b\\w\\b: word boundaries around single word char"},
        {"\\b\\w\\b", "_", true, true, "\\b\\w\\b: word boundaries around underscore"},
        {"\\b\\w\\b", "5", true, true, "\\b\\w\\b: word boundaries around digit"},
        {"\\b\\w\\b", "ab", true, false, "\\b\\w\\b: word boundaries around word char fails with multiple"},
        
        // Word boundaries with non-word character metacharacters
        {"\\b\\W", " ", true, false, "\\b\\W: word boundary + non-word char fails (space not at word boundary)"},
        {"\\b\\W", "a ", true, true, "\\b\\W: word boundary + non-word char matches (space at word boundary)"},
        {"\\W\\b", " ", true, false, "\\W\\b: non-word char + word boundary fails (space not at word boundary)"},
        {"\\W\\b", " a", true, true, "\\W\\b: non-word char + word boundary matches (space at word boundary)"},
        
        // Word boundaries with whitespace metacharacters
        {"\\b\\s", " ", true, false, "\\b\\s: word boundary + whitespace fails (space not word char)"},
        {"\\s\\b", " ", true, false, "\\s\\b: whitespace + word boundary fails (space not word char)"},
        
        // Word boundaries with non-whitespace metacharacters
        {"\\b\\S", "a", true, true, "\\b\\S: word boundary + non-whitespace matches letter"},
        {"\\b\\S", "5", true, true, "\\b\\S: word boundary + non-whitespace matches digit"},
        {"\\b\\S", " a", true, true, "\\b\\S: word boundary + non-whitespace matches after space"},
        
        {"\\S\\b", "a", true, true, "\\S\\b: non-whitespace + word boundary matches letter"},
        {"\\S\\b", "5", true, true, "\\S\\b: non-whitespace + word boundary matches digit"},
        {"\\S\\b", "a ", true, true, "\\S\\b: non-whitespace + word boundary matches before space"},
        
        // Complex patterns with multiple metacharacters and boundaries
        {"\\b\\w\\d", "a5", true, true, "\\b\\w\\d: word boundary + word char + digit"},
        {"\\b\\w\\d", " a5", true, true, "\\b\\w\\d: word boundary + word char + digit after space"},
        {"\\b\\w\\d", "ba5", true, false, "\\b\\w\\d: word boundary + word char + digit fails inside word"},
        
        {"\\d\\w\\b", "5a", true, true, "\\d\\w\\b: digit + word char + word boundary"},
        {"\\d\\w\\b", "5a ", true, true, "\\d\\w\\b: digit + word char + word boundary before space"},
        {"\\d\\w\\b", "5ab", true, false, "\\d\\w\\b: digit + word char + word boundary fails inside word"},
        
        {"\\b\\d\\d\\d\\b", "123", true, true, "\\b\\d\\d\\d\\b: word boundaries around three digits"},
        {"\\b\\w\\w\\w\\w\\b", "word", true, true, "\\b\\w\\w\\w\\w\\b: word boundaries around four word chars"},
        
        // Non-word boundaries with metacharacters
        {"\\B\\w", "ab", true, true, "\\B\\w: non-word boundary + word char inside word"},
        {"\\B\\w", "a", true, false, "\\B\\w: non-word boundary + word char fails at start"},
        {"\\B\\w", " a", true, false, "\\B\\w: non-word boundary + word char fails after space"},
        
        {"\\w\\B", "ab", true, true, "\\w\\B: word char + non-word boundary inside word"},
        {"\\w\\B", "a", true, false, "\\w\\B: word char + non-word boundary fails at end"},
        {"\\w\\B", "a ", true, false, "\\w\\B: word char + non-word boundary fails before space"},
        
        // Edge cases with metacharacters and boundaries
        {"\\b\\w*\\b", "word", true, true, "\\b\\w*\\b: word boundaries with word chars and wildcard (matches via wildcard)"},
        {"\\b\\d*\\b", "123", true, true, "\\b\\d*\\b: word boundaries with digits and wildcard (matches via wildcard)"},
        {"\\b\\S*\\b", "word", true, true, "\\b\\S*\\b: word boundaries with non-whitespace and wildcard (matches via wildcard)"},
    };
    
    for (size_t i = 0; i < sizeof(metachar_tests) / sizeof(metachar_tests[0]); i++) {
        run_test(metachar_tests[i]);
    }
}

// Test cases for edge cases at string boundaries
static void test_word_boundary_edge_cases() {
    printf("\n=== Testing Word Boundary Edge Cases at String Boundaries ===\n");
    
    test_case_t edge_tests[] = {
        // Requirements 2.3, 2.4 - Edge cases at string start and end
        {"\\b", "", true, false, "\\b: word boundary fails with empty string"},
        {"\\B", "", true, false, "\\B: non-word boundary fails with empty string"},
        
        // Single character strings
        {"\\ba", "a", true, true, "\\b: word boundary at start of single letter"},
        {"a\\b", "a", true, true, "\\b: word boundary at end of single letter"},
        {"\\ba\\b", "a", true, true, "\\b: word boundaries around single letter"},
        {"\\b5", "5", true, true, "\\b: word boundary at start of single digit"},
        {"5\\b", "5", true, true, "\\b: word boundary at end of single digit"},
        {"\\b5\\b", "5", true, true, "\\b: word boundaries around single digit"},
        {"\\b_", "_", true, true, "\\b: word boundary at start of underscore"},
        {"_\\b", "_", true, true, "\\b: word boundary at end of underscore"},
        {"\\b_\\b", "_", true, true, "\\b: word boundaries around underscore"},
        
        // Non-word boundaries with single characters
        {"\\Ba", "a", true, false, "\\B: non-word boundary fails at start of single letter"},
        {"a\\B", "a", true, false, "\\B: non-word boundary fails at end of single letter"},
        {"\\B5", "5", true, false, "\\B: non-word boundary fails at start of single digit"},
        {"5\\B", "5", true, false, "\\B: non-word boundary fails at end of single digit"},
        
        // Two character strings - word boundaries
        {"\\bab", "ab", true, true, "\\b: word boundary at start of two letters"},
        {"ab\\b", "ab", true, true, "\\b: word boundary at end of two letters"},
        {"a\\bb", "ab", true, false, "\\b: word boundary fails between two letters"},
        {"\\b12", "12", true, true, "\\b: word boundary at start of two digits"},
        {"12\\b", "12", true, true, "\\b: word boundary at end of two digits"},
        {"1\\b2", "12", true, false, "\\b: word boundary fails between two digits"},
        
        // Two character strings - non-word boundaries
        {"\\Bab", "ab", true, false, "\\B: non-word boundary fails at start of two letters"},
        {"ab\\B", "ab", true, false, "\\B: non-word boundary fails at end of two letters"},
        {"a\\Bb", "ab", true, true, "\\B: non-word boundary succeeds between two letters"},
        {"1\\B2", "12", true, true, "\\B: non-word boundary succeeds between two digits"},
        
        // Mixed word and non-word characters at boundaries
        {"\\b ", " ", true, false, "\\b: word boundary fails with single space"},
        {"\\b.", ".", true, false, "\\b: word boundary fails with single punctuation"},
        {" \\b", " ", true, false, "\\b: word boundary fails with single space"},
        {".\\b", ".", true, false, "\\b: word boundary fails with single punctuation"},
        
        {"\\B ", " ", true, true, "\\B: non-word boundary matches with single space (current behavior)"},
        {"\\B.", ".", true, true, "\\B: non-word boundary matches with single punctuation (current behavior)"},
        {" \\B", " ", true, true, "\\B: non-word boundary matches with single space (current behavior)"},
        {".\\B", ".", true, true, "\\B: non-word boundary matches with single punctuation (current behavior)"},
        
        // Transitions at string boundaries
        {"\\ba ", "a ", true, true, "\\b: word boundary at start with word->non-word"},
        {" a\\b", " a", true, true, "\\b: word boundary at end with non-word->word"},
        {"\\b5.", "5.", true, true, "\\b: word boundary at start with digit->punctuation"},
        {".5\\b", ".5", true, true, "\\b: word boundary at end with punctuation->digit"},
        
        // Complex edge cases
        {"\\b\\b", "a", true, true, "\\b\\b: double word boundary at word start"},
        {"\\b\\b", " ", true, false, "\\b\\b: double word boundary fails with space"},
        {"\\B\\B", "ab", true, true, "\\B\\B: double non-word boundary inside word"},
        {"\\B\\B", "a", true, false, "\\B\\B: double non-word boundary fails at word boundary"},
        
        // Boundaries with special characters
        {"\\b@", "@", true, false, "\\b: word boundary fails with special char at start"},
        {"@\\b", "@", true, false, "\\b: word boundary fails with special char at end"},
        {"\\B@", "@", true, true, "\\B: non-word boundary matches with special char (current behavior)"},
        
        // Very short patterns with boundaries
        {"\\b", "a", true, true, "\\b: word boundary matches at start of word"},
        {"\\b", " ", true, false, "\\b: word boundary fails at non-word char"},
        {"\\B", "ab", true, true, "\\B: non-word boundary matches inside word"},
        {"\\B", "a", true, false, "\\B: non-word boundary fails at word boundary"},
        
        // Boundaries at position 0 and end
        {"\\ba*", "abc", true, true, "\\b: word boundary at position 0 with wildcard"},
        {"*a\\b", "cba", true, true, "\\b: word boundary at end with wildcard"},
        {"\\Ba*", "abc", true, false, "\\B: non-word boundary at position 0 fails"},
        {"*a\\B", "cba", true, false, "\\B: non-word boundary at end fails"},
    };
    
    for (size_t i = 0; i < sizeof(edge_tests) / sizeof(edge_tests[0]); i++) {
        run_test(edge_tests[i]);
    }
}

// Test cases for case sensitivity with word boundaries
static void test_word_boundary_case_sensitivity() {
    printf("\n=== Testing Word Boundary Case Sensitivity ===\n");
    
    test_case_t case_tests[] = {
        // Case sensitivity with word boundaries and anchors
        {"^\\bTest\\b$", "test", false, true, "Case insensitive: anchored word boundary"},
        {"^\\bTest\\b$", "test", true, false, "Case sensitive: anchored word boundary"},
        {"^\\bTEST\\b$", "test", false, true, "Case insensitive: uppercase pattern"},
        {"^\\bTEST\\b$", "test", true, false, "Case sensitive: uppercase pattern"},
        
        // Case sensitivity with word boundaries and wildcards
        {"\\b*Test*\\b", "mytest", false, true, "Case insensitive: word boundary with wildcards (matches via wildcard)"},
        {"\\b*Test*\\b", "mytest", true, false, "Case sensitive: word boundary with wildcards"},
        {"\\b*TEST*\\b", "mytest", false, true, "Case insensitive: uppercase with wildcards (matches via wildcard)"},
        {"\\b*TEST*\\b", "mytest", true, false, "Case sensitive: uppercase with wildcards"},
        
        // Case sensitivity with word boundaries and metacharacters
        {"\\bTest\\w", "testa", false, true, "Case insensitive: word boundary + word char"},
        {"\\bTest\\w", "testa", true, false, "Case sensitive: word boundary + word char"},
        {"\\w\\bTest", "atest", false, false, "Case insensitive: word char + word boundary (no match at boundary)"},
        {"\\w\\bTest", "atest", true, false, "Case sensitive: word char + word boundary"},
    };
    
    for (size_t i = 0; i < sizeof(case_tests) / sizeof(case_tests[0]); i++) {
        run_test(case_tests[i]);
    }
}

// Main test function
int main() {
    printf("=== Word Boundary Integration Tests ===\n");
    printf("Testing word boundary integration with existing features\n");
    printf("Requirements: 2.3, 2.4, 6.1, 6.2\n");
    
    test_word_boundaries_with_anchors();
    test_word_boundaries_with_wildcards();
    test_word_boundaries_with_metacharacters();
    test_word_boundary_edge_cases();
    test_word_boundary_case_sensitivity();
    
    printf("\n=== Test Summary ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n", tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    
    return tests_failed > 0 ? 1 : 0;
}