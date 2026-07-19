# PRP — P1.M2.T2.S1: Extend runner to build+run test_notifier_os and verify full §11.2A-D gate

## Goal

**Feature Goal**: Extend `run_notifier_stub_tests.sh` so it stub-compiles
`notifier.c` ONCE into a shared object file, links that object into BOTH host
test binaries (`test_notifier_dispatch` AND the new `test_notifier_os`), runs
both, and reports each binary's `FAIL:` count — ending `✓ notifier stub-compile
gate PASSED` (exit 0) iff both report 0 failures. Then run the **FULL PRD §11.2
acceptance gate** (A: 9 pattern_match suites 0 failures; B: pathological NFA
stress < 50 ms; C: realistic patterns; D: the extended stub gate) and confirm
the whole delta is green with no new compiler warnings.

**Deliverable**: ONE file modified — `run_notifier_stub_tests.sh` (repo root),
growing from its current ~37 lines to ~55, renumbered `[1/4]`/`[2/4]`/`[3/4]`/
`[4/4]`, with an updated Mode-A header comment and a two-line fail summary. No
other source file changes.

**Success Definition**:
- `run_notifier_stub_tests.sh` compiles `notifier.c` to `/tmp/notifier_stub.o`
  ONCE (shared), then links BOTH `test_notifier_dispatch` and `test_notifier_os`
  against it, runs both, prints `notifier dispatch fails=N` AND
  `notifier os fails=N`, and ends `✓ notifier stub-compile gate PASSED` / exit 0.
- §11.2D green: `./run_notifier_stub_tests.sh` → both binaries 0 `FAIL:`, exit 0.
- §11.2A green: `./run_all_tests.sh` → all 9 pattern_match suites 0 failures,
  exit 0 (unchanged baseline; matcher is P1 scope, untouched).
- §11.2B green: `/tmp/nfa_stress` (pathological `a+a+…+b` vs 199 `a`s) prints
  `result=0` in < 50 ms.
- §11.2C: `/tmp/nfa_real` runs; 5 of 6 realistic patterns print `1`; the
  `^\w+@\w+$` vs `user_host` line prints `0` (CORRECT — no `@` in the input; the
  PRD `/* 1 */` comment is erroneous — pre-existing, out of scope).
- No new compiler warnings beyond pre-existing ones (the `-Wall -Wextra -std=c99`
  in the runner surfaces them).

## User Persona (if applicable)

**Target User**: The maintainer running the acceptance gate (PRD §11.2) and CI.
The runner is the §11.2D gate's single entry point.

**Use Case**: After the multi-OS dispatch + `notifier_set_os` + `test_notifier_os.c`
land, a developer runs `./run_notifier_stub_tests.sh` to confirm both the dispatch
backward-compat (reassembly/F4/ack/coexistence) AND the multi-OS F8/F9 contract
still hold against stub-compiled `notifier.c` — no QMK hardware needed.

**User Journey**: edit `notifier.c` → `./run_notifier_stub_tests.sh` → the runner
re-stub-compiles, links both binaries, runs both, prints both fail counts, exits
0/1. A non-zero exit (or either `fails>0`) means a regression.

**Pain Points Addressed**: (1) the multi-OS feature currently has NO regression
gate wired into the runner — `test_notifier_os` exists but is never built/run by
it; (2) the runner is CURRENTLY BROKEN (see *Why*) — its link step cannot find
`os_detection.h`, so even `test_notifier_dispatch` no longer builds from the
runner. This task fixes both.

## Why

- **The current runner is ALREADY BROKEN (the real driver).** P1.M1.T1.S1
  (COMPLETE) added `#include "os_detection.h"` to `notifier.h` (notifier.h:3).
  `os_detection.h` lives ONLY in `qmk_stubs/`. But the runner's `[2/3]` link step
  uses `gcc … -I. …` (no `-Iqmk_stubs`), so it now dies:
  `notifier.h:3:10: fatal error: os_detection.h: No such file or directory … LINK FAILED`
  (exit 3). This breaks BOTH binaries today — not just the future os binary.
  The parallel P1.M2.T1.S1 PRP explicitly names this: "the
  run_notifier_stub_tests.sh `[2/3]` `-Iqmk_stubs` gap (fixed by P1.M2.T2.S1)".
  **This task IS that fix.** Empirically verified: adding `-Iqmk_stubs` to the
  link steps makes dispatch link (0 fails) AND os link (0 fails).
- **Closes the §11.2D acceptance gate.** PRD §11.2D requires
  `./run_notifier_stub_tests.sh` to build+run BOTH `test_notifier_dispatch` AND
  `test_notifier_os`, each reporting 0 `FAIL:`. Until the runner is extended,
  §11.2D cannot be marked complete.
- **Completes P1.M2 (Host Test Harness, Acceptance & Docs).** This is the
  penultimate code task before P1.M2.T3.S1 (README). After it, the full §11.2
  A-D gate is green and the multi-OS delta is verified end-to-end.
- **Backward-compat is structurally guaranteed (findings F5, confirmed).**
  `test_notifier_dispatch.c` defines no `_OS` maps and never calls
  `notifier_set_os`, so `current_os` stays `OS_UNSURE` → `select_*_map_os`
  returns `{NULL,0}` → default maps scanned → identical to pre-multi-OS. It
  links (with the `-Iqmk_stubs` fix) and stays 11/11, 0 FAIL. **No edits** to
  `test_notifier_dispatch.c` (item-spec 3b).

## What

Modify ONE file: `run_notifier_stub_tests.sh`. The new runner:

1. `set -u; cd "$(dirname "$0")"` (unchanged).
2. Defines three `/tmp` paths: `OBJ` (shared object), `DRV` (dispatch binary),
   `OST` (os binary).
3. **`[1/4]` stub-compile `notifier.c` ONCE** → `/tmp/notifier_stub.o`, flags
   `-Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c`
   (IDENTICAL to the current `[1/3]` — unchanged). Fail → exit 2.
4. **`[2/4]` link dispatch** → `/tmp/test_notifier_dispatch` from `$OBJ` +
   `qmk_stubs/qmk_stubs.c` + `test_notifier_dispatch.c`, flags
   `-Wall -std=c99 -Iqmk_stubs -I.` (**adds `-Iqmk_stubs`** vs the old `[2/3]`).
   Fail → `rm -f $OBJ`, exit 3.
