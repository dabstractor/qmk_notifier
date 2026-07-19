# Test Infrastructure & Insertion Points

## Two Independent Harnesses (CRITICAL DISTINCTION)

### Harness A: `run_all_tests.sh` — Pattern Matcher Corpus (9 suites)

**Compilation** (lines 11-19): Each test compiled as:
```bash
gcc -o test_X test_X.c pattern_match.c [-std=c99 -DNOTIFIER_STUB] [-I.]
```
`pattern_match.c` is included **directly**, so `NFA_MAX_PATTERN` defaults to
**2048** (the `#ifndef` guard at `pattern_match.c:286-291`). No suite passes
`-DNFA_MAX_PATTERN=128`.

**Result contract**: `run_test()` greps for `Total tests run:` / `Tests passed:`
/ `Tests failed:` and accumulates aggregate counts.

**Suites**: `test_pattern_match`, `test_char_classification`,
`test_word_boundary_basic`, `test_word_boundary_integration`,
`test_metachar_verification`, `test_comprehensive_integration`,
`test_error_handling`, `test_memory_stress`, `test_invalid_patterns`.

**Current**: 2023/2023 passing.

### Harness B: `run_notifier_stub_tests.sh` — Notifier Stub Suites (3 drivers)

**Compilation** (lines 15-21): One shared stub-compiled object:
```bash
gcc -Wall -Wextra -std=c99 \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
```
Because this compiles `notifier.c`, the `#define NFA_MAX_PATTERN 128` at
`notifier.c:14` IS active. These drivers already run at the firmware budget.

**Linking** (lines 23-46): The shared `.o` is linked into 3 drivers:
1. `test_notifier_dispatch.c` → dispatch (F4 delimiter, ordering)
2. `test_notifier_os.c` → multi-OS (F8/F9)
3. `test_notifier_host.c` → typed commands / host rules

**Result contract**: `grep -c '^FAIL:'` + exit code per driver.

**Current**: dispatch 14/14, os 31/31, host 64/64.

---

## Issue 3 Fix: NFA_MAX_PATTERN=128 Fidelity Test

### Problem
The 9 pattern suites in Harness A compile `pattern_match.c` directly at
`NFA_MAX_PATTERN=2048`. A pattern between 129 and 2048 processed-pattern bytes
would pass every matcher test yet be silently clamped on hardware.

### Solution
Add a 10th suite to `run_all_tests.sh` compiled with `-DNFA_MAX_PATTERN=128`:

**Compile insertion** — `run_all_tests.sh` after line 19:
```bash
gcc -o test_fidelity_nfa128 test_fidelity_nfa128.c pattern_match.c -I. -DNFA_MAX_PATTERN=128 -std=c99
```

**Run insertion** — `run_all_tests.sh` after line ~104 (last `run_test` call):
```bash
run_test "NFA-128 Fidelity Gate" "test_fidelity_nfa128"
```

### New Source File: `test_fidelity_nfa128.c`
Must follow the `test_pattern_match.c` structure:
- File-scope `test_case_t` struct or simple assertions
- Counters: `tests_run`, `tests_passed`, `tests_failed`
- `main()` prints `Total tests run:` / `Tests passed:` / `Tests failed:` (MUST
  match the Harness A grep contract)

**Key test cases**:
1. A pattern at exactly 128 processed-pattern bytes → matches correctly
2. A pattern at 129 processed-pattern bytes → is clamped (NFA state pool reuse)
   and does NOT match incorrectly
3. The compile-time guard `(NFA_MAX_PATTERN <= 128) ? 1 : -1` passes

### Constraint
Do NOT add `-DNFA_MAX_PATTERN=128` to existing 9 suites — `test_memory_stress`
intentionally needs 2048 for its multi-KB patterns.

---

## Issue 1 Fix: Adversarial Typed-Command Test Cases

### Insertion Point: `test_notifier_host.c`

Append a new bannered block **after** the `(multi-rep)` section (ends ~line 405)
and **before** the final `printf("Total tests run: ...")` / `return` (~lines
406-408).

### Test Helpers Available
- **`CK(cond, name)`** macro: increments `g_pass`/`g_fail`, prints `PASS:`/`FAIL:`
- **`send_typed(cmd_id, args, nargs)`**: single-report typed driver, returns
  `stub_get_last_response()`
- **`stub_get_last_response()`**: last 32-byte HID response (last-write-wins)
- **`stub_get_active_layer()`**: current board activated_layer
- **`hid_notify(rep, 32)`**: direct low-level call for multi-report/manual framing
- Board state variables: `board_cmd_en`, `board_layer_en`, `mac_cmd_en`, etc.
- Callback counters: `cb_mute_en`, `cb_mute_dis`, `cb_layout_en`, `cb_layout_dis`

### Required Test Cases
1. **count/ids mismatch (too few ids)**: count=5, 1 id, ETX → verify typed_mode
   resets, then a legacy string dispatches correctly (board side effects fire)
2. **count/ids mismatch (too many ids)**: count=1, 3 ids + ETX → verify extra
   bytes don't corrupt state
3. **Truncated AHC (no ETX in report, then new legacy message)**: verify the
   new legacy message isn't consumed as literal bytes
4. **Abandoned typed message (full report, no ETX)**: verify recovery when next
   report is a legacy string
5. **Recovery after malformed**: verify a well-formed typed command works after
   a malformed one is dropped
6. **0x03 in AHC count position across report boundary**: verify the count byte
   being 0x03 doesn't break reassembly (the length-aware fix already handles
   this, but it's an adversarial probe)

### Multi-Report Pattern (from existing `(multi-rep)` block, lines 378-405)
```c
uint8_t rep1[32]; memset(rep1, 0, sizeof(rep1));
rep1[0] = 0x81; rep1[1] = 0x9F;
rep1[2] = NOTIFY_CMD_DISCRIMINATOR;
rep1[3] = NOTIFY_CMD_APPLY_HOST_CONTEXT;
/* ... set layer/flags/count/ids ... */
hid_notify(rep1, 32);
/* Report 2 with ETX */
uint8_t rep2[32]; memset(rep2, 0, sizeof(rep2));
rep2[0] = 0x81; rep2[1] = 0x9F;
/* ... payload + ETX ... */
hid_notify(rep2, 32);
const uint8_t *r = stub_get_last_response();
CK(r[0] == ..., "(tag) assertion [§ref]");
```

### Non-dispatch Test Idiom
For cases where no dispatch should occur, prime a known response first and
assert it's unchanged (see `(coexist-ii)` block, lines ~355-368):
```c
const uint8_t *r0 = send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);  /* prime */
uint8_t marker0 = r0[0], echo0 = r0[1];
/* ... send malformed report ... */
const uint8_t *r1 = stub_get_last_response();
CK(r1[0] == marker0 && r1[1] == echo0, "(tag) response unchanged [§ref]");
```