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

// Test cases for start anchor functionality (^ prefix)
static void test_start_anchor() {
    printf("\n=== Testing Start Anchor (^) Functionality ===\n");
    
    test_case_t start_anchor_tests[] = {
        // Requirement 1.1: Pattern starts with ^ should only match from beginning
        {"^searchterm", "searchterm", true, true, "Start anchor: exact match from beginning"},
        {"^searchterm", "presearchterm", true, false, "Start anchor: should not match when not at beginning"},
        {"^searchterm", "searchtermpost", true, true, "Start anchor: should match partial from beginning"},
        
        // Additional start anchor tests
        {"^test", "test123", true, true, "Start anchor: match with trailing content"},
        {"^test", "pretest", true, false, "Start anchor: no match with leading content"},
        {"^", "", true, true, "Start anchor: empty pattern matches empty string"},
        {"^abc", "ABC", false, true, "Start anchor: case insensitive match"},
        {"^abc", "ABC", true, false, "Start anchor: case sensitive no match"},
    };
    
    for (int i = 0; i < sizeof(start_anchor_tests) / sizeof(start_anchor_tests[0]); i++) {
        run_test(start_anchor_tests[i]);
    }
}

// Test cases for end anchor functionality ($ suffix)
static void test_end_anchor() {
    printf("\n=== Testing End Anchor ($) Functionality ===\n");
    
    test_case_t end_anchor_tests[] = {
        // Requirement 2.1: Pattern ends with $ should only match to end
        {"searchterm$", "searchterm", true, true, "End anchor: exact match to end"},
        {"searchterm$", "searchtermpost", true, false, "End anchor: should not match when not at end"},
        {"searchterm$", "presearchterm", true, true, "End anchor: should match partial to end"},
        
        // Additional end anchor tests
        {"test$", "pretest", true, true, "End anchor: match with leading content"},
        {"test$", "test123", true, false, "End anchor: no match with trailing content"},
        {"$", "", true, true, "End anchor: empty pattern matches empty string"},
        {"abc$", "ABC", false, true, "End anchor: case insensitive match"},
        {"abc$", "ABC", true, false, "End anchor: case sensitive no match"},
    };
    
    for (int i = 0; i < sizeof(end_anchor_tests) / sizeof(end_anchor_tests[0]); i++) {
        run_test(end_anchor_tests[i]);
    }
}

// Test cases for fully anchored patterns (^...$ exact match)
static void test_full_anchor() {
    printf("\n=== Testing Full Anchor (^...$) Functionality ===\n");
    
    test_case_t full_anchor_tests[] = {
        // Requirement 3.1-3.4: Both ^ and $ for exact string matches
        {"^searchterm$", "searchterm", true, true, "Full anchor: exact match"},
        {"^searchterm$", "presearchterm", true, false, "Full anchor: no match with leading content"},
        {"^searchterm$", "searchtermpost", true, false, "Full anchor: no match with trailing content"},
        {"^searchterm$", "presearchtermpost", true, false, "Full anchor: no match with both leading and trailing"},
        
        // Additional full anchor tests
        {"^test$", "test", true, true, "Full anchor: simple exact match"},
        {"^$", "", true, true, "Full anchor: empty pattern matches empty string"},
        {"^abc$", "ABC", false, true, "Full anchor: case insensitive exact match"},
        {"^abc$", "ABC", true, false, "Full anchor: case sensitive no match"},
    };
    
    for (int i = 0; i < sizeof(full_anchor_tests) / sizeof(full_anchor_tests[0]); i++) {
        run_test(full_anchor_tests[i]);
    }
}