5. **`[3/4]` link os** → `/tmp/test_notifier_os` from `$OBJ` +
   `qmk_stubs/qmk_stubs.c` + `test_notifier_os.c`, same flags. Fail →
   `rm -f $OBJ $DRV`, exit 4.
6. **`[4/4]` run both**: run `$DRV`, capture `rc_d` + `fails_d`
   (`grep -c '^FAIL:' || true`); run `$OST`, capture `rc_o` + `fails_o`;
   `rm -f $OBJ $DRV $OST`; print
   `notifier dispatch fails=$fails_d  (exit=$rc_d)` and
   `notifier os fails=$fails_o  (exit=$rc_o)`; exit 0 iff both `fails==0` AND
   both `rc==0`, else exit 1.
7. **Mode-A header comment** updated to state it builds BOTH the dispatch and the
   multi-OS os test binaries from a single shared `notifier.o` (PRD §11.1, §11.2D).

**Critical deviation from the item-spec example**: the item's `[2b]` os-link
command shows `-I.` only. That is **WRONG** (empirically fails with
`os_detection.h: No such file or directory`). The correct flags are
`-Iqmk_stubs -I.` for BOTH link steps — matching the `[1/4]` compile step and
the PRD §11.1 doc command (which already has `-Iqmk_stubs -I.`).

After landing the runner, run the **full §11.2 A-D gate** (see Validation Loop):
§11.2A `./run_all_tests.sh` (9 suites, 0 fails), §11.2B `/tmp/nfa_stress` (<50ms,
result=0), §11.2C `/tmp/nfa_real` (5/6 print 1; the `\w+@\w+` line correctly
prints 0), §11.2D `./run_notifier_stub_tests.sh` (both binaries 0 fails).

### Success Criteria

- [ ] `run_notifier_stub_tests.sh` is ~55 lines, renumbered `[1/4]`…`[4/4]`,
      compiles `notifier.c` ONCE, links BOTH binaries with `-Iqmk_stubs -I.`,
      runs both, prints both fail counts, ends PASSED iff both 0 fails.
- [ ] Header comment (Mode A) mentions BOTH the dispatch and the multi-OS os
      test binaries and cites PRD §11.1/§11.2D.
- [ ] §11.2D: `./run_notifier_stub_tests.sh` → both 0 `FAIL:`, exit 0,
      `✓ notifier stub-compile gate PASSED`.
- [ ] §11.2A: `./run_all_tests.sh` → all 9 suites 0 failures, exit 0.
- [ ] §11.2B: `/tmp/nfa_stress` → `result=0` in < 50 ms.
- [ ] §11.2C: `/tmp/nfa_real` runs; the `^\w+@\w+$` vs `user_host` line prints
      `0` (documented as correct/pre-existing, NOT a regression).
- [ ] No new compiler warnings beyond pre-existing (the runner's `-Wall -Wextra`
      on the compile step surfaces them; the link steps use `-Wall`).
- [ ] `test_notifier_dispatch.c` UNCHANGED and still 11/11. No edits to any file
      except `run_notifier_stub_tests.sh`.

## All Needed Context

### Context Completeness Check

**Pass.** The exact 55-line runner is given verbatim in "Implementation Tasks"
and was **executed end-to-end during research** against the current post-S2
`notifier.c` (with the research-validated `test_notifier_os_val.c` staged as a
stand-in for the not-yet-landed `test_notifier_os.c`): it compiled `notifier.c`
once, linked both binaries, ran both with `fails=0 / exit=0`, and printed
`✓ notifier stub-compile gate PASSED` / exit 0. The `-Iqmk_stubs` fix was
empirically confirmed as the resolution to the current `LINK FAILED`. The §11.2
A/B/C baselines were all run during research and their exact outputs recorded.
The sole ordering dependency — `test_notifier_os.c` must exist (P1.M2.T1.S1,
parallel) for the `[3/4]` link to succeed — is explicit. An implementer with
only this PRP + repo access can produce the runner and prove the full gate green.

### Documentation & References

