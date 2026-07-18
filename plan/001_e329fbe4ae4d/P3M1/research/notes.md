# P3.M1 Research Notes — Acceptance Gate & Documentation

Research method: **ran the actual gate** (not read-only guessing). Every number
below is a live measurement from this working tree on this machine.

---

## 1. ACCEPTANCE GATE STATE — ALL GREEN ✅ (blocker resolved)

The previous P3 attempt (see `plan/001_e329fbe4ae4d/P3/issue_feedback.md`)
HALTED because `test_memory_stress`'s "Anchored huge pattern exact match" failed
(2018/2019). **That P1 blocker is now RESOLVED** — the same case passes today.

### 11.2A — 9-suite gate (`./run_all_tests.sh`)
```
Total tests run across all suites: 2019
Total tests passed: 2019
Total tests failed: 0
Overall success rate: 100.0%
exit 0  ✓ VERIFICATION SUCCESSFUL
```

Per-suite (live, fails=0 everywhere):

| Suite | Count | Notes |
|---|---|---|
| test_pattern_match | 376 | main suite |
| test_char_classification | 179 | |
| test_word_boundary_basic | 74 | prints "Tests run:" (not "Total tests run:") |
| test_word_boundary_integration | 189 | prints "Tests run:" |
| test_metachar_verification | — | smoke test, boolean PASS/FAIL (no numeric count) |
| test_comprehensive_integration | 10 | "Test categories passed: 10" (not parsed into aggregate) |
| test_error_handling | 161 | |
| test_memory_stress | 32 | incl. "Anchored huge pattern exact match" → PASS |
| test_invalid_patterns | 1008 | |
| **aggregate parsed** | **2019** | 376+179+74+189+161+32+1008 |

### 11.2B — pathological NFA stress (`/tmp/nfa_stress`)
```
pattern = a+a+a+a+a+a+a+a+a+a+b   input = 199×'a'
result=0  1807.0 us   (gate: < 50_000 us)   ✅ PASS
```

