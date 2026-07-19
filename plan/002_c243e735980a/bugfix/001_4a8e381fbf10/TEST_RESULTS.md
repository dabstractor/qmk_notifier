# Bug Fix Requirements

## Overview

End-to-end PRD validation of the qmk-notifier implementation (session
`002_c243e735980a`, multi-OS delta). Testing was performed as an adversary and
as a user, covering: the exact §10.2 reference keymap against realistic desktop
messages; the wire protocol's real 30-byte-per-report framing and multi-report
reassembly; buffer boundaries (254 / 255 / 256 bytes + post-overflow state
bleed); empty-workspace / empty-title messages (§4.1 examples); multi-OS
dispatch with several OS maps defined simultaneously (MACOS + LINUX + WINDOWS +
IOS) and OS-change clearing; case-sensitivity through the full dispatch path;
regex classes (`\d+`, `\b`) through dispatch; ~9000 iterations of fuzzed/garbage
HID reports and `process_full_message` strings; the NULL-`on_enable` guard; and
the exact PRD acceptance micro-benchmarks (§11.2B pathological stress, §11.2C
realistic patterns).

**Headline assessment:** the implementation is correct, robust, and complete.
**All 2019 automated assertions pass** (`run_all_tests.sh`), **both notifier
stub-compile suites pass** (`run_notifier_stub_tests.sh`: dispatch + multi-OS),
the pathological NFA case finishes in ~1.7 ms (well under the 50 ms gate), and
**every creative/adversarial probe I wrote passed** — including fuzzing that
produced no crashes. There are **no Critical bugs** and **no defects in core
matching/dispatch/reassembly functionality**.

The one issue that warrants action is a **discrepancy in the PRD's own
acceptance gate (§11.2C)** where the **code is correct but the gate's expected
value is wrong** — so the gate (and the matching Definition-of-Done checklist
item) cannot pass as literally written. Two additional minor observations are
included for completeness.

> ⚠️ **Read this before touching any code:** the matcher (`pattern_match.c`) is
> **correct**. Issue 1 is a *specification/acceptance-gate* defect, not a code
> defect. The recommended fixes are (a) correct the PRD §11.2C example string,
> and/or (b) add a regression test that locks in the *correct* behavior so a
> future "rebuild to spec" does not regress the matcher toward the wrong
> expectation. **Do NOT modify `pattern_match.c` to make Issue 1's case return
> `1` — that would break literal-`@` matching and constitute a regression.**

## Critical Issues (Must Fix)

None. Core functionality (Raw HID reception, reassembly, sanitization,
Thompson-NFA matching, delimiter-aware F4 matching, dispatcher ordering, ack,
multi-OS map selection, OS-change clearing, weak-symbol defaults) all work
correctly and survive adversarial input without crashing.

## Major Issues (Should Fix)

### Issue 1: PRD §11.2C acceptance gate has a wrong expected value — the matcher is correct, the gate cannot pass
**Severity**: Major (blocks a formal acceptance criterion / Definition-of-Done item; carries regression risk)
**PRD Reference**: §11.2C "(C) Realistic patterns still match (all six must print `1`)" and the Definition-of-Done checklist item *"`/tmp/nfa_real` prints six `1`s (§11.2C)."*
**Expected Behavior** (per PRD §11.2C): the snippet's third line should print `1`:
```c
printf("%d\n", pattern_match("^\\w+@\\w+$","user_host",1));  /* PRD comment: 1 */
```
**Actual Behavior**: it prints `0`. Running the exact §11.2C snippet produces
`1, 1, 0, 1, 1, 1` — **five** `1`s, not six — so the §11.2C gate and the DoD
checklist item fail as written.

**Root cause (the bug is in the PRD, not the code):** the pattern `^\w+@\w+$`
requires a **literal `@`** between two `\w+` groups (`@` is an ordinary literal
byte — PRD §7.1/§7.7 — and `\w` = `[A-Za-z0-9_]`, PRD §15, which does **not**
include `@`). The string `user_host` contains an underscore, **not** an `@`, so
the pattern correctly does **not** match. The matcher is behaving exactly per
spec. The PRD example string is a typo — it very clearly intended `user@host`,
which does return `1` (verified):
```
pattern_match("^\w+@\w+$", "user_host", 1)  == 0   // actual (CORRECT)
pattern_match("^\w+@\w+$", "user@host", 1)  == 1   // what the PRD meant
pattern_match("^\w+_\w+$", "user_host", 1)  == 1   // if the pattern were \w+_\w+
```
**Steps to Reproduce:**
```bash
cat > /tmp/nfa_real.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  printf("%d\n", pattern_match("\\w+","hello",1));
  printf("%d\n", pattern_match("\\b\\w+\\b\\s+\\b\\w+\\b","hello world",1));
  printf("%d\n", pattern_match("^\\w+@\\w+$","user_host",1));   /* <- prints 0, PRD says 1 */
  printf("%d\n", pattern_match("v\\.code","v.code",1));
  printf("%d\n", pattern_match("a+b","aaab",1));
  printf("%d\n", pattern_match("*slack*","Slack - general",0));
  return 0;
}
EOF
gcc -w /tmp/nfa_real.c pattern_match.c -I. -o /tmp/nfa_real && /tmp/nfa_real
# output: 1 1 0 1 1 1   (third line is 0)
```
**Impact:**
- The manual §11.2C acceptance gate prints five `1`s, so the DoD item
  *"`/tmp/nfa_real` prints six `1`s"* cannot be honestly checked off.