```yaml
# MUST READ — the exact §11.1 build commands (authoritative flags)
- file: PRD.md   (also plan/002_c243e735980a/prd_snapshot.md)
  section: "### 11.1 Build all suites (exact flags — copy/paste)"
  why: "The §11.1 command for test_notifier_os is:
        gcc -DQMK_KEYBOARD_H='\"qmk_keyboard_stub.h\"' -Iqmk_stubs -I. \
            notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -std=c99
        — NOTE it has -Iqmk_stubs -I. (NOT -I. alone). This is the authoritative
        flag set; the runner's link steps MUST match (-Iqmk_stubs -I.)."
  critical: "The item-spec's example [2b] command shows '-I.' only — that is
        WRONG (os_detection.h not found). Use -Iqmk_stubs -I. on BOTH link steps."

# MUST READ — the §11.2 acceptance gate (the thing this task closes)
- file: PRD.md
  section: "### 11.2 Acceptance gate — all must be true"  (A/B/C/D)
  why: "Defines the four gates: (A) 9 suites 0 failures; (B) pathological NFA
        <50ms result=0; (C) 6 realistic patterns; (D) run_notifier_stub_tests.sh
        builds+runs BOTH test_notifier_dispatch AND test_notifier_os, each 0 FAIL."
  critical: "(D) says 'test_notifier_dispatch + test_notifier_os EACH report 0
        FAIL: lines'. The runner's PASSED condition must require BOTH."

# MUST READ — §11.2D's six criteria (what test_notifier_os verifies; this task
# just runs it, but the contract frames the gate)
- file: PRD.md
  section: "### 11.2 (D) Multi-OS selection"  (criteria i-vi)
  why: "Frames WHY test_notifier_os exists; the runner treats it as a black box
        (0 FAIL = pass). No need to re-encode the criteria here."
  critical: "The runner does NOT inspect test_notifier_os's internal criteria —
        it only greps '^FAIL:'. Keep the runner generic."

# The runner being extended (the ONLY file this task touches)
- file: run_notifier_stub_tests.sh
  why: "The current 37-line runner: [1/3] compile notifier.c->.o (has -Iqmk_stubs
        -I.); [2/3] link dispatch (-I. ONLY — the gap); [3/3] run + grep FAIL: +
        print 'notifier dispatch fails=N' + PASSED/FAILED. This task renumbers to
        [1/4]-[4/4], adds -Iqmk_stubs to both links, adds the os link+run, prints
        both fail counts."
  pattern: "Keep: set -u; cd dirname; /tmp paths; the compile flags; the
        'grep -c ^FAIL: || true' idiom; the rm cleanup; the ✓/✗ verdict lines.
        Add: OST path; [3/4] os link+run; second fail line; -Iqmk_stubs on links."
  critical: "Do NOT remove -Wextra from the [1/4] compile step (it surfaces
        notifier.c warnings). Do NOT add -Wextra to the link steps (keep -Wall,
        matching the old [2/3]). Do NOT compile notifier.c twice — share the .o."

# Why -Iqmk_stubs is mandatory (the gap this task fixes)
- file: notifier.h
  section: line 3  '#include "os_detection.h"'
  why: "notifier.h now includes os_detection.h (added by P1.M1.T1.S1, COMPLETE).
        os_detection.h lives ONLY in qmk_stubs/ (find . -name os_detection.h).
        #include \"...\" searches the includer's dir (notifier.h is at repo root
        '.') then -I paths; os_detection.h is in neither '.' nor the default
        path — it needs -Iqmk_stubs."
  critical: "This is why the current [2/3] link FAILS today (LINK FAILED, exit 3)
        and why BOTH link steps need -Iqmk_stubs. Empirically verified."

# The shared object discipline (item-spec 3a)
- file: run_notifier_stub_tests.sh  (the [1/3] step)
  why: "notifier.c is compiled ONCE into /tmp/notifier_stub.o and that $OBJ is
        passed to BOTH link gcc calls. The §11.1 doc command compiles all sources
        in one gcc invocation, but the runner object-compiles first (so the same
        notifier.c flags apply once and both binaries share it). Do NOT compile
        notifier.c separately per binary."
  critical: "The compile flags (-Wall -Wextra -std=c99 -DQMK_KEYBOARD_H=...
        -Iqmk_stubs -I.) are on the .o; the link steps only add the test TU +
        qmk_stubs.c. This is why the link steps do NOT need -DQMK_KEYBOARD_H
        (notifier.c is already compiled)."

# The backward-compat guarantee (item-spec 3b; findings F5)
- file: plan/002_c243e735980a/architecture/findings_and_risks.md
  section: "### F5. test_notifier_dispatch.c should pass UNCHANGED"
  why: "test_notifier_dispatch.c defines no _OS maps, never calls notifier_set_os
        => current_os stays OS_UNSURE => select_*_map_os returns {NULL,0} => OS
        scan 0 iterations => default scanned => identical to pre-multi-OS. It
        links with the -Iqmk_stubs fix and stays 11/11, 0 FAIL."
  critical: "Do NOT edit test_notifier_dispatch.c. The ONLY reason it currently
        fails in the runner is the -Iqmk_stubs gap, which this task fixes. If a
        tweak were ever needed, keep it scoped to the new OS surface (none
        expected)."

# The §11.2A-C baseline (pattern_match corpus — UNAFFECTED, already green)
- file: run_all_tests.sh
  why: "The §11.2A gate entry point: rebuilds the 9 pattern_match suites, runs
        them, prints a summary, then builds+runs an inline perf_test (100k iter x
        7 patterns < 1s). This task does NOT modify it — it only RUNS it as §11.2A
        and notes its exit code."
  critical: "run_all_tests.sh's perf_test is a COARSE benchmark (sub-second), NOT
        the strict §11.2B pathological case. §11.2B is the separate /tmp/nfa_stress
        (result=0 in <50ms). Run BOTH: run_all_tests.sh for §11.2A, nfa_stress for
        §11.2B. (Item-spec 3c acknowledges run_all_tests.sh 'already includes a
        perf benchmark; confirm sub-second' — that is the coarse check.)"

# The §11.2C discrepancy (document, do not "fix")
- file: PRD.md
  section: "### 11.2 (C) Realistic patterns still match"
  why: "Lists 6 patterns expected to print 1. Empirically, 5 print 1; the
        'pattern_match(\"^\\w+@\\w+$\",\"user_host\",1)' line prints 0 — CORRECT,
        because user_host has no '@' (an anchored ^\\w+@\\w+$ cannot match). The
        PRD's /* 1 */ comment for that line is erroneous (likely meant user@host)."
  critical: "This is a PRE-EXISTING pattern_match condition, OUTSIDE this task's
        scope (runner only; matcher is P1, complete). Document the actual output
        (1 1 0 1 1 1) in the PRP/validation; do NOT try to make all 6 print 1
        (that would mean editing pattern_match.c — scope creep + invariant 12).
        Nothing is red; the matcher is correct."

# Dependency PRP — what the parallel task produces (CONTRACT)
- file: plan/002_c243e735980a/P1M2T1S1/PRP.md
  section: "## Goal" + "## Implementation Blueprint"
  why: "P1.M2.T1.S1 (parallel, IMPLEMENTING) creates test_notifier_os.c AND adds
        stub_get_active_layer() to qmk_stubs/qmk_stubs.c. The accessor is ALREADY
        LANDED (qmk_stubs.c:26); test_notifier_os.c is NOT yet present at research
        time. This task CONSUMES both — it links test_notifier_os.c and
        (transitively) the accessor."
  critical: "The runner's [3/4] os link will fail with 'cannot find
        test_notifier_os.c' UNTIL P1.M2.T1.S1 lands that file. The runner is
        correct; it just needs its input. Do NOT create test_notifier_os.c here
        and do NOT touch qmk_stubs.c (the accessor is already there)."

# The §11.3 inventory (what each binary covers — context only)
- file: PRD.md
  section: "### 11.3 Test inventory (test_notifier_dispatch + test_notifier_os rows)"
  why: "test_notifier_dispatch = reassembly/ETX/F4/BUG-1/ordering/ack/coexistence;
        test_notifier_os = F8 merge/fallback per track, per-map-type independence,
        OS_UNSURE->default, F9 clear-on-change idempotence. Both PASS/FAIL style;
        the runner greps '^FAIL:'."
  critical: "Both use the SAME framework (g_pass/g_fail, 'Total tests run: N /
        passed: P / failed: F', return g_fail?1:0) so the runner's grep + exit
        logic is identical for both."

# The §11.4 framework (why grep '^FAIL:' works)
- file: PRD.md
  section: "### 11.4 The test framework"
  why: "Every counting suite prints 'PASS:'/'FAIL:' per case and a final summary;
        exit code is non-zero iff any case failed. So 'grep -c ^FAIL:' == 0 AND
        exit==0 are equivalent signals of a clean run (the runner checks both,
        belt-and-suspenders, matching the original runner)."
```

