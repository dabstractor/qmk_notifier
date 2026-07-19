# Findings & Risks — Bugfix 001_4a8e381fbf10

## Issue 1: Regression Test for @-Literal Semantics

### Finding
The PRD §11.2C gate was using `"user_host"` where it meant `"user@host"`. The
PRD.md was **already corrected** at commit `4d49460`. The matcher code
(`pattern_match.c`) is correct and must not be changed.

**Verified gap:** No test in `test_pattern_match.c` exercises the combined
`^\w+@\w+$` semantics. Confirmed by grep: `grep -rn 'w+@\|user_host\|user@host' test_*.c` → 0 hits.

### Correct semantics to lock in
```
pattern_match("^\\w+@\\w+$", "user@host",  1) == 1   // @ is literal, present → match
pattern_match("^\\w+@\\w+$", "user_host",  1) == 0   // no @, underscore is \w → no match
pattern_match("^\\w+_\\w+$", "user_host",  1) == 1   // literal _ between \w+ groups
```

### Test pattern to follow
`test_pattern_match.c` uses a data-driven `test_case_t` struct + `run_test()`
helper. Add a new `test_case_t[]` array and dispatch it from
`run_pattern_match_tests()`. No changes to `run_all_tests.sh` needed — it
auto-recompiles and auto-increments the count.

### Risk
- **LOW:** Adding test cases cannot break existing functionality.
- **Regression protection:** The `user_host → false` case is the critical one.
  Without it, a future developer could "fix" the matcher to make `^\w+@\w+$`
  match `user_host`, breaking literal-`@` matching.

---

## Issue 2: sanitize_string NUL Stripping Fix

### Finding
`sanitize_string` (notifier.c:46-69) uses `while (*read_ptr)` which terminates
at the first NUL byte. The allowlist correctly excludes `0x00`, but the loop
never reaches it — it truncates instead of stripping.

### Fix approach
Change signature from `static void sanitize_string(char *str)` to
`static void sanitize_string(char *str, size_t len)`:
- Replace `while (*read_ptr)` with `for (size_t i = 0; i < len; i++)`
- Access bytes as `str[i]` or equivalently `*read_ptr` with explicit advancement
- NUL bytes (not in allowlist) are skipped → stripped, not truncated
- NUL-terminate at `write_ptr` at the end (unchanged)

### Call site change (notifier.c:495)
Current:
```c
msg_buffer[msg_index] = '\0';
sanitize_string(msg_buffer);
```
After fix:
```c
sanitize_string(msg_buffer, (size_t)msg_index);
msg_buffer[msg_index] = '\0';  // still needed: sanitize may shrink the string
```
Wait — after sanitize, `write_ptr` NUL-terminates. But `msg_index` bytes were
written to the buffer originally, and some may have been stripped. The
NUL-terminator from sanitize's `*write_ptr = '\0'` is at the correct position
(write_ptr ≤ read_ptr ≤ str+len). The pre-sanitize `msg_buffer[msg_index] = '\0'`
can be removed since sanitize now handles termination. However, for safety,
keep it — sanitize will NUL-terminate at write_ptr which is ≤ msg_index, so
the explicit terminator at msg_index is redundant but harmless.

Actually, the cleanest approach: call `sanitize_string(msg_buffer, (size_t)msg_index)`
and let it NUL-terminate. The `msg_buffer[msg_index] = '\0'` line can be removed
since sanitize handles it. But keeping it is also safe (it just sets a byte that
sanitize may have already set to NUL or to a valid byte that was moved).

**Recommendation:** Move `sanitize_string(msg_buffer, (size_t)msg_index)` BEFORE
the `msg_buffer[msg_index] = '\0'` line, and let sanitize NUL-terminate. Then
remove the explicit `msg_buffer[msg_index] = '\0'`. OR: keep the current order
but pass msg_index. Both work; the key change is passing the length.

### Test approach
`sanitize_string` is `static` — not directly callable from test files. Test via
`hid_notify`:
1. Build a HID report with magic header `0x81 0x9F`, payload bytes including an
   embedded `0x00`, terminated by ETX `0x03`.
2. Call `hid_notify(report, len)`.
3. Define a command map pattern that would match the post-NUL portion.
4. If the NUL was stripped (correct), the pattern matches and the callback fires.
5. If the NUL truncated (bug), the pattern doesn't match.

Use the existing `ck()` helper pattern from `test_notifier_dispatch.c`.

### Risks
- **MEDIUM — spec drift:** PRD §8.2 documents the signature as
  `sanitize_string(char *str)`. The fix changes it to take a length parameter.
  Since it's `static` with one caller, this is contained. Document the drift
  here and in system_context.md.
- **LOW — call site safety:** `msg_index` is always valid (0 to MSG_BUFFER_SIZE-1)
  at the sanitize call site because the overflow path resets `msg_index` and
  enters `dropping` mode (skips sanitize).

---

## Issue 3: CONSOLE_ENABLE Layer Track Debug Print

### Finding
The `#ifdef CONSOLE_ENABLE` block at notifier.c:417-434 prints only the command
track. The layer track match/miss is not printed. The PRD §8.6 step 9 says
"print per-track match/miss."

### Variables available in scope (process_full_message)
- `received_command[256]` — local buffer, already GS→`|` substituted (line 418-423)
- `command_found` — `command_map_t *` (NULL if no command matched)
- `layer_found` — `uint8_t` (LAYER_UNSET=255 if no layer matched, else the layer index)

### Fix approach
After the existing command-track print (line 433), add:
```c
if (layer_found != LAYER_UNSET) {
    uprintf("Matched message %s on layer: %d\n", received_command, layer_found);
} else {
    uprintf("Did not match message %s on any layer\n", received_command);
}
```

This mirrors the command track output format. The layer index is printed
(sufficient for debugging; the matched pattern would require storing a
`layer_map_t *` pointer, which is a larger change not warranted for a Minor
debug issue).

### Testing challenge
`uprintf` requires QMK's `print.h` (notifier.c:20-21: `#ifdef CONSOLE_ENABLE`
+ `#include "print.h"`). The stub harness doesn't provide it. To compile-check
with `-DCONSOLE_ENABLE`:
1. Create `qmk_stubs/print.h` with a minimal `uprintf` macro wrapping `printf`:
   ```c
   #pragma once
   #include <stdio.h>
   #define uprintf(...) printf(__VA_ARGS__)
   ```
2. Build: `gcc -DCONSOLE_ENABLE -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c`
3. Verify clean compilation (no new warnings).

Output verification is manual/debug-only — the print statements are guarded
by CONSOLE_ENABLE and not part of the automated test gates.

### Risk
- **LOW:** Adding debug print statements guarded by `#ifdef CONSOLE_ENABLE`
  cannot affect production behavior (CONSOLE_ENABLE is off in test builds).
- **LOW — print.h stub:** The stub must not conflict with any QMK header
  patterns. Mapping `uprintf` to `printf` is standard for host builds.