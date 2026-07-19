# Test Infrastructure — qmk-notifier

## run_all_tests.sh (9-suite pattern matcher runner)

### Compilation
Hardcoded per-test compile lines (lines 13-21):
```bash
gcc -o test_pattern_match test_pattern_match.c pattern_match.c
gcc -o test_char_classification test_char_classification.c pattern_match.c
# ... 7 more suites
```
Pattern: `gcc -o <bin> <bin>.c pattern_match.c [-I. | -std=c99 -DNOTIFIER_STUB]`

### Aggregation
`run_test()` function captures stdout, greps summary lines:
```
Total tests run: N
Tests passed: N
Tests failed: N
```
Totals summed across all 9 suites. Exit 1 iff any failures.

### Adding tests to existing files
If adding cases to an existing `.c` file (e.g. `test_pattern_match.c`), **no
script changes needed** — auto-recompiles, count auto-increments. Only new `.c`
files need a gcc line + run_test call.

## test_pattern_match.c (PRIMARY TARGET for Issue 1)

- **376 tests, 376 pass** (verified)
- Data-driven: `test_case_t` struct + `run_test()` helper (lines 7-40)
```c
typedef struct {
    const char *pattern;
    const char *input;
    bool case_sensitive;
    bool expected_result;
    const char *description;
} test_case_t;
```
- Categories dispatched from `run_pattern_match_tests()` (~line 770-810)
- Summary format: "Total tests run: N\nTests passed: N\nTests failed: N"
- Exit `tests_failed > 0 ? 1 : 0`

### Existing @ coverage (but NOT ^\w+@\w+$)
| File:line | Test | 
|---|---|
| test_pattern_match.c:281 | `\w` vs `"@"` → false |
| test_invalid_patterns.c:109 | `"@"` vs `"@"` → true (literal) |

**GAP:** No test for `^\w+@\w+$` semantics. `grep -rn 'w+@\|user_host\|user@host' test_*.c` → 0 hits.

## run_notifier_stub_tests.sh (notifier dispatch + OS gate)

### How it works
1. Stub-compile notifier.c → /tmp/notifier_stub.o
2. Link test_notifier_dispatch + test_notifier_os against .o + qmk_stubs.c
3. Run both, count `^FAIL:` lines
4. Gate: 0 FAILs + exit 0 for each → PASSED

### To add a 3rd driver
Add gcc link step, run + fails grep, AND into final gate condition.

## test_notifier_dispatch.c (TARGET for Issue 2 test)

- Uses `ck()` helper pattern: `static void ck(const char *p, const char *m, int cs, int want)`
- Re-declares external entry points: hid_notify, process_full_message, match_pattern
- `DEFINE_SERIAL_COMMANDS` macro available for test command maps
- Summary: "Total tests run: N / passed: N / failed: N" (slash format)
- Exit `g_fail ? 1 : 0`

## test_notifier_os.c

- Uses `CK(cond, name)` macro pattern
- 31 cases covering F8/F9 multi-OS contract
- Same slash-format summary

## Stubs (qmk_stubs/)

| File | Provides |
|---|---|
| qmk_keyboard_stub.h | layer_on/layer_off decls, RAW_EPSIZE=32 |
| qmk_stubs.c | layer_on/off impl (tracks g_active_layer), raw_hid_send impl, stub_get_active_layer() |
| os_detection.h | os_variant_t enum (OS_UNSURE=0, OS_LINUX=1, etc.) |
| raw_hid.h | raw_hid_receive decl |

**Missing:** print.h / uprintf — needed for CONSOLE_ENABLE compile-check (Issue 3).