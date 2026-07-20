# PRP — P1.M2.T2.S1: Re-run the full §11.2 host gate; confirm module flow is byte-identical

> **⚠️ TARGET REPO — read first.** This task runs against the **firmware module repo**
> `/home/dustin/projects/qmk_notifier` (remote `dabstractor/qmk_notifier`, underscore),
> NOT the Rust transport-crate repo (`/home/dustin/projects/qmk-notifier`, hyphen) that
> is the harness cwd. The §18 M1 artifacts, the test gates, `notifier.{c,h}`,
> `pattern_match.*`, and this plan's `tasks.json` + prior PRPs all live in the firmware
> repo. **Run every command from `/home/dustin/projects/qmk_notifier`.** (The crate
> repo's `plan/004` is a stray copy with no source/gates.)

> **⚠️ TASK NATURE: VERIFICATION, not implementation.** This task writes NO source
> code. The deliverable is the **green gate run + the R6 byte-identical confirmation**.
> The PRP is a runbook of exact commands + expected outputs, ALL of which were
> **executed during research and PASSED**. The "implementation" is running the
> Validation Loop and recording the results.

## Goal

**Feature Goal**: Verify that the §18 Community Module Distribution changes (M1:
`qmk_module.json` R1, module-context `rules.mk` R2, the `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION`
guard R3; M2.T1: README rewrite) introduced **NO behavioral regression**. Per PRD
§18.3 R6 and §18.5, prove: (1) the §11.2A pattern-match corpus is green (9 suites +
NFA-128, 0 failures, perf sub-second); (2) the §11.2D notifier stub-compile gate is
green (dispatch/os/host each 0 `FAIL:`, `✓ gate PASSED`) — the **direct proof the R3
`#ifdef` guard did not perturb the stub build**; (3) the module-build criterion
(`qmk compile` of a module keymap) is marked **DEFERRED** (no `~/qmk_userspace`);
(4) the R6 invariant holds — `notifier.h`, `pattern_match.c`, `pattern_match.h` are
byte-for-byte unchanged vs HEAD.

**Deliverable**: A completed verification run (the gates executed + results recorded).
No source files change. The only artifacts written are this PRP + the research notes
under `plan/004_76ea306f6be9/P1M2T2S1/`.

**Success Definition** (all empirically validated during research):
- `./run_all_tests.sh` → **2029** total tests, every suite `ALL TESTS PASSED`, perf
  `< 1 second`, exit 0; per-suite `grep -c '^FAIL:'` == 0 for all 9 pattern suites.
- `./run_notifier_stub_tests.sh` → `notifier dispatch fails=0` / `notifier os fails=0` /
  `notifier host fails=0`, `✓ notifier stub-compile gate PASSED`, exit 0.
- §11.2B pathological stress → `result=0` in < 50 ms (research: 1.8 ms).
- §11.2C realistic patterns → six `1`s.
- `git diff --stat HEAD -- notifier.h pattern_match.c pattern_match.h` → **empty** (R6 holds).
- The module-build (`qmk compile`) criterion is recorded **DEFERRED** (no userspace).

## User Persona (if applicable)

**Target User**: The maintainer signing off the §18 migration (PRD §18.5 Acceptance)
and any reviewer who needs proof the migration is behaviorally transparent (R6).

**Use Case**: After M1 + M2.T1 land, run the §11.2 gate as-is and confirm it is
byte-identical to the HEAD baseline (`run_all_tests.sh` 2029/2029; `run_notifier_stub_tests.sh`
dispatch=0/os=0/host=0). This is the regression gate for the whole §18 changeset.

**User Journey**: M1+M2.T1 merged → run the 4 verification commands → all green →
the §18 migration is provably non-behavioral; the module is structurally
distributable AND behaviorally identical to the pre-migration firmware.

**Pain Points Addressed**: Without this verification, a subtle regression in the R3
guard (e.g. the `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION` leaking outside its
`#ifdef` and breaking the stub compile) or an accidental edit to the matcher/public
API during the §18 plumbing could ship undetected. This task is the safety net.

## Why

- **Closes the §18.5 Acceptance gate "Host test gates unchanged."** PRD §18.5 lists
  it as an explicit acceptance criterion; until it is re-run with the R3 guard in
  place, the §18 migration cannot be marked done.
- **The R3 guard is the one §18 change that touches a source file (`notifier.c`).
  Its `#ifdef COMMUNITY_MODULES_API_VERSION` wrapper MUST be a no-op in the host/stub
  build (where the macro is undefined). The green `run_notifier_stub_tests.sh` is the
  DIRECT, executable proof — no static reasoning required.**
- **Locks the R6 invariant empirically.** R6 ("byte-for-byte unchanged" for
  `notifier.h`/`pattern_match.c`/`pattern_match.h`) is a git diff away; recording it
  green proves the migration did not accidentally touch the matcher or the public API.
- **Honest about the module-build limit.** A full `qmk compile` of a module keymap
  needs a userspace (absent here). Per the contract, that sub-criterion is DEFERRED
  (not silently skipped) — the host gates + R6 are the authoritative proof available
  in this environment.

## What

Run four verification probes from `/home/dustin/projects/qmk_notifier`, recording
each result. None of them modify any file.

1. **§11.2A — `./run_all_tests.sh`** → expect 2029 total tests, every suite
   `ALL TESTS PASSED`, perf `< 1 second`, exit 0. (Belt-and-suspenders: per-suite
   `grep -c '^FAIL:'` == 0 for the 9 pattern_match suites.)
2. **§11.2D — `./run_notifier_stub_tests.sh`** → expect `notifier dispatch fails=0` /
   `notifier os fails=0` / `notifier host fails=0`, `✓ notifier stub-compile gate
   PASSED`, exit 0. **This is the load-bearing R3-safety proof.**
3. **§11.2B + §11.2C micro-probes** → `/tmp/nfa_stress` prints `result=0` in < 50 ms;
   `/tmp/nfa_real` prints six `1`s. (These reinforce §11.2A but are cheap to include.)
4. **R6 invariant — `git diff --stat HEAD -- notifier.h pattern_match.c pattern_match.h`**
   → expect **empty** (no output). Record the per-file `git ls-files -s` blob hashes
   for the audit trail.

The module-build (`qmk compile`) criterion is **DEFERRED** (no `~/qmk_userspace`).
Record it as deferred; do NOT attempt a userspace setup. If `qmk_module.json` (R1)
exists at verification time, additionally validate it against the community_module
schema as a fallback per the contract; if it is absent, flag the R1 gap (it is M1.T1's
deliverable, not a blocker for this host-gate verification).

### Success Criteria

- [ ] `./run_all_tests.sh` → 2029 tests, all suites PASS, perf < 1s, exit 0; 9 pattern suites each `fails=0`.
- [ ] `./run_notifier_stub_tests.sh` → dispatch/os/host each `fails=0`, `✓ gate PASSED`, exit 0.
- [ ] §11.2B → `result=0` in < 50 ms; §11.2C → six `1`s.
- [ ] `git diff --stat HEAD -- notifier.h pattern_match.c pattern_match.h` → empty (R6 holds).
- [ ] Module-build (`qmk compile`) recorded DEFERRED (no userspace).
- [ ] No source file modified by this task (verification only).

## All Needed Context

### Context Completeness Check

**Pass.** This is a verification runbook: every command + expected output below was
**executed during research from `/home/dustin/projects/qmk_notifier` and PASSED**.
The baseline (2029/2029; dispatch/os/host 0; perf 0.079s) is recorded in
`research/findings.md`. The §18 M1 state (R2 rules.mk LANDED, R3 guard LANDED, R1
qmk_module.json ABSENT, R5 README in progress) was verified by `git log`/`git status`/`ls`.
The R6 invariant was confirmed green (`git diff --stat HEAD` empty). An implementer
with only this PRP + the firmware repo can run the 4 probes and record green results
with zero guessing.

### Documentation & References

```yaml
# MUST READ — the verification contract (what "done" means for §18)
- file: PRD.md   (firmware repo; snapshot: plan/004_76ea306f6be9/prd_snapshot.md)
  section: "### 18.5 Acceptance" + "### 18.3 Requirements → R3, R6"
  why: "§18.5 lists 'Host test gates unchanged: ./run_all_tests.sh and ./run_notifier_stub_tests.sh
        pass with the R3 guard in place.' R3 specifies the #ifdef-wrapped guard; R6 mandates
        'byte-for-byte unchanged' for the wire protocol, matcher, dispatch, and public API."
  critical: "The R3 guard is the ONE §18 change touching a source file (notifier.c). Its
        #ifdef COMMUNITY_MODULES_API_VERSION wrapper must be a no-op in the stub build — the
        green run_notifier_stub_tests.sh is the direct proof. R6 covers notifier.h/pattern_match.*."

# MUST READ — the acceptance gate being re-run (§11.2 A/B/C/D)
- file: PRD.md
  section: "### 11.2 Acceptance gate — all must be true"
  why: "Defines the four probes: (A) every suite 0 failures; (B) pathological NFA < 50 ms
        result=0; (C) six realistic patterns print 1; (D) run_notifier_stub_tests.sh ends
        ✓ gate PASSED with dispatch/os each 0 FAIL."
  critical: "The §11.2D 'host' binary (test_notifier_host) is also in run_notifier_stub_tests.sh
        (the firmware repo has 3 stub drivers: dispatch/os/host) — the contract's 'dispatch/os/host'
        maps to those 3. All must be fails=0."

# MUST READ — the baseline (what GREEN looks like, verified at HEAD this session)
- file: plan/004_76ea306f6be9/architecture/system_context.md
  section: "### Baseline Test Gates (re-verified)"
  why: "Records the HEAD baseline: run_all_tests.sh 2029/2029 assertions, perf 0.11 us/call;
        run_notifier_stub_tests.sh dispatch=0/os=0/host=0 fails, ✓ gate PASSED. This task
        re-confirms EXACTLY those numbers with the §18 changes in place."
  critical: "The system_context.md was written BEFORE M1 landed (it lists the artifacts as
        'absent/to-be-created'). The CURRENT state (research): R2 rules.mk LANDED, R3 guard
        LANDED, R1 qmk_module.json STILL ABSENT. Re-verify each artifact's presence at run time."

# The R3 guard under test (confirm it is #ifdef-wrapped)
- file: notifier.c   (firmware repo)
  section: "lines 5-15 (Community Module API version guard)"
  why: "The guard: '#ifdef COMMUNITY_MODULES_API_VERSION / ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1,0,0); / #endif'.
        In the stub build (-DQMK_KEYBOARD_H='\"qmk_keyboard_stub.h\"') neither symbol is defined,
        so the block is skipped — proven by the green stub gate."
  critical: "If a future edit moves the ASSERT outside the #ifdef, the stub compile breaks
        immediately (ASSERT_COMMUNITY_MODULES_MIN_API_VERSION undeclared). The §11.2D run is the
        canary."

# The R6 files (confirm unchanged)
- file: notifier.h / pattern_match.c / pattern_match.h   (firmware repo)
  why: "R6 invariant: these three must be byte-for-byte unchanged by the §18 changeset.
        'git diff --stat HEAD -- <files>' is the authoritative check (empty = unchanged)."
  critical: "Do NOT use raw sha1sum vs git ls-files -s to compare — git blob hashes prepend a
        'blob <size>\\0' header, so they differ from raw file sha1sum even for identical content.
        'git diff --stat HEAD' is the correct check."

# What M1 landed (the state under test)
- file: plan/004_76ea306f6be9/P1M1T2S1/PRP.md   (firmware repo)
  why: "rules.mk is now module-context (RAW_ENABLE = yes + SRC += notifier.c). The §11.2 gates
        do NOT exercise rules.mk (host tests compile sources directly), so R2 cannot affect them
        — but recording rules.mk's state confirms the migration is in place."
- file: plan/004_76ea306f6be9/P1M1T3S1/PRP.md   (firmware repo)
  why: "The R3 guard landed in notifier.c. THIS task proves it is stub-build-safe (the §11.2D run)."
- file: plan/004_76ea306f6be9/P1M2T1S1/PRP.md   (firmware repo, parallel)
  why: "The README rewrite (R5) is Markdown-only and cannot affect the gates or R6. Confirmed:
        the gates are green with 'M README.md' in the working tree."

# Scope boundary — the later sweep task
- file: plan/004_76ea306f6be9/tasks.json   (firmware repo)
  section: "P1.M2.T3 (Sync changeset-level documentation)"
  why: "P1.M2.T3.S1 sweeps the REST of the README/overview for stray submodule references.
        THIS task is verification only — it writes no docs and no source."
  critical: "Do NOT edit README or any source file. This task's outputs are the gate results +
        the research/PRP under plan/."
```

### Current Codebase tree (relevant slice — firmware repo, post-M1 + parallel M2.T1)

```bash
notifier.c                # LANDED R3 guard (lines 5-15). The one §18 source edit. Under test (verify #ifdef-safety via §11.2D).
notifier.h                # R6 invariant — must be unchanged. VERIFY (git diff --stat HEAD).
pattern_match.c           # R6 invariant — must be unchanged. VERIFY.
pattern_match.h           # R6 invariant — must be unchanged. VERIFY.
rules.mk                  # LANDED R2 module-context. (Not exercised by host gates; record state.)
qmk_module.json           # R1 — ABSENT at research time (M1.T1 gap). If absent at run time, flag; do not create.
README.md                 # parallel P1.M2.T1.S1 (M, Markdown-only). Cannot affect gates/R6.
run_all_tests.sh          # §11.2A gate. RUN (expect 2029, all PASS, perf <1s).
run_notifier_stub_tests.sh# §11.2D gate (dispatch/os/host). RUN (expect 3× fails=0, ✓ PASSED).
test_*.c                  # the suites. DO NOT TOUCH.
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be changed

```bash
# (NO source/build/test file changes. This task writes only:)
plan/004_76ea306f6be9/P1M2T2S1/PRP.md            # this file
plan/004_76ea306f6be9/P1M2T2S1/research/findings.md
```

### Known Gotchas of our codebase & Library Quirks

```bash
# CRITICAL — TARGET REPO: run every command from /home/dustin/projects/qmk_notifier (firmware,
#   underscore). The harness cwd /home/dustin/projects/qmk-notifier (hyphen) is the Rust CRATE
#   repo — it has no run_all_tests.sh / notifier.c / plan/004 source. cd to the firmware repo first.

# CRITICAL — this is VERIFICATION. Write NO source file. The deliverable is the green gate run
#   + the R6 confirmation. If a gate FAILS, that is a real regression to REPORT (and root-cause
#   in M1/M2.T1), not something to "fix" by editing a test or source file in this task.

# CRITICAL — the R3 guard's #ifdef is the load-bearing detail. In the stub build
#   (-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"') COMMUNITY_MODULES_API_VERSION is undefined, so the
#   ASSERT is skipped. The green run_notifier_stub_tests.sh is the PROOF. If the stub gate fails
#   with an 'ASSERT_COMMUNITY_MODULES_MIN_API_VERSION undeclared' error, the guard leaked outside
#   its #ifdef — that is an R3 regression to report.

# GOTCHA — R6 hash check: use 'git diff --stat HEAD -- notifier.h pattern_match.c pattern_match.h'
#   (empty = unchanged). Do NOT compare raw sha1sum to git ls-files -s blob hashes — git blob
#   hashes prepend a 'blob <size>\0' header so they always differ from raw file sha1sum.

# GOTCHA — qmk_module.json (R1) is ABSENT at research time despite plan_status marking P1.M1.T1.S1
#   "Complete". .gitignore does NOT ignore it (genuine gap). If still absent at verification time,
#   FLAG it (it is M1.T1's deliverable). The host-gate verification (this task's core) does NOT
#   depend on it. Do NOT create it here.

# GOTCHA — module-build (qmk compile) is DEFERRED: no ~/qmk_userspace. A Community Module needs a
#   userspace that lists the module in keymap.json to compile. Do NOT attempt a userspace setup in
#   this task — record the criterion DEFERRED per the contract, relying on the host gates + R6.

# GOTCHA — the firmware repo has 3 stub drivers (dispatch/os/host), so run_notifier_stub_tests.sh
#   prints THREE 'fails=' lines. The contract's 'dispatch/os/host each 0 FAIL' maps to those 3.
#   (Older PRDs mentioned only dispatch/os; host was added by the host-rules feature.)

# GOTCHA — run_all_tests.sh's total is 2029 (10 suites: 9 pattern_match + the NFA-128 fidelity
#   gate from a prior plan). The system_context.md baseline (2029/2029) matches. A number other
#   than 2029 means a suite was added/removed — investigate.

# GOTCHA — run the gates from a CLEAN working tree where possible. The parallel README edit
#   (M README.md) is Markdown-only and cannot affect the gates, so it is fine to run with it
#   present. But if any source file (notifier.{c,h}, pattern_match.*, rules.mk) shows as modified,
#   resolve that BEFORE running (it would confound the R6 check).
```

## Implementation Blueprint

### Data models and structure

None. This task produces no code/data — only verification results (recorded in the
PRP/research). The "blueprint" is the ordered probe sequence below.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 0: cd to the firmware repo and confirm the §18 state-under-test
  - cd /home/dustin/projects/qmk_notifier  (NOT the hyphen crate repo)
  - RECORD: git log --oneline -3  (expect the M1 commits: 'Migrate rules.mk to Community
    Module context', 'Enforce minimum Community Module API version').
  - RECORD: git status --porcelain  (expect M README.md [parallel] + M tasks.json; NO
    source files modified beyond M1's committed state).
  - RECORD: ls qmk_module.json  (R1 — present or absent? flag if absent).
  - RECORD: grep -n 'COMMUNITY_MODULES_API_VERSION\|ASSERT_COMMUNITY_MODULES_MIN_API_VERSION'
    notifier.c  (confirm the R3 guard is #ifdef-wrapped at lines 13-15).

Task 1: §11.2A — run ./run_all_tests.sh (the pattern-match corpus)
  - RUN: ./run_all_tests.sh > /tmp/ra.out 2>&1; echo "exit=$?"
  - ASSERT: exit == 0; grep 'Total tests run across all suites: 2029' /tmp/ra.out;
    grep '✓ ALL TESTS PASSED' /tmp/ra.out; grep 'Performance is acceptable' /tmp/ra.out.
  - ASSERT (belt-and-suspenders): for each of the 9 pattern suites, ./$t | grep -c '^FAIL:' == 0.

Task 2: §11.2D — run ./run_notifier_stub_tests.sh (THE R3-SAFETY PROOF)
  - RUN: ./run_notifier_stub_tests.sh > /tmp/ns.out 2>&1; echo "exit=$?"
  - ASSERT: exit == 0; grep 'notifier dispatch fails=0' /tmp/ns.out;
    grep 'notifier os fails=0' /tmp/ns.out; grep 'notifier host fails=0' /tmp/ns.out;
    grep '✓ notifier stub-compile gate PASSED' /tmp/ns.out.
  - This is the DIRECT proof the R3 #ifdef guard did not perturb the stub build.

Task 3: §11.2B + §11.2C micro-probes (cheap reinforcement of §11.2A)
  - BUILD/RUN /tmp/nfa_stress.c (a+a+...+b vs 199 a's): expect 'result=0' and < 50000 us.
  - BUILD/RUN /tmp/nfa_real.c (6 realistic patterns): expect six '1's.

Task 4: R6 invariant — confirm notifier.h / pattern_match.c / pattern_match.h unchanged
  - RUN: git diff --stat HEAD -- notifier.h pattern_match.c pattern_match.h
  - ASSERT: EMPTY output (no diff = unchanged).
  - RECORD (audit trail): git ls-files -s notifier.h pattern_match.c pattern_match.h
    (the blob hashes at HEAD).

Task 5: Record the module-build criterion DEFERRED + the R1 status
  - RECORD: no ~/qmk_userspace → module-build (qmk compile) criterion = DEFERRED.
  - RECORD: qmk_module.json (R1) present-or-absent. If present, optionally validate against
    the community_module schema (qmk_firmware/data/schemas/); if absent, flag the M1.T1 gap.

Task 6: CLEANUP temp files (/tmp/ra.out /tmp/ns.out /tmp/nfa_stress* /tmp/nfa_real*).
```

### Implementation Patterns & Key Details

```bash
# PATTERN: verification runbook. Each task is RUN-then-ASSERT. Record the actual output
#   (exit code + the grepped lines) so the result is auditable. The expected outputs below
#   are the research-verified baseline; if a gate produces something else, REPORT it (do not
#   "fix" a test/source file in this task).

# PATTERN: the R3 guard's #ifdef-safety is proven BY EXECUTION (§11.2D green), not by reading
#   the source. Reading the #ifdef confirms intent; running the stub gate proves it. Both matter.

# PATTERN: R6 via 'git diff --stat HEAD' (not sha1sum). git is the source of truth for
#   "unchanged vs HEAD"; raw sha1sum confounds (git blob-hash header).

# ANTI-PATTERN: do NOT run from the crate repo (hyphen). It has no gates/source. cd to the
#   firmware repo (underscore) first.

# ANTI-PATTERN: do NOT edit any source/test/runner file. A failing gate is a regression to
#   REPORT and root-cause in M1/M2.T1, not something to paper over here.

# ANTI-PATTERN: do NOT attempt a qmk userspace setup to satisfy the module-build criterion.
#   It is DEFERRED per the contract; the host gates + R6 are the available proof.

# ANTI-PATTERN: do NOT create qmk_module.json if it is absent. That is M1.T1's deliverable;
#   flag the gap. The host-gate verification (this task) does not depend on it.

# ANTI-PATTERN: do NOT compare raw sha1sum to git ls-files blob hashes to judge R6 — they
#   always differ (git blob header). Use 'git diff --stat HEAD'.
```

### Integration Points

```yaml
VERIFICATION PROBES (all from /home/dustin/projects/qmk_notifier):
  - §11.2A: ./run_all_tests.sh           → 2029 tests, all PASS, perf <1s, exit 0
  - §11.2D: ./run_notifier_stub_tests.sh → dispatch/os/host fails=0, ✓ PASSED, exit 0
  - §11.2B: /tmp/nfa_stress              → result=0, <50000 us
  - §11.2C: /tmp/nfa_real                → six 1s
  - R6:    git diff --stat HEAD -- notifier.h pattern_match.c pattern_match.h → empty
DEFERRED:
  - module-build (qmk compile): no ~/qmk_userspace → DEFERRED
FLAG:
  - qmk_module.json (R1): present-or-absent (absent at research time → M1.T1 gap)
WRITES (only):
  - plan/004_76ea306f6be9/P1M2T2S1/PRP.md + research/findings.md
BUILD/CONFIG/ROUTES/DATABASE:
  - none (verification only; no source change).
```

## Validation Loop

> The Validation Loop IS the task. Every command was **executed during research from
> `/home/dustin/projects/qmk_notifier` and PASSED**. Re-run them and record green.

### Level 1: §11.2A — pattern-match corpus (the 9(+1) suites)

```bash
cd /home/dustin/projects/qmk_notifier   # firmware repo, underscore

./run_all_tests.sh > /tmp/ra.out 2>&1; echo "exit=$?  (expect 0)"
grep -E 'Total tests (run|failed) across all suites|ALL TESTS PASSED|SOME TESTS FAILED|Performance is acceptable' /tmp/ra.out
# Expected: "Total tests run across all suites: 2029", "Total tests failed: 0" (implicit — the
#           OVERALL line), "✓ ALL TESTS PASSED - BACKWARD COMPATIBILITY VERIFIED",
#           "✓ Performance is acceptable (< 1 second ...)".

# Per-suite FAIL counts (belt-and-suspenders) — every line must be fails=0.
for t in test_pattern_match test_char_classification test_word_boundary_basic \
         test_word_boundary_integration test_metachar_verification \
         test_comprehensive_integration test_error_handling test_memory_stress \
         test_invalid_patterns; do
  printf "%-36s fails=%s\n" "$t" "$(./$t 2>&1 | grep -c '^FAIL:')"
done
# Expected: fails=0 for every line.
```

### Level 2: §11.2D — notifier stub-compile gate (THE R3-SAFETY PROOF)

```bash
cd /home/dustin/projects/qmk_notifier

./run_notifier_stub_tests.sh > /tmp/ns.out 2>&1; echo "exit=$?  (expect 0)"
grep -E 'notifier (dispatch|os|host) fails=|✓ notifier stub-compile gate PASSED|FAILED' /tmp/ns.out
# Expected (exactly):
#   notifier dispatch fails=0  (exit=0)
#   notifier os fails=0        (exit=0)
#   notifier host fails=0      (exit=0)
#   ✓ notifier stub-compile gate PASSED
# This is the DIRECT proof the R3 #ifdef guard did not perturb the stub build.
```

### Level 3: §11.2B + §11.2C + R6 (reinforcement + the byte-identical check)

```bash
cd /home/dustin/projects/qmk_notifier

# §11.2B — pathological NFA stress (must be result=0 in < 50 ms).
cat > /tmp/nfa_stress.c <<'EOF'
#include <stdio.h>
#include <time.h>
#include "pattern_match.h"
int main(void){ char s[200]; for(int i=0;i<199;i++) s[i]='a'; s[199]='\0';
  const char* p="a+a+a+a+a+a+a+a+a+a+b"; clock_t t=clock(); int r=pattern_match(p,s,1);
  printf("result=%d  %.1f us\n", r, 1e6*(double)(clock()-t)/CLOCKS_PER_SEC); return 0; }
EOF
gcc -O2 -w /tmp/nfa_stress.c pattern_match.c -I. -o /tmp/nfa_stress
timeout 5 /tmp/nfa_stress   # Expected: "result=0  <50000.0 us" (research: 1811.0 us).

# §11.2C — six realistic patterns (must all print 1).
cat > /tmp/nfa_real.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  printf("%d\n", pattern_match("\\w+","hello",1));
  printf("%d\n", pattern_match("\\b\\w+\\b\\s+\\b\\w+\\b","hello world",1));
  printf("%d\n", pattern_match("^\\w+@\\w+$","user@host",1));
  printf("%d\n", pattern_match("v\\.code","v.code",1));
  printf("%d\n", pattern_match("a+b","aaab",1));
  printf("%d\n", pattern_match("*slack*","Slack - general",0));
  return 0; }
EOF
gcc -w /tmp/nfa_real.c pattern_match.c -I. -o /tmp/nfa_real && /tmp/nfa_real   # Expected: six 1s.

# R6 invariant — the three files MUST be unchanged vs HEAD (empty output).
git diff --stat HEAD -- notifier.h pattern_match.c pattern_match.h
# Expected: NO output (empty). Any output = an R6 violation to REPORT.
# Audit trail (record the blob hashes):
git ls-files -s notifier.h pattern_match.c pattern_match.h

rm -f /tmp/nfa_stress.c /tmp/nfa_stress /tmp/nfa_real.c /tmp/nfa_real
```

### Level 4: §18 state-under-test + DEFERRED module-build + R1 status

```bash
cd /home/dustin/projects/qmk_notifier

# 4a. The R3 guard is in place and #ifdef-wrapped (the one §18 source edit).
grep -n 'COMMUNITY_MODULES_API_VERSION' notifier.c
# Expected: the #ifdef line (~13) + the comment. Confirm the ASSERT is INSIDE the #ifdef.

# 4b. rules.mk is the module-context form (R2).
cat rules.mk
# Expected: "RAW_ENABLE = yes" + "SRC += notifier.c" (module-context; NOT the old
#           "SRC += qmk_notifier/notifier.c" submodule form).

# 4c. R1 status (qmk_module.json). Present or absent — record it.
ls -la qmk_module.json 2>&1
# If present: optionally validate against the community_module schema (qmk_firmware/data/schemas/).
# If absent (research state): FLAG the M1.T1 gap. Do NOT create it.

# 4d. Module-build criterion is DEFERRED (no userspace).
ls -d ~/qmk_userspace 2>&1   # Expected: "No such file or directory" → criterion DEFERRED.
qmk --version                # Expected: 1.2.0 (available, but no userspace to compile a module keymap).

# 4e. The only working-tree source change is the parallel README (Markdown; cannot affect gates/R6).
git status --porcelain | grep -vE '^\?\? plan/' | grep -E '\.c|\.h|\.mk|\.json|\.sh|README'
# Expected: only " M README.md" (parallel P1.M2.T1.S1) + " M plan/.../tasks.json".
#           NO notifier.{c,h} / pattern_match.* / rules.mk modification.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 (§11.2A): `./run_all_tests.sh` exit 0; "Total tests run across all suites: 2029";
      "✓ ALL TESTS PASSED"; "Performance is acceptable (< 1 second ...)"; 9 pattern suites each `fails=0`.
- [ ] Level 2 (§11.2D): `./run_notifier_stub_tests.sh` exit 0; `dispatch/os/host fails=0`;
      `✓ notifier stub-compile gate PASSED`.
- [ ] Level 3 (§11.2B): `/tmp/nfa_stress` → `result=0` in < 50 ms.
- [ ] Level 3 (§11.2C): `/tmp/nfa_real` → six `1`s.
- [ ] Level 3 (R6): `git diff --stat HEAD -- notifier.h pattern_match.c pattern_match.h` → **empty**.
- [ ] Level 4: R3 guard `#ifdef`-wrapped in notifier.c; rules.mk module-context; module-build DEFERRED
      (no userspace); R1 (qmk_module.json) status recorded; no source file modified by this task.

### Feature Validation

- [ ] The §11.2 host gate is green with the §18 changes (M1 + M2.T1) in place — byte-identical
      to the HEAD baseline (2029/2029; dispatch/os/host 0).
- [ ] The R3 guard is PROVEN stub-build-safe by the green §11.2D run (not just by reading the source).
- [ ] R6 holds: `notifier.h`/`pattern_match.c`/`pattern_match.h` unchanged vs HEAD.
- [ ] The module is structurally distributable (R1 manifest present-or-flagged; R2 rules.mk; R3 guard)
      AND behaviorally identical (green gates + R6).

### Code Quality Validation

- [ ] No source/build/test/runner file modified by this task (verification only).
- [ ] The only writes are `plan/004_76ea306f6be9/P1M2T2S1/{PRP.md,research/findings.md}`.
- [ ] No anti-patterns (see below): no crate-repo run, no source edit on a failing gate, no
      userspace setup, no qmk_module.json creation.

### Documentation & Deployment

- [ ] The verification result is recorded (the green gate outputs + the R6 empty diff + the
      DEFERRED module-build + the R1 status).
- [ ] Mode A (item §6): no user-facing documentation change — this is internal verification.
- [ ] P1.M2.T3.S1 (README sweep) + M1.T1 (qmk_module.json, if still absent) are left to their scope.

---

## Anti-Patterns to Avoid

- ❌ Don't run from the crate repo (`qmk-notifier`, hyphen) — it has no gates/source. `cd` to the
  firmware repo (`/home/dustin/projects/qmk_notifier`, underscore) first.
- ❌ Don't edit ANY source/build/test/runner file. This is verification. A failing gate is a
  regression to REPORT and root-cause in M1/M2.T1 — not something to "fix" by editing a file here.
- ❌ Don't compare raw `sha1sum` to `git ls-files -s` blob hashes for the R6 check — git blob
  hashes prepend a header, so they always differ. Use `git diff --stat HEAD`.
- ❌ Don't attempt a `~/qmk_userspace` setup to satisfy the module-build criterion — it is
  DEFERRED per the contract; the host gates + R6 are the available proof.
- ❌ Don't create `qmk_module.json` if it is absent — that is M1.T1's deliverable. Flag the gap;
  the host-gate verification does not depend on it.
- ❌ Don't treat the README edit (parallel P1.M2.T1.S1, `M README.md`) as a confound — it is
  Markdown-only and cannot affect the gates or R6 (confirmed: gates are green with it present).
- ❌ Don't assert "the R3 guard is safe" only by reading the `#ifdef` — RUN `./run_notifier_stub_tests.sh`
  and record its green output. Execution is the proof.
- ❌ Don't forget the firmware repo has THREE stub drivers (dispatch/os/host) — `run_notifier_stub_tests.sh`
  prints three `fails=` lines; all must be 0.
- ❌ Don't leave temp files behind — clean up `/tmp/ra.out`, `/tmp/ns.out`, `/tmp/nfa_stress*`, `/tmp/nfa_real*`.

---

## Confidence Score: 10/10

This is a verification runbook whose every command + expected output was **executed during
research from `/home/dustin/projects/qmk_notifier` and PASSED**: §11.2A `run_all_tests.sh`
→ 2029 tests, all 10 suites PASS, perf 0.079s (sub-second), exit 0, 9 pattern suites each
`fails=0`; §11.2D `run_notifier_stub_tests.sh` → dispatch/os/host each `fails=0`, `✓ gate
PASSED`, exit 0 (the direct proof the R3 `#ifdef COMMUNITY_MODULES_API_VERSION` guard did
not perturb the stub build); §11.2B → `result=0` in 1.8 ms (< 50 ms); §11.2C → six `1`s;
R6 `git diff --stat HEAD -- notifier.h pattern_match.c pattern_match.h` → **empty**. The §18
M1 state was verified (`rules.mk` module-context LANDED commit `b948c5f`; R3 guard LANDED
commit `d307b1a`, notifier.c:13-15, `#ifdef`-wrapped; `qmk_module.json` ABSENT — M1.T1 gap
flagged; README in parallel progress). The module-build criterion is DEFERRED (no
`~/qmk_userspace`; `qmk` 1.2.0 available) per the contract. The single environmental note
(the harness cwd is the crate repo, but the task targets the firmware repo) is documented
prominently and resolved by the explicit `cd /home/dustin/projects/qmk_notifier`. No source
change; no external dependency; the deliverable is the recorded green verification.