### Current Codebase tree (relevant slice — POST P1.M1 / parallel P1.M2.T1.S1)

```bash
notifier.c                 # LANDED (multi-OS): process_full_message (OS-first/default-
                           #   fallback) + notifier_set_os + per-OS weak accessors.
                           #   DO NOT TOUCH.
notifier.h                 # LANDED: #include "os_detection.h" (line 3) + DEFINE_*_OS
                           #   macros + notifier_set_os decl. DO NOT TOUCH.
pattern_match.{c,h}        # unaffected (P1 matcher, complete). DO NOT TOUCH.
qmk_stubs/
  os_detection.h           # os_variant_t enum (the header the runner's -Iqmk_stubs
                           #   resolves). DO NOT TOUCH.
  qmk_keyboard_stub.h      # QMK_KEYBOARD_H stand-in. DO NOT TOUCH.
  raw_hid.h                # raw_hid_send decl. DO NOT TOUCH.
  qmk_stubs.c              # LANDED: layer_on/off + raw_hid_send + stub_get_active_layer
                           #   (accessor, lines 22-26, added by parallel P1.M2.T1.S1).
                           #   DO NOT TOUCH (accessor already present).
test_notifier_dispatch.c   # backward-compat canary (11/11). DO NOT TOUCH.
test_notifier_os.c         # parallel P1.M2.T1.S1 deliverable (NOT yet present at
                           #   research time). The runner [3/4] link consumes it.
run_notifier_stub_tests.sh # ← MODIFY (this task). Currently BROKEN at [2/3] (-Iqmk_stubs gap).
run_all_tests.sh           # 9-suite pattern_match corpus + perf_test. DO NOT TOUCH (run as §11.2A).
PRD.md                     # READ-ONLY.
```

### Desired Codebase tree with files to be changed

```bash
run_notifier_stub_tests.sh # MODIFIED: ~37 -> ~55 lines. Compiles notifier.c ONCE,
                           #   links BOTH test_notifier_dispatch AND test_notifier_os
                           #   (each -Iqmk_stubs -I.), runs both, prints both fail
                           #   counts, PASSED iff both 0 fails. Mode-A header updated.
# (nothing else changes)
```

### Known Gotchas of our codebase & Library Quirks

```bash
# CRITICAL — the [2/3] (now [2/4],[3/4]) link steps MUST have -Iqmk_stubs, NOT just -I.
#   notifier.h:3 does `#include "os_detection.h"`; os_detection.h is ONLY in qmk_stubs/.
#   With `-I.` alone the link fails: `notifier.h:3:10: fatal error: os_detection.h:
#   No such file or directory`. The current runner is ALREADY BROKEN this way (exit 3)
#   as a side-effect of P1.M1.T1.S1 landing. The fix is -Iqmk_stubs -I. on BOTH link
#   steps (matching the [1/4] compile step and the PRD §11.1 doc command). VERIFIED.
#   The item-spec's example [2b] command (`-I.` only) is WRONG — do not copy it.

# CRITICAL — compile notifier.c ONCE, link into both. The [1/4] step produces
#   /tmp/notifier_stub.o with the full stub flags (-DQMK_KEYBOARD_H -Iqmk_stubs -I.
#   -Wall -Wextra -std=c99). BOTH [2/4] and [3/4] pass $OBJ (notifier.c source) to
#   gcc. Do NOT compile notifier.c separately per binary (item-spec 3a: "The .o is
#   compiled ONCE and linked into both; do not compile notifier.c twice"). This is
#   also why the link steps do NOT need -DQMK_KEYBOARD_H (notifier.c is already
#   compiled into the .o).

# CRITICAL — test_notifier_os.c does NOT exist until the parallel P1.M2.T1.S1 lands
#   it. Until then, the [3/4] link fails with `cannot find test_notifier_os.c`. That
#   is NOT a bug in this task's runner — it is an ordering dependency. The runner is
#   correct; it needs its input. Do NOT create test_notifier_os.c here.

# GOTCHA — `grep -c '^FAIL:'` returns exit 1 when the count is 0. Under `set -u`
#   and command substitution this would abort the script. The original runner guards
#   with `|| true`; KEEP that idiom (`fails_d=$("$DRV" 2>/dev/null | grep -c '^FAIL:'
#   || true)`). Dropping `|| true` makes a PASSING binary crash the runner.

# GOTCHA — run each binary with stderr suppressed when counting fails
#   (`"$DRV" 2>/dev/null | grep -c '^FAIL:'`), because qmk_stubs.c prints
#   `[stub] layer_on(...)` traces to stderr that are NOT failures. The first run
#   (`"$DRV"` capturing rc_d) DOES show stderr (human-readable diagnostics); the
#   second run (for counting) suppresses it. This mirrors the original runner.

# GOTCHA — cleanup order. On a [3/4] link failure, rm -f BOTH $OBJ and $DRV (the
#   dispatch binary was already built in [2/4]); on the final verdict, rm -f all
#   three ($OBJ $DRV $OST). Leaving stale /tmp binaries could mask a later
#   recompile failure. The original runner cleans up; preserve that discipline.

# GOTCHA — distinct exit codes aid triage: 2 (compile), 3 (dispatch link), 4 (os
#   link), 1 (gate fail: either binary had fails>0 or rc!=0), 0 (pass). The
#   original used 2/3/1; this task adds 4 for the os link. Keep them distinct.

# GOTCHA — the PASSED condition must check BOTH binaries. The original checked
#   `[ "$fails" -eq 0 ] && [ $rc -eq 0 ]` for one binary; the new one checks all
#   four (fails_d, rc_d, fails_o, rc_o). A common bug is to forget the os half.

# GOTCHA — do NOT add -Wextra to the link steps. The [1/4] compile uses -Wall
#   -Wextra (surfaces notifier.c warnings); the link steps use -Wall only (matching
#   the original [2/3]). The official §11.1 test_notifier_os command has NO -Wall at
#   all; the runner is STRICTER (-Wall on links), which is fine because the official
#   test (per the P1.M2.T1.S1 PRP) puts an explicit `, false` on every map row.

