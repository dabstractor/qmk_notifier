# Scout: Test Infrastructure Patterns & Insertion Points

Scope: how tests are organized/compiled in `/home/dustin/projects/qmk-notifier`, and
exactly where a new `NFA_MAX_PATTERN=128` fidelity test (Issue 3) and new adversarial
typed-command cases would land.

---

## 1. Two parallel test harnesses (CRITICAL DISTINCTION)

There are **two independent test runners**, built differently, that must not be confused:

### A. `run_all_tests.sh` — the pattern-matcher corpus (9 suites, links `pattern_match.c` DIRECTLY)
- Each test compiled as: `gcc -o test_X test_X.c pattern_match.c [-std=c99 -DNOTIFIER_STUB] [-I.]`
- **`pattern_match.c` is included directly** (not via `notifier.c`), so `NFA_MAX_PATTERN`
  falls back to the `#ifndef` default of **2048** (`pattern_match.c:286-292`).
- **No suite passes `-DNFA_MAX_PATTERN=128`** → this is exactly the Issue-3 fidelity gap:
  these suites run at the host ceiling (2048), not the firmware budget (128).
- Runners: `test_pattern_match`, `test_char_classification`, `test_word_boundary_basic`,
  `test_word_boundary_integration`, `test_metachar_verification`,
  `test_comprehensive_integration`, `test_error_handling`, `test_memory_stress`,
  `test_invalid_patterns`.
- Result-grep contract: looks for `Total tests run:` / `Tests passed:` / `Tests failed:`
  (NOT `^FAIL:`). Aggregate summary printed at the end.
- Source structure (`test_pattern_match.c:1-60`): file-scope `test_case_t` struct
  (`{pattern, input, case_sensitive, expected_result, description}`), a `run_test()`
  helper, file-scope counters `tests_run/passed/failed`, `main(void)` prints
  `Total tests run:` at line 835.

### B. `run_notifier_stub_tests.sh` — the notifier receiver/dispatcher/host suites (3 drivers)
- **One shared stub-compiled object**: `gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c -o /tmp/notifier_stub.o` (`run_notifier_stub_tests.sh:15-21`).
- Because this compiles `notifier.c`, the **`#define NFA_MAX_PATTERN 128` at `notifier.c:14`**
  IS active — so these three drivers already run at the firmware budget (128). The
  compile-time guard at `notifier.c:28-30` (`typedef char ...[ (NFA_MAX_PATTERN<=128)?1:-1 ]`)
  fires a build error if it ever regresses.
- The shared `.o` is then linked into **three** drivers, each with
  `qmk_stubs/qmk_stubs.c` + its own `test_*.c`:
  1. `test_notifier_dispatch.c` → `/tmp/test_notifier_dispatch`
  2. `test_notifier_os.c`        → `/tmp/test_notifier_os`
  3. `test_notifier_host.c`      → `/tmp/test_notifier_host`
- Result-grep contract: `grep -c '^FAIL:'` (DIFFERENT from run_all_tests.sh). Exit code
  per driver is also checked. Cleanly removes the `.o`/binaries at the end.

---

## 2. `test_notifier_host.c` — internal structure (typed-command test home)

### Test helper functions (file-scope, lines 50-100)
- **`CK(cond, name)` macro** (lines ~70-74): `if (cond){g_pass++; printf("PASS: %s\n",name);} else {g_fail++; printf("FAIL: %s\n",name);}`. The runner greps `^FAIL:` (or `^PASS:`), so assertion label strings are the contract.
- **`send_typed(cmd_id, args, nargs)`** (lines ~83-92): builds a single 32-byte report
  `[0x81][0x9F][0xF0][cmd_id][args…][0x03]`, calls `hid_notify(rep, 32)`, returns
  `stub_get_last_response()` (a 32-byte const buffer). This is THE way to drive a typed
  command in one report.
- Counters: file-scope `g_pass`, `g_fail`. `main(void)` ends with
  `printf("Total tests run: %d / passed: %d / failed: %d\n", ...)` and `return g_fail?1:0`.
- Stub accessors used (MANUAL extern, F6 convention — not a header):
  `uint8_t stub_get_active_layer(void);` and `const uint8_t *stub_get_last_response(void);`.
- Process-full-message path (legacy string dispatch): `bool process_full_message(char *data);` — used to set up board state before a typed command (e.g. `(ii)` setup).

