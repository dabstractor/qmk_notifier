# Research Notes — P1.M2.T1.S1 (sanitize_string NUL-stripping fix)

## Task scope
A surgical 2-region fix to `notifier.c`:
1. Change `sanitize_string` signature `static void sanitize_string(char *str)`
   → `static void sanitize_string(char *str, size_t len)` and replace the
   `while (*read_ptr)` loop with a length-bounded `for (size_t i = 0; i < len; i++)`
   so an embedded NUL (0x00) is **stripped** (not in allowlist) instead of
   **truncating** the scan. (PRD F2.3 / §8.2.)
2. Update the sole call site in `hid_notify` to pass `(size_t)msg_index` and drop
   the now-redundant pre-sanitize `msg_buffer[msg_index] = '\0'` (sanitize owns
   NUL-termination).

The formal embedded-NUL test (P1.M2.T2.S1 → test_notifier_dispatch.c) is the
NEXT subtask. This task is the CODE FIX + prove-no-regression only.

## Ground-truth locations (verified against current notifier.c, 523 lines)
- `sanitize_string` definition: **notifier.c:46–69** (signature line 46;
  `while (*read_ptr)` at line 52; allowlist lines 55–60; `*write_ptr = '\0'`
  at line 68; NULL guard `if (!str) return;` at line 47).
- **Sole caller**: `hid_notify` at **notifier.c:495** (`sanitize_string(msg_buffer)`),
  inside the `if (!dropping)` branch of the ETX handler. The line above (494)
  is `msg_buffer[msg_index] = '\0';`. At this point `msg_index` is the count of
  valid payload bytes written (the overflow path resets msg_index + sets
  `dropping`, skipping sanitize entirely — so msg_index is always valid here).
- `sanitize_string` is **NOT in any header** (grep: 0 hits in notifier.h,
  pattern_match.h, qmk_stubs/*.h) — confirmed `static` / file-local. Signature
  change is fully contained; no header edit.

## The bug (findings_and_risks.md §Issue 2)
`while (*read_ptr)` stops at the first NUL byte. The allowlist correctly EXCLUDES
0x00, but the loop never reaches it → truncation, not stripping. Unreachable via
the spec'd transport (ETX appended before zero-fill; desktop sends printable
ASCII + GS), but a theoretical robustness gap. PRD F2.3 / §8.2 intent: "strip
every other byte" — 0x00 is "every other byte."

## Spec drift (documented, contained)
PRD §8.2 documents the signature as `sanitize_string(char *str)`. The fix changes
it to take `size_t len`. Because the function is `static` with exactly ONE caller
(not public API, not in any header), the drift is contained to notifier.c. It is
documented in architecture/findings_and_risks.md §Issue 2 (Risk: MEDIUM — spec
drift). No user-facing/config/header surface changes (item-spec §5).

## Exact fix validated end-to-end (PASSED)
Applied both edits to a /tmp copy of notifier.c via a python surgical replace,
then:
1. **Stub-compile** (`-Wall -Wextra -std=c99 -DQMK_KEYBOARD_H=… -Iqmk_stubs -I.`):
   **rc=0, 0 warnings.**
2. **Embedded-NUL probe** (fed `hid_notify` a report `0x81 0x9F 'p''r''e' 0x00
   's''u''f''f''i''x' 0x03` with a command map `{ "*suffix*" }`):
   - FIXED notifier.c → `suffix_fired=1`, rc=0 (**NUL STRIPPED** ✓).
   - ORIGINAL notifier.c → `suffix_fired=0`, rc=1 (**NUL TRUNCATED** — bug
     confirmed; fix resolves it).
3. **Empty-payload edge** (msg_index==0 → `sanitize(buf, 0)`): 0-iteration loop,
   write_ptr==str, `*write_ptr='\0'` → safe NUL-termination, no crash (rc=0).
4. **No regression** — both stub suites against the FIXED notifier.c:
   `test_notifier_dispatch` **11/11, 0 FAIL**; `test_notifier_os` **31/31,
   0 FAIL**; `run_notifier_stub_tests.sh` → "✓ notifier stub-compile gate PASSED".

## Diff is surgical (verified: exactly 2 regions)
- Region 1: `sanitize_string` (lines 46–69) — signature + loop rewritten;
  allowlist condition, NULL guard, NUL-termination byte-for-byte preserved.
- Region 2: `hid_notify` call site (lines 492–495) — `sanitize_string(msg_buffer)`
  → `sanitize_string(msg_buffer, (size_t)msg_index)`; redundant
  `msg_buffer[msg_index] = '\0'` removed.
- No other region of notifier.c changed (diff confirmed).

## Key implementation decisions
- **Drop the `read_ptr` variable** — read via `str[i]` directly inside the
  `for` loop. The contract (§3b) explicitly permits either (`read_ptr` advancing
  OR `str[i]`). `str[i]` is cleaner (one fewer pointer) and the index IS the
  loop variable. `write_ptr` is KEPT (in-place compaction needs a separate write
  cursor; it advances only on allowlist pass).
- **Remove the pre-sanitize `msg_buffer[msg_index] = '\0'`** — sanitize_string
  now owns NUL-termination (`*write_ptr = '\0'`, where write_ptr ≤ str + len).
  Keeping it is harmless (redundant — sanitize's NUL at write_ptr ≤ msg_index is
  the one strlen finds), but removing it makes ownership clear. The contract
  recommends removal.
- **len==0 is safe without a special-case guard** — 0 iterations → write_ptr
  unchanged (==str) → `*write_ptr = '\0'` NUL-terminates at str[0]. Verified
  empirically. The contract (§3d) permits letting the loop handle it.
- **Keep allowlist EXACTLY as-is** — the fix is the LOOP BOUND, not the filter.
  Do not touch the 0x20–0x7E / 9 / 10 / 13 / GS(0x1D) / ETX(0x03) condition.

## Files touched / NOT touched
- TOUCH: `notifier.c` (2 regions only).
- DO NOT TOUCH: notifier.h, pattern_match.{c,h}, qmk_stubs/*, test_notifier_*,
  run_*.sh, PRD.md, tasks.json, prd_snapshot.md, rules.mk, .gitignore.
- The embedded-NUL regression TEST is P1.M2.T2.S1 (next subtask, adds to
  test_notifier_dispatch.c) — NOT this task. This task proves no-regression only.