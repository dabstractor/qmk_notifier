name: "P1.M3.T2.S1 — Extend run_notifier_stub_tests.sh for test_notifier_host; verify §11.2A-D gate + no regressions"
description: >
  Wire test_notifier_host into run_notifier_stub_tests.sh as a THIRD driver built
  from the SAME shared stub-compiled notifier.o (do NOT recompile notifier.c
  thrice), renumber the steps [1/5]..[5/5], run all three, grep ^FAIL: per binary,
  print "notifier host fails=N", and end "✓ notifier stub-compile gate PASSED"
  IFF all three have 0 FAIL and exit 0. Update the header comment (Mode A). Then
  run the full PRD §11.2 acceptance gate (A run_all_tests / B NFA stress / C six
  realistic patterns / D the three-binary notifier gate).
  ⚠️ VERIFIED BLOCKER: with the CURRENT notifier.c, test_notifier_host produces
  7 FAIL lines, ALL from the SET_OS blocks (i-iv) — because SET_OS's cmd_id 0x03
  == ETX 0x03 in the reassembly loop, so the SET_OS handler is unreachable via
  hid_notify. This is an upstream framing flaw in notifier.c OWNED BY P1.M2 (the
  reassembly loop's binary-payload framing), NOT in this task's scope. A
  faithfully-implemented runner therefore ends "✗ ... FAILED" + exit 1 today —
  the CORRECT signal that surfaces the flaw. §11.2A/B/C are GREEN; §11.2D is
  GREEN for dispatch+os (no regression) but RED on host until P1.M2 fixes the
  framing. The runner must NOT be weakened to hide the 7 FAILs.

---

## Goal

**Feature Goal**: Make `run_notifier_stub_tests.sh` build and run a THIRD
notifier test binary — `test_notifier_host` (produced by P1.M3.T1.S2/S3/S4) —
from the single shared `notifier_stub.o` it already compiles, alongside the
existing `test_notifier_dispatch` and `test_notifier_os`, and then execute the
full PRD §11.2 acceptance gate (A/B/C/D). The runner must report a per-binary
`fails=N` line for all three and resolve the gate PASS/FAIL honestly from ALL
three binaries' `^FAIL:` counts and exit codes.

**Deliverable**: ONE file MODIFIED — `run_notifier_stub_tests.sh` (repo root).
The complete new content is given verbatim below (pervasive step-renumbering
[1/4]→[1/5] etc. makes a full-file target the one-pass-robust choice). No other
file is modified by this task.

**Success Definition** (split — see "⚠️ BLOCKED CRITERION" below):
- *(achievable, in-scope)* The runner, when executed, COMPILES `notifier.c` into
  a shared stub object **once**, LINKS all three drivers (`test_notifier_dispatch`,
  `test_notifier_os`, `test_notifier_host`) from that one object, RUNS all three,
  prints three `notifier <name> fails=N  (exit=M)` lines, and cleans up the
  temporaries. The header comment names the third binary and what it covers.
  `test_notifier_dispatch` and `test_notifier_os` still report **fails=0 / exit=0**
  (no regression — verified green today). The 9 `pattern_match` suites are green
  (§11.2A: 2023/2023), §11.2B (NFA stress < 50 ms, result=0), §11.2C (six patterns
  all `1`) — all verified GREEN.
- *(blocked, requires upstream fix NOT in scope)* "all three 0 FAIL" / "gate
  green" is **NOT met** today: `test_notifier_host` emits **7 FAILs** (all SET_OS
  blocks i-iv) from the verified ETX-collision framing flaw in `notifier.c`'s
  reassembly loop (SET_OS cmd_id `0x03` == ETX `0x03`; OS_MACOS os_byte also
  `0x03`). The faithful runner therefore prints **"✗ notifier stub-compile gate
  FAILED"** and **exit 1** with the current firmware. This is the INTENDED,
  CORRECT signal — see "⚠️ BLOCKED CRITERION". The gate turns green only after
  P1.M2 fixes the typed-binary-payload framing in `notifier.c`.

## ⚠️ BLOCKED CRITERION — read before judging pass/fail

**The task's stated OUTPUT ("the full §11.2A-D gate is green" / "all three
notifier binaries 0 FAIL") CANNOT be fully met against the current `notifier.c`.**
This is a **VERIFIED, upstream blocker**, not a defect in this task's deliverable:

- `test_notifier_host.c` builds + runs; it reports `Total tests run: 64 / passed:
  57 / failed: 7`. The 7 failures are **exactly** the four SET_OS blocks (i-iv) —
  see the table under "Expected Test Results". The AHC (v-viii), QUERY_INFO,
  QUERY_CALLBACK, coexistence, and multi-report blocks **all PASS**.
- Root cause: `hid_notify`'s reassembly loop (notifier.c ~L771-814) does
  `if (c == ETX_TERMINATOR[0]) { dispatch; break; }` for EVERY byte. SET_OS's
  cmd_id is `0x03` (== ETX), so the loop dispatches on the cmd_id byte before
  reaching the SET_OS handler (`case NOTIFY_CMD_SET_OS:` at notifier.c:693).
  `OS_MACOS==3`, so the os_byte argument ALSO collides. The handler is unreachable.
