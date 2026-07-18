#include <stdio.h>
#include <stdbool.h>
#include "pattern_match.h"

int main() {
    printf("Testing Basic Metacharacter Implementation\n");
    printf("==========================================\n\n");
    
    // Test Requirements 1.1, 1.2 - \d and \D
    printf("Testing \\d (digit) and \\D (non-digit):\n");
    printf("\\d matches '5': %s\n", pattern_match("\\d", "5", true) ? "PASS" : "FAIL");
    printf("\\d matches 'a': %s\n", pattern_match("\\d", "a", true) ? "FAIL" : "PASS");
    printf("\\D matches 'a': %s\n", pattern_match("\\D", "a", true) ? "PASS" : "FAIL");
    printf("\\D matches '5': %s\n", pattern_match("\\D", "5", true) ? "FAIL" : "PASS");
    
    // Test Requirements 1.3, 1.4 - Multiple \d and with operators
    printf("\\d\\d matches '42': %s\n", pattern_match("\\d\\d", "42", true) ? "PASS" : "FAIL");
    printf("^\\d$ matches '7': %s\n", pattern_match("^\\d$", "7", true) ? "PASS" : "FAIL");
    printf("\\d* matches '123': %s\n", pattern_match("\\d*", "123", true) ? "PASS" : "FAIL");
    
    // Test Requirements 2.1, 2.2 - \w and \W
    printf("\nTesting \\w (word) and \\W (non-word):\n");
    printf("\\w matches 'a': %s\n", pattern_match("\\w", "a", true) ? "PASS" : "FAIL");
    printf("\\w matches 'Z': %s\n", pattern_match("\\w", "Z", true) ? "PASS" : "FAIL");
    printf("\\w matches '5': %s\n", pattern_match("\\w", "5", true) ? "PASS" : "FAIL");
    printf("\\w matches '_': %s\n", pattern_match("\\w", "_", true) ? "PASS" : "FAIL");
    printf("\\w matches ' ': %s\n", pattern_match("\\w", " ", true) ? "FAIL" : "PASS");
    printf("\\W matches ' ': %s\n", pattern_match("\\W", " ", true) ? "PASS" : "FAIL");
    printf("\\W matches 'a': %s\n", pattern_match("\\W", "a", true) ? "FAIL" : "PASS");
    
    // Test Requirements 3.1, 3.2 - \s and \S
    printf("\nTesting \\s (whitespace) and \\S (non-whitespace):\n");
    printf("\\s matches ' ': %s\n", pattern_match("\\s", " ", true) ? "PASS" : "FAIL");
    printf("\\s matches '\\t': %s\n", pattern_match("\\s", "\t", true) ? "PASS" : "FAIL");
    printf("\\s matches 'a': %s\n", pattern_match("\\s", "a", true) ? "FAIL" : "PASS");
    printf("\\S matches 'a': %s\n", pattern_match("\\S", "a", true) ? "PASS" : "FAIL");
    printf("\\S matches ' ': %s\n", pattern_match("\\S", " ", true) ? "FAIL" : "PASS");
    
    // Test case sensitivity for word characters
    printf("\nTesting case sensitivity:\n");
    printf("\\w matches 'A' (case sensitive): %s\n", pattern_match("\\w", "A", true) ? "PASS" : "FAIL");
    printf("\\w matches 'a' (case sensitive): %s\n", pattern_match("\\w", "a", true) ? "PASS" : "FAIL");
    
    // Test with existing features
    printf("\nTesting with existing operators:\n");
    printf("^\\d\\w$ matches '5a': %s\n", pattern_match("^\\d\\w$", "5a", true) ? "PASS" : "FAIL");
    printf("\\s* matches '   ': %s\n", pattern_match("\\s*", "   ", true) ? "PASS" : "FAIL");
    printf("\\w*\\d matches 'abc123': %s\n", pattern_match("\\w*\\d", "abc123", true) ? "PASS" : "FAIL");
    
    return 0;
}
