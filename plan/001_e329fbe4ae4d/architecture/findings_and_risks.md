# Findings, Risks & Known Defects — qmk-notifier

Research-derived findings from codebase analysis, QMK firmware source verification,
Thompson NFA algorithm validation, and PRD cross-checking.

---

## BUG-1: NULL pattern dereference in match_pattern [MEDIUM, LATENT]

**Location:** `notifier.c` lines 153–156
**Severity:** Medium (latent — not triggered by normal call chain, but a contract violation)

### The Bug

```c
bool match_pattern(const char *pattern, const char *message, bool case_sensitive) {
    const char *pattern_delimiter = find_first_delimiter(pattern);   // line 154: DEREF before guard!
    if (message == NULL || pattern == NULL) {                        // line 156: guard TOO LATE
        return false;
    }
```

`find_first_delimiter(NULL)` does `for (const char *p = str; *p ...)` → **dereferences NULL → segfault**.

### Contract Violated

PRD §8.5 step 2: "NULL pattern or message → false"
PRD §12: "NULL inputs → false"
PRD §13 robustness requirements

### Impact

- **Production:** LATENT — `match_pattern` is only called from `process_full_message`
  with `received_command` (non-NULL local buffer) and `cmd_map[i].pattern` /
  `lyr_map[i].pattern` (non-NULL string literals from `DEFINE_*` macros)
- **Defensive gap:** Any future caller passing NULL pattern would crash

### Fix

Move the NULL guard ABOVE the `find_first_delimiter(pattern)` call:
```c
bool match_pattern(const char *pattern, const char *message, bool case_sensitive) {
    if (message == NULL || pattern == NULL) {
        return false;
    }
    const char *pattern_delimiter = find_first_delimiter(pattern);
```

---

## DRIFT-1: notifier.h GS_DELIMITER comment is wrong [LOW]

**Location:** `notifier.h` line 22

### The Issue

```c
#define GS_DELIMITER "\x1D"  // ASCII 31 (Unit Separator)
```

`0x1D` = decimal **29** = **Group Separator (GS)**.
"ASCII 31 (Unit Separator)" describes `0x1F` (US), not `0x1D`.

### Impact

Byte value `0x1D` is correct and authoritative. Only the comment is wrong.
Same incorrect "unit delimiter" wording also at `notifier.c:116` and `notifier.c:301`.

### Fix

```c
#define GS_DELIMITER "\x1D"  // ASCII 29 (Group Separator)
```

PRD §5.3 documents this explicitly: "the byte value 0x1D (29 = GS) is authoritative."

---

## DRIFT-2: PRD §11.2C acceptance gate test #3 annotation wrong [MEDIUM, DOC-ONLY]

**Location:** PRD §11.2C, line 883

### The Issue

```c
printf("%d\n", pattern_match("^\\w+@\\w+$","user_host",1));            /* 1 */
```

The code returns **`0`** (correct), but the PRD annotation says `/* 1 */`.

`^\w+@\w+$` requires a literal `@` between two `\w+` runs. `user_host` has no `@`
(underscore is a word char but `@` is not). So `0` (no match) is **semantically correct**.

### Impact

PRD §11.2C says "all six must print 1" but the third correctly prints 0.
This is a **spec error, not a code bug**. The code is right.

### Verified

```
pattern_match("^\\w+@\\w+$", "user@host", 1)  →  1  (correct)
pattern_match("^\\w+@\\w+$", "user_host", 1)  →  0  (correct — no @ in input)
```

---

## DRIFT-3: PRD §4.4 "ack silently dropped" claim may be inaccurate [LOW, INFORMATIONAL]

### The Issue

PRD §4.4: "QMK's host-side raw_hid_send consumer currently requires length ==
RAW_EPSIZE and silently drops this ack."

QMK researcher found that qmk-notifier passes `RAW_REPORT_SIZE = 32` to
`raw_hid_send`, which passes all platform guards (LUFA/ChibiOS check
`RAW_EPSIZE = 32`; V-USB checks `RAW_BUFFER_SIZE = 32`). **The ack IS sent.**

The guard only drops responses with length ≠ 32. Since the code always passes 32,
the ack is never dropped.

### Impact

Informational only — does not affect implementation. The desktop app may not
read the IN endpoint, so "the desktop does not rely on it" may still be true
for a different reason.

---

## RISK-1: notifier.c F4/dispatcher logic has ZERO host-side test coverage [MEDIUM]

### The Issue

`run_all_tests.sh` compiles all 9 test suites linking **only** `pattern_match.c`.
No test exercises `notifier.c` (it `#include`s `QMK_KEYBOARD_H`, preventing
standalone compilation). Consequently:

- F4 delimiter-aware matching (`match_pattern`) — UNTESTED by automated suite
- `process_full_message` ordering invariants — UNTESTED
- `hid_notify` reassembly logic — UNTESTED
- `sanitize_string` — UNTESTED

### Mitigation

The scout agent stub-compiled `notifier.c` and verified F4.1–F4.4 empirically
(all correct), but this is not part of the shipped test suite.

### Recommendation

A host-testable build (e.g., extract `match_pattern`/`process_full_message` behind
a `-DNOTIFIER_TEST` shim, or add a stub harness) would close the gap.

---

## RISK-2: -DNOTIFIER_STUB is vestigial [TRIVIAL]

**Location:** `run_all_tests.sh` line 19

`test_comprehensive_integration.c` is built with `-DNOTIFIER_STUB` but the macro
is referenced nowhere in any source file. PRD §3 acknowledges this ("currently
vestigial … may be passed harmlessly").

---

## NFA Algorithm Validation (ALL CONFIRMED ✅)

1. Thompson construction (State pool with OP_CHAR/OP_ANY/OP_SPLIT/OP_ASSERT/OP_MATCH) — ✅
2. OP_SPLIT for epsilon-splitting (alternation/quantifiers) — ✅
3. Two-list simulation (clist/nlist) with generation tag deduplication — ✅
4. O(states × input_length) with no catastrophic backtracking — ✅
   - Pathological `a+a+a+a+a+a+a+a+a+a+b` vs 199×`a`: 1.6ms (< 50ms gate)
5. Generation tag (lastlist == nfa_gen) prevents infinite recursion — ✅
6. `\b`/`\B` word-boundary OP_ASSERT in epsilon-closure — ✅ valid approach
7. `\B` on empty string returns false (legacy semantics) — ✅ (very low severity semantic note)

## QMK Integration Validation (ALL 8 CLAIMS CONFIRMED ✅)

| # | Claim | Status |
|---|---|---|
| 1 | RAW_ENABLE → usage page 0xFF60, usage 0x61 | ✅ `usb_descriptor_common.h` |
| 2 | 32-byte reports for receive/send | ✅ `raw_hid.h` + 3 protocol impls |
| 3 | V-USB 8-byte→32-byte reassembly | ✅ `vusb.c` |
| 4 | Length guard = 32 on all platforms | ✅ (V-USB uses RAW_BUFFER_SIZE, not RAW_EPSIZE) |
| 5 | layer_on/layer_off exist | ✅ `action_layer.h` |
| 6 | SRC += in rules.mk | ✅ `build_keyboard.mk` |
| 7 | QMK_KEYBOARD_H + raw_hid.h includes | ✅ `-D` flag + `quantum/raw_hid.h` |
| 8 | __attribute__((weak)) works | ✅ pervasive in QMK source |

## Current Test Results

- **Total assertions:** 1826
- **Passing:** 1826 (100%)
- **Performance:** ~0.1 µs per pattern_match call
- **Pathological NFA:** ~1.6ms (gate: < 50ms)
