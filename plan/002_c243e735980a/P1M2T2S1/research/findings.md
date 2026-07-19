# Research Notes — P1.M2.T2.S1: Extend runner + verify full §11.2A-D gate

## THE load-bearing finding — the current runner is ALREADY BROKEN

The current `run_notifier_stub_tests.sh` [2/3] link step uses `gcc -Wall -std=c99
-I. …` — only `-I.`, NO `-Iqmk_stubs`. This USED to work (pre-multi-OS) because
notifier.h did not include os_detection.h. But P1.M1.T1.S1 (COMPLETE) added
`#include "os_detection.h"` to notifier.h (notifier.h:3), and os_detection.h
lives ONLY in `qmk_stubs/`. So the current runner now FAILS:

```
$ ./run_notifier_stub_tests.sh
[1/3] stub-compile notifier.c ...
[2/3] link dispatch driver ...
In file included from test_notifier_dispatch.c:6:
notifier.h:3:10: fatal error: os_detection.h: No such file or directory
LINK FAILED
runner exit=3
```

This breaks BOTH binaries (dispatch AND os) — the [2/3] dispatch link is broken
TODAY, not just the future os link. The parallel P1.M2.T1.S1 PRP explicitly
references this: "the run_notifier_stub_tests.sh [2/3] -Iqmk_stubs gap (fixed by
P1.M2.T2.S1)". **This task IS that fix.**

### The fix (verified)

Add `-Iqmk_stubs` to BOTH link steps (matching the [1/3] compile step which
already has `-Iqmk_stubs -I.`). The item description's example `[2b]` command
(`gcc -Wall -std=c99 -I. $OBJ … test_notifier_os.c`) is **INCORRECT** — it must
be `gcc -Wall -std=c99 -Iqmk_stubs -I. …`. Verified: with `-Iqmk_stubs -I.` on
both link steps, dispatch links (0 fails) AND os links (0 fails).

NOTE on why `-I.` alone ever worked: `#include "os_detection.h"` searches (1)
the directory of the *file containing the #include line* — for the directive in
notifier.h that is `.` (notifier.h is at repo root) — then (2) the `-I` paths.
os_detection.h is NOT in `.`; it is in `qmk_stubs/`. So `-Iqmk_stubs` is
REQUIRED. (The directory-of-includer rule does not recurse into subdirs.)

## The exact draft runner (verified end-to-end, 55 lines)

Staged the research artifact `test_notifier_os_val.c` as a stand-in for the
not-yet-landed `test_notifier_os.c`, dropped in the 55-line runner below, ran it:

```
[1/4] stub-compile notifier.c (shared by both test binaries) ...
[2/4] link dispatch driver (test_notifier_dispatch) ...
[3/4] link multi-OS driver (test_notifier_os) ...
[4/4] run both ...
notifier dispatch fails=0  (exit=0)
notifier os fails=0  (exit=0)
✓ notifier stub-compile gate PASSED
exit=0
```

The script (verbatim, to be reproduced in the PRP):

