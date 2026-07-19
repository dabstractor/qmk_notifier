# Research Notes — P1.M2.T2.S1: Add embedded-NUL sanitization test to test_notifier_dispatch.c

## The bug under test (Issue 2)

`sanitize_string` used `while (*read_ptr)`, truncating the scan at the first
`0x00` instead of stripping it (PRD F2.3 / §8.2 says "strip every other byte",
and `0x00` is "every other byte"). The parallel P1.M2.T1.S1 fixes it: signature
becomes `sanitize_string(char *str, size_t len)` with a length-bounded
`for (size_t i = 0; i < len; i++)` loop; the sole caller (`hid_notify`) passes
`sanitize_string(msg_buffer, (size_t)msg_index)`.

**The fix is ALREADY LANDED** in the current `notifier.c` (verified during
research): `sanitize_string` at notifier.c:50 has the `size_t len` param + index
loop; the call site at notifier.c:502 is `sanitize_string(msg_buffer,
(size_t)msg_index)`; the redundant `msg_buffer[msg_index] = '\0'` is gone. So
this task's test validates the already-fixed code (treat the fixed notifier.c as
the baseline contract).

## THE critical data-flow detail — the magic header is stripped BEFORE reassembly

`hid_notify` (notifier.c) does, in order:
1. Coexistence guard: `if (length < 2 || data[0] != 0x81 || data[1] != 0x9F) return;`
2. **`data += 2; length -= 2;`** — strips the 2-byte magic header.
3. Reassembly loop: appends each payload byte to `msg_buffer[msg_index++]` until
   `ETX (0x03)`.
4. At ETX (and not `dropping`): `sanitize_string(msg_buffer, (size_t)msg_index);`
   then `process_full_message(msg_buffer);`

**Consequence for the test:** the magic header `0x81 0x9F` is NEVER placed in
`msg_buffer` and is NEVER subject to sanitize. (Good — they're >0x7E, so the
allowlist WOULD strip them, but they never reach it.) The report layout for the
test is therefore:

```
rep[0]=0x81 rep[1]=0x9F            <- magic header (consumed by hid_notify)
rep[2]='h'..'o' (5 bytes)          <- payload prefix "hello"
rep[7]=0x00                        <- embedded NUL (must be STRIPPED)
rep[8]='w'..'d' (5 bytes)          <- payload suffix "world"
rep[13]=0x03                       <- ETX (terminates reassembly)
rep[14..31]=0x00                   <- zero-pad to 32 (never read; loop breaks at ETX)
```

After reassembly: `msg_buffer = "hello\0world"`, `msg_index = 11`. After the
fixed sanitize: `msg_buffer = "helloworld"` (NUL stripped) + NUL terminator.
`process_full_message("helloworld")` then scans the command map.

(If the OLD truncating sanitize were still present: `msg_buffer = "hello"` —
"world" lost. The test asserts against THIS to discriminate.)

## NULL-callback safety (verified) — map row may use on_disable = 0

- `enable_command` (notifier.c:195) guards `if (command->on_enable != NULL)`.
- `disable_command` (notifier.c:208) guards `if (current_command != NULL &&
  current_command->on_disable != NULL)`.

So a map row `{ "*world*", on_en_nul, 0 }` with `on_disable = NULL` is SAFE —
the NULL on_disable is never dereferenced. (The research smoke test in
P1.M2.T1.S1's PRP used the same `{ "*suffix*", on_en, 0, false }` idiom.)

## DEFINE_SERIAL_COMMANDS + command_map_t shape (verified)

```c
typedef struct {
    const char *pattern;
    callback_t on_enable;
    callback_t on_disable;
    const bool case_sensitive;   // LAST field, OPTIONAL (omits -> false)
} command_map_t;
```
- The existing test_notifier_dispatch.c uses 3-field rows `{ "neovide",
  on_en_cmd, on_dis_cmd }` (case_sensitive defaults false). Under the runner's
  `-Wall` (NOT -Wextra), 3-field rows in a 4-field struct do NOT warn
  (`-Wmissing-field-initializers` is gated on -Wextra). So my new 3-field row
  `{ "*world*", on_en_nul, 0 }` matches the existing style and is -Wall-clean.
- DEFINE_SERIAL_COMMANDS is at FILE SCOPE. So the new callback + flag must be
  DECLARED BEFORE the macro (the macro references them by name). The new map
  entry is appended as a 3rd row to the existing block.

## The test design (validated 14/14 — see prototype below)

Three additions to test_notifier_dispatch.c:

1. **A dedicated callback + flag** (declared before DEFINE_SERIAL_COMMANDS, after
   the existing on_dis_cmd):
   ```c
   static int nul_cmd_fired = 0;
   static void on_en_nul(void){ fprintf(stderr,"  -> on_enable (nul-test) fired\n"); nul_cmd_fired++; }
   ```

2. **A map entry** (3rd row in DEFINE_SERIAL_COMMANDS): `{ "*world*", on_en_nul, 0 }`
   — matches the dispatched message ONLY if the post-NUL "world" survived
   (NUL stripped). First-match-wins: "neovide" and WT("*chrome*","*claude*") do
   not match "helloworld", so "*world*" is the one that fires.

3. **A test block in main()** (before the summary printf):
   - Reset `nul_cmd_fired = 0`.
   - Build the 32-byte report (magic + "hello\0world" + ETX + zero-pad).
   - `hid_notify(rep, 32);`
   - Assert `nul_cmd_fired == 1` → g_pass++/"PASS: embedded NUL stripped …" else
     g_fail++/"FAIL: embedded NUL NOT stripped …".
   - Two `ck()` discrimination calls proving "*world*" matches "helloworld"
     (stripped) but NOT "hello" (truncated) — locks the semantics at the match
     layer (item-spec 3e negative control).

### Why "*world*" discriminates (item 3e)
- Stripped message = "helloworld" → "*world*" matches (fired=1). ✓
- Truncated message = "hello" → "*world*" does NOT match (fired=0). ✗
So `nul_cmd_fired == 1` conclusively means the NUL was STRIPPED, not truncated.
The two ck() calls make this discrimination explicit and regression-proof.

### First-match-wins + disable-before-scan accounting
- The existing test's last dispatch before this block is m3="totally-unknown"
  (unmatched → disable_command clears the prior command; no new match). So
  entering the block, `current_command` is disabled.
- This block's dispatch: disable_before_scan fires the prior on_disable (NOT
  on_en_nul — different callback), then scans → "*world*" matches → on_en_nul
  fires exactly once. `nul_cmd_fired` goes 0→1. Clean. (Reset to 0 right before
  the dispatch makes the assertion unambiguous regardless of prior state.)

