# PRP — P1.M2.T1.S1: Change `sanitize_string` signature to accept length and fix the iteration loop

## Goal

**Feature Goal**: Fix `sanitize_string` (notifier.c:46–69) so an embedded NUL
(`0x00`) byte is **stripped** (per PRD F2.3 / §8.2: "strip every other byte")
instead of **truncating** the scan at the first NUL. The current
`while (*read_ptr)` loop stops at the first `0x00` even though the allowlist
correctly excludes it. Change the signature to take an explicit length and bound
the loop by that length; update the sole caller (`hid_notify`) to pass the byte
count. This is the code fix; the formal embedded-NUL regression test is the next
subtask (P1.M2.T2.S1).

**Deliverable**: The modified file `notifier.c` at the repo root, changed in
exactly TWO regions: (1) the `sanitize_string` definition (signature + loop),
and (2) the one call site in `hid_notify`. The allowlist condition, the NULL
guard, and the NUL-termination are preserved byte-for-behavior. No other file
changes.

**Success Definition**:
- `sanitize_string(char *str, size_t len)` reads `len` bytes via an index loop
  `for (size_t i = 0; i < len; i++)`; a `0x00` is NOT in the allowlist so it is
  skipped (stripped), and bytes after it are reached.
- The sole caller passes `sanitize_string(msg_buffer, (size_t)msg_index)`; the
  now-redundant `msg_buffer[msg_index] = '\0'` is removed (sanitize owns
  NUL-termination).
- `notifier.c` stub-compiles with `-Wall -Wextra -std=c99` → **0 warnings**.
- `./run_notifier_stub_tests.sh` prints **"✓ notifier stub-compile gate PASSED"**;
  `test_notifier_dispatch` 11/11 + `test_notifier_os` 31/31, **0 `FAIL:`** each.
- The diff is confined to the two regions (no other code changed).

## User Persona (if applicable)

**Target User**: (1) The transport robustness guarantee — a malformed/hostile
HID frame (or USB corruption) that injects a NUL before ETX must not silently
truncate the dispatched message. (2) The next subtask (P1.M2.T2.S1) which adds
the formal regression test to `test_notifier_dispatch.c` driving this through
`hid_notify`.

**Use Case**: A report arrives whose payload contains an embedded `0x00` (not a
real-transport scenario today, but a defense-in-depth guard). `hid_notify`
accumulates the bytes; at ETX, `sanitize_string(msg_buffer, msg_index)` strips
the NUL and preserves the surrounding valid bytes; `process_full_message`
dispatches the intact message.

**User Journey**: `hid_notify` reassembles payload into `msg_buffer` (msg_index
counts bytes) → at ETX calls `sanitize_string(msg_buffer, (size_t)msg_index)`
(strips NUL/non-ASCII in place, NUL-terminates at write_ptr) →
`process_full_message(msg_buffer)`.

**Pain Points Addressed**: The `while (*read_ptr)` idiom conflated "string end"
with "byte to filter," so a NUL — which the allowlist already says to strip —
instead terminated the scan. The length-bounded loop makes "filter these many
bytes" and "where does the string end" distinct concerns, matching PRD F2.3.

## Why

- **Aligns code with PRD F2.3 / §8.2 intent**: the spec says "strip every other
  byte"; `0x00` is "every other byte." Today the loop never strips it — it
  truncates. This makes the code do what the spec already requires.
- **Defense-in-depth**: the bug is unreachable via the spec'd transport (ETX is
  appended before zero-fill; the desktop sends printable ASCII + GS), but a
  malformed/hostile frame or USB corruption could inject a NUL. Stripping (not
  truncating) is the robust behavior and removes a latent surprise.
- **Contained, surgical, low-risk**: `sanitize_string` is `static` with exactly
  ONE caller (verified by grep); it is not in any header. The signature change
  ripples only to `hid_notify`. Both stub suites prove no regression.
- **Unblocks the formal test**: P1.M2.T2.S1 will add the embedded-NUL regression
  case to `test_notifier_dispatch.c`; this fix is its prerequisite (without it,
  the test would fail).

## What

Two edits to `notifier.c`:

1. **`sanitize_string` (lines 46–69)**: change the signature to
   `static void sanitize_string(char *str, size_t len)`; replace
   `while (*read_ptr) { … read_ptr++; }` with
   `for (size_t i = 0; i < len; i++) { char c = str[i]; … }`. KEEP the allowlist
   condition (0x20–0x7E, 9, 10, 13, GS_DELIMITER[0], ETX_TERMINATOR[0]) exactly;
   KEEP the NULL guard `if (!str) return;`; KEEP `*write_ptr = '\0'` at the end.
   Drop the now-unused `read_ptr` variable (read via `str[i]`); keep `write_ptr`
   (in-place compaction).
2. **Call site (`hid_notify`, lines 492–495)**: change
   `sanitize_string(msg_buffer)` to `sanitize_string(msg_buffer, (size_t)msg_index)`
   and remove the preceding `msg_buffer[msg_index] = '\0';` (sanitize now
   NUL-terminates internally at write_ptr ≤ str + msg_index).

### Success Criteria

- [ ] Signature is `static void sanitize_string(char *str, size_t len)`.
- [ ] Loop is `for (size_t i = 0; i < len; i++)` reading `str[i]` (or `*read_ptr`
      with explicit advancement — contract §3b permits either).
- [ ] Allowlist condition byte-for-byte identical to the original (0x20–0x7E /
      9 / 10 / 13 / GS / ETX).
- [ ] NULL guard `if (!str) return;` retained; `*write_ptr = '\0'` retained.
- [ ] Sole call site is `sanitize_string(msg_buffer, (size_t)msg_index)`; the
      redundant `msg_buffer[msg_index] = '\0';` removed.
- [ ] `notifier.c` stub-compiles `-Wall -Wextra -std=c99` → 0 warnings.
- [ ] `run_notifier_stub_tests.sh` → "✓ notifier stub-compile gate PASSED";
      dispatch 11/11 + os 31/31, 0 FAIL.