# GOTCHA — do NOT run run_all_tests.sh to validate the STUB gate. run_all_tests.sh
#   builds only the 9 pattern_match suites (it does NOT build notifier.c or the stub
#   binaries). The stub gate is run_notifier_stub_tests.sh. They are complementary:
#   §11.2A = run_all_tests.sh; §11.2D = run_notifier_stub_tests.sh. Run BOTH.

# GOTCHA — §11.2C line 3 (`^\w+@\w+$` vs `user_host`) prints 0, NOT 1. This is
#   CORRECT (user_host has no '@'). The PRD's /* 1 */ comment is erroneous. This is
#   a pre-existing pattern_match condition, OUTSIDE this task's scope. Document the
#   actual output; do NOT "fix" it (would mean editing pattern_match.c — scope creep
#   + invariant 12). The matcher is right.
```

## Implementation Blueprint

### Data models and structure

No data models. This task edits one bash script: it adds one `/tmp` path
variable (`OST`), renumbers the step labels `[1/3]`/`[2/3]`/`[3/3]` →
`[1/4]`/`[2/4]`/`[3/4]`/`[4/4]`, inserts the os link step, extends the run step
to run both binaries, prints a second fail line, and updates the verdict
condition + header comment.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: REWRITE run_notifier_stub_tests.sh (the ONLY change)
  - KEEP: shebang, the Mode-A header (to be EXTENDED), set -u, cd dirname, the
    three core ideas (stub-compile notifier.c -> .o; link a test binary; run +
    grep FAIL + verdict). Keep -Wall -Wextra -std=c99 on the compile step.
  - ADD path: OST=/tmp/test_notifier_os  (alongside OBJ and DRV).
  - RENUMBER steps [1/3][2/3][3/3] -> [1/4][2/4][3/4][4/4].
  - [1/4] compile notifier.c -> $OBJ: UNCHANGED flags
        (-Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c).
  - [2/4] link dispatch -> $DRV: ADD -Iqmk_stubs (so `-Wall -std=c99 -Iqmk_stubs -I.`).
  - [3/4] link os -> $OST: NEW. Same flags as [2/4]: `$OBJ qmk_stubs/qmk_stubs.c
        test_notifier_os.c`. On fail: rm -f $OBJ $DRV; exit 4.
  - [4/4] run both: run $DRV (capture rc_d); fails_d=$("$DRV" 2>/dev/null | grep
        -c '^FAIL:' || true); run $OST (capture rc_o); fails_o=(... || true);
        rm -f $OBJ $DRV $OST; print BOTH fail lines; verdict: exit 0 iff
        fails_d==0 && rc_d==0 && fails_o==0 && rc_o==0, else exit 1.
  - HEADER (Mode A): state it stub-compiles notifier.c ONCE and links+runs BOTH
        test_notifier_dispatch (reassembly/F4/ack/coexistence) AND test_notifier_os
        (multi-OS F8/F9), citing PRD §11.1 + §11.2D.
  - PRESERVE: set -u; cd "$(dirname "$0")"; the grep||true idiom; the ✓/✗ verdict
    lines; /tmp cleanup discipline.
  - DO NOT: compile notifier.c twice; omit -Iqmk_stubs from either link; edit any
    other file; create test_notifier_os.c; touch qmk_stubs.c.

Task 2: VERIFY the full §11.2 A-D gate (run the Validation Loop, Levels 1-4).
```

**The exact `run_notifier_stub_tests.sh` to write** (verbatim — executed
end-to-end during research with the validated test stand-in; 55 lines):

```bash
#!/usr/bin/env bash
# P2 stub-compile validation gate for notifier.c (closes RISK-1).
#
# notifier.c cannot compile standalone: it does `#include QMK_KEYBOARD_H` (a
# -D-expanded header name) and pulls in QMK symbols (layer_on/layer_off,
# raw_hid_send) that the 9-suite corpus — which links only pattern_match.c —
# cannot provide. This harness substitutes minimal QMK stubs so the receiver /
# reassembler / F4 delimiter matcher / dispatcher ordering / hid_notify ack
# logic AND the multi-OS map-selection (F8) / OS-change-clear (F9) logic can be
# validated with plain gcc on a host. It builds BOTH the dispatch test
# (test_notifier_dispatch) and the multi-OS test (test_notifier_os) from a
# SINGLE stub-compiled notifier.o (PRD §11.1, §11.2D). See PRP P2 / P1.M2.T2.
set -u
cd "$(dirname "$0")"

OBJ=/tmp/notifier_stub.o
DRV=/tmp/test_notifier_dispatch
OST=/tmp/test_notifier_os

echo "[1/4] stub-compile notifier.c (shared by both test binaries) ..."
gcc -Wall -Wextra -std=c99 \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. \
    -c notifier.c -o "$OBJ"
if [ $? -ne 0 ]; then echo "COMPILE FAILED"; exit 2; fi

echo "[2/4] link dispatch driver (test_notifier_dispatch) ..."
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    "$OBJ" qmk_stubs/qmk_stubs.c test_notifier_dispatch.c \
    -o "$DRV"
if [ $? -ne 0 ]; then echo "LINK FAILED (dispatch)"; rm -f "$OBJ"; exit 3; fi

echo "[3/4] link multi-OS driver (test_notifier_os) ..."
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    "$OBJ" qmk_stubs/qmk_stubs.c test_notifier_os.c \
    -o "$OST"
if [ $? -ne 0 ]; then echo "LINK FAILED (os)"; rm -f "$OBJ" "$DRV"; exit 4; fi

echo "[4/4] run both ..."
"$DRV"
rc_d=$?
fails_d=$("$DRV" 2>/dev/null | grep -c '^FAIL:' || true)
"$OST"
rc_o=$?
fails_o=$("$OST" 2>/dev/null | grep -c '^FAIL:' || true)
echo "------------------------------------------------"
echo "notifier dispatch fails=$fails_d  (exit=$rc_d)"
echo "notifier os fails=$fails_o  (exit=$rc_o)"
rm -f "$OBJ" "$DRV" "$OST"
if [ "$fails_d" -eq 0 ] && [ $rc_d -eq 0 ] && [ "$fails_o" -eq 0 ] && [ $rc_o -eq 0 ]; then
    echo "✓ notifier stub-compile gate PASSED"
    exit 0
