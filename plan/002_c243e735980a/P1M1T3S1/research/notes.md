# Research Notes — P1.M1.T3.S1

**Item**: Per-OS weak accessors + `select_command_map_os`/`select_layer_map_os` selectors
**Scope**: Add the multi-OS map-selection core to `notifier.c` (the `current_os`
global, 16 per-OS weak accessor functions, and the 2 `static` selector helpers).
This is the **provider + dispatcher** half of §8.3; the **consumer** half
(`notifier_set_os` impl + `process_full_message` OS-first/default-fallback scan)
is the NEXT subtask **P1.M1.T3.S2**. Tests land in **P1.M2.T1.S1**.

---

## 1. What already exists in the tree (contract baseline)

| Dependency | Status | Source |
|---|---|---|
| `notifier.h` — `#include "os_detection.h"`, `void notifier_set_os(os_variant_t os);` decl, `DEFINE_SERIAL_COMMANDS_OS`/`DEFINE_SERIAL_LAYERS_OS` macros (##os token-paste) | ✅ LANDED | P1.M1.T1.S1 |
| `qmk_stubs/os_detection.h` — minimal `os_variant_t` enum stub (`OS_UNSURE=0..OS_IOS=4`) | ✅ LANDED (in tree now) | P1.M1.T2.S1 |
| `notifier.c` default weak accessors (`get_command_map`/`get_layer_map` + `_size`) returning `empty_command_map`/`empty_layer_map` | ✅ exists (lines 84–104) | pre-existing |

→ **This task is purely additive to `notifier.c`.** It depends only on the enum
type (`os_variant_t`) and the macro-generated symbol names — both already
present. No header changes, no new files.

## 2. The symbol contract (load-bearing — verified empirically)

`DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {…})` (in `notifier.h`, lines 60–73) expands
via `##os` token-paste to:

```
_notifier_command_map_OS_MACOS[]            (the array)
_notifier_command_map_OS_MACOS_size         (size const)
_notifier_get_command_map_OS_MACOS()        (map accessor)   ← STRONG, overrides weak
_notifier_get_command_map_OS_MACOS_size()   (size accessor)  ← STRONG, overrides weak
```

`notifier.c` (THIS task) provides the matching **WEAK** defaults with the EXACT
same names, each returning `{NULL, 0}`. At link time a keymap's strong definition
overrides the weak one; an un-overridden OS keeps the weak `{NULL, 0}`.
`select_*_map_os()` reference these names by `case OS_X:` → accessor-call. A typo
in any name = link failure (caught by the Level-2 override test).

Verified names (from PRD §8.3, reproduced verbatim in the PRP):
command: `_notifier_get_command_map_OS_{LINUX,WINDOWS,MACOS,IOS}` + `_size`
layer:   `_notifier_get_layer_map_OS_{LINUX,WINDOWS,MACOS,IOS}` + `_size`
(16 functions total: 4 OSes × 2 tracks × {map, size}.)

## 3. Placement decision (additive, least-disruption)

Current `notifier.c` order around the insertion site:
```
84  // Default empty map ...
85  static command_map_t empty_command_map[1] = {0};
86  static layer_map_t   empty_layer_map[1]   = {0};
88–104  __attribute__((weak)) get_command_map/get_layer_map + _size  (4 fns)
106 #define LAYER_UNSET 255
107 uint8_t activated_layer = LAYER_UNSET;
108 // reference to currently active command:
109 command_map_t *current_command = {0};
110 (blank)
111 void activate_layer(uint8_t layer) { ...        ← first state-machine fn
```

**Decision** (satisfies item constraints, keeps all NEW code in one contiguous
region, moves nothing existing):
- Insert `os_variant_t current_os = OS_UNSURE;` **immediately after**
  `command_map_t *current_command = {0};` (line 109) — item (a): "near the other
  globals (after activated_layer/current_command)".
- Insert the 16 per-OS weak accessors + the 2 `static` selectors **immediately
  after the `current_os` global** and **before `void activate_layer(...)`**.
  This is "after the existing default weak-accessor block" (ends line 104) AND
  "before the activate/deactivate/enable/disable functions" (start line 111) —
  both item constraints hold; the new block is contiguous and self-documenting.

Resulting region:
```
... default weak accessors (unchanged) ...
LAYER_UNSET / activated_layer / current_command   (unchanged globals)
os_variant_t current_os = OS_UNSURE;              ← NEW (a)
/* per-OS weak accessors + selector block comment */ ← NEW (docs)
16 __attribute__((weak)) _notifier_*_map_OS_*     ← NEW (b)
static void select_command_map_os(...)            ← NEW (c)
static void select_layer_map_os(...)              ← NEW (d)
void activate_layer(...) { ...                    (unchanged)
```

## 4. Backward-compatibility guarantee (structurally proven, not a special case)

When no `DEFINE_*_OS` macros exist, all 16 per-OS accessors return `{NULL, 0}`;
`select_*_map_os()` returns `{NULL, 0}` for every OS (incl. the boot state
`OS_UNSURE`). The OS-first scan loop (S2) runs **0 iterations** (size 0) and
falls through to the default-map scan — **identical machine behavior** to today.
No `#ifdef` needed; the zero-size loop IS the guarantee (invariant 19, F8.7).
Verified: Test B — `test_notifier_dispatch` (no `_OS` macros, `current_os`
stays `OS_UNSURE`) still passes **11/11** against the modified `notifier.c`.

## 5. Sequencing gotchas (document as EXPECTED)

- **`-Wunused-function` for the 2 static selectors in THIS subtask.**
  `select_command_map_os`/`select_layer_map_os` are `static` and are not yet
  called (S2's `process_full_message` is the only caller). gcc `-Wall -Wextra`
  emits exactly 2 `-Wunused-function` warnings. The build is NOT `-Werror`
  (PRD §11.1 uses plain `-Wall`, no `-Werror`), so this is acceptable and clears
  when S2 lands. Verified: Test A produces exactly these 2 warnings and the
  object builds fine. Document this in the PRP so the implementer doesn't "fix"
  it by deleting the selectors or adding `__attribute__((unused))`.

- **`run_notifier_stub_tests.sh` [2/3] link step lacks `-Iqmk_stubs`.**
  This is a PRE-EXISTING issue (documented in P1.M1.T2.S1's PRP), NOT caused by
  this task: `test_notifier_dispatch.c` does `#include "notifier.h"` →
  `os_detection.h`, and the runner's link step (`-I.` only) can't find the stub.
  P1.M2.T2.S1 fixes the runner. For THIS task, validate with the **corrected-flag
  harness** (`-Iqmk_stubs` on both steps) — mirrors the post-fix runner. The
  compile step `[1/3]` already has `-Iqmk_stubs` and works.

## 6. Override mechanics (why two TUs are needed to test dispatch)

The per-OS accessors are **non-static** (external linkage, `__attribute__((weak))`)
so a keymap TU's strong `DEFINE_*_OS` definition overrides them at link time. The
`select_*_map_os()` helpers are **static** (internal to `notifier.c`'s TU) — they
are NOT callable from another TU, and they are intentionally NOT declared in
`notifier.h` (internal linkage contract, §5.5).

Testing dispatch end-to-end therefore requires TWO translation units:
- TU1 = `notifier.c` (weak accessors + static selectors + a test `main`)
- TU2 = keymap (strong `_notifier_get_command_map_OS_MACOS` etc.)
Link them: the static selector's call to `_notifier_get_command_map_OS_MACOS()`
binds to the **strong** (keymap) symbol at runtime. Verified: Test C —
`SELTEST ALL=1` (override for MACOS-command + LINUX-layer; all other OSes +
`OS_UNSURE` resolve `{NULL,0}`; no duplicate-symbol errors). This is exactly how
the real `test_notifier_os.c` (P1.M2.T1.S1) will exercise it via
`process_full_message`.

## 7. Empirical validation results (all PASS — see `/tmp/p1m1t3s1_val/`)

| Test | What | Result |
|---|---|---|
| A | `gcc -Wall -Wextra -std=c99 ... -c notifier_mod.c` (stub harness) | ✅ builds; exactly 2 `-Wunused-function` warnings (expected, §5) |
| B | link `test_notifier_dispatch` → run | ✅ **11/11 passed, 0 FAIL** (backward-compat) |
| C | selectors + override across 2 TUs (`SELTEST` main) | ✅ `SELTEST ALL=1`, exit 0 |
| C2 | direct accessor probe (override vs weak) | ✅ MACOS `size=1 pat=iTerm`; LINUX `size=0 ptr=(nil)` |
| — | duplicate-symbol check (weak+strong coexist) | ✅ link clean, no errors |

## 8. Scope boundaries (do NOT do in this subtask)

- ❌ Do NOT implement `notifier_set_os()` body (S2) — only the header decl exists.
- ❌ Do NOT modify `process_full_message()` (S2 adds the OS-first/default-fallback scan).
- ❌ Do NOT create `test_notifier_os.c` (P1.M2.T1.S1).
- ❌ Do NOT extend `run_notifier_stub_tests.sh` (P1.M2.T2.S1) or touch any test/runner.
- ❌ Do NOT modify `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, `rules.mk`, `PRD.md`, `tasks.json`.
- ❌ Do NOT declare the per-OS accessors in `notifier.h` (internal linkage contract).

## 9. Documentation requirements (item point 5 — Mode A inline comments)

The block comment above the per-OS accessors MUST cover:
(a) each returns `{NULL,0}` unless overridden by `DEFINE_SERIAL_*_OS` in the keymap;
(b) the symbol names MUST match the `##os` paste in `notifier.h` exactly;
(c) `OS_UNSURE` and unexpected values resolve to `{NULL,0}` ⇒ default map fallback.
The `select_*_map_os` comment MUST say: "dispatch by current_os; OS_UNSURE /
unexpected => {NULL,0} => default map fallback (§8.3)". The `current_os` global
comment (from PRD §8.1, verbatim): push-only, never calls `detected_host_os()`,
`OS_UNSURE ⇒ default maps only` (invariant 17, §2 F8.2/F8.6).