- [ ] Diff confined to the two regions (git diff shows only those).
- [ ] No edits to notifier.h, pattern_match.*, qmk_stubs/*, test_notifier_*,
      run_*.sh, PRD.md, tasks.json, rules.mk, .gitignore.

## All Needed Context

### Context Completeness Check

**Pass.** The exact target bodies for BOTH edits are given verbatim below
("Implementation Tasks") and were **empirically validated during research** by
applying them to a /tmp copy of notifier.c: stub-compiles with 0 warnings; an
embedded-NUL HID report through `hid_notify` now has the post-NUL portion
survive (fired=1) where the original truncated it (fired=0); the empty-payload
edge (msg_index==0) NUL-terminates safely without crashing; and both stub suites
(dispatch 11/11, os 31/31) pass against the fixed file with 0 FAIL. An
implementer with only this PRP + repo access can make the two edits and prove
them green.

### Documentation & References

```yaml
# MUST READ — the bug + the fix approach
- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/architecture/findings_and_risks.md
  section: "## Issue 2: sanitize_string NUL Stripping Fix"
  why: "Documents the bug (while(*read_ptr) truncates at NUL), the fix approach
        (signature + length-bounded loop), the call-site change, the spec drift
        (PRD §8.2 documents sanitize_string(char*)), and the test approach
        (P1.M2.T2.S1, via hid_notify)."
  critical: "Risk MEDIUM = the spec drift (PRD §8.2 says sanitize_string(char*)).
             It is contained (static, one caller, no header). Document it; do NOT
             'fix' PRD.md (READ-ONLY). The allowlist is correct — only the LOOP
             BOUND is wrong."

# MUST READ — the intended filter semantics
- file: PRD.md
  section: "### 8.2 sanitize_string(char *str) — in-place ASCII filter"  +  "F2.3 (§2)"
  why: "The allowlist contract: keep byte b iff
        (b>=32 && b<=126) || b==9 || b==10 || b==13 || b==0x1D || b==0x03;
        'strip every other byte' (F2.3). 0x00 is 'every other byte' → strip it."
  critical: "§8.2 documents the OLD signature sanitize_string(char*) — this is
             the documented spec drift. Do not edit PRD.md; just note the drift
             (already in findings_and_risks.md). The NEW signature is
             sanitize_string(char*, size_t)."

# The file being modified — exact anchors + the function body to replace
- file: notifier.c
  section: "sanitize_string definition (lines 46–69) + hid_notify call site (lines 492–495)"
  why: "Line 46 = the signature to change; line 52 = while(*read_ptr) to replace;
        lines 55–60 = the allowlist to PRESERVE; line 68 = *write_ptr='\\0' to KEEP.
        Lines 492–495 = the call site: drop msg_buffer[msg_index]='\\0'; pass msg_index."
  pattern: "In-place compaction idiom: a write_ptr that advances only on allowlist
            pass, then *write_ptr='\\0'. Keep write_ptr; the read cursor becomes the
            loop index i over str[i]."
  gotcha: "sanitize_string is STATIC and has EXACTLY ONE caller (hid_notify:495).
           grep confirms 0 hits in any header. So the signature change cannot break
           anything outside notifier.c."

# The build/test gate to satisfy
- file: run_notifier_stub_tests.sh
  why: "The acceptance gate: object-compiles notifier.c, links test_notifier_dispatch
        AND test_notifier_os, runs both, asserts 0 FAIL. Must print '✓ notifier
        stub-compile gate PASSED'."
  critical: "The [1/3] stub-compile uses -Wall -Wextra; the fixed notifier.c must
             produce 0 warnings (size_t/for-loop are clean C99). The [2/3] link
             step uses -Iqmk_stubs -I. — already correct in this runner."

# No conflict with the parallel task
- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/P1M1T1S1/PRP.md
  why: "P1.M1.T1.S1 adds @-literal regression cases to test_pattern_match.c
        (matcher file, test corpus). It does NOT touch notifier.c or sanitize_string.
        grep confirms: 0 references to notifier.c/sanitize_string in that PRP."
  critical: "No file overlap. This task edits only notifier.c; that task edits only
             test_pattern_match.c. They can land independently/in parallel."

# The next subtask (NOT this task — for boundary clarity)
- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/architecture/findings_and_risks.md
  section: "Issue 2 → Test approach"
  why: "P1.M2.T2.S1 adds the embedded-NUL regression test to test_notifier_dispatch.c
        (drive hid_notify with a NUL-bearing report, assert post-NUL pattern matches).
        This task is the CODE FIX; that task is the TEST. Do not add the test here."
```

### Current Codebase tree (relevant slice)

```bash
notifier.c                 # ← MODIFY (2 regions: sanitize_string + hid_notify call site). NOTHING ELSE.
notifier.h                 # unaffected (sanitize_string is NOT here). DO NOT TOUCH.
pattern_match.{c,h}        # unaffected. DO NOT TOUCH.
qmk_stubs/                 # unaffected. DO NOT TOUCH.
test_notifier_dispatch.c   # backward-compat canary (11/11). DO NOT TOUCH (P1.M2.T2.S1 adds the NUL test here).
test_notifier_os.c         # OS suite (31/31). DO NOT TOUCH.
run_notifier_stub_tests.sh # the gate. DO NOT TOUCH.
run_all_tests.sh           # 9-suite pattern_match corpus — unaffected. DO NOT TOUCH.
PRD.md                     # READ-ONLY (§8.2 documents the OLD signature — the spec drift).
```

### Desired Codebase tree with files to be changed

```bash
notifier.c                 # MODIFIED: sanitize_string (len param + index loop) + hid_notify call site.
# (no new files; no header changes)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — sanitize_string is STATIC with exactly ONE caller (hid_notify:495).
//   grep confirms 0 hits in any header (notifier.h / pattern_match.h / qmk_stubs/*.h).
//   So changing the signature ripples ONLY to hid_notify. Do not add it to any
//   header (it is internal). This is why the spec drift is contained.

// CRITICAL — the bug is the LOOP BOUND, not the filter. The allowlist (lines
//   55–60) already EXCLUDES 0x00 (it is < 32 and not 9/10/13/GS/ETX). The only
//   defect is `while (*read_ptr)` stopping at 0x00 before the allowlist can
//   reject it. Fix the loop (bound by len); DO NOT touch the allowlist.

// CRITICAL — keep the in-place compaction (write_ptr advances only on allowlist
//   pass). Drop read_ptr (read str[i]); KEEP write_ptr. Removing write_ptr would
//   require a different (slower or buffer-copying) algorithm — keep the idiom.

// GOTCHA — the redundant `msg_buffer[msg_index] = '\0'` at the call site CAN be
//   removed because sanitize_string now NUL-terminates at write_ptr (<= str+len).
//   strlen in process_full_message finds sanitize's NUL. KEEPING it is harmless
//   (redundant) but REMOVING it is cleaner (sanitize owns termination). The
//   contract recommends removal. (Verified: both stub suites pass with it removed.)

// GOTCHA — len==0 is SAFE with no special-case guard: 0 iterations → write_ptr
//   unchanged (==str) → `*write_ptr = '\0'` NUL-terminates at str[0]. Verified
//   empirically (empty-payload HID report dispatches an empty string, no crash).
//   The contract (§3d) permits letting the loop handle it.

// GOTCHA — msg_index is always valid (0..MSG_BUFFER_SIZE-1) at the sanitize call
//   site: the overflow path resets msg_index AND sets `dropping`, and the
//   `if (!dropping)` guard wraps the sanitize call. So passing (size_t)msg_index
//   never over-reads. (size_t) cast is explicit (msg_index is uint16_t).

// GOTCHA — do NOT run the "normal build" to validate; notifier.c cannot compile
//   standalone (it #includes QMK_KEYBOARD_H). Validate with the STUB build:
//   run_notifier_stub_tests.sh (or the -DQMK_KEYBOARD_H=... -Iqmk_stubs command).

// GOTCHA — the formal embedded-NUL test is P1.M2.T2.S1 (next subtask, adds to
//   test_notifier_dispatch.c). This task proves NO REGRESSION only (the existing
//   11+31 cases still pass). Do not add the test here.
```

## Implementation Blueprint

### Data models and structure

No new types. `sanitize_string` gains one parameter (`size_t len`); the body
swaps a NUL-terminated read loop for a length-bounded index loop. `hid_notify`'s
call passes `(size_t)msg_index`. No new `#include` (`size_t` is in scope via
`<string.h>`, already included by notifier.c).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — REWRITE sanitize_string (signature + loop)
  - LOCATE: `grep -n 'static void sanitize_string' notifier.c` → line 46.
  - REPLACE the ENTIRE function (lines 46–69, from `static void sanitize_string(char *str) {`
    through the closing `}`) with the "Exact target body — sanitize_string" block
    below (byte-exact, validated).
  - The new body, IN ORDER: signature with `size_t len`; NULL guard; declare
    `write_ptr = str`; `for (size_t i = 0; i < len; i++)` reading `char c = str[i]`;
    allowlist condition (UNCHANGED) advancing write_ptr on pass; `*write_ptr = '\0'`.
  - PRESERVE: the allowlist condition byte-for-byte; the NULL guard; the
    NUL-termination. DROP the `read_ptr` variable (read via str[i]).
  - DO NOT touch any other function.

Task 2: MODIFY notifier.c — UPDATE the hid_notify call site
  - LOCATE: `grep -n 'sanitize_string(msg_buffer)' notifier.c` → line 495
    (inside hid_notify's `if (!dropping)` ETX branch).
  - REPLACE the two-line block:
        msg_buffer[msg_index] = '\0'; // Ensure the buffer is properly terminated
        <comment>
        sanitize_string(msg_buffer);
    with:
        <comment>
        sanitize_string(msg_buffer, (size_t)msg_index);
    (exact text in "Exact target body — call site" below).
  - PRESERVE: the surrounding `if (!dropping) { … }` structure, the
    `match = process_full_message(msg_buffer);` line after, the `msg_index = 0;`
    reset after the branch. Only the two lines (terminator + call) change.
  - DEPENDENCIES: Task 1 (new signature); msg_index (existing uint16_t global).

Task 3: VERIFY (no edit) — compile + no-regression
  - Run Validation Level 1 (stub-compile; 0 warnings).
  - Run Validation Level 2 (run_notifier_stub_tests.sh → dispatch 11/11 + os 31/31,
    0 FAIL; "✓ notifier stub-compile gate PASSED").
  - Run Validation Level 3 (git diff confined to the 2 regions).
  - Run Level 4 (optional embedded-NUL smoke — proves the fix end-to-end through
    hid_notify; the formal test is P1.M2.T2.S1).
```

**Exact target body — `sanitize_string` (byte-exact, validated in research):**

```c
// Function to sanitize strings by removing non-ASCII characters
// This prevents Unicode decode errors when the data is processed by the Python CLI.
// Iterates by explicit length (PRD F2.3) so an embedded NUL (0x00) — which is not
// in the allowlist — is STRIPPED rather than truncating the scan at the first NUL.
static void sanitize_string(char *str, size_t len) {
    if (!str) return;

    char *write_ptr = str;

    // Length-bounded scan: read exactly `len` bytes. A NUL (0x00) is < 32 and is
    // not 9/10/13/GS/ETX, so it fails the allowlist and is skipped (stripped),
    // letting subsequent valid bytes through. (Previously `while (*read_ptr)`
    // stopped at the first NUL, truncating instead of stripping — bug §Issue 2.)
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        // Only allow printable ASCII (32-126) + essential control chars (9,10,13)
        // + our delimiter (GS) and terminator (ETX).
        if ((c >= 32 && c <= 126) ||
            c == 9 ||   // tab
            c == 10 ||  // newline
            c == 13 ||  // carriage return
            c == GS_DELIMITER[0] ||  // group separator (our delimiter)
            c == ETX_TERMINATOR[0]) { // end of text (our terminator)
            *write_ptr++ = c;
        }
        // Skip every other byte (incl. NUL 0x00, >127, other control chars).
    }

    // Null terminate the sanitized string at the write pointer (<= str + len).
    *write_ptr = '\0';
}
```

**Exact target body — call site in `hid_notify` (byte-exact, validated):**

```c
                // Sanitize the buffer in place, iterating by explicit length so an
                // embedded NUL is stripped (PRD F2.3) rather than truncating the scan.
                // sanitize_string NUL-terminates at write_ptr (<= str + msg_index).
                sanitize_string(msg_buffer, (size_t)msg_index);
```

> The block being replaced at the call site is exactly:
> ```c
>                 msg_buffer[msg_index] = '\0'; // Ensure the buffer is properly terminated
>                 
>                 // Sanitize the buffer to remove non-ASCII characters that could cause decode errors
>                 sanitize_string(msg_buffer);
> ```
> (the trailing-whitespace line between the terminator and the comment is part of
> the match — use the editor on the real file, matching its exact bytes).

### Implementation Patterns & Key Details

```c
// PATTERN: length-bounded in-place compaction. write_ptr starts at str and
//   advances ONLY when a byte passes the allowlist; the index i drives the read.
//   At the end *write_ptr='\0' marks the compacted string's end. write_ptr is
//   always <= str + len, so the NUL is written within bounds.

// PATTERN: the (size_t) cast on msg_index at the call site is explicit (msg_index
//   is uint16_t; the param is size_t). Harmless widening; silences any sign/width
//   conversion warning under -Wall -Wextra.

// ANTI-PATTERN: do NOT change the allowlist. The fix is the LOOP BOUND only. The
//   0x20-0x7E / 9 / 10 / 13 / GS / ETX condition is correct and must stay identical.

// ANTI-PATTERN: do NOT keep read_ptr "to minimize the diff" — it is now dead
//   (the index i reads str[i]). Drop it; read via str[i]. Keeping a read_ptr that
//   is incremented but never dereferenced for the loop test is confusing.

// ANTI-PATTERN: do NOT add a special-case `if (len == 0) { *str = '\0'; return; }`.
//   The 0-iteration loop already leaves write_ptr==str, so *write_ptr='\0' handles
//   it. Verified empirically (empty-payload report, no crash). Extra guards add
//   noise without benefit.

// ANTI-PATTERN: do NOT edit PRD.md to "fix" the §8.2 signature (it documents
//   sanitize_string(char*)). PRD.md is READ-ONLY; the drift is documented in
//   findings_and_risks.md §Issue 2 and is contained (static, one caller).

// ANTI-PATTERN: do NOT add the embedded-NUL regression test here — that is
//   P1.M2.T2.S1 (test_notifier_dispatch.c). This task is the code fix + prove
//   no-regression only.

// ANTI-PATTERN: do NOT touch any file other than notifier.c.
```

### Integration Points

```yaml
SANITIZE_STRING (rewrite):
  - file: notifier.c (lines 46–69)
  - change: signature += size_t len; while(*read_ptr) -> for(i<len) str[i]; drop read_ptr.
  - invariant: allowlist, NULL guard, *write_ptr='\0' all preserved.
CALL SITE (hid_notify):
  - file: notifier.c (lines 492–495, inside `if (!dropping)` ETX branch)
  - change: drop msg_buffer[msg_index]='\0'; pass (size_t)msg_index.
  - invariant: surrounding !dropping guard, process_full_message call, msg_index=0 reset.
LINKAGE:
  - sanitize_string is static, one caller, not in any header. No external ripple.
BUILD / CONFIG / DATABASE / ROUTES:
  - none. No rules.mk edit, no new files, no includes. Pure C edit in notifier.c.
```

## Validation Loop

> Toolchain: gcc (C project — no ruff/mypy/pytest). All commands were
> **executed during research** against a /tmp copy of notifier.c with both fixes
> applied and **PASSED**: 0 warnings; embedded-NUL survives (fired=1) where the
> original truncated (fired=0); empty-payload safe; both stub suites 0 FAIL.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Stub-compile notifier.c with -Wall -Wextra (mirrors run_notifier_stub_tests.sh [1/3]).
gcc -Wall -Wextra -std=c99 \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier.o
# Expected: rc=0, ZERO warnings. (size_t + for-index loop are clean C99.)
test -f /tmp/notifier.o && echo "✓ notifier.c compiles (object present)"
echo "warnings: $(gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='\"qmk_keyboard_stub.h\"' -Iqmk_stubs -I. -c notifier.c -o /tmp/notifier.o 2>&1 | grep -c 'warning:')  (expect 0)"
rm -f /tmp/notifier.o

# 1b. Confirm the signature + loop changed; the allowlist did NOT.
grep -n 'static void sanitize_string(char \*str, size_t len)' notifier.c   # expect 1 line
grep -n 'for (size_t i = 0; i < len; i++)' notifier.c                       # expect 1 line (in sanitize_string)
! grep -q 'while (\*read_ptr)' notifier.c && echo "✓ NUL-truncating while(*read_ptr) is GONE"
grep -q 'c == GS_DELIMITER\[0\]' notifier.c && echo "✓ allowlist (GS) preserved"
grep -q 'c == ETX_TERMINATOR\[0\]' notifier.c && echo "✓ allowlist (ETX) preserved"
grep -q '\*write_ptr = .\\0.' notifier.c && echo "✓ NUL-termination preserved"

# 1c. Confirm the call site passes msg_index and the redundant terminator is gone.
grep -n 'sanitize_string(msg_buffer, (size_t)msg_index)' notifier.c         # expect 1 line (hid_notify)
! grep -q 'msg_buffer\[msg_index\] = .\\0.' notifier.c && echo "✓ redundant msg_buffer[msg_index]='\\0' removed"
```

### Level 2: No-Regression (THE PRIMARY GATE)

```bash
cd /home/dustin/projects/qmk-notifier

# The acceptance gate: object-compile + link BOTH notifier suites + run + 0 FAIL.
./run_notifier_stub_tests.sh
# Expected (last lines):
#   notifier dispatch fails=0  (exit=0)
#   notifier os fails=0        (exit=0)
#   ✓ notifier stub-compile gate PASSED
# (test_notifier_dispatch 11/11; test_notifier_os 31/31; both 0 FAIL:)

# Explicit per-suite FAIL counts (belt-and-suspenders):
gcc -Wall -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -o /tmp/td
gcc -Wall -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -o /tmp/tos
echo "dispatch fails=$(/tmp/td 2>/dev/null | grep -c '^FAIL:')  (expect 0)"
echo "os fails=$(/tmp/tos 2>/dev/null | grep -c '^FAIL:')  (expect 0)"
rm -f /tmp/td /tmp/tos
```

### Level 3: Integration Testing (Diff Hygiene)

```bash
cd /home/dustin/projects/qmk-notifier

# 3a. Only notifier.c changed (plus your PRP/research under plan/).
git status --porcelain
# Expected: ` M notifier.c` and `?? plan/002.../bugfix/.../P1M2T1S1/...`. NOTHING ELSE.

# 3b. The diff is confined to the TWO regions (sanitize_string + hid_notify call site).
git diff -- notifier.c
# Expected: hunks ONLY around line 46 (sanitize_string) and ~line 492 (call site).
#           No other function touched; no include changes; no reorder.
```

### Level 4: Creative & Domain-Specific Validation (the fix actually strips NUL)

```bash
cd /home/dustin/projects/qmk-notifier

# End-to-end proof that an embedded NUL is now STRIPPED (not truncated), driven
# through the PUBLIC hid_notify entry. This is the behavior P1.M2.T2.S1 will
# formalize into test_notifier_dispatch.c; here it is a throwaway smoke check.
cat > /tmp/nul_smoke.c <<'EOF'
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"
void hid_notify(uint8_t *data, uint8_t length);
static int suffix_fired = 0;
static void on_en(void) { suffix_fired++; }
/* matches the POST-NUL portion only; if NUL truncated, no match */
DEFINE_SERIAL_COMMANDS({ { "*suffix*", on_en, 0, false } });
int main(void) {
    uint8_t rep[32]; memset(rep, 0, sizeof(rep));
    rep[0]=0x81; rep[1]=0x9F;
    rep[2]='p'; rep[3]='r'; rep[4]='e'; rep[5]=0x00;        /* embedded NUL */
    rep[6]='s'; rep[7]='u'; rep[8]='f'; rep[9]='f'; rep[10]='i'; rep[11]='x';
    rep[12]=0x03;                                            /* ETX */
    hid_notify(rep, 32);
    printf("post-NUL 'suffix' fired=%d (want 1 => NUL STRIPPED)\n", suffix_fired);
    return suffix_fired == 1 ? 0 : 1;
}
EOF
gcc -w -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    notifier.c qmk_stubs/qmk_stubs.c /tmp/nul_smoke.c -o /tmp/nul_smoke
/tmp/nul_smoke; echo "rc=$?  (0 = fix works: NUL stripped, post-NUL bytes survive)"
# Expected: "post-NUL 'suffix' fired=1 (want 1 => NUL STRIPPED)"; rc=0.

