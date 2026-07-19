# Research Notes — P1.M3.T1.S1 (CONSOLE_ENABLE layer-track print + print.h stub)

## What this task is
Close PRD §8.6 step 9 "print per-track match/miss" gap (findings Issue 3): the
`#ifdef CONSOLE_ENABLE` block at the end of `process_full_message` (notifier.c)
currently prints ONLY the command track. Add the layer-track match/miss print,
mirroring the command-track format, and provide a host `print.h` stub so the
CONSOLE_ENABLE code path can be compile-checked on a host.

## Key files / anchors (CURRENT repo state — P1.M2.T1.S1 already landed)

### notifier.c CONSOLE_ENABLE block (the ONLY notifier.c edit site)
Located inside `process_full_message` (def at notifier.c:341). Current text
(line numbers are APPROXIMATE because P1.M2.T1.S1 shifted sanitize_string by
~7 lines vs the findings doc's "417-434" reference — ANCHOR ON TEXT, NOT LINES):

```c
    #ifdef CONSOLE_ENABLE
    // replace all group separators (GS) with '|' for console readability
    for (size_t i = 0; i < strlen(received_command); i++) {
        if (received_command[i] == GS_DELIMITER[0]) {
            received_command[i] = '|';
        }
    }

    /* DEBUG (step 9): print per-track match/miss. ... */
    if (command_found != NULL) {
        uprintf("Matched message %s on command: %s\n", received_command, command_found->pattern);
    } else {
        uprintf("Did not match message %s on any command\n", received_command);
    }
    #endif
```

**INSERT POINT**: between the command-track `}` (closing the else) and the
`#endif`. i.e. AFTER `uprintf("Did not match message %s on any command\n",
received_command);\n    }` and BEFORE `    #endif`.

### notifier.c uprintf declaration (unchanged — already correct)
notifier.c:20-21:
```c
#ifdef CONSOLE_ENABLE
#include "print.h"
#endif
```
`print.h` is resolved via `-Iqmk_stubs`. Today NO such file exists → a
`-DCONSOLE_ENABLE` build fails at the `#include "print.h"`. This task creates it.

### Variables in scope at the insert point (all already set, no new work)
- `received_command[256]` — local buffer, GS already substituted to '|' by the
  loop above (so the print shows GS as '|', per §8.6 step 9).
- `command_found` — `command_map_t *`, NULL if no command matched.
- `layer_found` — `uint8_t`, `LAYER_UNSET` (255, notifier.c:113) if no layer
  matched, else the resolved layer index. **This is what we print.**
- `GS_DELIMITER` = `"\x1D"` (notifier.h:27).

### ALL uprintf call sites in notifier.c (print.h stub must satisfy all 5)
```
178:    uprintf("Activating layer %d\n", layer);
189:    uprintf("Deactivating layer %d\n", activated_layer);
437:    uprintf("Matched message %s on command: %s\n", received_command, command_found->pattern);
439:    uprintf("Did not match message %s on any command\n", received_command);
469:    uprintf("notifier: OS %u -> %u; clearing state\n", (unsigned)current_os, (unsigned)os);
+ 2 NEW layer-track calls this task adds.
```
All are printf-style. `#define uprintf(...) printf(__VA_ARGS__)` covers all.

## No conflict with the parallel task (P1.M2.T2.S1)
P1.M2.T2.S1 modifies ONLY `test_notifier_dispatch.c` (adds a callback + map row
+ test block). It does NOT touch notifier.c, print.h, qmk_stubs, or
CONSOLE_ENABLE. → zero file overlap, safe to run in parallel. (Verified by
reading the P1.M2.T2S1 PRP "What"/"Deliverable" and grepping it for
print.h/CONSOLE_ENABLE/uprintf — no hits.)

## EMPIRICAL VALIDATION (prototype in /tmp, current notifier.c)
Applied the exact contract change to a /tmp copy and ran both builds:

1. **Normal build (NO CONSOLE_ENABLE)** — replicates run_notifier_stub_tests.sh:
   ```
   gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
       -Iqmk_stubs -I. -c notifier.c -o /tmp/proto_normal.o
   → clean, exit 0, 0 warnings
   ```

2. **Contract compile-check (WITH -DCONSOLE_ENABLE)**:
   ```
   gcc -Wall -Wextra -std=c99 -DCONSOLE_ENABLE \
       -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
       -Iqmk_stubs -I. -c notifier.c -o /tmp/notifier_console_check.o
   → clean, exit 0, 0 warnings
   ```

3. **Baseline gate** (current repo, pre-change):
   `./run_notifier_stub_tests.sh` → 31/31 PASS, 0 FAIL, both binaries exit 0,
   `✓ notifier stub-compile gate PASSED`.

## print.h stub design (exact)
```c
#pragma once
#include <stdio.h>
#define uprintf(...) printf(__VA_ARGS__)
```
- `#pragma once` matches every other qmk_stubs header (os_detection.h,
  qmk_keyboard_stub.h, raw_hid.h all use `#pragma once`).
- `printf` is variadic → the `__VA_ARGS__` macro forwards all args. Works for
  every existing call site (see list above).
- This file is ONLY pulled in when building with `-DCONSOLE_ENABLE` (notifier.c:20
  guard). Normal builds never see it. No collision with any real QMK header
  (qmk_keyboard_stub.h / raw_hid.h / os_detection.h do not declare uprintf).

## Why print just the layer INDEX, not a layer_map_t* pattern
(findings Issue 3 "Fix approach"): the command track prints `command_found->pattern`
because `command_found` is a `command_map_t*` already in scope. The layer track
has only `layer_found` (a `uint8_t` index) in scope — there is no
`layer_map_t *layer_entry` pointer. Storing one would require touching the two
layer-scan loops (notifier.c ~395-410) to capture the matched entry address,
which is a dispatch-logic change not warranted for a Minor debug-only issue.
The index is sufficient for debugging. Contract explicitly approves this.

## Risk assessment (from findings doc, confirmed)
- **LOW (code change):** debug prints guarded by `#ifdef CONSOLE_ENABLE`, which
  is OFF in every automated test gate → zero production behavior change.
- **LOW (print.h stub):** maps uprintf→printf, standard host-build shim. Only
  included under -DCONSOLE_ENABLE. No header collisions (verified).
- **GATING:** the committed test gate (run_notifier_stub_tests.sh) does NOT pass
  -DCONSOLE_ENABLE, so the CONSOLE path is validated by a ONE-OFF manual command
  (documented in the PRP Validation Loop), not a committed script. This is the
  contract's explicit design (OUTPUT §4: "No behavior change in production;
  CONSOLE_ENABLE is off in all automated test gates").