#!/bin/bash

# Comprehensive test runner for pattern matching library
# Task 18: Final backward compatibility verification

echo "=========================================="
echo "Pattern Matching Library - Full Test Suite"
echo "Task 18: Final Backward Compatibility Verification"
echo "=========================================="
echo

# Compile all tests
echo "Compiling all test files..."
gcc -o test_pattern_match test_pattern_match.c pattern_match.c
gcc -o test_char_classification test_char_classification.c pattern_match.c
gcc -o test_word_boundary_basic test_word_boundary_basic.c pattern_match.c
gcc -o test_word_boundary_integration test_word_boundary_integration.c pattern_match.c
gcc -o test_metachar_verification test_metachar_verification.c pattern_match.c
gcc -o test_comprehensive_integration test_comprehensive_integration.c pattern_match.c -std=c99 -DNOTIFIER_STUB
gcc -o test_error_handling test_error_handling.c pattern_match.c -I.
gcc -o test_memory_stress test_memory_stress.c pattern_match.c -I.
gcc -o test_invalid_patterns test_invalid_patterns.c pattern_match.c -I.

echo "Compilation complete."
echo

# Track overall results
total_tests=0
total_passed=0
total_failed=0

# Function to run a test and capture results
run_test() {
    local test_name="$1"
    local test_executable="$2"
    
    echo "=========================================="
    echo "Running $test_name"
    echo "=========================================="
    
    if [ -x "$test_executable" ]; then
        output=$(./$test_executable 2>&1)
        exit_code=$?
        
        echo "$output"
        
        # Extract test counts from output
        if echo "$output" | grep -q "Total tests run:"; then
            tests_run=$(echo "$output" | grep "Total tests run:" | sed 's/.*Total tests run: \([0-9]*\).*/\1/')
            tests_passed=$(echo "$output" | grep "Tests passed:" | sed 's/.*Tests passed: \([0-9]*\).*/\1/')
            tests_failed=$(echo "$output" | grep "Tests failed:" | sed 's/.*Tests failed: \([0-9]*\).*/\1/')
            
            total_tests=$((total_tests + tests_run))
            total_passed=$((total_passed + tests_passed))
            total_failed=$((total_failed + tests_failed))
        elif echo "$output" | grep -q "Tests run:"; then
            tests_run=$(echo "$output" | grep "Tests run:" | sed 's/.*Tests run: \([0-9]*\).*/\1/')
            tests_passed=$(echo "$output" | grep "Tests passed:" | sed 's/.*Tests passed: \([0-9]*\).*/\1/')
            tests_failed=$(echo "$output" | grep "Tests failed:" | sed 's/.*Tests failed: \([0-9]*\).*/\1/')
            
            total_tests=$((total_tests + tests_run))
            total_passed=$((total_passed + tests_passed))
            total_failed=$((total_failed + tests_failed))
        fi
        
        echo
        if [ $exit_code -eq 0 ]; then
            echo "✓ $test_name: ALL TESTS PASSED"
        else
            echo "✗ $test_name: SOME TESTS FAILED (exit code: $exit_code)"
        fi
        echo
    else
        echo "✗ $test_name: Test executable not found or not executable"
        echo
    fi
}

# Run all test suites
run_test "Main Pattern Match Tests" "test_pattern_match"
run_test "Character Classification Tests" "test_char_classification"
run_test "Word Boundary Basic Tests" "test_word_boundary_basic"
run_test "Word Boundary Integration Tests" "test_word_boundary_integration"
run_test "Metacharacter Verification Tests" "test_metachar_verification"
run_test "Comprehensive Integration Tests" "test_comprehensive_integration"
run_test "Error Handling Tests" "test_error_handling"
run_test "Memory Stress Tests" "test_memory_stress"
run_test "Invalid Patterns Tests" "test_invalid_patterns"

# (Escape-processing and get_escaped_char plumbing are covered by test_metachar_verification;
#  their standalone binaries had no source and have been removed.)

# Overall summary
echo "=========================================="
echo "OVERALL TEST SUMMARY"
echo "=========================================="
echo "Total tests run across all suites: $total_tests"
echo "Total tests passed: $total_passed"
echo "Total tests failed: $total_failed"

if [ $total_failed -eq 0 ]; then
    echo "✓ ALL TESTS PASSED - BACKWARD COMPATIBILITY VERIFIED"
    success_rate="100.0"
else
    success_rate=$(echo "scale=1; $total_passed * 100 / $total_tests" | bc -l)
    echo "✗ SOME TESTS FAILED - ISSUES DETECTED"
fi

echo "Overall success rate: ${success_rate}%"
echo

# Performance check - run a simple performance test
echo "=========================================="
echo "PERFORMANCE VERIFICATION"
echo "=========================================="
echo "Testing performance impact on existing patterns..."

# Create a simple performance test
cat > perf_test.c << 'EOF'
#include <stdio.h>
#include <time.h>
#include "pattern_match.h"

int main() {
    clock_t start, end;
    double cpu_time_used;
    int iterations = 100000;
    
    // Test simple patterns (should have minimal performance impact)
    const char* patterns[] = {
        "test",
        "^test",
        "test$",
        "^test$",
        "test*",
        "*test",
        "^test*$"
    };
    
    const char* input = "test";
    
    printf("Running performance test with %d iterations...\n", iterations);
    
    start = clock();
    for (int i = 0; i < iterations; i++) {
        for (int p = 0; p < 7; p++) {
            pattern_match(patterns[p], input, true);
        }
    }
    end = clock();
    
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Time taken: %f seconds\n", cpu_time_used);
    printf("Average time per pattern match: %f microseconds\n", 
           (cpu_time_used * 1000000) / (iterations * 7));
    
    if (cpu_time_used < 1.0) {
        printf("✓ Performance is acceptable (< 1 second for %d iterations)\n", iterations * 7);
        return 0;
    } else {
        printf("⚠ Performance may be impacted (> 1 second for %d iterations)\n", iterations * 7);
        return 1;
    }
}
EOF

gcc -o perf_test perf_test.c pattern_match.c
./perf_test
rm perf_test.c perf_test

echo
echo "=========================================="
echo "BACKWARD COMPATIBILITY VERIFICATION COMPLETE"
echo "=========================================="

if [ $total_failed -eq 0 ]; then
    echo "✓ VERIFICATION SUCCESSFUL: All tests pass, backward compatibility maintained"
    exit 0
else
    echo "✗ VERIFICATION FAILED: $total_failed tests failed, issues need to be addressed"
    exit 1
fi