# Edge case: empty payload (msg_index==0) must NUL-terminate safely (no crash).
cat > /tmp/empty_smoke.c <<'EOF'
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"
void hid_notify(uint8_t *data, uint8_t length);
int main(void) {
    uint8_t rep[32]; memset(rep, 0, sizeof(rep));
    rep[0]=0x81; rep[1]=0x9F; rep[2]=0x03;   /* just ETX, msg_index stays 0 */
    hid_notify(rep, 32);                      /* sanitize(buf, 0) -> no crash */
    printf("empty-payload OK (no crash)\n");
    return 0;
}
EOF
gcc -w -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    notifier.c qmk_stubs/qmk_stubs.c /tmp/empty_smoke.c -o /tmp/empty_smoke
/tmp/empty_smoke; echo "rc=$?  (0 = len==0 handled safely)"
# Expected: "empty-payload OK (no crash)"; rc=0.
rm -f /tmp/nul_smoke.c /tmp/nul_smoke /tmp/empty_smoke.c /tmp/empty_smoke
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `notifier.c` stub-compiles (`-Wall -Wextra -std=c99`); object present; **0 warnings**.
- [ ] Level 1: signature is `sanitize_string(char *str, size_t len)`; loop is `for (size_t i = 0; i < len; i++)`;
      allowlist (GS/ETX/0x20–0x7E/9/10/13) + NULL guard + `*write_ptr='\0'` preserved.