fi
echo "✗ notifier stub-compile gate FAILED"
exit 1
```

### Implementation Patterns & Key Details

```bash
# PATTERN: object-compile once, link many. notifier.c is the expensive, flag-heavy
#   compile (-DQMK_KEYBOARD_H -Iqmk_stubs -I. -Wall -Wextra). Produce $OBJ once,
#   then link it into each test binary with just the test TU + qmk_stubs.c. This
#   matches the original runner's discipline and the §11.1 intent (one notifier.c,
#   two consumers).

# PATTERN: fail-fast with distinct exit codes. 2=compile, 3=dispatch-link,
#   4=os-link, 1=gate(fails), 0=pass. A non-zero exit from the runner unambiguously
#   tells CI/developer WHICH stage broke.

# PATTERN: count via `2>/dev/null | grep -c '^FAIL:' || true`. The stub's stderr
#   ([stub] layer_on…) is human diagnostics, not failures — suppress when counting.
#   The `|| true` is MANDATORY under set -u (grep exits 1 on zero matches).

# PATTERN: verdict checks BOTH binaries (fails AND rc for each). rc and fails are
#   redundant signals (a test binary exits non-zero iff it printed a FAIL:), but
#   checking both is belt-and-suspenders and matches the original runner.

# ANTI-PATTERN: do NOT use `-I.` alone on the link steps — notifier.h includes
#   "os_detection.h" which is in qmk_stubs/. Use `-Iqmk_stubs -I.`. (The item-spec's
#   example [2b] with `-I.` only is wrong; empirically it fails.)

# ANTI-PATTERN: do NOT compile notifier.c twice (once per binary). Share $OBJ.

# ANTI-PATTERN: do NOT add -Wextra to the link steps (keep -Wall, matching the old
#   [2/3]). -Wextra stays on the [1/4] compile step only.

# ANTI-PATTERN: do NOT drop the `|| true` after grep -c — a passing binary (0
#   matches) makes grep exit 1, aborting the script under set -u.

# ANTI-PATTERN: do NOT forget to clean up $OST in the final rm — leaving it stale
#   in /tmp could mask a future recompile.

# ANTI-PATTERN: do NOT run run_all_tests.sh as the stub gate — it builds only the
#   9 pattern_match suites, not notifier.c. §11.2A = run_all_tests.sh; §11.2D =
#   run_notifier_stub_tests.sh. They are complementary; run both.

# ANTI-PATTERN: do NOT "fix" the §11.2C line that prints 0 — it is correct
#   (user_host has no '@'); the PRD comment is wrong. Editing pattern_match.c to
#   force a 1 would be scope creep + invariant-12 violation.
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - REWRITE run_notifier_stub_tests.sh in place (repo root). ~37 -> ~55 lines.
  - No other file changes.

CONSUMES (LANDED / parallel — unchanged by this task):
  - notifier.c (multi-OS dispatch + notifier_set_os)         [P1.M1.T3 — COMPLETE]
  - notifier.h (#include os_detection.h + DEFINE_*_OS)       [P1.M1.T1 — COMPLETE]
  - qmk_stubs/qmk_stubs.c (stub_get_active_layer accessor)   [P1.M2.T1.S1 — accessor LANDED]
  - test_notifier_dispatch.c (backward-compat canary, 11/11) [unchanged]
  - test_notifier_os.c (multi-OS F8/F9 test)                 [P1.M2.T1.S1 — parallel, NOT yet present]
  - qmk_stubs/os_detection.h (os_variant_t enum)             [P1.M1.T2 — COMPLETE]

BUILD:
  - No build-system change (rules.mk untouched). Plain gcc via the runner.
  - §11.2A entry point: ./run_all_tests.sh (unchanged).
  - §11.2D entry point: ./run_notifier_stub_tests.sh (this task).

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module host-test runner).
```

## Validation Loop

> Toolchain: gcc + bash (C project — no ruff/mypy/pytest). All commands were
> **executed during research** and their outputs recorded. NOTE: the §11.2D gate
> can only fully pass once the parallel P1.M2.T1.S1 lands `test_notifier_os.c`;
> until then `[3/4]` fails with "cannot find test_notifier_os.c" (ordering
> dependency, not a runner bug). The runner mechanics were verified during
> research using the validated `test_notifier_os_val.c` as a stand-in.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Bash syntax check (no execution) — catches quoting/structure errors.
bash -n run_notifier_stub_tests.sh && echo "bash -n: OK"
# Expected: "bash -n: OK", exit 0.

# 1b. The runner is executable (preserve the +x bit).
[ -x run_notifier_stub_tests.sh ] && echo "executable: OK" || echo "MISSING +x"
# Expected: "executable: OK".

# 1c. Confirm the structure: 4 renumbered steps, both binaries, -Iqmk_stubs on links.
grep -c '^\[1/4\]\|^\[2/4\]\|^\[3/4\]\|^\[4/4\]' run_notifier_stub_tests.sh   # expect 4
grep -q 'OST=/tmp/test_notifier_os' run_notifier_stub_tests.sh && echo "OST path present"
grep -q 'test_notifier_os.c' run_notifier_stub_tests.sh && echo "os test source present"
# BOTH link steps must have -Iqmk_stubs (NOT just -I.):
awk '/link (dispatch|multi-OS)/{f=NR} f&&/gcc/{print NR": "$0; f=0}' run_notifier_stub_tests.sh \
  | grep -q -- '-Iqmk_stubs' && echo "link steps have -Iqmk_stubs (ok)" \
  || echo "ERROR: a link step is missing -Iqmk_stubs"
# notifier.c compiled ONCE (only one `-c notifier.c`):
[ "$(grep -c -- '-c notifier.c' run_notifier_stub_tests.sh)" -eq 1 ] && echo "notifier.c compiled once (ok)" \
  || echo "ERROR: notifier.c compiled more than once"
# Both fail lines present:
grep -q 'notifier dispatch fails=' run_notifier_stub_tests.sh && \
grep -q 'notifier os fails=' run_notifier_stub_tests.sh && echo "both fail lines present (ok)"

# 1d. Header (Mode A) mentions BOTH binaries + cites §11.1/§11.2D.
head -16 run_notifier_stub_tests.sh | grep -q 'test_notifier_dispatch' && \
head -16 run_notifier_stub_tests.sh | grep -q 'test_notifier_os' && echo "header mentions both binaries (ok)"
head -16 run_notifier_stub_tests.sh | grep -qE '11\.1|11\.2D|§11' && echo "header cites §11.1/§11.2D (ok)"
```