// Test cases for anchors combined with wildcards
static void test_anchors_with_wildcards() {
    printf("\n=== Testing Anchors Combined with Wildcards ===\n");
    
    test_case_t anchor_wildcard_tests[] = {
        // Requirement 4.1-4.4: Combine anchors with wildcard characters
        {"^sear*term$", "searchterm", true, true, "Full anchor with wildcard: exact match"},
        {"^sear*term$", "searedsalmonterm", true, true, "Full anchor with wildcard: wildcard expansion"},
        {"^sear*term$", "somesearchterm", true, false, "Full anchor with wildcard: no match with leading"},
        {"^sear*term$", "searchtermhere", true, false, "Full anchor with wildcard: no match with trailing"},
        
        // Additional wildcard combinations
        {"^*test", "anytest", true, true, "Start anchor with leading wildcard"},
        {"test*$", "testany", true, true, "End anchor with trailing wildcard"},
        {"^*$", "anything", true, true, "Full anchor with full wildcard"},
        {"^a*b*c$", "abc", true, true, "Full anchor with multiple wildcards"},
        {"^a*b*c$", "aabbcc", true, true, "Full anchor with multiple wildcards expanded"},
    };
    
    for (int i = 0; i < sizeof(anchor_wildcard_tests) / sizeof(anchor_wildcard_tests[0]); i++) {
        run_test(anchor_wildcard_tests[i]);
    }
}

// Test cases for escape sequences
static void test_escape_sequences() {
    printf("\n=== Testing Escape Sequences ===\n");
    
    test_case_t escape_tests[] = {
        // Requirement 5.1: Escape ^ character
        {"\\^searchterm", "^searchterm", true, true, "Escape ^: literal ^ match"},
        {"\\^searchterm", "searchterm", true, false, "Escape ^: should not match without literal ^"},
        
        // Requirement 5.2: Escape $ character  
        {"searchterm\\$", "searchterm$", true, true, "Escape $: literal $ match"},
        {"searchterm\\$", "searchterm", true, false, "Escape $: should not match without literal $"},
        
        // Requirement 5.3: Escape * character
        {"search\\*term", "search*term", true, true, "Escape *: literal * match"},
        {"search\\*term", "searchanyterm", true, false, "Escape *: should not act as wildcard"},
        
        // Requirement 5.5: Escape backslash (double backslash)
        {"\\\\^searchterm", "\\^searchterm", true, true, "Escape \\: literal backslash followed by ^"},
        
        // Additional escape sequence tests
        {"\\^test\\$", "^test$", true, true, "Multiple escapes: ^ and $ literals"},
        {"test\\*\\*test", "test**test", true, true, "Multiple escaped asterisks"},
        {"\\\\test", "\\test", true, true, "Escaped backslash"},
        {"test\\", "test\\", true, true, "Trailing backslash treated as literal"},
        
        // Case sensitivity with escapes
        {"\\^Test", "^test", false, true, "Escaped ^ with case insensitive"},
        {"\\^Test", "^test", true, false, "Escaped ^ with case sensitive"},
        
        // Edge cases for escape processing
        {"\\a", "\\a", true, true, "Invalid escape: backslash + invalid char treated as literal"},
        {"\\\\\\^", "\\^", true, true, "Complex escape: \\\\\\^ should become \\^"},
        {"\\\\\\\\", "\\\\", true, true, "Double escaped backslash: \\\\\\\\ should become \\\\"},
        {"test\\\\", "test\\", true, true, "Escaped backslash at end"},
        {"\\", "\\", true, true, "Single trailing backslash"},
        {"\\\\", "\\", true, true, "Escaped backslash only"},
        
        // Mixed escape scenarios
        {"\\^\\$\\*\\\\", "^$*\\", true, true, "All escape sequences together"},
        {"pre\\^mid\\$post", "pre^mid$post", true, true, "Escapes in middle of pattern"},
        {"\\^start", "^start", true, true, "Escaped ^ at start (not anchor)"},
        {"end\\$", "end$", true, true, "Escaped $ at end (not anchor)"},
        
        // Verify escapes don't interfere with normal matching
        {"normal", "normal", true, true, "Normal pattern still works"},
        {"nor\\*mal", "nor*mal", true, true, "Escaped * in normal pattern"},
    };
    
    for (int i = 0; i < sizeof(escape_tests) / sizeof(escape_tests[0]); i++) {
        run_test(escape_tests[i]);
    }
}

