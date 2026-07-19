# Bug Fix Requirements

## Overview

Creative end-to-end QA was performed against the qmk-notifier PRD (§1–§17), exercising
the full acceptance gate (`run_all_tests.sh` + `run_notifier_stub_tests.sh`), the PRD
§11.2 B/C stress probes, and a large battery of **adversarial probes written from
scratch** (not the shipped suites) targeting wire-protocol framing, the typed-command
namespace, multi-OS dispatch, delimiter matching, buffer boundaries, and state
transitions.

**Baseline result: the implementation is high quality.** Every shipped suite passes
(pattern corpus 2023/2023; notifier stub gate: dispatch 14/14, OS 31/31, host 64/64).
The §11.2B pathological NFA case finishes in ~1.2 ms (< 50 ms). The §11.2C realistic
patterns all print `1`. The full §10.2 reference keymap dispatches correctly end-to-end.
The reference keymap's anchored `WT(...)` patterns, F4 delimiter matrix, multi-OS
merge/fallback independence (F8.4/F8.5), F9 OS-change clearing, and backward
compatibility (no `DEFINE_*`, no `DEFINE_HOST_CALLBACKS`) all behave per spec.

**However, probing *beyond* the shipped corpus uncovered one Major robustness defect in
the newest code (the §4.6 typed-command length-aware reassembly) and one Minor
documentation inaccuracy.** The Major issue is entirely missed by the shipped host test
suite, which deliberately constructs `APPLY_HOST_CONTEXT` payloads that avoid `0x03`
bytes and never exercises a `count`/ids mismatch or a truncated/abandoned typed message.

---

## Critical Issues (Must Fix)

_None found._ No in-contract input crashes the firmware or produces a wrong match/dispatch.
Core functionality (context-aware layer switching via legacy focus-change strings) works
correctly for all PRD-conformant inputs.

---

## Major Issues (Should Fix)