- **Note:** this does **not** affect the automated gate. §11.2C is a manual
  snippet and is **not** included in `run_all_tests.sh`
  (`grep -c 'nfa_real\|user_host\|w+@' run_all_tests.sh` → `0`). The 2019-test
  automated suite and both notifier stub suites pass fully. So CI is green; only
  a human/agent running the literal §11.2C/DoD gate observes the "failure".
- **Regression risk:** because the PRD is the spec, a future developer or agent
  rebuilding "to spec" could reproduce the wrong expectation and "fix" the
  matcher to make `^\w+@\w+$` match `user_host` — which would break literal-`@`
  matching. A regression test prevents this.

**Suggested Fix (pick one or both; the first is doc-only, the second is the
actionable code-pipeline fix):**
1. **Doc fix (preferred):** correct the PRD §11.2C example string from
   `"user_host"` to `"user@host"`, and update the DoD checklist item
   accordingly. (PRD.md is human-owned; this is the authoritative correction.)
2. **Regression test (actionable in code):** add a small host test that asserts
   the **correct** semantics so the matcher is never regressed toward the broken
   expectation — e.g. assert `pattern_match("^\\w+@\\w+$","user_host",1)==0`
   and `pattern_match("^\\w+@\\w+$","user@host",1)==1`. This locks in that `@`
   is a literal and `\w` excludes `@`.

**Do NOT:** alter `pattern_match.c` / `process_escapes` / `pattern_char_matches`
to make the `user_host` case return `1`. That is the wrong direction.

## Minor Issues (Nice to Fix)

### Issue 2: `sanitize_string` truncates at a NUL byte instead of stripping it (unreachable in normal operation)
**Severity**: Minor (theoretical robustness quirk; no crash; unreachable via the real transport)
**PRD Reference**: §F2.3 / §8.2 ("keep only bytes 0x20–0x7E plus 0x09/0x0A/0x0D/0x1D/0x03; strip every other byte")
**Expected Behavior**: a `0x00` (NUL) byte is "every other byte" and should be
**stripped**, leaving subsequent valid bytes intact.
**Actual Behavior**: `sanitize_string` loops `while (*read_ptr)`, so it **stops
at the first NUL** and abandons everything after it (truncation, not stripping).
If a NUL appeared in `msg_buffer` before the terminator, the dispatched message
would be the prefix up to that NUL and the rest would be silently lost.
**Reachability (why this is Minor):** I verified this is **not reachable in
normal operation**. The transport crate appends ETX to the payload *before*
framing and zero-fills only the **final** report **after** the ETX (PRD §4.2);
`hid_notify` breaks at ETX before any trailing zero-fill is appended, so
`msg_buffer` never contains an embedded NUL before its terminator. The desktop
sends only printable ASCII + GS in the class/title. A NUL before ETX would
require a malformed/hostile frame or USB corruption.
**Steps to Reproduce** (requires a synthetic malformed frame; not a real
transport scenario): feed `hid_notify` a report whose payload is
`'n'` + 29× `0x00` (a 1-byte-payload report left zero-filled before the real
payload), repeated; the NULs accumulate and sanitize truncates.
**Suggested Fix (optional):** have `sanitize_string` iterate by an explicit
length (or document that NUL terminates). Low priority given it is unreachable
via the spec'd transport.

### Issue 3: `CONSOLE_ENABLE` debug block omits the layer-track match/miss print
**Severity**: Minor (debug-only output; no functional impact)
**PRD Reference**: §8.6 step 9 ("(CONSOLE_ENABLE) print per-track match/miss; GS shown as `|`")
**Expected Behavior**: debug output for **both** the command track and the layer
track (the spec says "per-track").
**Actual Behavior**: the `#ifdef CONSOLE_ENABLE` block at the end of
`process_full_message` prints only the **command** track
(`"Matched message %s on command: %s"` / `"Did not match message %s on any
command"`). The **layer** track match/miss is not printed. (The GS→`|`
substitution is applied correctly.)
**Steps to Reproduce:** build with `-DCONSOLE_ENABLE` and a matching layer rule;
observe no layer-track line in the console output.
**Suggested Fix (optional):** add a layer-track `uprintf` mirroring the command
one (e.g. print the matched layer index or a miss), to match §8.6 step 9's
"per-track" wording.

## Testing Summary
- **Total tests performed:** ~9,300+ executions across ~20 bespoke probe
  programs, plus the full existing corpus (2019 automated assertions + 2
  notifier stub suites + §11.2B/§11.2C micro-benchmarks).
- **Passing:** all automated gates (2019 + dispatch + multi-OS); all
  creative/adversarial probes (reference keymap, wire reassembly, boundaries,
  empty messages, multi-OS w/ multiple maps, case-sensitivity, regex classes,
  fuzzing, NULL callbacks, long patterns); §11.2B pathological stress (~1.7 ms).
- **Failing:** the manual §11.2C snippet (Issue 1 — code is correct; the PRD
  expected value is wrong). No automated gate fails.
- **Areas with good coverage:** Thompson-NFA matching (2019 cases), multi-OS
  selection/fallback/clearing, dispatcher ordering, reassembly/ETX framing,
  coexistence guard, weak-symbol defaults, backward compatibility, fuzz
  robustness.
- **Areas needing more attention:** the **acceptance-gate text itself** (§11.2C
  expected value is wrong) — this is a documentation/spec drift that the
  automated suites cannot catch because §11.2C is a manual snippet. A regression
  test (Issue 1 fix #2) would close this gap permanently.