- [ ] Level 1: call site is `sanitize_string(msg_buffer, (size_t)msg_index)`; redundant
      `msg_buffer[msg_index]='\0'` removed.
- [ ] Level 2: `run_notifier_stub_tests.sh` → "✓ notifier stub-compile gate PASSED";
      dispatch 11/11 + os 31/31, 0 FAIL.
- [ ] Level 3: `git status` shows only `notifier.c` modified (+ plan/ PRP/research);
      diff confined to the two regions.
- [ ] Level 4: embedded-NUL smoke → post-NUL `suffix` survives (fired=1); empty-payload smoke → no crash.

### Feature Validation

- [ ] An embedded `0x00` in `msg_buffer` is stripped (post-NUL bytes survive to dispatch).
- [ ] The allowlist behavior is unchanged for all allowed bytes (printable ASCII, tab/LF/CR, GS, ETX).
- [ ] `sanitize_string` still NUL-terminates at the write pointer.
- [ ] `msg_index`-bounded read never over-reads (overflow path skips sanitize via `dropping`).

### Code Quality Validation

- [ ] Diff confined to the two regions; no restyle/reorder of unrelated code.
- [ ] No dead `read_ptr` left behind (read via `str[i]`); `write_ptr` retained (compaction).
- [ ] No anti-patterns (see below): allowlist untouched, no PRD.md edit, no test added here.
- [ ] No new `#include` (`size_t` already in scope via `<string.h>`).