```bash
#!/usr/bin/env bash
# P2 stub-compile validation gate for notifier.c (closes RISK-1).
# [header updated to mention BOTH binaries — Mode A]
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

### Design points verified

- **Shared .o**: notifier.c compiled ONCE (`-c notifier.c -o "$OBJ"`), then `$OBJ`
  is passed to BOTH link gcc calls. Do NOT compile notifier.c twice. (Item-spec
  3a: "The .o is compiled ONCE and linked into both.")
- **Distinct exit codes** for each failure mode: 2 (compile), 3 (dispatch link),
  4 (os link), 1 (gate fail on fails>0), 0 (pass). Aids triage.
- **PASSED iff** both `fails==0` AND both `rc==0`. (The test binaries return
  non-zero iff they had a FAIL: line, so `rc==0` and `fails==0` are redundant —
  but checking both is belt-and-suspenders and matches the original runner's
  `[ "$fails" -eq 0 ] && [ $rc -eq 0 ]`.)
- **Cleanup**: `rm -f "$OBJ" "$DRV" "$OST"` before the final verdict (matches
  the original runner's cleanup discipline; the binaries are in /tmp).
- **`set -u`** preserved (treats unset vars as errors — catches typos).
- **`cd "$(dirname "$0")"`** preserved (runner works from any CWD).
- **`|| true`** after `grep -c`: grep returns 1 when count is 0, which under
  `set -u`/command-substitution would otherwise abort. The original runner has
  this; keep it.

## qmk_stubs.c state — accessor ALREADY landed

`qmk_stubs/qmk_stubs.c` ALREADY contains `stub_get_active_layer()` (lines 22-26,
Mode-A documented) — the parallel P1.M2.T1.S1 landed that part. So the runner
does NOT need to touch qmk_stubs.c; it only consumes the accessor (transitively,
via test_notifier_os.c linking qmk_stubs.c). This task touches ONLY
run_notifier_stub_tests.sh.

## §11.2A-C baseline (pattern_match corpus — UNAFFECTED by multi-OS, already green)

Verified by running each gate during research:

- **§11.2A** `./run_all_tests.sh`: all 9 suites report `ALL TESTS PASSED`;
  per-suite `grep -c '^FAIL:'` = 0 for every one; runner exit 0.
  (test_pattern_match 376 cases, test_char_classification 179, etc. — the matcher
  is P1 scope, already complete, untouched by this multi-OS plan.)
- **§11.2B** pathological NFA stress `a+a+a+a+a+a+a+a+a+a+b` vs 199 `a`s:
  `result=0  1816.0 us` — 1.8 ms, well under the 50 ms bar. Green.
  (run_all_tests.sh ALSO has its OWN perf_test — 100k iterations × 7 simple
  patterns < 1s — but that is a DIFFERENT benchmark; the §11.2B gate is the
  separate /tmp/nfa_stress.c. The item-spec 3c note "run_all_tests.sh already
  includes a perf benchmark; confirm sub-second" refers to the inline perf_test,
  which is the coarse check; §11.2B is the strict <50ms pathological check.)
- **§11.2C** realistic patterns: outputs `1 1 0 1 1 1`. Five of six print 1.
  The `^\w+@\w+$` vs `user_host` line prints **0** — which is CORRECT regex
  behavior: `user_host` contains NO `@`, so an anchored `^\w+@\w+$` cannot match.
  The PRD §11.2C comment `/* 1 */` for that line is **erroneous** (likely meant
  `user@host`). This is a PRE-EXISTING pattern_match condition, OUTSIDE this
  task's scope (runner only). Per invariant 12 ("if a test flips red, fix the
  matcher, not the test") — but nothing is red here; the matcher is correct.
  The PRP must document this so the implementer does not mistake it for a
  regression or try to "fix" the matcher.

## test_notifier_dispatch backward-compat (findings F5 — confirmed)

test_notifier_dispatch.c defines `DEFINE_SERIAL_COMMANDS`/`DEFINE_SERIAL_LAYERS`
(no `_OS` variants) and never calls `notifier_set_os`, so `current_os` stays
`OS_UNSURE` → `select_*_map_os` returns `{NULL,0}` → OS scan runs 0 iterations →
default map scanned → identical to pre-multi-OS behavior. Verified: it links
(with the `-Iqmk_stubs` fix) and reports 11/11, 0 FAIL. **No edits needed** to
test_notifier_dispatch.c (item-spec 3b). The only reason it currently fails in
the runner is the `-Iqmk_stubs` gap, which this task fixes.

## Dependencies / boundary with the parallel task (P1.M2.T1.S1)

- P1.M2.T1.S1 (parallel, IMPLEMENTING) produces `test_notifier_os.c` AND adds
  `stub_get_active_layer()` to qmk_stubs.c. The accessor is ALREADY landed;
  test_notifier_os.c is NOT yet present at research time.
- This task (P1.M2.T2.S1) consumes both: the runner links test_notifier_os.c and
  (transitively) the accessor. It does NOT create test_notifier_os.c and does NOT
  touch qmk_stubs.c.
- The runner will only fully pass once test_notifier_os.c lands. Until then, the
  [3/4] os link fails with "cannot find test_notifier_os.c". The PRP notes this
  ordering dependency explicitly (the runner is correct; it just needs its input).

## Item-description discrepancy (resolved)

The item-spec step (3a) gives the [2b] os-link command as
`gcc -Wall -std=c99 -I. $OBJ qmk_stubs/qmk_stubs.c test_notifier_os.c -o …`
(i.e. `-I.` only). This is **WRONG** — empirically it fails with
`os_detection.h: No such file or directory`. The correct flags are
`-Iqmk_stubs -I.` (matching the compile step and the §11.1 doc command, which
HAS `-Iqmk_stubs -I.`). The PRP prescribes `-Iqmk_stubs -I.` for BOTH link
steps. This is the single substantive deviation from the item's literal example,
and it is forced by the empirical failure.