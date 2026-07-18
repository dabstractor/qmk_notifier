# P3 Research Notes — Acceptance Gate & Documentation Sync

Empirical findings captured by running the actual gates against the live codebase
(P1 mid-implementation: `pattern_match.c` modified; P2 harness not yet created).

## 1. Dependency contracts (what P3 consumes)

- **P1** (`plan/001_e329fbe4ae4d/P1/PRP.md`): produces `pattern_match.{c,h}` + the
  9 `test_*.c` suites + `run_all_tests.sh`. P3's 9-suite gate (11.2A/B/C) IS P1's gate.
- **P2** (`plan/001_e329fbe4ae4d/P2/PRP.md`): produces `notifier.{c,h}` (BUG-1 +
  DRIFT-1 fixed), `rules.mk` (verified), and a NEW stub harness: `qmk_stubs/` (3 files),
  `test_notifier_dispatch.c`, `run_notifier_stub_tests.sh`. P3 must ALSO run that stub
  gate (it closes RISK-1 and is the only host coverage for notifier.c).
- P3 MODIFIES ONLY `README.md`. It runs gates (read-only execution) + syncs docs.

## 2. Acceptance gate 11.2A — 9-suite status (LIVE, current)

Per-suite `grep -c '^FAIL:'` + summary lines:

| Suite | Live count | Passing | Notes |
|---|---|---|---|
| test_pattern_match | 376 | 376 | prints "Total tests run:" |
| test_char_classification | 179 | 179 | "Total tests run:" |
| test_word_boundary_basic | 74 | 74 | "Tests run:" (no "Total") |
| test_word_boundary_integration | 189 | 189 | "Tests run:" |
| test_metachar_verification | — | PASS | boolean PASS/FAIL smoke (no count) |
| test_comprehensive_integration | 10 cats / 3629 ops | PASS | "Test categories failed: 0" (not summed) |
| test_error_handling | 161 | 161 | "Total tests run:" |
| test_memory_stress | 32 | **31** | **1 FAIL: "Anchored huge pattern exact match"** ← P1 in-progress |
| test_invalid_patterns | 1008 | 1008 | "Total tests run:" |

- `run_all_tests.sh` aggregate line: **"Total tests run across all suites: 2019"**
  (sums only suites printing Total/Tests run: 376+179+74+189+161+32+1008 = 2019).
- **PRD §11.3's "~1,826"** = 376+179+74+189+1008 (only the 5 explicitly-numbered
  suites). It is a SUBSET of the live 2019. → README should report the LIVE 2019
  (what `./run_all_tests.sh` prints), NOT the PRD's 1,826.
- **BLOCKER for P3 gate**: `test_memory_stress` currently fails 1 case. This is a
  P1 bug (NFA simulation, P1.M2.T2 in progress). P3's gate is GREEN only when P1
  reaches 0 failures. If still red at P3 time → escalate as P1 blocker; do NOT
  weaken/skip the test.

## 3. Acceptance gate 11.2B — pathological NFA (LIVE: PASS)

```
pattern "a+a+a+a+a+a+a+a+a+a+b" vs 199×'a' → result=0  1819.0 us
```
1.8 ms << 50 ms gate. PASS. (Confirms Thompson NFA linear-time; no catastrophic
backtracking. Architecture doc measured ~1.6ms; live ~1.8ms — both well under gate.)

## 4. Acceptance gate 11.2C — realistic patterns (LIVE: DRIFT-2 spec error)

Six expected `1`s; live output:
```
\w+ vs hello              -> 1   ✓
\b\w+\b\s+\b\w+\b vs "hello world" -> 1  ✓
^\w+@\w+$ vs user_host    -> 0   ✗ (PRD expects 1)  ← DRIFT-2
v\.code vs v.code         -> 1   ✓
a+b vs aaab               -> 1   ✓
*slack* vs "Slack - general" (cs=0) -> 1  ✓
```