### Section structure — `main(void)` is a sequence of braced blocks, each a labeled section
Blocks are delimited by banner comments and named with roman numerals / tags:
1. **`P1.M3.T1.S2`** QUERY_INFO / QUERY_CALLBACK (i)–(iv), plus side-effect-free check (~lines 95-185).
2. **`P1.M3.T1.S3 SET_OS (0x03)`** (ii-pre), (i), (ii), (iii), (iv) (~lines 195-275).
3. **`P1.M3.T1.S3 APPLY_HOST_CONTEXT (0x05)`** (v)–(viii) (~lines 285-360).
4. **`P1.M3.T1.S4 COEXISTENCE / backward-compat`** (coexist-i), (coexist-ii) (~lines 365-368).
5. **`P1.M3.T1.S4 MULTI-REPORT typed framing`** — `(multi-rep)` block, banner at
   **line 369**, test case at **line 378**, CK assertions at **lines 400-404**.
   (Task said "around line 357"; the section banner is at 369, last assertion 404.)

### How multi-report is tested (`multi-rep`, lines 378-405)
- Hand-builds TWO 32-byte reports with `memset` then sets explicit bytes; **no** `send_typed`
  (send_typed assumes single-report ETX framing).
- Report 1 (no ETX): `[0x81][0x9F][0xF0][0x05][layer=224][flags=0][count=28][id0..id24]`
  → `hid_notify(rep1,32)` (count=28 forces a 2nd report; ids all 0, none == 0x03 ETX).
- Report 2 (with ETX): `[0x81][0x9F][id25][id26][id27][0x03]` → `hid_notify(rep2,32)`.
- Asserts the reassembled dispatch ACK: `r[0]==0x51`, `r[1]==0x05` (cmd echo persisted
  across reports), `r[2]==1`, layer 224 active, callback fired once.
- Key invariants to mirror: every payload byte deliberately chosen `!= 0x03` to avoid the
  ETX-collision ambiguity (length-aware `typed_literal_remaining` reassembly).

### Callback registry / stub setup (lines 36-65)
- `DEFINE_HOST_CALLBACKS({...})` macro (from `notifier.h`) registers named callbacks;
  here 2 entries (`mute`, `layout`) so `QUERY_INFO` reports `callback_count=2`.
- Per-callback enable/disable counters + monotonic `g_seq` stamps for ordering tests.
- `DEFINE_SERIAL_COMMANDS` / `DEFINE_SERIAL_LAYERS` (board rules) + `..._OS(OS_MACOS, ...)`
  variants — these are the matchable typed/legacy string targets.

---

## 3. Where a new `NFA_MAX_PATTERN=128` fidelity test goes (Issue 3 fix)

**The gap is exclusively in harness A (`run_all_tests.sh`)** — harness B already compiles
via `notifier.c` so it is at 128. The fidelity test must therefore be added to
**`run_all_tests.sh`**, NOT to `run_notifier_stub_tests.sh`.

### Recommended insertion point in `run_all_tests.sh`
The compile block is lines 11-19 (9 `gcc` lines). Add a 10th compile that pins the budget:

```bash
# Issue-3 fidelity gate: exercise the matcher at the firmware NFA_MAX_PATTERN=128 budget.
# Links pattern_match.c DIRECTLY (like the other 9 suites) but -D pins it to 128 so a
# pattern in the 129..2048 range that would pass at the host ceiling is caught.
gcc -o test_fidelity_nfa128 test_fidelity_nfa128.c pattern_match.c -I. -DNFA_MAX_PATTERN=128 -std=c99
```

Then register it in the run sequence (lines ~96-104): add
`run_test "NFA-128 Fidelity Gate" "test_fidelity_nfa128"`.

### Caveats for the new source file
- Its `main` must print `Total tests run:` / `Tests passed:` / `Tests failed:` to be picked
  up by `run_test()`'s `grep -q` parsing (`run_all_tests.sh:50-66`).
- `-DNFA_MAX_PATTERN=128` must come **after** `pattern_match.c` is on the command line is
  NOT sufficient — `-D` is a global preprocessor define applied before any file, so order
  on the line does not matter; the `#ifndef` guard at `pattern_match.c:286` picks it up.
- Do NOT add `-DNFA_MAX_PATTERN=128` to the existing 9 suites (they legitimately need 2048
  for the multi-KB `test_memory_stress` patterns; `test_memory_stress.c:185` documents this).

---

## 4. Where adversarial typed-command test cases go (`test_notifier_host.c`)

`test_notifier_host.c` is explicitly designed to **APPEND blocks** (see header comment
lines 18-25: "Siblings … APPEND blocks to this file"). New adversarial cases go into
`main(void)` as additional braced `{ ... }` blocks with the same banner/`(tag)` convention,
**before** the final `printf("Total tests run: ...")` (currently the last statement).