### Documentation & Deployment

- [ ] Inline comment in `sanitize_string` cites PRD F2.3 and explains the NUL-strip rationale.
- [ ] Inline comment at the call site notes sanitize owns NUL-termination.
- [ ] The spec drift (PRD §8.2 documents the old signature) is recorded in
      architecture/findings_and_risks.md §Issue 2 (already present) — no PRD.md edit.
- [ ] The formal embedded-NUL regression test is deferred to P1.M2.T2.S1 (boundary clear).

---

## Anti-Patterns to Avoid

- ❌ Don't change the allowlist condition — the bug is the LOOP BOUND (`while (*read_ptr)`),
  not the filter. The 0x20–0x7E / 9 / 10 / 13 / GS / ETX condition is correct and stays identical.
- ❌ Don't keep a dead `read_ptr` "to minimize the diff" — read via `str[i]`; drop `read_ptr`.
- ❌ Don't add a special-case `if (len == 0)` guard — the 0-iteration loop + `*write_ptr='\0'`
  already handle it (verified). Extra guards are noise.
- ❌ Don't keep the redundant `msg_buffer[msg_index] = '\0'` at the call site — sanitize now
  owns NUL-termination. (Keeping it is harmless but unclear; the contract recommends removal.)
- ❌ Don't edit PRD.md to "fix" the §8.2 signature — it's READ-ONLY; the drift is documented
  in findings_and_risks.md and is contained (static, one caller, no header).