- This is a **wire-protocol framing flaw** (ETX-termination is safe for text
  0x20–0x7E but incompatible with binary typed payloads). It is **owned by
  P1.M2** (the reassembly loop's binary framing), which is marked Complete but
  did not fix it. P1.M3.T1.S3 diagnosed it fully and escalated it (see that
  PRP's "CRITICAL BLOCKER" section).
- **What this task MUST do about it**: implement the runner FAITHFULLY (require
  0 FAIL; PASS iff all three green), then RUN the full §11.2 gate and REPORT
  §11.2D as RED on `test_notifier_host`'s 7 SET_OS FAILs. The RED gate is the
  acceptance signal that surfaces the flaw — this is the S3-endorsed philosophy:
  *"the acceptance gate MUST capture rather than hide the flaw."*

**FORBIDDEN "fixes" (each would mask the flaw and violate the contract):**
- ❌ Do NOT exclude `test_notifier_host` from the gate (the contract says three).
- ❌ Do NOT count only some FAILs / allow N "expected" FAILs (contract: "iff all
  three have 0 FAIL").
- ❌ Do NOT weaken `test_notifier_host.c` (out of scope; S3 explicitly forbids
  weakening the SET_OS assertions).
- ❌ Do NOT modify `notifier.c` / `notifier.h` / `qmk_stubs/*` to make SET_OS
  pass — the framing fix is P1.M2's job, NOT this runner task.
- ❌ Do NOT delete or `#if 0` the SET_OS test blocks.

The faithful deliverable is: runner correctly extended + §11.2A/B/C green +
dispatch/os green + a clear report that §11.2D is RED on host pending the P1.M2
framing fix.

## User Persona (if applicable)

**Target User**: The contributor/maintainer who runs `./run_notifier_stub_tests.sh`
as the §11.2D acceptance gate, and the P1.M2 owner responsible for the
typed-command framing.

**Use Case**: (1) Anyone edits `notifier.c` (dispatch, OS selection, host
handlers, or the reassembly loop) → the gate rebuilds all three notifier binaries
from one shared object and reports per-binary FAIL counts. (2) The P1.M2 owner
fixes the SET_OS framing → `test_notifier_host`'s 7 FAILs flip to PASS and the
gate goes green — proving the fix end-to-end through the public `hid_notify` path.

**Pain Points Addressed**: P1.M3.T1.S2/S3/S4 wrote `test_notifier_host.c` but it
is NOT yet wired into the committed gate — so the typed-command coverage is
invisible to `run_notifier_stub_tests.sh`. This task closes that gap (one shared
object, three drivers) so the typed/host contract is continuously gated.

## Why

- **Closes the typed-command test-coverage gap in the committed gate.** §11.2D is
  the notifier acceptance gate; today it runs only dispatch + os. The host/typed
  half (QUERY_INFO/QUERY_CALLBACK/SET_OS/APPLY_HOST_CONTEXT, coexistence,
  multi-report framing — §4.6/§4.7/§14) has 64 assertions sitting in
  `test_notifier_host.c` that no gate exercises. Wiring it in is the whole point
  of P1.M3.T2.
- **One shared object, three drivers (no thrice-compiled notifier.c).** The
  contract is explicit: link host from the SAME `notifier_stub.o`. notifier.c is
  the slowest TU to compile and the most warning-sensitive (`-Wall -Wextra`);
  compiling it once and reusing the object is both faster and avoids divergence.
- **Surfaces the SET_OS framing flaw in the acceptance gate (TDD value).** A
  faithful three-binary gate turns RED today — exactly the signal that makes the
  P1.M2 ETX-collision flaw visible to anyone running the gate, rather than buried
  in a standalone probe. (Mirrors P1.M3.T1.S3's stated intent.)
- **Zero regression risk.** This is a shell-script + validation task; it changes
  no C source. The existing dispatch/os binaries are byte-for-byte unchanged
  (verified: fails=0/0 today); only the runner's step list grows by one link + one
  run + one summary line.

## What

**EDIT `run_notifier_stub_tests.sh`** to (a) extend the header comment to name the
third binary, (b) add a `HST` temp path, (c) renumber `[1/4]→[1/5]`,
`[2/4]→[2/5]`, `[3/4]→[3/5]`, insert a new `[4/5] link host` step, and relabel
`[4/4] run both` → `[5/5] run all three`, (d) run `test_notifier_host` and capture
`rc_h`/`fails_h`, (e) print `notifier host fails=$fails_h  (exit=$rc_h)`, (f) add
`$HST` to every `rm -f` cleanup, and (g) extend the PASS condition with
`&& [ "$fails_h" -eq 0 ] && [ $rc_h -eq 0 ]`.

The host LINK step uses the **exact same flags** as the dispatch/os link steps
(`gcc -Wall -std=c99 -Iqmk_stubs -I.`) and links `$OBJ` (the shared stub object)
— never recompiles `notifier.c`. (`test_notifier_host.c` is clean under
`-Wall -Wextra`; `-Wall` is a subset, so it links clean.)

### The complete new `run_notifier_stub_tests.sh` (authoritative target)

Because step-renumbering touches nearly every line, write this as the full file
contents (then `chmod +x run_notifier_stub_tests.sh` to preserve the executable
bit). It is the existing 55-line script + the host step, byte-faithful to the
original for the unchanged portions:

```bash
#!/usr/bin/env bash
# P2 stub-compile validation gate for notifier.c (closes RISK-1).
#
# notifier.c cannot compile standalone: it does `#include QMK_KEYBOARD_H` (a
# -D-expanded header name) and pulls in QMK symbols (layer_on/layer_off,
# raw_hid_send) that the 9-suite corpus — which links only pattern_match.c —
# cannot provide. This harness substitutes minimal QMK stubs so the receiver /
# reassembler / F4 delimiter matcher / dispatcher ordering / hid_notify ack
# logic, the multi-OS map-selection (F8) / OS-change-clear (F9) logic, AND the
# typed-command / host-rules logic (QUERY_INFO / QUERY_CALLBACK / SET_OS /
# APPLY_HOST_CONTEXT, coexistence, and multi-report framing) can be validated
# with plain gcc on a host. It builds THREE drivers — test_notifier_dispatch,
# test_notifier_os, and test_notifier_host — from a SINGLE stub-compiled
# notifier.o (PRD §11.1, §11.2D). See PRP P2 / P1.M2.T2 / P1.M3.T1.
set -u
cd "$(dirname "$0")"

OBJ=/tmp/notifier_stub.o
DRV=/tmp/test_notifier_dispatch
OST=/tmp/test_notifier_os
HST=/tmp/test_notifier_host

echo "[1/5] stub-compile notifier.c (shared by all three test binaries) ..."
gcc -Wall -Wextra -std=c99 \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. \
    -c notifier.c -o "$OBJ"
if [ $? -ne 0 ]; then echo "COMPILE FAILED"; exit 2; fi

echo "[2/5] link dispatch driver (test_notifier_dispatch) ..."
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    "$OBJ" qmk_stubs/qmk_stubs.c test_notifier_dispatch.c \
    -o "$DRV"
if [ $? -ne 0 ]; then echo "LINK FAILED (dispatch)"; rm -f "$OBJ"; exit 3; fi

echo "[3/5] link multi-OS driver (test_notifier_os) ..."
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    "$OBJ" qmk_stubs/qmk_stubs.c test_notifier_os.c \
    -o "$OST"
if [ $? -ne 0 ]; then echo "LINK FAILED (os)"; rm -f "$OBJ" "$DRV"; exit 4; fi

echo "[4/5] link host driver (test_notifier_host) ..."
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    "$OBJ" qmk_stubs/qmk_stubs.c test_notifier_host.c \
    -o "$HST"
if [ $? -ne 0 ]; then echo "LINK FAILED (host)"; rm -f "$OBJ" "$DRV" "$OST"; exit 5; fi

echo "[5/5] run all three ..."
"$DRV"
rc_d=$?
fails_d=$("$DRV" 2>/dev/null | grep -c '^FAIL:' || true)
"$OST"
rc_o=$?
fails_o=$("$OST" 2>/dev/null | grep -c '^FAIL:' || true)
"$HST"
rc_h=$?
fails_h=$("$HST" 2>/dev/null | grep -c '^FAIL:' || true)
echo "------------------------------------------------"
echo "notifier dispatch fails=$fails_d  (exit=$rc_d)"
echo "notifier os fails=$fails_o  (exit=$rc_o)"
echo "notifier host fails=$fails_h  (exit=$rc_h)"
rm -f "$OBJ" "$DRV" "$OST" "$HST"
if [ "$fails_d" -eq 0 ] && [ $rc_d -eq 0 ] \
   && [ "$fails_o" -eq 0 ] && [ $rc_o -eq 0 ] \
   && [ "$fails_h" -eq 0 ] && [ $rc_h -eq 0 ]; then
    echo "✓ notifier stub-compile gate PASSED"
    exit 0
fi
echo "✗ notifier stub-compile gate FAILED"
exit 1
```

> The ONLY semantic changes vs the original are: (1) header names the 3rd binary,
> (2) `HST` var + `[4/5]` link step, (3) `[5/5]` runs `$HST` too, (4) one new
> summary line, (5) `$HST` in cleanups, (6) `fails_h`/`rc_h` in the PASS test.
> The compile flags, the `|| true` on grep, `set -u`, and exit codes 2/3/4/1 are
> unchanged; the host link failure adds exit 5.

### Success Criteria

- [ ] `run_notifier_stub_tests.sh` compiles `notifier.c` into `$OBJ` exactly ONCE
      and links all three drivers from it (grep the script: exactly one `-c notifier.c`).
- [ ] Steps are numbered `[1/5] … [5/5]`; host link is `[4/5]`; run step is `[5/5]`.
- [ ] Host link uses the SAME flags as dispatch/os (`-Wall -std=c99 -Iqmk_stubs -I.`).
- [ ] The script prints all three `notifier <name> fails=N  (exit=M)` lines.
- [ ] The PASS condition tests `fails_h==0 && rc_h==0` alongside dispatch/os.
- [ ] Header comment names `test_notifier_host` and its coverage.
- [ ] `test_notifier_dispatch` + `test_notifier_os` still report **fails=0, exit=0**
      (no regression — verified green today).
- [ ] §11.2A green (2023/2023), §11.2B green (result=0, <50 ms), §11.2C green (six `1`s).
- [ ] §11.2D: with current `notifier.c`, the gate ends **"✗ … FAILED", exit 1**,
      driven by `test_notifier host fails=7 (exit=1)` — the 7 FAILs are all SET_OS
      blocks (i-iv); AHC/coexist/multi-report/QUERY blocks all pass. This is the
      EXPECTED signal of the upstream blocker, NOT a task defect (see BLOCKED
      CRITERION). No file other than `run_notifier_stub_tests.sh` is modified.

## All Needed Context

### Context Completeness Check

**Pass (with the documented blocker).** The exact target file contents are given
verbatim above. The current runner (55 lines), the current test_notifier_host.c
behavior (64 tests, 7 SET_OS FAILs), and the §11.2A–D gate results were ALL
captured by running the actual commands during research (see
research/gate_ground_truth.md). The blocker (SET_OS cmd_id 0x03 == ETX 0x03) is
located precisely in notifier.c (reassembly loop ~L771-814; SET_OS handler L693
exists but unreachable). An implementer with only this PRP + the repo can apply
the new script, run the four sub-gates, and correctly report §11.2D as RED-on-host.

### Documentation & References

```yaml
# MUST READ — the file being MODIFIED (the ONLY file this task touches)
- file: run_notifier_stub_tests.sh
  why: "The current 55-line gate: [1/4] compile notifier.c -> $OBJ; [2/4] link
        dispatch; [3/4] link os; [4/4] run both; grep ^FAIL:; print two fails=
        lines; PASS iff both green. THIS TASK adds the host binary as a 3rd driver
        and renumbers to [1/5]..[5/5]."
  pattern: "Mirror the EXISTING link step verbatim for the host: same flags
            (gcc -Wall -std=c99 -Iqmk_stubs -I.), same $OBJ reuse, same
            '|| true' on grep, same rm-on-failure + exit-code escalation (5)."
  critical: "Do NOT recompile notifier.c for the host step — link $OBJ (shared).
             Do NOT use -Wextra on the LINK steps (the original uses -Wall only;
             -Wextra is only on the single object-compile). test_notifier_host.c
             is clean under -Wall (verified)."

# MUST READ — the new binary being wired in (the contract this task consumes)
- file: test_notifier_host.c
  why: "Produces 'Total tests run: N / passed: P / failed: F', exits non-zero iff
        g_fail!=0 (return g_fail?1:0), prints 'PASS:'/^FAIL:' lines. The runner
        greps '^FAIL:' exactly like for dispatch/os. Currently 64 tests / 7 fail
        (all SET_OS)."
  critical: "Currently EMITS 7 ^FAIL: LINES (SET_OS i-iv). A faithful runner will
             therefore exit 1 today. This is the intended signal. Do NOT modify
             test_notifier_host.c — it is correct; the firmware framing is flawed."

# MUST READ — the upstream blocker diagnosis (sets expectations honestly)
- file: plan/003_16d737de7a3e/P1M3T1S3/PRP.md
  section: "🚨 CRITICAL BLOCKER — SET_OS cmd_id (0x03) == ETX terminator (0x03)"
  why: "Full diagnosis: hid_notify's reassembly loop dispatches on ANY 0x03 byte;
        SET_OS's cmd_id 0x03 trips it before the handler runs; OS_MACOS=3 makes the
        os_byte collide too. The handler (notifier.c:693) exists but is unreachable.
        Owned by P1.M2. S3 explicitly chose to surface (not mask) the flaw."
  critical: "This is WHY test_notifier_host has 7 FAILs and WHY the faithful gate
             is RED. The fix is a notifier.c framing change (length-prefix or
             escaping for binary typed payloads) — OUT OF SCOPE for this task."

# MUST READ — the parallel task that finishes test_notifier_host.c
- file: plan/003_16d737de7a3e/P1M3T1S4/PRP.md
  why: "S4 appends the coexistence + multi-report blocks to test_notifier_host.c
        (running in parallel with this PRP's research). It confirms the same 7-FAIL
        SET_OS reality and that AHC/coexist/multi-report all PASS. The file S4
        produces is EXACTLY the one this task wires into the runner."
  critical: "S4 does NOT change the SET_OS blocker. After S4 lands the file is
             still 7-FAIL-on-SET_OS. This task consumes the post-S4 file as-is."

# MUST READ — the §11.2 acceptance gate (the commands this task runs)
- file: PRD.md
  section: "### 11.2 Acceptance gate — all must be true (A/B/C/D)"
  why: "The EXACT gate commands: A) for-loop over 9 pattern_match suites, expect
        fails=0 each; B) /tmp/nfa_stress.c -> result=0 in <50ms; C) six
        realistic patterns each print 1; D) ./run_notifier_stub_tests.sh -> ends
        PASSED with dispatch+os (now +host) each 0 FAIL."
  critical: "§11.2D's 'ends PASSED' is aspirational for the THREE-binary gate:
             with the current notifier.c it ends FAILED (host=7). A/B/C ARE green.
             The PRP runs all four sub-gates and reports each honestly."

# MUST READ — the build flags (§11.1) — confirms host link flags
- file: PRD.md
  section: "### 11.1 Build all suites (exact flags — copy/paste)"
  why: "The notifier driver links use '-DQMK_KEYBOARD_H=... -Iqmk_stubs -I.
        ... -std=c99'. The runner's link steps additionally pass -Wall. This task's
        host link mirrors the dispatch/os link flags EXACTLY."

# MUST READ — the stub-compile gate's rationale + shared-object design
- file: plan/003_16d737de7a3e/architecture/system_context.md
  why: "Documents the verified baseline (2023/2023 pattern_match; 31/31 + dispatch
        notifier stub) and that notifier.c is stub-compiled ONCE into a shared
        object for all notifier drivers (closes RISK-1). This task EXTENDS that
        one-object design to a third driver."

# REFERENCE — the verified gate ground truth (this task's research)
- file: plan/003_16d737de7a3e/P1M3T2S1/research/gate_ground_truth.md
  why: "Captured by running the actual commands: §11.2A 2023/2023; §11.2B
        result=0/1828us; §11.2C 1×6; dispatch+os fails=0/0; host=7 (all SET_OS).
        The exact current runner structure + the precise extension diff."
```

### Current Codebase tree (relevant subset — POST all host work)

```bash
run_notifier_stub_tests.sh   # ← MODIFY (this task): add 3rd driver, renumber [1/5]..[5/5].
notifier.c                   # implementation under test (P1.M2). SET_OS ETX flaw lives here. DO NOT TOUCH.
notifier.h                   # NOTIFY_* constants + DEFINE_* macros. DO NOT TOUCH.
qmk_stubs/
  qmk_stubs.c                # layer_on/off, raw_hid_send, stub_get_active_layer/last_response (S1). DO NOT TOUCH.
  os_detection.h             # os_variant_t (OS_MACOS==3). DO NOT TOUCH.
test_notifier_host.c         # produced by S2+S3+S4 (running/just-finished). 64 tests, 7 FAIL (SET_OS). DO NOT TOUCH.
test_notifier_dispatch.c     # precedent driver. DO NOT TOUCH.
test_notifier_os.c           # precedent driver. DO NOT TOUCH.
run_all_tests.sh             # §11.2A gate. DO NOT TOUCH.
```

### Desired Codebase tree with files to be modified

```bash
run_notifier_stub_tests.sh   # MODIFIED: header + HST var + [4/5] link host + [5/5] run all + fails_h/rc_h
                             #   summary line + cleanups + PASS-condition. ~75 lines. Nothing else changes.
```

### Known Gotchas of our codebase & Library Quirks

```bash
# 🚨 CRITICAL: the faithful runner is RED today. test_notifier_host emits 7 ^FAIL:
#    lines (SET_OS i-iv) because SET_OS cmd_id 0x03 == ETX 0x03 in notifier.c's
#    reassembly loop. So `./run_notifier_stub_tests.sh` will print
#    "notifier host fails=7  (exit=1)" and end "✗ ... FAILED", exit 1. This is the
#    CORRECT signal (the gate surfaces the upstream flaw). Do NOT "fix" it by
#    weakening the runner/tests/firmware. (See ⚠️ BLOCKED CRITERION.)

# CRITICAL: link host from the SHARED $OBJ — do NOT add a second `gcc ... -c notifier.c`.
#    grep -c '\-c notifier.c' run_notifier_stub_tests.sh  ->  must be 1.

# CRITICAL: the LINK steps use `-Wall` (NOT -Wextra). Only the single object-compile
#    uses -Wall -Wextra. Mirror the existing dispatch/os link flags for host.
#    test_notifier_host.c is clean under -Wall -Wextra (verified), so -Wall is clean.

# GOTCHA: `grep -c '^FAIL:'` returns exit 1 when count is 0; the `|| true` defends
#    it (set -u is set; set -e is NOT). Mirror the existing `|| true` for host.

# GOTCHA: cleanup. Every failure path + the success path must `rm -f` ALL four
#    temporaries ($OBJ $DRV $OST $HST) so no stale /tmp binary masks a later run.

# GOTCHA: the executable bit. If you rewrite the whole file with `write`, run
#    `chmod +x run_notifier_stub_tests.sh` afterward (the original is mode 755).

# GOTCHA: test_notifier_host's `return g_fail?1:0` means rc_h==1 when ANY test
#    fails. With 7 SET_OS FAILs, rc_h=1 and fails_h=7 -> gate FAILED. Both the
#    count AND the exit code must be 0 for PASS; testing both is belt-and-suspenders.

# GOTCHA: do NOT reorder dispatch/os/host runs or change their flags — the contract
#    (LOGIC c) requires dispatch+os pass UNCHANGED (legacy path intact).
```

## Implementation Blueprint

### Data models and structure

**None.** This is a shell-script edit. No C, no types, no config.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: REWRITE run_notifier_stub_tests.sh to the authoritative target (above)
  - WRITE the complete file shown in the "What" section (verbatim).
  - PRESERVE: `#!/usr/bin/env bash` shebang, `set -u`, `cd "$(dirname "$0")"`,
        the EXACT gcc compile flags (`-Wall -Wextra -std=c99 -DQMK_KEYBOARD_H=...
        -Iqmk_stubs -I. -c notifier.c`) and link flags (`-Wall -std=c99 -Iqmk_stubs
        -I.`), the `|| true` grep idiom, and exit codes 2/3/4/1.
  - CHANGE: header names the 3rd binary; add `HST=/tmp/test_notifier_host`;
        renumber [1/4]->[1/5], [2/4]->[2/5], [3/4]->[3/5]; add [4/5] link host;
        [4/4] run both -> [5/5] run all three; run $HST (rc_h, fails_h); add the
        `notifier host fails=$fails_h (exit=$rc_h)` line; add $HST to every
        `rm -f`; add `&& [ "$fails_h" -eq 0 ] && [ $rc_h -eq 0 ]` to PASS test.
  - THEN: chmod +x run_notifier_stub_tests.sh  (preserve executable bit).
  - DO NOT: modify any other file. Do NOT recompile notifier.c for the host step.

Task 2: SYNTAX-CHECK the script
  - RUN: bash -n run_notifier_stub_tests.sh   # parse-check; expect no output, exit 0
  - RUN: shellcheck run_notifier_stub_tests.sh 2>/dev/null || true   # best-effort; the
        pre-existing `grep ... || true` and `[ $? -ne 0 ]` idioms are intentional.

Task 3: RUN the extended runner (expect RED on host — the honest outcome)
  - RUN: ./run_notifier_stub_tests.sh
  - EXPECT (current notifier.c):
        [1/5]..[5/5] steps print with no compile/link errors
        notifier dispatch fails=0  (exit=0)
        notifier os fails=0        (exit=0)
        notifier host fails=7      (exit=1)
        ✗ notifier stub-compile gate FAILED
        (script exit code = 1)
  - This is the CORRECT outcome. If dispatch/os are non-zero, that is a REGRESSION
        (investigate). If host is 0, the SET_OS blocker has been fixed upstream
        (rejoice; the gate is now green — verify the 7 SET_OS PASS lines).

Task 4: RUN the full §11.2 acceptance gate (A/B/C) and confirm D's honest state
  - §11.2A: ./run_all_tests.sh 2>&1 | grep 'Total tests run across'  -> 2023
            (0 failures; no regression in the 9 pattern_match suites).
  - §11.2B: build+run /tmp/nfa_stress.c (see PRD §11.2B / Validation Level 3)
            -> "result=0  <us>" with < 50000 us (~1800 us observed).
  - §11.2C: build+run /tmp/nfa_real.c (six patterns) -> six "1" lines.
  - §11.2D: ./run_notifier_stub_tests.sh -> dispatch/os green, host=7 (SET_OS).
  - RECORD each sub-gate's result. §11.2A/B/C = GREEN; §11.2D = RED-on-host.

Task 5: CONFIRM no regressions + isolate the host FAILs to SET_OS
  - Regression: dispatch + os MUST be fails=0 (Task 3 already shows this).
  - Isolation: the host FAIL lines must all be the SET_OS blocks (i)-(iv):
        gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
            -Iqmk_stubs -I. notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c \
            -o /tmp/tnh && /tmp/tnh 2>/dev/null | grep '^FAIL:' | grep -v 'SET_OS'
        EXPECT: no output (every FAIL is a SET_OS block). rm -f /tmp/tnh.
```

### Implementation Patterns & Key Details

```bash
# PATTERN: one shared object, N drivers. The runner compiles notifier.c ONCE into
#   $OBJ (the slow, -Wall -Wextra TU) and links each driver against it. Each link
#   step is `gcc -Wall -std=c99 -Iqmk_stubs -I. $OBJ qmk_stubs/qmk_stubs.c <driver>.c -o <bin>`.
#   This is the existing dispatch/os design — host just adds a third identical row.

# PATTERN: per-binary FAIL tally + honest gate. `fails=$(<bin> 2>/dev/null | grep
#   -c '^FAIL:' || true)` then `rc=$?`. PASS iff EVERY binary has fails==0 && rc==0.
#   This means a faithful runner is RED when ANY binary fails — by design.

# PATTERN: escalate, don't mask. When host=7 (SET_OS blocker), the gate is RED and
#   the task's report says so plainly: "§11.2D RED on test_notifier_host (7 SET_OS
#   FAILs) — blocked by notifier.c SET_OS/ETX framing flaw (P1.M2 scope). A/B/C +
#   dispatch/os all green."

# CRITICAL: the host link flags MUST equal the dispatch/os link flags. Do not
#   "improve" them (e.g. add -Wextra) — uniformity with the existing two is the
#   contract. (-Wall is clean for test_notifier_host.c; verified under -Wextra.)

# CRITICAL: exit-code escalation. compile=2, dispatch=3, os=4, host=5, gate=1.
#   This preserves the existing numbering and adds host at 5.

# ANTI-PATTERN: do NOT add a `grep -v SET_OS` or an "expected fails" allowance to
#   the gate — that hides the flaw. The contract is "iff all three have 0 FAIL".
```

### Integration Points

```yaml
NO database / config / route / migration / firmware changes. One shell script edited.

BUILD/LINK:
  - run_notifier_stub_tests.sh compiles notifier.c -> $OBJ ONCE, links 3 drivers.
    No new gcc flag. The host link reuses $OBJ (same as dispatch/os).

COMMITTED GATE BEHAVIOR (the contract this task ships):
  - ./run_notifier_stub_tests.sh exits 0 ONLY when all three notifier binaries
    report 0 FAIL and exit 0. Today (current notifier.c) it exits 1 (host=7).

SCOPE BOUNDARY (do NOT cross — siblings/other tasks own these):
  - notifier.c SET_OS/ETX framing fix                  -> P1.M2 (BLOCKER; out of scope)
  - test_notifier_host.c (S2/S3/S4)                    -> P1.M3.T1.S2/S3/S4
  - README.md host-rules + Running Tests sync          -> P1.M3.T3.S1
  - qmk_stubs.c / notifier.h / pattern_match.*         -> P1.M2 / earlier
```

## Validation Loop

> Toolchain: bash + gcc (no ruff/mypy). Validate by parse-check, running the
> gate, and isolating the host FAILs. NOTE: with the current notifier.c the
> three-binary gate is EXPECTED to be RED on host — see BLOCKED CRITERION.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier
bash -n run_notifier_stub_tests.sh && echo "PARSE OK"           # expect: PARSE OK
ls -l run_notifier_stub_tests.sh | grep -q 'x' && echo "EXEC OK" # expect: EXEC OK (else chmod +x)
# Sanity: notifier.c is compiled exactly ONCE; all three links reuse $OBJ:
grep -c '\-c notifier.c' run_notifier_stub_tests.sh   # expect: 1
grep -c '"\$OBJ"'            run_notifier_stub_tests.sh   # expect: >=3 (dispatch/os/host links)
grep -c 'fails=\$fails_'     run_notifier_stub_tests.sh   # expect: 3 (d/o/h summary lines)
grep -q '\[4/5\] link host'  run_notifier_stub_tests.sh && echo "HOST STEP OK"
```

### Level 2: Run the extended gate (THE primary outcome — expect RED on host)

```bash
cd /home/dustin/projects/qmk-notifier
./run_notifier_stub_tests.sh; echo "gate exit=$?"
# EXPECT (current notifier.c):
#   notifier dispatch fails=0  (exit=0)
#   notifier os fails=0        (exit=0)
#   notifier host fails=7      (exit=1)
#   ✗ notifier stub-compile gate FAILED
#   gate exit=1
# dispatch/os MUST be 0 (regression check). host=7 is the SET_OS blocker (expected).
```

### Level 3: Full §11.2 acceptance gate (A/B/C green; D red-on-host)

```bash
cd /home/dustin/projects/qmk-notifier

# §11.2A — 9 pattern_match suites, 0 failures:
./run_all_tests.sh 2>&1 | grep -E 'Total tests run across|Total tests failed'
# expect: "Total tests run across all suites: 2023" and "Total tests failed: 0"

# §11.2B — pathological NFA stress (result=0, < 50 ms):
cat > /tmp/nfa_stress.c <<'EOF'
#include <stdio.h>
#include <time.h>
#include "pattern_match.h"
int main(void){ char s[200]; for(int i=0;i<199;i++) s[i]='a'; s[199]='\0';
  const char* p="a+a+a+a+a+a+a+a+a+a+b"; clock_t t=clock(); int r=pattern_match(p,s,1);
  printf("result=%d  %.1f us\n", r, 1e6*(double)(clock()-t)/CLOCKS_PER_SEC); return 0; }
EOF
gcc -O2 -w /tmp/nfa_stress.c pattern_match.c -I. -o /tmp/nfa_stress
timeout 5 /tmp/nfa_stress          # expect: "result=0  ~1800.0 us" (must be < 50000 us)

# §11.2C — six realistic patterns (all print 1):
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
gcc -w /tmp/nfa_real.c pattern_match.c -I. -o /tmp/nfa_real && /tmp/nfa_real   # expect: 1 (six times)
rm -f /tmp/nfa_stress /tmp/nfa_stress.c /tmp/nfa_real /tmp/nfa_real.c

# §11.2D — the three-binary notifier gate (already run in Level 2):
#   dispatch=0, os=0, host=7 -> gate FAILED/exit 1 today (SET_OS blocker).
#   A/B/C are GREEN; D is RED ONLY on test_notifier_host's SET_OS blocks.
```

### Level 4: Isolate the host FAILs to SET_OS (proves AHC/coexist/multi-report pass)

```bash
cd /home/dustin/projects/qmk-notifier
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c -o /tmp/tnh
echo "--- all FAIL lines ---"
/tmp/tnh 2>/dev/null | grep '^FAIL:'
echo "--- non-SET_OS FAIL lines (MUST be empty) ---"
/tmp/tnh 2>/dev/null | grep '^FAIL:' | grep -v 'SET_OS'
echo "--- summary ---"
/tmp/tnh 2>/dev/null | grep -iE 'Total tests|passed|failed'
# EXPECT: 7 FAIL lines, ALL containing "SET_OS"; the non-SET_OS grep is EMPTY;
#   summary "Total tests run: 64 / passed: 57 / failed: 7". This proves the
#   AHC (v-viii), QUERY_INFO/QUERY_CALLBACK, coexistence, and multi-report blocks
#   all PASS — only the 4 SET_OS blocks (i-iv) fail, exactly the documented blocker.
rm -f /tmp/tnh
```

### Expected Test Results (authoritative — read before judging pass/fail)

| Binary | Scenario | Expected (current notifier.c) | Why |
|---|---|---|---|
| `test_notifier_dispatch` | F4/reassembly/ack/coexistence | **fails=0, exit=0** ✅ | legacy path intact (no regression) |
| `test_notifier_os` | F8/F9 multi-OS | **fails=0, exit=0** ✅ | multi-OS path intact (no regression) |
| `test_notifier_host` — QUERY_INFO/CALLBACK | read-only typed queries | **PASS** ✅ | cmd 0x01/0x02 ≠ ETX |
| `test_notifier_host` — APPLY_HOST_CONTEXT (v-viii) | stack/replace/diff/clear | **PASS** ✅ | cmd 0x05 ≠ ETX; args safe |
| `test_notifier_host` — coexistence (i/ii) | legacy vs typed | **PASS** ✅ | printable data[2] ≠ 0xF0 |
| `test_notifier_host` — multi-report | two-report AHC reassembly | **PASS** ✅ | cmd 0x05; no 0x03 arg byte |
| `test_notifier_host` — SET_OS (i-iv) | host-authoritative OS | **FAIL** (7 lines) 🔴 | SET_OS cmd_id 0x03 == ETX 0x03 (BLOCKER) |
| **gate** | all three | **FAILED, exit 1** 🔴 | host=7 (SET_OS); dispatch/os=0 |
| §11.2A / B / C | pattern_match corpus | **GREEN** ✅ | unaffected by notifier changes |

**The task's primary deliverable (a correct three-binary runner) IS achievable and
ships here. Its OUTPUT criterion "gate green / all three 0 FAIL" is BLOCKED by the
SET_OS framing flaw in notifier.c (P1.M2 scope); the faithful runner correctly
reports the gate as RED on host until that flaw is fixed.**

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `bash -n` parse OK; file is executable; `-c notifier.c` appears once;
      `$OBJ` appears in ≥3 links; 3 `fails=` summary lines; `[4/5] link host` present.
- [ ] Level 2: gate runs all three; dispatch=0, os=0, host=7 (current notifier.c);
      ends "✗ … FAILED", exit 1.
- [ ] Level 3: §11.2A 2023/0; §11.2B result=0 & <50 ms; §11.2C six 1s; §11.2D as in L2.
- [ ] Level 4: every host `^FAIL:` line contains "SET_OS"; non-SET_OS FAIL grep empty.

### Feature Validation

- [ ] Runner compiles notifier.c into `$OBJ` exactly ONCE; links 3 drivers from it.
- [ ] Host link uses the SAME flags as dispatch/os (`-Wall -std=c99 -Iqmk_stubs -I.`).
- [ ] Steps numbered `[1/5]`..`[5/5]`; host link `[4/5]`; run step `[5/5]`.
- [ ] Three `notifier <name> fails=N (exit=M)` summary lines printed.
- [ ] PASS condition tests all three binaries' `fails==0 && rc==0`.
- [ ] Header comment names `test_notifier_host` + its coverage (Mode A).
- [ ] dispatch + os pass UNCHANGED (legacy + multi-OS paths intact — no regression).
- [ ] No regression in the 9 pattern_match suites (§11.2A green).

### Code Quality Validation

- [ ] Runner mirrors the existing dispatch/os idioms (flags, `|| true`, rm-on-fail).
- [ ] Exit-code escalation preserved (2 compile / 3 dispatch / 4 os / 5 host / 1 gate).
- [ ] All four temporaries cleaned up on every path (success + each failure).
- [ ] Executable bit preserved (`chmod +x` if the file was rewritten).
- [ ] No anti-patterns (see below).

### Documentation & Deployment

- [ ] Header comment (Mode A) updated to name the third binary + its coverage.
- [ ] No README change (README sync is P1.M3.T3.S1). No new env vars / config.
- [ ] The blocker is REPORTED (gate RED on host) — not masked — with its owner (P1.M2).

---

## Anti-Patterns to Avoid

- ❌ Don't recompile `notifier.c` for the host step — link the shared `$OBJ`. The
  contract: "do not compile notifier.c thrice." (`grep -c '\-c notifier.c'` must be 1.)
- ❌ Don't use different link flags for host — mirror dispatch/os EXACTLY
  (`-Wall -std=c99 -Iqmk_stubs -I.`). Do NOT add `-Wextra` to the link steps.
- ❌ Don't weaken the gate to tolerate the 7 SET_OS FAILs (no `grep -v SET_OS`, no
  "expected fails=N" allowance, no excluding `test_notifier_host`). The contract:
  "iff all three have 0 FAIL." The RED gate is the intended signal of the blocker.