### Suggested insertion points
- **New adversarial typed-command cases** (e.g. malformed framing, ETX-in-payload
  collisions, truncated multi-report, OOB cmd_id, zero-length/over-length args): append a
  new bannered section **after the `(multi-rep)` block (ends ~line 405) and before the
  final `printf`/`return` (~lines 406-408)**. Reuse `send_typed()` for single-report cases
  and the manual `hid_notify(rep,32)` pattern from `(multi-rep)` for framing-edge cases.
- Each case: drive via `send_typed`/`hid_notify`, capture `stub_get_last_response()`, then
  a sequence of `CK(...)` assertions. Counter updates are automatic via the `CK` macro.
- For multi-report edge cases, copy the exact `memset`+explicit-byte pattern from
  `(multi-rep)` (lines 378-399); avoid payload bytes `== 0x03` unless deliberately testing
  the ETX collision.

### If the new cases should run at the firmware budget too
They already do — `test_notifier_host.c` is built via `run_notifier_stub_tests.sh`, which
compiles `notifier.c` (`NFA_MAX_PATTERN=128` active). No `-D` change needed there.

---

## 5. Key file/line references

| File | Lines | Why it matters |
|---|---|---|
| `test_notifier_host.c` | 70-74 | `CK` macro definition (PASS:/FAIL: contract) |
| `test_notifier_host.c` | 83-92 | `send_typed()` helper (single-report typed driver) |
| `test_notifier_host.c` | 369-405 | `(multi-rep)` block — multi-report pattern to mirror |
| `test_notifier_host.c` | ~406-408 | final `printf`/`return` — INSERT new blocks ABOVE this |
| `run_notifier_stub_tests.sh` | 15-21 | shared stub-compiled `notifier.o` (NFA_MAX_PATTERN=128 active) |
| `run_notifier_stub_tests.sh` | 23-46 | per-driver link steps (3 drivers from 1 `.o`) |
| `run_notifier_stub_tests.sh` | 49-55 | `grep -c '^FAIL:'` result contract |
| `run_all_tests.sh` | 11-19 | 9 `gcc` lines linking `pattern_match.c` DIRECTLY (NFA_MAX_PATTERN=2048) — INSERT fidelity compile here |
| `run_all_tests.sh` | 50-66 | `run_test()` parses `Total tests run:`/`Tests passed:`/`Tests failed:` |
| `run_all_tests.sh` | 96-104 | the `run_test` call sequence — INSERT fidelity run here |
| `pattern_match.c` | 286-293 | `#ifndef NFA_MAX_PATTERN` guard → `-D` overrides cleanly |
| `notifier.c` | 14 | `#define NFA_MAX_PATTERN 128` (firmware budget) |
| `notifier.c` | 28-30 | compile-time `<=128` guard (only fires on budget regression) |
| `test_pattern_match.c` | 1-60, 835, 849 | canonical pattern-suite structure (test_case_t, counters, main) |

---

## 6. Constraints / risks / open questions

- **Result-grep contract differs between harnesses**: A greps `Total tests run:`/`Tests
  passed:`, B greps `^FAIL:`. A new suite's `main` MUST match the harness it joins:
  - In `run_all_tests.sh` → print `Total tests run:` / `Tests passed:` / `Tests failed:`.
  - In `run_notifier_stub_tests.sh` → use `CK` macro emitting `PASS:`/`FAIL:` lines.
- **Do not** add `-DNFA_MAX_PATTERN=128` to the existing 9 pattern suites — `test_memory_stress`
  intentionally needs 2048 (its ~40KB processed pattern; documented `test_memory_stress.c:185`).
  Pinning it would flip a currently-passing test to a (correct) failure and break the gate.
- The compile-time guard in `notifier.c:28` only enforces the *budget*, not per-pattern fit;
  a 128-fidelity suite is what actually catches a 129..2048-byte pattern regression (this is
  the whole point of Issue 3).
- `stub_get_last_response()` is "last write wins"; for a non-dispatch case (e.g. discarded
  non-magic report) you must prime a known response first and assert it is UNCHANGED — see
  `(coexist-ii)` (lines ~355-368) for the exact idiom.

## Start Here
`run_all_tests.sh` (lines 11-19) — the Issue-3 fidelity `-DNFA_MAX_PATTERN=128` compile is
added as a 10th `gcc` line here, and its `run_test` call added near line 104. For adversarial
typed cases, start at `test_notifier_host.c` (lines 369-408) — append a new bannered block
after `(multi-rep)` and before the final `printf`/`return`.