- ❌ Don't add the embedded-NUL regression test here — that is P1.M2.T2.S1
  (test_notifier_dispatch.c). This task is the code fix + no-regression proof only.
- ❌ Don't run the "normal build" to validate — notifier.c can't compile standalone
  (`#include QMK_KEYBOARD_H`). Use the stub build (`run_notifier_stub_tests.sh`).
- ❌ Don't touch notifier.h, pattern_match.*, qmk_stubs/*, test_notifier_*, run_*.sh,
  PRD.md, tasks.json, prd_snapshot.md, rules.mk, or .gitignore. Only `notifier.c` changes.

---

## Confidence Score: 10/10

The deliverable is a precise two-region edit to `notifier.c`: (1) rewrite
`sanitize_string` (signature += `size_t len`; `while (*read_ptr)` →
`for (size_t i = 0; i < len; i++)` reading `str[i]`; allowlist/NULL-guard/
NUL-termination preserved; `read_ptr` dropped), and (2) update the sole
`hid_notify` call site to pass `(size_t)msg_index` and drop the redundant
pre-sanitize terminator. The exact target bodies (byte-exact, validated) are
given verbatim above and were **empirically validated during research** by
applying both edits to a /tmp copy of notifier.c: stub-compiles with **0
warnings**; an embedded-NUL HID report through `hid_notify` now has the post-NUL
portion survive (**fired=1**) where the original truncated it (**fired=0** — bug
confirmed, fix resolves it); the empty-payload edge (msg_index==0) NUL-terminates
safely with no crash; and both stub suites pass against the fixed file
(**dispatch 11/11, os 31/31, 0 FAIL**; "✓ notifier stub-compile gate PASSED").
`sanitize_string` is `static` with exactly one caller and is not in any header
(verified), so the signature change is fully contained. The diff is surgical
(confirmed: only the two regions). No conflict with the parallel P1.M1.T1.S1
(test_pattern_match.c). The spec drift (PRD §8.2) is documented and requires no
PRD.md edit. The formal embedded-NUL test is explicitly deferred to P1.M2.T2.S1.