// Test cases for backward compatibility with existing wildcard behavior
static void test_backward_compatibility() {
    printf("\n=== Testing Backward Compatibility ===\n");
    
    test_case_t compatibility_tests[] = {
        // Requirement 6.1-6.4: Existing wildcard functionality unchanged
        {"searchterm", "presearchtermpost", true, true, "Unanchored: substring match"},
        {"sear*term", "presearchtermpost", true, true, "Unanchored wildcard: substring match"},
        {"*term", "searchterm", true, true, "Leading wildcard: suffix match"},
        {"search*", "searchterm", true, true, "Trailing wildcard: prefix match"},
        
        // Additional backward compatibility tests
        {"test", "test", true, true, "Simple exact match"},
        {"test", "testing", true, true, "Simple substring match"},
        {"*", "anything", true, true, "Full wildcard matches anything"},
        {"", "", true, true, "Empty pattern matches empty string"},
        {"abc", "ABC", false, true, "Case insensitive backward compatibility"},
        {"abc", "ABC", true, false, "Case sensitive backward compatibility"},
        
        // Complex wildcard patterns
        {"a*b*c", "aabbcc", true, true, "Multiple wildcards"},
        {"*test*", "pretestpost", true, true, "Wildcards on both sides"},
        {"a*", "a", true, true, "Wildcard with minimum match"},
    };
    
    for (int i = 0; i < sizeof(compatibility_tests) / sizeof(compatibility_tests[0]); i++) {
        run_test(compatibility_tests[i]);
    }
}

// Test cases for case sensitivity with new features
static void test_case_sensitivity() {
    printf("\n=== Testing Case Sensitivity with New Features ===\n");
    
    test_case_t case_tests[] = {
        // Requirement 7.1: Case insensitive with full anchors
        {"^SearchTerm$", "searchterm", false, true, "Case insensitive: full anchor match"},
        
        // Requirement 7.2: Case sensitive with full anchors
        {"^SearchTerm$", "searchterm", true, false, "Case sensitive: full anchor no match"},
        
        // Requirement 7.3: Case insensitive with escaped characters
        {"\\^SearchTerm", "^searchterm", false, true, "Case insensitive: escaped ^ match"},
        
        // Requirement 7.4: Case sensitive with escaped characters
        {"\\^SearchTerm", "^searchterm", true, false, "Case sensitive: escaped ^ no match"},
        
        // Additional case sensitivity tests
        {"^Test*", "testany", false, true, "Case insensitive: start anchor with wildcard"},
        {"^Test*", "testany", true, false, "Case sensitive: start anchor with wildcard"},
        {"*Test$", "anytest", false, true, "Case insensitive: end anchor with wildcard"},
        {"*Test$", "anytest", true, false, "Case sensitive: end anchor with wildcard"},
        {"Test\\*", "test*", false, true, "Case insensitive: escaped asterisk"},
        {"Test\\*", "test*", true, false, "Case sensitive: escaped asterisk"},
    };
    
    for (int i = 0; i < sizeof(case_tests) / sizeof(case_tests[0]); i++) {
        run_test(case_tests[i]);
    }
}

