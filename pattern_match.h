#pragma once

#include <stdbool.h>

/**
 * Enhanced pattern matching with regex anchor and escape sequence support.
 * 
 * This function performs pattern matching with support for:
 * - Wildcard characters (*) for matching any sequence of characters
 * - Anchor characters (^ and $) for position-specific matching
 * - Escape sequences (\^, \$, \*, \\) for literal character matching
 * - Case-sensitive and case-insensitive matching
 * 
 * ANCHOR CHARACTERS:
 * - ^ at the start of pattern: matches only from the beginning of the string
 * - $ at the end of pattern: matches only to the end of the string
 * - ^...$ together: exact string matching (entire string must match)
 * 
 * ESCAPE SEQUENCES:
 * - \^ matches literal ^ character
 * - \$ matches literal $ character  
 * - \* matches literal * character
 * - \\ matches literal \ character
 * 
 * WILDCARD BEHAVIOR:
 * - * matches any sequence of characters (including empty sequence)
 * - Can be combined with anchors: ^prefix*suffix$ 
 * 
 * BACKWARD COMPATIBILITY:
 * - Patterns without anchors work exactly as before (substring matching)
 * - Existing wildcard functionality is preserved
 * - Case sensitivity behavior is unchanged
 * 
 * EXAMPLES:
 * - pattern_match("hello", "hello world", false) → true (substring match)
 * - pattern_match("^hello", "hello world", false) → true (starts with)
 * - pattern_match("world$", "hello world", false) → true (ends with)
 * - pattern_match("^hello$", "hello", false) → true (exact match)
 * - pattern_match("^hello$", "hello world", false) → false (not exact)
 * - pattern_match("\\^start", "^start", false) → true (escaped ^)
 * - pattern_match("end\\$", "end$", false) → true (escaped $)
 * - pattern_match("file\\*.txt", "file*.txt", false) → true (escaped *)
 * 
 * @param pattern The pattern to match against, may contain wildcards, anchors, and escapes
 * @param str The string to test for pattern matching
 * @param case_sensitive If true, performs case-sensitive matching; if false, case-insensitive
 * @return true if the string matches the pattern according to the specified rules, false otherwise
 * 
 * @note Returns false if either pattern or str is NULL
 * @note Memory is managed internally; no cleanup required by caller
 * @note Thread-safe (no global state modified)
 */
bool pattern_match(const char *pattern, const char *str, bool case_sensitive);
