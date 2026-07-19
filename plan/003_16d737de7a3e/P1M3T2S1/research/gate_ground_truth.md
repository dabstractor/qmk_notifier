# P1.M3.T2.S1 Research — Runner Extension + §11.2 Gate Ground Truth

## 0. THE PIVOTAL FINDING (read first)

`test_notifier_host.c` (404 lines, produced by S2+S3+S4) **currently produces 7
`FAIL:` lines, all from the SET_OS blocks (i-iv)**, when built+run against the
current `notifier.c`:

```
Total tests run: 64 / passed: 57 / failed: 7
FAIL: (i)   SET_OS r[1]=0x03 cmd echo [§4.6]
FAIL: (i)   SET_OS r[2]=ack=1 [§4.6]
FAIL: (ii)  post-SET_OS(OS_MACOS): OS_MACOS command fired (current_os changed) [§4.7]
FAIL: (ii)  post-SET_OS(OS_MACOS): OS_MACOS layer 44 selected [§4.7]
FAIL: (iii) SET_OS change: prev command on_disable fired [§4.7/F9.1]
FAIL: (iii) SET_OS change: board layer deactivated (cleared) [§4.7/F9.1]
FAIL: (iv)  SET_OS idempotent: no layer change on same-OS [§4.7/F9.3]
```

**Root cause (VERIFIED, UNRESOLVED in current notifier.c):** `hid_notify`'s
reassembly byte loop (notifier.c:771-814) treats EVERY `0x03` byte as ETX:
```c
for (uint8_t i = 0; i < length; i++) {
    char c = (char)data[i];
    if (c == ETX_TERMINATOR[0]) {  // 0x03 -> dispatch NOW
        ... handle_typed_command(msg_buffer); break;
    } else { msg_buffer[msg_index++] = c; }
}
```
SET_OS's cmd_id is `0x03` (== ETX), so for the byte stream
`[0xF0, 0x03(cmd_id), os_byte, 0x03(ETX)]` the loop appends `0xF0`, then sees
the cmd_id `0x03` and dispatches with `msg_buffer=[0xF0,0,0…]`. The SET_OS
*handler* (notifier.c:693 `case NOTIFY_CMD_SET_OS:`) EXISTS and is correct, but
is **unreachable** via `hid_notify`. Doubly broken for macOS: `OS_MACOS==3`
(0x03), so the os_byte argument also collides with ETX. This is a wire-protocol
framing flaw (ETX-termination is safe for text 0x20–0x7E but incompatible with
binary typed payloads). **Owned by P1.M2 (the reassembly loop's binary-payload
framing). P1.M2 is marked Complete but the flaw persists.**

## 1. CONSEQUENCE FOR THIS TASK (P1.M3.T2.S1)

The task contract says:
- LOGIC (a): "...end '✓ notifier stub-compile gate PASSED' **iff all three have
  0 FAIL:** and exit 0."
- OUTPUT: "...the full §11.2A-D gate is green..."

These two are **in direct tension with the codebase reality**: a faithfully
implemented runner (one that requires 0 FAIL for all three binaries, per the
contract) will print **"✗ ... FAILED"** and **exit 1** with the current
notifier.c, because test_notifier_host has 7 SET_OS FAILs.

**Resolution chosen for the PRP (faithful engineering, per the S3 philosophy):
"the acceptance gate MUST capture rather than hide the flaw."**
- Build the runner FAITHFULLY (require 0 FAIL; PASSED iff all three green).
- The runner will be RED on test_notifier_host — this is the CORRECT, intended
  signal, NOT a task defect.
- §11.2A/B/C ARE green (verified). §11.2D is green for the 2 existing binaries
  (dispatch, os — verified, no regression); D turns RED ONLY because of
  test_notifier_host's 7 SET_OS FAILs.
- The PRP's Success Definition splits: (a) runner correctly built + dispatch/os
  unchanged + A/B/C green = ACHIEVABLE in-scope; (b) "all three 0 FAIL / gate
  green" = BLOCKED, requires the P1.M2 notifier.c framing fix (out of scope here).
- The PRP explicitly forbids the masking anti-patterns: do NOT exclude
  test_notifier_host, do NOT count only some FAILs, do NOT weaken
  test_notifier_host.c, do NOT "fix" notifier.c here.

## 2. §11.2A–D gate ground truth (captured 2025-07-19)