- ❌ Don't modify `test_notifier_host.c` to make SET_OS pass (S3 forbids weakening
  the assertions; the tests are correct, the firmware framing is flawed).
- ❌ Don't modify `notifier.c` / `notifier.h` / `qmk_stubs/*` — the SET_OS framing
  fix is P1.M2's job, NOT this runner task.
- ❌ Don't reorder the dispatch/os/host runs or change their flags — the contract
  requires dispatch+os pass UNCHANGED.
- ❌ Don't forget `$HST` in a `rm -f` cleanup path (success or any link failure) —
  a stale `/tmp/test_notifier_host` could mask a later run.
- ❌ Don't drop the executable bit if you rewrite the file (`chmod +x` after `write`).
- ❌ Don't claim "§11.2D green" — with the current `notifier.c` it is RED on host.
  Report it honestly and attribute the blocker to P1.M2's notifier.c framing.

---

## Confidence Score

**Confidence: 10/10 for the runner deliverable (it is a mechanical, well-specified
extension of an existing 55-line script, with the complete target file given
verbatim). 0/10 for "all three 0 FAIL / gate green" against the CURRENT notifier.c —
this is a VERIFIED upstream blocker (SET_OS cmd_id `0x03` == ETX `0x03`; OS_MACOS
os_byte also `0x03`), located precisely in `hid_notify`'s reassembly loop
(notifier.c ~L771-814), with the SET_OS handler (L693) existing but unreachable.
It is owned by P1.M2 (marked Complete but the flaw persists) and is OUT OF SCOPE
for this runner task.** The PRP ships a faithful three-binary runner that correctly
reports §11.2D as RED on `test_notifier_host` (7 SET_OS FAILs) until P1.M2 fixes
the typed-binary-payload framing; §11.2A/B/C and the dispatch/os binaries are
GREEN (all verified by running the actual commands). This surfaces the flaw in the
acceptance gate rather than masking it — the correct, contract-faithful outcome.