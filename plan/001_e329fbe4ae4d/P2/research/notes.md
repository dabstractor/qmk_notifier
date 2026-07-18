# P2 Research Notes — QMK Firmware Module (notifier.{c,h}, rules.mk)

## 0. Situation (critical framing)

Unlike P1 (where `pattern_match.c` was RESET to stubs and is being rebuilt),
the P2 files **already exist in the working tree as the complete reference
implementation** and are **committed + unmodified**:

```
$ git status                      → notifier.c / notifier.h / rules.mk NOT listed as modified
$ git diff 81df853:notifier.c -- notifier.c  → (empty — identical to reference)
$ git diff 81df853:notifier.h -- notifier.h  → (empty)
$ git diff 81df853:rules.mk   -- rules.mk    → (empty)
```

So P2 is NOT a from-scratch rebuild. It is:
1. **Verify** the three files conform to PRD §5 / §8 / §9.
2. **Fix two documented defects** (BUG-1 latent NULL deref; DRIFT-1 wrong GS comment).
3. **Close RISK-1** (notifier.c has ZERO host-side test coverage) by building a
   **stub-compile + host-link validation harness** — the ONLY sanctioned way to
   validate notifier.c off-device (item description: "validated on-device or via
   stub-compile").

## 1. The stub-compile harness — PROVEN WORKING

Built in `/tmp/notifier_harness` during research. Compiles + links + runs cleanly.
The exact files (reproduce verbatim in the repo):

### 1a. `qmk_stubs/qmk_keyboard_stub.h` (the `#include QMK_KEYBOARD_H` target)
```c
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* Minimal QMK surface consumed by notifier.c */
void layer_on(uint8_t layer);
void layer_off(uint8_t layer);
#ifndef RAW_EPSIZE
#define RAW_EPSIZE 32
#endif
```

### 1b. `qmk_stubs/raw_hid.h` (resolved via `-Iqmk_stubs` for `#include "raw_hid.h"`)
```c
#pragma once
#include <stdint.h>
void raw_hid_send(uint8_t *data, uint8_t length);
```

### 1c. `qmk_stubs/qmk_stubs.c` (observable QMK symbol impls for linking)
```c
#include <stdint.h>
#include <stdio.h>
static uint8_t g_active_layer = 255;
void layer_on(uint8_t layer){ g_active_layer = layer;
  printf("[stub] layer_on(%u) -> active=%u\n", layer, g_active_layer); }
void layer_off(uint8_t layer){ (void)layer; g_active_layer = 255;
  printf("[stub] layer_off -> active=255\n"); }
void raw_hid_send(uint8_t *data, uint8_t length){ (void)length;
  printf("[stub] raw_hid_send response[0]=%u\n", data[0]); }
```

### 1d. Verified compile command (exact)
```bash
gcc -Wall -Wextra -std=c99 \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier.o
# exit 0. Three -Wsign-compare warnings (int vs size_t in loop bounds) — PRE-EXISTING
# in the reference; harmless (map sizes are tiny). Optionally cleaned up.
```
**GOTCHA (resolved):** `-DQMK_KEYBOARD_H='"stubs/qmk_keyboard_stub.h"'` FAILS
(quoted path resolves relative to notifier.c, double-dir). Use a **bare filename**
`-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"'` resolved via `-Iqmk_stubs`. Confirmed.

### 1e. Verified link + run
```bash
gcc -Wall -std=c99 -I. /tmp/notifier.o qmk_stubs/qmk_stubs.c \
    test_notifier_dispatch.c -o /tmp/test_notifier && /tmp/test_notifier
```
All F4.1–F4.4 PASS; dispatcher ordering (disable-before-scan,
deactivate-before-activate, exactly-one-layer) PASS; hid_notify reassembly +
ack PASS; magic-header guard ignores non-matching reports. (Full output below.)

## 2. BUG-1 — latent NULL-deref in match_pattern [CONFIRMED SEGFAULT]

**Location:** `notifier.c:153-156`
```c
bool match_pattern(const char *pattern, const char *message, bool case_sensitive) {
    const char *pattern_delimiter = find_first_delimiter(pattern);  // line 154: DEREFs NULL
    if (message == NULL || pattern == NULL) {                        // line 156: guard TOO LATE
        return false;
    }
```
**Repro:** `match_pattern(NULL, "x", 0)` → **SIGSEGV (exit 139)**, confirmed via the
stub harness. `find_first_delimiter(NULL)` does `for (p=str; *p; ...)` → derefs NULL.

**Contract violated:** PRD §8.5 step 2 ("NULL pattern or message → false"), §12
("NULL inputs → false"), §13 robustness.

**Production impact:** LATENT — `match_pattern` is only called from
`process_full_message` with `received_command` (non-NULL local) and map entry
`.pattern` (non-NULL literals). But it's a defensive-correctness gap and the fix
is a 2-line reorder with **zero behavior change for non-NULL inputs**.

**Fix:** move the NULL guard above `find_first_delimiter(pattern)`:
```c
bool match_pattern(const char *pattern, const char *message, bool case_sensitive) {
    if (message == NULL || pattern == NULL) {
        return false;
    }
    const char *pattern_delimiter = find_first_delimiter(pattern);
    // ... rest unchanged
```
**Verified:** after reorder, `match_pattern(NULL,"x",0)` returns false (exit 0).
All F4 results unchanged (re-ran the harness: identical PASS/FAIL).

## 3. DRIFT-1 — wrong GS comment at 3 locations [CONFIRMED, LOW]

`0x1D` = decimal **29** = ASCII **Group Separator (GS)**. The comment wrongly says
"ASCII 31 (Unit Separator)" (that's `0x1F`/US). PRD §5.3: "the byte value 0x1D
(29 = GS) is authoritative." Byte value is CORRECT; only comments wrong.

```
notifier.h:22 : #define GS_DELIMITER "\x1D"  // ASCII 31 (Unit Separator)
notifier.c:116: // Helper function to find unit delimiters in a string
notifier.c:297: // replace all unit delimeters with |
```
**Fix:** notifier.h:22 → `// ASCII 29 (Group Separator)`; the two notifier.c
comments → "group separator" wording. Cosmetic; no behavior change.

## 4. F4 delimiter-matching matrix — ALL VERIFIED via stub harness

| Case | Pattern | Message | Expected | Got | |
|---|---|---|---|---|---|
| F4.4 | `abc` | `xabcx` | 1 | 1 | ✅ direct |
| F4.2 | `code` | `code\x1dmain.rs` | 1 | 1 | ✅ class-half only |
| F4.2 | `zzz` | `code\x1dmain.rs` | 0 | 0 | ✅ |
| F4.3 | `code\x1dmain` | `code` | 1 | 1 | ✅ pattern class vs whole |
| F4.1 | `*chrome*\x1d*claude*` | `Chrome\x1dClaude - Chat` | 1 | 1 | ✅ AND both halves |
| F4.1 | `*chrome*\x1d*zzz*` | `Chrome\x1dClaude` | 0 | 0 | ✅ right half fails |

## 5. Dispatcher ordering — VERIFIED via stub harness

Traced through `process_full_message` with a DEFINE_SERIAL_COMMANDS/LAYERS driver:
- `"neovide"` → on_enable fires (command only).
- `"Chrome\x1dstuff"` → **on_disable fires first** (prev cmd), then layer_on(5).
- `"firefox\x1dmy github repo"` → layer_off (255) **then** layer_on(7).
- `"totally-unknown"` → layer_off (clears), no enable → returns 0 (unmatched clears state).

Confirms PRD §8.6 ordering: disable-before-scan, deactivate-before-activate,
first-match-wins, exactly-one-notifier-layer, unmatched-clears-state.

## 6. hid_notify reassembly + ack — VERIFIED via stub harness

Fed a real framed report `[0x81][0x9F]["neovide"][0x03]` padded to 32 bytes →
on_enable fires + `raw_hid_send response[0]=1`. Fed `[0xAB][0xCD]...` → ignored
(coexistence guard). Confirms PRD §8.7.

## 7. Sign-compare warnings (pre-existing, OPTIONAL)

`notifier.c:247` (`int length >= sizeof(...)`), `:266` and `:275` (`int i < size_t`).
Harmless (tiny map sizes). Reference has them. Optional fix: `for (size_t i...)`
+ cast. Not required (PRD §17: code wins); flagged for implementer discretion.

## 8. P1 status (cross-phase dependency)

`pattern_match.c` is ~99.7% complete (run_all_tests.sh: 5 tests still failing in
final P1 cleanup, match_with_anchors IS real at line 234). So the stub harness
**validates the full P1→P2 path TODAY** — it transitively exercises pattern_match.c
(via notifier.c's `#include "pattern_match.c"`) and is therefore ALSO a P1
regression detector. Once P1's last 5 tests pass, the harness + the 9 suites are
fully green together.

## 9. Harness artifacts from this research session

`/tmp/notifier_harness/` contains the working prototype (notifier.o, qmk_stubs.c,
test_notifier.c, bug1.c). The PRP reproduces these verbatim as the shipped harness.