## PROTOTYPE — validated 14/14, 0 FAIL, 0 warnings (exact runner flags)

Applied the three additions to a /tmp copy of test_notifier_dispatch.c, built
with the exact run_notifier_stub_tests.sh flags, ran it:

```
PASS: match_pattern("abc","xabcx",cs=0)=1
... (existing 11 PASS lines) ...
PASS: dispatcher ordering (disable/deactivate/first-match/clear)
PASS: embedded NUL stripped — post-NUL "world" survived to dispatch (F2.3)
PASS: match_pattern("*world*","helloworld",cs=0)=1
PASS: match_pattern("*world*","hello",cs=0)=0

Total tests run: 14 / passed: 14 / failed: 0
exit=0  fails=0
```

Build (object-compile notifier.c with -Wall -Wextra; link with -Wall):
```
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c -o /tmp/notifier_stub.o   # 0 warnings
gcc -Wall -std=c99 -Iqmk_stubs -I. /tmp/notifier_stub.o qmk_stubs/qmk_stubs.c /tmp/td_proto.c -o /tmp/td_proto               # 0 warnings
```

## Test count delta

Existing: 11 PASS (6 ck + 2 NULL-robustness + 1 hid_notify reassembly + 1
coexistence guard + 1 dispatcher ordering). Additions: +1 callback assertion +
2 ck() discrimination = **3 new → total 14**. Summary prints "Total tests run:
14 / passed: 14 / failed: 0". `grep -c '^FAIL:'` = 0.

## Scope / boundaries

- This task edits ONLY `test_notifier_dispatch.c` (adds callback+flag, map row,
  test block). No other file.
- It CONSUMES the fixed `notifier.c` (parallel P1.M2.T1.S1, already landed) —
  does NOT touch notifier.c.
- It does NOT touch the runner (run_notifier_stub_tests.sh already builds+runs
  test_notifier_dispatch; the new cases auto-increment the count — no script
  change, per test_infrastructure.md "Adding tests to existing files ... no
  script changes needed").
- It does NOT touch qmk_stubs/*, test_notifier_os.c, pattern_match.*, notifier.h,
  PRD.md, tasks.json, rules.mk, .gitignore.
- No conflict with P1.M1.T1.S1 (test_pattern_match.c — different file).

## Item-spec clarifications / resolutions

- Item 3a pattern "*world*" (case-insensitive): used verbatim as the map entry.
  Case-insensitive is the default (case_sensitive omitted → false). "helloworld"
  is lowercase so it matches regardless.
- Item 3b report layout: used exactly as specified (magic + "hello" + 0x00 +
  "world" + ETX), zero-padded to 32 bytes. The 0x00 is at rep[7] (after the
  2-byte magic + 5-byte "hello"). VERIFIED the index math: rep[0..1] magic,
  rep[2..6] "hello", rep[7] NUL, rep[8..12] "world", rep[13] ETX.
- Item 3e negative control: resolved via the "Or alternatively" branch — the
  two ck() calls prove "*world*" discriminates stripped ("helloworld", match)
  from truncated ("hello", no-match). (The literal "hello" prefix interpretation
  in the item is a red herring — "hello" WOULD match "helloworld" as a prefix,
  so it is not a useful negative control on its own; the discrimination is in
  whether the POST-NUL portion is present, which "*world*" captures.)