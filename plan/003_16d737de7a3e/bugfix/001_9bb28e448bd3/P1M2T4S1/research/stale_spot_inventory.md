# Research Notes — P1.M2.T4.S1 (README changeset sync)

## Method
All facts below were verified by RUNNING the actual test suites on the current
working tree (gcc 16.1.1) and by `grep` of README.md. No counts are assumed from
PRP text — every number is live.

## Changeset dependency chain (this task runs LAST, Mode B)
- P1.M1.T1.S1 — `typed_awaiting_terminator` watchdog in `notifier.c` (DONE)
- P1.M1.T2.S1 — adversarial test cases in `test_notifier_host.c` (DONE)
- P1.M2.T1.S1 — README "Overall Test Results" SET_OS claim fix (DONE)
- P1.M2.T2.S1 — `test_fidelity_nfa128.c` + register in `run_all_tests.sh` (DONE)
- P1.M2.T3.S1 — `has_been_queried` write-only doc comment in `notifier.c` (IN PROG)
- P1.M2.T4.S1 — THIS TASK: broad README sweep (runs last)

## Live test counts (verified by execution — these are the source of truth)
```
$ ./run_all_tests.sh   (10 suites now — test_fidelity_nfa128 is the new 10th)
  Total tests run across all suites: 2029
  Total tests passed: 2029
  Total tests failed: 0
  ✓ ALL TESTS PASSED

  per-suite: test_pattern_match=380, test_char_classification=179,
    test_word_boundary_basic=74, test_word_boundary_integration=189,
    test_metachar_verification=smoke, test_comprehensive_integration=10,
    test_error_handling=161, test_memory_stress=32, test_invalid_patterns=1008,
    test_fidelity_nfa128=6  ← NEW (was 9 suites / 2023 before P1.M2.T2.S1)

$ ./run_notifier_stub_tests.sh
  dispatch 14/14, os 31/31, host 79/79   (host was 64 before P1.M1.T2.S1)
  ✓ notifier stub-compile gate PASSED
```
- `test_fidelity_nfa128` alone: `Total tests run: 6 / passed: 6 / failed: 0`, exit 0.

## STALE README SPOTS (MUST FIX) — exact current text, verified via grep+sed
Only THREE thematic locations are stale; grep confirms no others.

### Spot 1 — line 479 (suite count in "Comprehensive Test Suite")
  Current: `To build and run all 9 host-side suites plus a performance micro-benchmark:`
  Fix:     `9` → `10`

### Spot 2 — line 485 (suite count + table intro)
  Current: `This builds and runs the 9 pattern-matcher suites (all linking \`pattern_match.c\`) and prints an aggregate summary. The live per-suite breakdown:`
  Fix:     `9` → `10`

### Spot 3 — suite table (lines 487-497) is MISSING the 10th row
  The table currently ends with `test_invalid_patterns | 1008 | ...`.
  ADD a row for test_fidelity_nfa128 AFTER test_invalid_patterns:
  `| \`test_fidelity_nfa128\` | 6 | NFA-budget fidelity gate: matcher exercised at the firmware \`NFA_MAX_PATTERN=128\` budget (128-byte boundary fit + over-budget safe-clamp); \`#error\`-guarded so the gate cannot silently drift back to 2048 |`
  (Source: P1.M2.T2.S1 created this suite; count verified = 6.)

### Spot 4 — line 537 (aggregate count + suite count in "Overall Test Results")
  Current: `- Pattern-match corpus (\`./run_all_tests.sh\`, 9 suites): **2023/2023** tests passing.`
  Fix:     `9 suites` → `10 suites`, `2023/2023` → `2029/2029`

## ALREADY ACCURATE (verify only — do NOT restate; P1.M2.T1.S1 owns these)
- `test_notifier_host` count is **79** (line ~506 and ~541). P1.M1.T2.S1 added 15
  adversarial cases (64→79); P1.M2.T1.S1 already bumped the README count.
- SET_OS resolution language (lines 541-547): correctly states the collision is
  resolved by length-aware typed reassembly (`typed_literal_remaining`).
- Line 547 already names "the adversarial typed-command framing gate (Issue 1
  watchdog regression)" — the watchdog recovery IS reflected here.
- `test_notifier_dispatch` 14/14 + `test_notifier_os` 31/31 — unchanged, correct.

## ITEM POINT (C) — robustness/guarantees section: CONDITIONAL → NOT MET
The item says: "IF the README has a robustness/guarantees section discussing
firmware behavior under malformed input, ensure it reflects the new watchdog
recovery." Verified: the README has NO dedicated robustness/guarantee section.
The only malformed-input-robustness mention is line 547 (watchdog regression,
already accurate). `grep -niE 'robust|garbage|crash|guarantee|malformed|truncat|
watchdog|desync|abandon' README.md` returns: lines 262 & 412 ("Backward
compatibility (the guarantee)" — feature opt-in, NOT robustness), 495/496
(test-suite descriptions), 547 (watchdog — accurate). CONCLUSION: no new
robustness section needed; adding one would be scope creep beyond "sync docs".

## ITEM POINT (B) — host count: ALREADY DONE
Item spec says "currently 64" but README already shows 79 (P1.M2.T1.S1 landed
the adversarial count before this task). Verified 79 is the live count. No edit.

## SCOPE BOUNDARIES (files NOT to touch)
- notifier.c — P1.M1.T1.S1 (watchdog) + P1.M2.T3.S1 (comment) own it.
- test_notifier_host.c — P1.M1.T2.S1 owns it.
- test_fidelity_nfa128.c / run_all_tests.sh — P1.M2.T2.S1 owns it.
- run_notifier_stub_tests.sh — unchanged, run only as validation.
- README.md — THIS task edits ONLY the 4 stale spots above. Nothing else.
- PRD.md / tasks.json / prd_snapshot.md / .gitignore — READ-ONLY.

## EXACT EDIT COUNT
4 edits to README.md:
  (1) line 479: `9 host-side` → `10 host-side`
  (2) line 485: `9 pattern-matcher` → `10 pattern-matcher`
  (3) after the test_invalid_patterns table row: add test_fidelity_nfa128 row
  (4) line 537: `9 suites` → `10 suites` AND `2023/2023` → `2029/2029`
No other README line changes.