### Level 2: §11.2D — the stub-compile gate (THE PRIMARY GATE)

```bash
cd /home/dustin/projects/qmk-notifier

# PREREQ: test_notifier_os.c must exist (parallel P1.M2.T1.S1). If absent, this
# fails at [3/4] with "cannot find test_notifier_os.c" — that is an ordering
# dependency, not a runner bug.
./run_notifier_stub_tests.sh 2>/dev/null | grep -E '^\[|fails=|PASSED|FAILED|FAILED \('
echo "runner exit=${PIPESTATUS[0]}"
# Expected (when test_notifier_os.c is present):
#   [1/4] stub-compile notifier.c (shared by both test binaries) ...
#   [2/4] link dispatch driver (test_notifier_dispatch) ...
#   [3/4] link multi-OS driver (test_notifier_os) ...
#   [4/4] run both ...
#   notifier dispatch fails=0  (exit=0)
#   notifier os fails=0  (exit=0)
#   ✓ notifier stub-compile gate PASSED
#   runner exit=0
# FAIL if: either fails>0, or exit!=0, or any LINK FAILED line appears.

# 2b. No new compiler warnings from the compile step (the -Wall -Wextra surfaces them).
./run_notifier_stub_tests.sh 2>&1 | grep -iE 'warning:' || echo "0 warnings (ok)"
# Expected: "0 warnings (ok)" — or only pre-existing warnings, if any. (During
# research the compile was clean.)

# 2c. Backward-compat canary: test_notifier_dispatch STILL 11/11, 0 FAIL
#     (unchanged — this task does not edit it; only the -Iqmk_stubs fix lets it link).
#     (The runner already built+ran it above; this just confirms the count.)
./run_notifier_stub_tests.sh 2>/dev/null | grep 'notifier dispatch fails=0'
```

### Level 3: §11.2A — pattern_match corpus (regression gate, UNAFFECTED)

```bash
cd /home/dustin/projects/qmk-notifier

# §11.2A: all 9 suites 0 failures, runner exit 0.
./run_all_tests.sh >/tmp/ra.out 2>&1; echo "run_all_tests exit=$?  (expect 0)"
grep -E 'Total tests failed:|ALL TESTS PASSED|SOME TESTS FAILED' /tmp/ra.out
# Expected: "Total tests failed: 0", "✓ ALL TESTS PASSED - BACKWARD COMPATIBILITY VERIFIED".

# Per-suite FAIL: counts (belt-and-suspenders) — every line must be fails=0.
for t in test_pattern_match test_char_classification test_word_boundary_basic \
         test_word_boundary_integration test_metachar_verification \
         test_comprehensive_integration test_error_handling test_memory_stress \
         test_invalid_patterns; do
  printf "%-36s fails=%s\n" "$t" "$(./$t 2>&1 | grep -c '^FAIL:')"
done
# Expected: fails=0 for every line. (Matcher is P1 scope, complete, untouched.)
```

### Level 4: §11.2B + §11.2C + the full delta verification