### 11.2C — realistic patterns (`/tmp/nfa_real`, DRIFT-2-corrected)
With input `user@host` (NOT the PRD's literal `user_host` — see DRIFT-2):
```
1   \w+ vs hello
1   \b\w+\b\s+\b\w+\b vs "hello world"
1   ^\w+@\w+$ vs user@host        ← corrected input
1   v\.code vs v.code
1   a+b vs aaab
1   *slack* vs "Slack - general"  (case-insensitive)
```
All six = 1. ✅ PASS

### P2 notifier stub gate (`./run_notifier_stub_tests.sh`) — RISK-1 closure
```
Total tests run: 11 / passed: 11 / failed: 0
notifier dispatch fails=0  exit=0   ✓ notifier stub-compile gate PASSED
```
Exercises notifier.c (F4 delimiter matching, dispatcher ordering, hid_notify
reassembly, sanitize, ack) via `qmk_stubs/`. This is shipped infra, not just P2.

### No-new-warnings (11.2D)
- `pattern_match.c` with `-Wall -Wextra -std=c99`: **clean** (0 warnings)
- `notifier.c` stub-compiled with `-Wall -Wextra -std=c99`: **clean** (0 warnings)

### Performance
`run_all_tests.sh` perf micro-benchmark: `0.104533 µs` per pattern_match call
(~0.1 µs). Matches PRD §12 "~0.1 µs".

---

## 2. KEY NUANCE — LIVE COUNT IS 2019, NOT THE PRD's 1826

PRD §11.3 states "~1 826 assertions" as the total, then immediately says:
> "the live counts above are authoritative — `./run_all_tests.sh` prints the real totals."

The PRD's own 1826 figure is **itself stale**. The live aggregate today is **2019**.
➡ The README MUST reflect the LIVE number from `./run_all_tests.sh` (2019),
   NOT a hard-coded "1826" copied from the PRD. Capture live, then write.

Per-suite live breakdown (authoritative for the README "Running Tests" table):
376 / 179 / 74 / 189 / smoke / 10-categories / 161 / 32 / 1008.

---

## 3. DRIFT-2 — PRD §11.2C test #3 annotation is WRONG (code is right)

PRD §11.2C line: `pattern_match("^\\w+@\\w+$","user_host",1)  /* 1 */`
- The code returns **0** (correct) — `user_host` has no `@` char.
- The PRD's `/* 1 */` annotation is wrong.
- Verified: `^\w+@\w+$` vs `user@host` → 1 (correct); vs `user_host` → 0 (correct).
- ➡ Acceptance gate 11.2C MUST use input `user@host`, NOT `user_host`.
- This is a DOC drift in the PRD; the PRP/README note it; do NOT change code.

## 4. DRIFT-1 + BUG-1 — ALREADY FIXED IN CODE

- DRIFT-1 (notifier.h GS_DELIMITER comment): FIXED. `notifier.h:22` now reads
  `#define GS_DELIMITER "\x1D"  // ASCII 29 (Group Separator)`. notifier.c:116
  also correct ("group separator (GS, 0x1D)").
- BUG-1 (match_pattern NULL guard too late): FIXED. NULL guard now precedes
  `find_first_delimiter(pattern)` with an explicit comment referencing BUG-1.
  Verified by stub gate PASS "match_pattern(NULL,...) = false (no segv)".
➡ No code changes needed for these. Only README documentation remains.

---

## 5. README.md — STALE SECTIONS TO FIX (sole remaining artifact)

Located at repo root `README.md`. Concrete defects found:

### (a) "Running Tests" — stale per-suite counts
```
- Main pattern match tests (383 tests)        ← live 376
- Character classification tests (179 tests)  ← correct
- Word boundary tests (263 tests)             ← live split: basic 74 + integration 189
...                                           ← MISSING: test_error_handling (161),
                                                test_memory_stress (32), test_invalid_patterns (1008),
                                                test_metachar_verification (smoke), notifier stub gate (11)
```

### (b) "Current Test Status" — stale aggregate + phantom file
```
**Overall Test Results**: 1,992/2,048 tests passing (97.2% success rate)   ← live 2019/2019 (100%)
**Backward Compatibility**: 100% (65/65 original functionality tests passing) ← stale, remove
**Performance Impact**: Negligible (0.087 microseconds per operation)        ← live ~0.1 µs
⚠️ **Some advanced feature edge cases** - documented in `backward_compatibility_report.md`  ← FILE DOES NOT EXIST; remove
```

### (c) "Pattern Matching Syntax" — incomplete (only 3 of ~13 constructs)
Current lists only `*`, `^`, `$`. Must add the full PRD §15 construct table:
`\d \D`, `\w \W`, `\s \S`, `\b \B`, `.`, `X+`, escapes (`\^ \$ \* \\ \. \+`),
and the "no anchors ⇒ substring" default.

### (d) GS_DELIMITER — NOT documented in README at all
README never names the `class\x1Dtitle` delimiter or its byte value.
Must add: message format is `<app_class>\x1D<window_title>` where
`GS_DELIMITER = "\x1D"` = ASCII 29 = **Group Separator** (NOT "Unit Separator").
(PRD §4.1, §5.3, §16.)

### (e) Setup — minor: submodule command missing target dir
`git submodule add https://github.com/dabstractor/qmk-notifier.git`
→ add `qmk-notifier` target dir (PRD §10.1: `...git qmk-notifier`).
rules.mk `include` line and keymap.c `raw_hid_receive → hid_notify` delegation
already match PRD §10.1.

### (f) Companion Projects — align with PRD §1.2 ecosystem
Per PRD §1.2:
- `qmk_notifier` (underscore, dabstractor/qmk_notifier) = Rust **transport crate**
  (owns wire framing: magic header, 32-byte chunking, ETX, device cache) — NOT "the app".
- `QMKonnect` (dabstractor/qmkonnect) = Rust cross-platform **desktop daemon**
  (detects foreground window, sends `class\x1Dtitle`).
Current README mislabels `qmk_notifier` as "Rust application that captures window
information" (that's QMKonnect's job now) and omits QMKonnect. The community tools
(hyprland-qmk-window-notifier, zigotica) are legitimate third-party alternatives — keep.

---

## 6. SCOPE BOUNDARY (from prior attempt + tasks.json)

P3.M1 is two subtasks:
- **P3.M1.T1.S1** — validate acceptance gate 11.2A/B/C (+ invariants §13, no warnings).
  READ-ONLY. Capture evidence. Document DRIFT-2. No code changes.
- **P3.M1.T2.S1** — update README.md (the ONLY file P3 may modify).

FORBIDDEN for P3: editing test_*.c, pattern_match.{c,h}, notifier.{c,h}, rules.mk,
run_all_tests.sh, PRD.md, tasks.json. (Gate is green, so no code edits are needed.)

The gate is GREEN → P3 can complete fully. The prior halt was a stale condition.
