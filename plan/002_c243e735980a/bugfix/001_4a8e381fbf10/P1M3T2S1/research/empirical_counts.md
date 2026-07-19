# P1.M3.T2.S1 Research — Empirical Test Counts & README Edit Map

> Captured by running the actual gates after P1.M1.T1.S1 (4 @-literal cases)
> and P1.M2.T2.S1 (3 embedded-NUL dispatch cases) have landed. P1.M3.T1.S1
> (CONSOLE_ENABLE print + print.h) does NOT change any test count — only adds
> debug output + a compile-only stub.

## 1. Exact counts (empirically verified, 2025-07-18)

### `./run_all_tests.sh` — aggregate + per-suite

```
OVERALL TEST SUMMARY
Total tests run across all suites: 2023   ← was 2019 (+4)
Total tests passed: 2023
Total tests failed: 0
```

Per-suite (from each binary's own summary line):
| Suite | Count (now) | Count (before) | Counted in aggregate? |
|---|---|---|---|
| test_pattern_match | **380** | 376 | yes (`Total tests run:`) |
| test_char_classification | 179 | 179 | yes |
| test_word_boundary_basic | 74 | 74 | yes (`Tests run:`) |
| test_word_boundary_integration | 189 | 189 | yes (`Tests run:`) |
| test_metachar_verification | smoke | smoke | NO (boolean PASS/FAIL) |
| test_comprehensive_integration | 10 | 10 | NO (prints "Test categories failed") |
| test_error_handling | 161 | 161 | yes |
| test_memory_stress | 32 | 32 | yes |
| test_invalid_patterns | 1008 | 1008 | yes |

**Aggregate math check:** 380+179+74+189+161+32+1008 = **2023** ✓ (7 numeric
suites; metachar + comprehensive are intentionally excluded by the script's
`grep "Total tests run:" / "Tests run:"` counters — see run_all_tests.sh:48-66).

### `./run_notifier_stub_tests.sh`

```
notifier dispatch fails=0  (exit=0)        → test_notifier_dispatch: 14/14  (was 11/11)
notifier os fails=0        (exit=0)        → test_notifier_os:       31/31  (unchanged)
✓ notifier stub-compile gate PASSED
```

`test_notifier_dispatch` summary line: `Total tests run: 14 / passed: 14 / failed: 0`
(3 added by P1.M2.T2.S1: 1 `nul_cmd_fired==1` callback assertion + 2 `ck()`
discination calls: `ck("*world*","helloworld",0,1)` + `ck("*world*","hello",0,0)`).

## 2. README.md — every count-bearing line that needs editing

Grepped with: `grep -nE '376|2019|1826|11 cases|11/11|test_notifier_dispatch|sanitize|CONSOLE_ENABLE|@-literal|literal @|9 host-side|9 suites|all 9' README.md`

| Line | Current text | New text | Why |
|---|---|---|---|
| 320 | `To build and run all 9 host-side suites ...` | **NO CHANGE** | still 9 suites |
| 330 | `\| \`test_pattern_match\` \| 376 \| ...` | `... \| 380 \| ...` (+4 cases) | P1.M1.T1.S1 |
| 349 | `- **\`test_notifier_dispatch\`** (11 cases) — F4 ...` | `(14 cases)` | P1.M2.T2.S1 |
| 370 | `... **2019/2019** tests passing.` | `... **2023/2023** ...` | aggregate +4 |
| 371-372 | `test_notifier_dispatch\n  **11/11** + ... **31/31** ...` | `**14/14** + ... **31/31** ...` | P1.M2.T2.S1 |

**Recommended optional polish** (accurate, low-risk, reflects the new cases):
- Line 330 Covers column: append `, @-literal regression guards` to mirror the
  4 new rows added by P1.M1.T1.S1.
- Line 349 description already says "sanitization" (covers the new embedded-NUL
  case) — no wording change strictly needed.

## 3. Contract point (d) keyword review — VERIFIED NO EDITS NEEDED

Grepped README.md for `sanitize`, `CONSOLE_ENABLE`, `@-literal`, `literal @`:
**zero matches.** The "Running Tests" and "Current Test Status" sections make no
references to sanitize_string, CONSOLE_ENABLE debug, or @-literal matching that
would need updating. (The new behavior is reflected by the COUNT changes only.)
This must be documented in the PRP so the implementer knows point (d) is
already satisfied — no speculative rewrites.

## 4. Out-of-scope (contract point e — do NOT touch)

- **PRD.md** — human-owned; §11.2C doc fix already applied at commit 4d49460.
- **pattern_match.h JSDoc** — no API change.
- All `*.c`, `*.h`, `*.sh` source — this is a README-only (Mode B docs) task.

## 5. Pre-existing structure note (DO NOT "fix")

The README table has 9 rows but the aggregate (2023) is the sum of only 7
numeric suites (metachar=smoke and comprehensive=categories are excluded by the
script's counters). This is intentional and was already true before this
changeset (old table numerics summed to 2029 while README cited 2019 — same
~10/category gap). The implementer must update only the numbers that CHANGED,
not reconcile table-vs-aggregate math.