```bash
cd /home/dustin/projects/qmk-notifier

# §11.2B: pathological NFA stress — result=0 in < 50 ms.
cat > /tmp/nfa_stress.c <<'EOF'
#include <stdio.h>
#include <time.h>
#include "pattern_match.h"
int main(void){
  char s[200]; for(int i=0;i<199;i++) s[i]='a'; s[199]='\0';
  const char* p="a+a+a+a+a+a+a+a+a+a+b";
  clock_t t=clock(); int r=pattern_match(p,s,1);
  printf("result=%d  %.1f us\n", r, 1e6*(double)(clock()-t)/CLOCKS_PER_SEC);
  return 0;
}
EOF
gcc -O2 -w /tmp/nfa_stress.c pattern_match.c -I. -o /tmp/nfa_stress
timeout 5 /tmp/nfa_stress
# Expected: "result=0  <50000.0 us" (research: 1816.0 us). FAIL if result!=0 or >50000 us.

# §11.2C: realistic patterns. NOTE: line 3 prints 0 (CORRECT — user_host has no '@';
#         the PRD /* 1 */ comment is erroneous). This is pre-existing, out of scope.
cat > /tmp/nfa_real.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  printf("%d\n", pattern_match("\\w+","hello",1));                       /* 1 */
  printf("%d\n", pattern_match("\\b\\w+\\b\\s+\\b\\w+\\b","hello world",1)); /* 1 */
  printf("%d\n", pattern_match("^\\w+@\\w+$","user_host",1));            /* 0 (no '@' — PRD comment is wrong) */
  printf("%d\n", pattern_match("v\\.code","v.code",1));                  /* 1 */
  printf("%d\n", pattern_match("a+b","aaab",1));                         /* 1 */
  printf("%d\n", pattern_match("*slack*","Slack - general",0));          /* 1 */
  return 0;
}
EOF
gcc -w /tmp/nfa_real.c pattern_match.c -I. -o /tmp/nfa_real && /tmp/nfa_real
# Expected output (exact): "1\n1\n0\n1\n1\n1". The 0 on line 3 is CORRECT.

# 4c. Diff hygiene: ONLY run_notifier_stub_tests.sh changed, plus plan/ PRP/research.
git status --porcelain
# Expected: ` M run_notifier_stub_tests.sh` and `?? plan/002_c243e735980a/P1M2T2S1/`.
#           NOTHING else (notifier.c/h, pattern_match.*, qmk_stubs/*, test_*.c,
#           run_all_tests.sh, rules.mk, PRD.md, tasks.json, .gitignore untouched).
git diff --stat -- run_notifier_stub_tests.sh
# Expected: run_notifier_stub_tests.sh shows the renumber + os step additions
#           (~37 -> ~55 lines). Inspect `git diff run_notifier_stub_tests.sh`.

rm -f /tmp/nfa_stress.c /tmp/nfa_stress /tmp/nfa_real.c /tmp/nfa_real /tmp/ra.out
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `bash -n run_notifier_stub_tests.sh` → OK; executable bit present;
      4 renumbered steps; both link steps have `-Iqmk_stubs`; notifier.c compiled
      once; both fail lines present; Mode-A header mentions both binaries + §11.1/§11.2D.
- [ ] Level 2 (§11.2D): `./run_notifier_stub_tests.sh` → both `fails=0 / exit=0`,
      `✓ notifier stub-compile gate PASSED`, exit 0; 0 new warnings.
- [ ] Level 3 (§11.2A): `./run_all_tests.sh` → `Total tests failed: 0`, exit 0;
      every suite `fails=0`.
- [ ] Level 4 (§11.2B): `/tmp/nfa_stress` → `result=0` in < 50 ms.
- [ ] Level 4 (§11.2C): `/tmp/nfa_real` → `1 1 0 1 1 1` (the `0` is correct/pre-existing).
- [ ] Level 4: `git status` shows ONLY `run_notifier_stub_tests.sh` modified +
      plan/ PRP/research.

### Feature Validation

- [ ] Runner compiles `notifier.c` ONCE into `$OBJ`; links BOTH binaries from it.
- [ ] Both link steps use `-Iqmk_stubs -I.` (NOT `-I.` alone — the gap is fixed).
- [ ] `[3/4]` builds `test_notifier_os`; `[4/4]` runs BOTH binaries.
- [ ] Summary prints `notifier dispatch fails=N` AND `notifier os fails=N`.
- [ ] Verdict: PASSED (exit 0) iff both binaries 0 fails AND exit 0.
- [ ] `test_notifier_dispatch` still 11/11, 0 FAIL (no regression, no edit).
- [ ] Distinct exit codes (2/3/4/1/0) preserved/extended.

### Code Quality Validation

- [ ] Mirrors the original runner's discipline (`set -u`, `cd dirname`, `/tmp`
      paths, `grep -c … || true`, `rm -f` cleanup, ✓/✗ verdicts).
- [ ] Header comment (Mode A) updated to describe both binaries + cite PRD §11.1/§11.2D.
- [ ] ~55 lines (grows ~37 → ~55, within the item-spec "~45 -> ~55" target).
- [ ] No anti-patterns (see below): no `-I.`-only links, no double-compile of
      notifier.c, no dropped `|| true`, no `-Wextra` on link steps.

### Documentation & Deployment

- [ ] Header comment (Mode A) explains the runner builds BOTH the dispatch and the
      multi-OS os test binaries from one shared `notifier.o`.
- [ ] The printed summary shows BOTH fail counts (dispatch + os).
- [ ] No new env vars / config / build-system changes (rules.mk untouched).
- [ ] README multi-OS section is P1.M2.T3.S1 (NOT this task).

---

## Anti-Patterns to Avoid

- ❌ Don't use `-I.` alone on the link steps — notifier.h includes "os_detection.h"
  which is in `qmk_stubs/`. Use `-Iqmk_stubs -I.` on BOTH link steps. The
  item-spec's example `[2b]` with `-I.` only is wrong (empirically fails with
  `os_detection.h: No such file or directory`).
- ❌ Don't compile `notifier.c` twice (once per binary). Object-compile ONCE into
  `$OBJ` and link it into both (item-spec 3a; matches the original runner).
- ❌ Don't drop the `|| true` after `grep -c '^FAIL:'` — a passing binary (0
  matches) makes grep exit 1, aborting the script under `set -u`.
- ❌ Don't count `FAIL:` from stderr — the stub prints `[stub] layer_on(...)` traces
  to stderr. Count from stdout (`2>/dev/null | grep -c`), mirroring the original.
- ❌ Don't forget to check BOTH binaries in the verdict — a common bug is to
  forget the os half (`fails_o`/`rc_o`).
- ❌ Don't add `-Wextra` to the link steps (keep `-Wall`, matching the old `[2/3]`).
  `-Wextra` stays on the `[1/4]` compile step only.
- ❌ Don't create `test_notifier_os.c` or touch `qmk_stubs.c` — those are the
  parallel P1.M2.T1.S1's deliverables (the accessor is already landed). This task
  only consumes them via the link step.
- ❌ Don't run `run_all_tests.sh` as the stub gate — it builds only the 9
  pattern_match suites, not notifier.c. §11.2A = `run_all_tests.sh`; §11.2D =
  `run_notifier_stub_tests.sh`. Run both.
- ❌ Don't "fix" the §11.2C line that prints `0` (`^\w+@\w+$` vs `user_host`) — it
  is CORRECT (no `@` in the input); the PRD comment is wrong. Editing
  pattern_match.c to force a `1` would be scope creep + invariant-12 violation.
- ❌ Don't edit `notifier.c`, `notifier.h`, `pattern_match.*`, `test_*.c`,
  `run_all_tests.sh`, `rules.mk`, `PRD.md`, `tasks.json`, `prd_snapshot.md`, or
  `.gitignore`. Only `run_notifier_stub_tests.sh` changes.

---

## Confidence Score: 10/10

The deliverable is a single-file edit (`run_notifier_stub_tests.sh`) whose exact
55-line content is given verbatim above and was **executed end-to-end during
research** (with the validated `test_notifier_os_val.c` staged as a stand-in for
the not-yet-landed `test_notifier_os.c`): it compiled `notifier.c` once, linked
both binaries with `-Iqmk_stubs -I.`, ran both with `fails=0 / exit=0`, and
printed `✓ notifier stub-compile gate PASSED` / exit 0. The **single
non-obvious, load-bearing finding** — that the current runner is ALREADY BROKEN
(`LINK FAILED`, exit 3) because its link steps lack `-Iqmk_stubs` (notifier.h:3
now includes "os_detection.h", added by the COMPLETE P1.M1.T1.S1) — was
**empirically diagnosed and resolved** (add `-Iqmk_stubs` to both link steps),
directly contradicting the item-spec's erroneous `-I.`-only `[2b]` example. The
full §11.2 A-D baseline was run during research and the exact outputs recorded
(A: 9 suites 0 fails; B: result=0 in 1.8 ms; C: `1 1 0 1 1 1` with the `0`
correctly documented; D: both binaries 0 fails). The §11.2C discrepancy (PRD's
`/* 1 */` comment vs the correct `0`) is flagged as pre-existing/out-of-scope so
the implementer does not mistake it for a regression. Dependencies (the parallel
P1.M2.T1.S1's `test_notifier_os.c` + the already-landed `stub_get_active_layer`
accessor) and the boundaries with P1.M2.T3.S1 (README) are explicit. No external
dependencies are added.