// Test pattern parsing functionality indirectly through behavior
static void test_pattern_parsing() {
    printf("\n=== Testing Pattern Parsing Infrastructure ===\n");
    
    test_case_t parsing_tests[] = {
        // Test anchor detection through behavior
        {"^test", "test", true, true, "Parse start anchor: should detect ^ and match from beginning"},
        {"^test", "pretest", true, false, "Parse start anchor: should detect ^ and reject non-beginning match"},
        {"test$", "test", true, true, "Parse end anchor: should detect $ and match to end"},
        {"test$", "testpost", true, false, "Parse end anchor: should detect $ and reject non-end match"},
        {"^test$", "test", true, true, "Parse both anchors: should detect ^$ and require exact match"},
        {"^test$", "pretest", true, false, "Parse both anchors: should reject with leading content"},
        {"^test$", "testpost", true, false, "Parse both anchors: should reject with trailing content"},
        
        // Test escape sequence processing through behavior
        {"\\^test", "^test", true, true, "Parse escaped ^: should treat as literal character"},
        {"\\^test", "test", true, false, "Parse escaped ^: should not act as anchor"},
        {"test\\$", "test$", true, true, "Parse escaped $: should treat as literal character"},
        {"test\\$", "test", true, false, "Parse escaped $: should not act as anchor"},
        {"test\\*test", "test*test", true, true, "Parse escaped *: should treat as literal character"},
        {"test\\*test", "testanytest", true, false, "Parse escaped *: should not act as wildcard"},
        {"\\\\test", "\\test", true, true, "Parse escaped \\: should treat as literal backslash"},
        
        // Test complex parsing scenarios
        {"\\^test\\$", "^test$", true, true, "Parse multiple escapes: both ^ and $ escaped"},
        {"^\\^test", "^test", true, true, "Parse mixed: start anchor with escaped ^"},
        {"test\\$$", "test$", true, true, "Parse mixed: escaped $ with end anchor"},
        {"^test\\*$", "test*", true, true, "Parse mixed: anchors with escaped *"},
        
        // Test edge cases in parsing
        {"^", "", true, true, "Parse lone start anchor"},
        {"$", "", true, true, "Parse lone end anchor"},
        {"^$", "", true, true, "Parse both anchors only"},
        {"\\", "\\", true, true, "Parse lone backslash"},
        {"\\^", "^", true, true, "Parse escaped ^ only"},
        {"\\$", "$", true, true, "Parse escaped $ only"},
        {"\\*", "*", true, true, "Parse escaped * only"},
        
        // Test parsing with case sensitivity
        {"^Test", "test", false, true, "Parse with case insensitive: anchor preserved"},
        {"^Test", "test", true, false, "Parse with case sensitive: anchor preserved"},
        {"\\^Test", "^test", false, true, "Parse escaped with case insensitive"},
        {"\\^Test", "^test", true, false, "Parse escaped with case sensitive"},
    };
    
    for (int i = 0; i < sizeof(parsing_tests) / sizeof(parsing_tests[0]); i++) {
        run_test(parsing_tests[i]);
    }
}

// Test edge cases and error conditions
static void test_edge_cases() {
    printf("\n=== Testing Edge Cases ===\n");
    
    test_case_t edge_tests[] = {
        // NULL and empty string handling
        {"", "", true, true, "Empty pattern and string"},
        {"test", "", true, false, "Non-empty pattern, empty string"},
        {"", "test", true, false, "Empty pattern, non-empty string"},
        
        // Special character combinations
        {"^^test", "^test", true, true, "Double ^ at start"},
        {"test$$", "test$", true, true, "Double $ at end"},
        {"^$", "", true, true, "Only anchors, empty string"},
        {"^$", "a", true, false, "Only anchors, non-empty string"},
        
        // Complex escape scenarios
        {"\\\\\\^", "\\^", true, true, "Complex escape: \\\\\\^ -> \\^"},
        {"test\\", "test\\", true, true, "Trailing backslash"},
        {"\\", "\\", true, true, "Single backslash"},
        
        // Wildcard edge cases with anchors
        {"^*", "anything", true, true, "Start anchor with immediate wildcard"},
        {"*$", "anything", true, true, "End anchor with immediate wildcard"},
        {"^**$", "test", true, true, "Multiple consecutive wildcards with anchors"},
    };
    
    for (int i = 0; i < sizeof(edge_tests) / sizeof(edge_tests[0]); i++) {
        run_test(edge_tests[i]);
    }
}

// Main test runner function
void run_pattern_match_tests(void) {
    printf("Starting Pattern Match Test Suite\n");
    printf("=================================\n");
    
    // Reset counters
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;
    
    // Run all test categories
    test_start_anchor();
    test_end_anchor();
    test_full_anchor();
    test_anchors_with_wildcards();
    test_escape_sequences();
    test_backward_compatibility();
    test_case_sensitivity();
    test_pattern_parsing();
    test_edge_cases();
    
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
}

// Main function for standalone testing
int main(void) {
    run_pattern_match_tests();
    return tests_failed > 0 ? 1 : 0;  // Return non-zero if any tests failed
}