### Issue 1: A malformed/truncated typed command permanently breaks legacy layer/command routing (typed reassembly desync)
**Severity**: Major
**PRD Reference**: §4.6 (typed-command framing / `APPLY_HOST_CONTEXT`), §4.2 (burst-write
reliability), §8.8 (`hid_notify`), §1.3 & §12 ("Robust to garbage … no input can crash
it"), §2 F9.4 (KVM / USB-switch environments), §13 invariant 21 (host/board state planes).
**Module file**: `notifier.c` — `typed_literal_remaining` (decl line ~115), the count
extension in the byte loop (lines ~924–926), and the `typed_literal`/ETX gate (lines ~858–862).

**Expected Behavior**
A typed command whose `APPLY_HOST_CONTEXT.count` does not match the number of trailing id
bytes — e.g. a host bug, a truncated frame, or a USB/KVM disconnect mid-`APPLY_HOST_CONTEXT`
— must not corrupt the module's *primary* function. Per §1.3/§12 the firmware must remain
"robust to garbage"; at minimum, a malformed/abandoned typed message should be dropped
silently (like an oversized legacy message, F2.2) and the very next well-formed legacy
focus-change string must resume switching layers/commands normally.

**Actual Behavior**
The length-aware typed reassembly trusts `APPLY_HOST_CONTEXT.count` unconditionally: when
the count byte is read (`msg_buffer[4]`, notifier.c ~924), `typed_literal_remaining` is
extended by the **declared** count (clamped only to buffer *room*, ~925–926, **not** to the
bytes actually present in the stream). If the host sends fewer ids than `count` declares
(or the stream is interrupted) the reassembly keeps consuming **every subsequent byte —
including the `0x03` ETX terminator and all bytes of later messages — as literal id bytes**.
Because the ETX is swallowed, `handle_typed_command` is never invoked, so the only places
that clear `typed_mode` (the ETX branch ~889 and the overflow branch ~934) never run.
`typed_mode` is left `true` **persistently**, so every later legacy focus-change string is
misrouted to `handle_typed_command` (where it matches no typed cmd and emits a default
`[0x51]` ack with **no board side effects**). The board layer/command dispatch — the
module's whole purpose — stops working until either the 256-byte `msg_buffer` finally
overflows (which can require hundreds of focus changes) or the keyboard is replugged.

This is not a crash, so it technically satisfies "no input can crash it", but it is a
persistent silent failure of core functionality triggered by garbage/abnormal input —
exactly the class §1.3/§12 target, and explicitly relevant to the KVM/USB-switch world of
§2 F9.4.

**Steps to Reproduce** (host stub harness; compile like `test_notifier_host`):
```c
/* 1. Send a malformed AHC: count=5 but only ONE id byte, then ETX. */
uint8_t r[32] = {0};
r[0]=0x81; r[1]=0x9F; r[2]=0xF0; r[3]=0x05; r[4]=224; r[5]=0; r[6]=5; r[7]=0x41; r[8]=0x03;
hid_notify(r, 32);
/* observed: response[0] == 0x00  (the ETX was consumed as a literal id; AHC never dispatched) */

/* 2. Now send a normal legacy focus-change string that SHOULD match a board command/layer. */
uint8_t s[32] = {0}; s[0]=0x81; s[1]=0x9F;
memcpy(s+2, "neovide\x03", 8); hid_notify(s, 32);
/* observed: response[0] == 0x51  (misrouted to the TYPED path), board on_enable NEVER fires */
```
Observed output from the probe:
```
after malformed AHC: response[0]=0x00 (ETX consumed as literal id; no typed dispatch)
after legacy 'neovide': response[0]=0x51, board_on=0   ← legacy routing BROKEN
```
Sending a well-formed `QUERY_INFO` after the desync does **not** recover it — its bytes are
also consumed as outstanding literal ids — so even a reconnect handshake cannot restore
legacy routing without a replug.

**Notes on why shipped tests missed it**
- `test_notifier_host.c` (multi-rep block, ~line 357) constructs a 2-report AHC with
  `count=28` and *exactly* 28 ids, and every byte is explicitly chosen `!= 0x03`. No case
  sends `count != ids`, no case truncates, and no case sends any id byte equal to `0x03`.
  The length-aware fix is therefore tested only on the happy path.
- The defense-in-depth clamp in `handle_typed_command` (notifier.c ~785–790) bounds `count`
  to the *reassembled* length — but it only runs **if dispatch happens**, and in this
  failure mode dispatch never happens, so the clamp cannot save the reassembly.

**Suggested Fix (direction only — not prescribing exact code)**
Bound the typed-literal window by the bytes that can actually still arrive for *this*
logical message rather than by raw buffer room, and/or add a watchdog that resets the typed
reassembly state (`typed_mode`/`typed_literal_remaining`/`msg_index`) when a typed message
is abandoned (e.g. a full 32-byte report arrives while `typed_literal_remaining > 0` with no
ETX and no progress, or `typed_literal_remaining` exceeds the bytes remaining in the current
report stream). The simplest robust option: cap `typed_literal_remaining` so it can never
span past the current report's remaining bytes plus a bounded look-ahead, and treat an
end-of-stream with outstanding literal bytes as a dropped (malformed) message — resetting
`typed_mode` exactly like the overflow path does today.

---

## Minor Issues (Nice to Fix)

### Issue 2: README falsely reports the `SET_OS` typed-command tests as broken / pending a fix
**Severity**: Minor (documentation, but about a core v0.3.0 feature)
**PRD Reference**: §4.6 / §4.7 (`SET_OS`), §11 acceptance gate, Definition of Done.
**File**: `README.md` lines ~540–545.

**Expected Behavior**
The README's "Overall Test Results" should reflect the live gate: the `SET_OS`
ETX-collision framing fix is already implemented (length-aware typed reassembly in
`notifier.c`) and all `test_notifier_host` cases pass.

**Actual Behavior**
`README.md` (lines ~540–545) states:
> `test_notifier_host` (64 cases): the four `SET_OS` blocks are currently pending an
> upstream ETX-collision framing fix in `notifier.c` (the `SET_OS` `cmd_id` `0x03`
> collides with the `ETX` terminator during reassembly); the remaining 57 assertions … pass.

This is stale: `./run_notifier_stub_tests.sh` reports `Total tests run: 64 / passed: 64 /
failed: 0` (verified), i.e. **all 64** pass and the fix already ships. A maintainer or user
reading this would incorrectly believe `SET_OS` — the host-authoritative OS seam central to
the v0.3.0 host-rules feature (§4.7) — is non-functional.

**Steps to Reproduce**: `./run_notifier_stub_tests.sh 2>&1 | grep -E 'Total tests|fails='`
→ `test_notifier_host … 64 / passed: 64 / failed: 0`; compare to README lines ~540–545.

**Suggested Fix**: Update the README block to state all 64 `test_notifier_host` cases pass
and that the `SET_OS` `0x03`/ETX collision is resolved by the length-aware typed reassembly.

### Issue 3: Matcher tested at `NFA_MAX_PATTERN=2048` but firmware runs at `128` (test-fidelity gap)
**Severity**: Minor (latent; no active failure with current patterns)
**PRD Reference**: §7.9, §11.1/§11.3, Appendix B (`NFA_MAX_PATTERN = 128`).

**Expected Behavior**
The acceptance gate should exercise the matcher at the same `NFA_MAX_PATTERN` budget the
firmware actually uses, so a pattern that is too long for hardware is caught.

**Actual Behavior**
`pattern_match.c` defaults `NFA_MAX_PATTERN` to **2048**; only `notifier.c` (which
`#define`s it to **128** before `#include "pattern_match.c"`) runs at the firmware budget.
The 9 `test_*pattern*.c` suites compile `pattern_match.c` **directly** (§11.1), so they run
at 2048, while only the `test_notifier_*` stub suites run at 128. A user pattern between 129
and 2048 processed-pattern bytes would pass every matcher test yet be silently clamped on
hardware (`NEW()` reuses the last pool slot, notifier.c ~364) and could match incorrectly.
The compile-time guard in `notifier.c` only checks the *budget* (`NFA_MAX_PATTERN <= 128`),
not that an individual pattern fits. The PRD says patterns are short (§7.9), so this is
latent — but the gate cannot detect a hardware-only regression here.

**Suggested Fix**: Add at least one matcher-corpus suite compiled via the `notifier.c`
path (or with `-DNFA_MAX_PATTERN=128`) so the matcher is gated at the production budget.

### Issue 4: `has_been_queried` is written but never read
**Severity**: Minor (dead state / code smell)
**PRD Reference**: §4.6 ("The firmware therefore sets a `has_been_queried` bool …").

**Expected Behavior / Actual Behavior**: `has_been_queried` is set to `true` on the first
`QUERY_INFO` (notifier.c, `QUERY_INFO` case) but is never read by any code path. The PRD
only requires the firmware to *set* it (the host enforces "at most once per board boot"),
so this is compliant, but the variable is effectively dead. Consider either documenting
that it is intentionally write-only for future host observability, or exposing it (e.g. via
`QUERY_INFO`) so it carries meaning. Negligible functional impact.

---

## Testing Summary

- **Total tests performed**: ~120 hand-written adversarial probes across 12 standalone
  programs, plus the full shipped acceptance gate (9 pattern suites + 3 notifier stub
  suites) and the PRD §11.2 B/C stress probes.
- **Passing**: All shipped-suite assertions (2023 matcher + 14 dispatch + 31 OS + 64 host)
  and ~115 of the ~120 adversarial probes. The PRD §11.2B pathological case ~1.2 ms; §11.2C
  six `1`s. Full §10.2 reference keymap dispatches correctly end-to-end.
- **Failing**: The malformed/truncated typed-command desync (Issue 1, Major) and its
  knock-on legacy-routing breakage; the stale README claim (Issue 2).
- **Areas with good coverage** (shipped + this QA): pattern semantics (anchors, escapes,
  classes, `\b`/`\B`, dot-vs-star, `+` quantifier, case folding); F4 delimiter matrix;
  F8 multi-OS merge/fallback/per-track independence; F9 OS-change clear/idempotence; the
  four typed commands on well-formed single- and multi-report frames; backward
  compatibility (no-macro configs); buffer boundaries (255 OK / 256 overflow); embedded-NUL
  sanitization; coexistence guard; ack path.
- **Areas needing more attention**: **malformed / truncated / abandoned typed commands**
  (Issue 1 — the length-aware reassembly trusts `count` unconditionally and has no
  watchdog); typed-command payloads containing `0x03` in *every* arg position across
  *worst-case* report boundaries (only the happy path is gated today); matcher fidelity at
  the firmware `NFA_MAX_PATTERN=128` budget (Issue 3).