**DRIFT-2 (architecture/findings_and_risks.md, confirmed here):** `@` is an
ordinary literal char (not a metachar — only `* + . ^ $ \` are). So `^\w+@\w+$`
REQUIRES a literal `@`. `user_host` has none → **0 is semantically correct**.
The PRD §11.2C annotation `/* 1 */` is a spec typo.

Verified: `pattern_match("^\\w+@\\w+$","user@host",1)` → **1** (correct).

**P3 resolution (DO NOT touch the matcher — it's right):**
- Prefer honoring the "six 1s" intent by using the corrected input `user@host`
  in the P3 gate run, and record the drift (PRD §17: "code + passing tests win;
  report the drift").
- Equally valid: run 11.2C verbatim, observe 5 ones + 1 zero, document the drift.
- NEVER alter pattern_match.c to make `user_host` match `^\w+@\w+$` — that would
  break the literal-`@` semantics and flip real tests red.

## 5. P2 stub gate (not yet present — parallel)

`qmk_stubs/`, `test_notifier_dispatch.c`, `run_notifier_stub_tests.sh` do NOT
exist yet (P2 in progress). P3 assumes they exist per the P2 PRP contract.
P3 runs `./run_notifier_stub_tests.sh` → expect exit 0, "fails=0".
If absent at P3 time → P2 dependency not met; escalate.

## 6. README.md stale-claim audit (lines that need syncing)

| README claim (line) | Stale value | Authoritative source | New value |
|---|---|---|---|
| L84-86 Pattern syntax | `*`, `^`, `$` only | PRD §15/§7 + test corpus | full regex set: `* ^ $ . X+ \d\D \w\W \s\S \b\B \`escapes + `WT`/GS_DELIMITER |
| L133 "Main pattern match tests" | 383 | live | **376** |
| L134 "Character classification" | 179 | live | 179 (correct) |
| L135 "Word boundary tests" | 263 (combined) | live | 74 basic + 189 integration |
| L149 "Overall Test Results" | 1,992/2,048 (97.2%) | live | **2019 total, 100% passing** (when P1 done) |
| L150 "Backward Compat" | 65/65 | — | drop or restate (the glob-subset still matches) |
| L151 "Performance Impact" | 0.087 µs | live perf section | **~0.1 µs** (0.10086 µs measured) |
| L149 caveat "Some advanced edge cases" | present | — | REMOVE (now 100%) |
| Companion projects L93-99 | qmk_notifier + hyprland + zigotica | PRD §1.2 ecosystem | add **QMKonnect** (desktop daemon, primary); clarify qmk_notifier = transport crate |

Also MISSING from README (must add):
- The **`class\x1Dtitle` message format** + `GS_DELIMITER` (0x1D=29=Group Separator)
  + `WT(class,title)` two-part matching semantics (PRD §4.1, §5.3, §8.5 F4).
- `case_sensitive` defaults to false (PRD §5.4, §10.2).
- Dot `.` excludes `\n`/`\r`; glob `*` includes them (PRD §13 #8).
- The 9-suite inventory names (currently README lists only 4 + perf).

Companion-projects accuracy (PRD §1.2): QMKonnect = desktop daemon (Rust,
`dabstractor/qmkonnect`) — detects foreground window, sends `class\x1Dtitle`.
qmk_notifier (underscore) = transport crate QMKonnect LINKS (owns wire framing).
README currently describes qmk_notifier as "Rust application that captures window
information" — that conflates it with QMKonnect. P3 should clarify the roles.

## 7. Performance (LIVE perf section of run_all_tests.sh)

```
Running performance test with 100000 iterations...
Time taken: 0.070602 seconds
Average time per pattern match: 0.100860 microseconds   ← ~0.1 µs
✓ Performance is acceptable (< 1 second for 700000 iterations)
```
Matches PRD §12 "~0.1 µs per call". README's "0.087" is stale.

## 8. Files P3 touches

- MODIFY: `README.md` (the ONLY source file P3 edits).
- RUN (read-only): `./run_all_tests.sh`, gate 11.2A/B/C harness, `./run_notifier_stub_tests.sh`.
- Does NOT modify: any `.c/.h`, `rules.mk`, `run_all_tests.sh`, PRD.md, tasks.json.

## 9. Validation approach for a doc-only change

No new tests. Validation = (a) gate runs green (proves the numbers are real), (b)
README claims match live gate output (grep-verified), (c) `git diff README.md` shows
only doc prose + number changes, (d) no code/behavior change (the 9-suite + stub
gates produce identical pass/fail before and after the README edit).