| Sub-gate | Command | Result | Status |
|---|---|---|---|
| §11.2A | `./run_all_tests.sh` | 2023/2023, 0 failures | ✅ GREEN |
| §11.2B | NFA stress `a+×10+b` vs 199×a | result=0, **1828 µs** (<50 ms) | ✅ GREEN |
| §11.2C | six realistic patterns | **1 1 1 1 1 1** | ✅ GREEN |
| §11.2D (dispatch) | `./run_notifier_stub_tests.sh` (2 binary) | fails=0, exit=0 | ✅ GREEN |
| §11.2D (os) | (same) | fails=0, exit=0 | ✅ GREEN |
| §11.2D (host, NEW) | probe build+run | **fails=7**, exit≠0 | 🔴 RED (SET_OS blocker) |

No regression: dispatch + os still 0 FAIL after all the host work (P1.M1/M2/M3.T1).
The 9 pattern_match suites still 0 failures.

## 3. test_notifier_host.c contract (what the runner must link+run)

- Compiles clean with `-Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I.` (verified: 0 warnings).
- Links against the SAME shared `notifier_stub.o` the dispatch/os drivers use
  (the contract says "do not compile notifier.c thrice").
- Prints `PASS:`/`FAIL:` lines + a summary `Total tests run: N / passed: P /
  failed: F`; **exits non-zero iff g_fail != 0** (`return g_fail?1:0`).
- The runner greps `^FAIL:` (matches the dispatch/os pattern).
- Currently: 64 tests, 57 pass, 7 fail (all SET_OS). AHC (v-viii), QUERY_*,
  coexistence, multi-report blocks all PASS.

## 4. Current run_notifier_stub_tests.sh structure (55 lines, to be extended)

```
set -u ; cd "$(dirname "$0")"
OBJ=/tmp/notifier_stub.o
DRV=/tmp/test_notifier_dispatch
OST=/tmp/test_notifier_os
[1/4] gcc -Wall -Wextra -std=c99 ... -c notifier.c -o $OBJ     ; fail->exit 2
[2/4] gcc -Wall -std=c99 -Iqmk_stubs -I. $OBJ qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -o $DRV ; fail-> rm $OBJ; exit 3
[3/4] gcc -Wall -std=c99 -Iqmk_stubs -I. $OBJ qmk_stubs/qmk_stubs.c test_notifier_os.c       -o $OST ; fail-> rm $OBJ $DRV; exit 4
[4/4] run $DRV (rc_d, fails_d) ; run $OST (rc_o, fails_o)
      echo "notifier dispatch fails=$fails_d (exit=$rc_d)"
      echo "notifier os       fails=$fails_o (exit=$rc_o)"
      rm -f $OBJ $DRV $OST
      if fails_d==0 && rc_d==0 && fails_o==0 && rc_o==0 -> echo PASSED; exit 0
      else -> echo FAILED; exit 1
```

## 5. Exact extension (the PRP's deliverable)

- Header comment: add mention of the 3rd binary (test_notifier_host) covering
  typed commands + host rules (QUERY_INFO/QUERY_CALLBACK/SET_OS/APPLY_HOST_CONTEXT
  + coexistence + multi-report framing).
- Add `HST=/tmp/test_notifier_host`.
- Renumber [1/4]→[1/5], [2/4]→[2/5], [3/4]→[3/5]; add [4/5] link host; [4/4] run→[5/5].
- New link step (flags IDENTICAL to dispatch/os link): `gcc -Wall -std=c99
  -Iqmk_stubs -I. $OBJ qmk_stubs/qmk_stubs.c test_notifier_host.c -o $HST`;
  fail -> `rm -f $OBJ $DRV $OST; exit 5`.
- [5/5]: run $HST (rc_h, fails_h).
- Summary: add `echo "notifier host fails=$fails_h (exit=$rc_h)"`.
- Cleanup: `rm -f $OBJ $DRV $OST $HST`.
- Final: add `&& fails_h==0 && rc_h==0` to the PASSED condition.
- **Compile flags rationale**: the LINK steps use `-Wall -std=c99` (NOT -Wextra);
  test_notifier_host.c is clean under -Wextra (verified), so -Wall is clean too.
  The COMPILE step stays `-Wall -Wextra`.

## 6. Compile-flag note (why host link uses -Wall, not -Wextra)

The existing dispatch/os link invocations use `-Wall -std=c99` (the -Wextra is
only on the single shared object-compile of notifier.c). test_notifier_host.c
compiles clean even under `-Wall -Wextra` (verified: 0 warnings in the probe),
so the host link step mirrors dispatch/os exactly: `-Wall -std=c99`. This keeps
all three link steps uniform. (No -Wmissing-field-initializers under -Wall because
that's -Wextra-gated; the OS_MACOS map rows already carry trailing `false`.)