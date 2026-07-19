# Research Notes — P1.M2.T1.S2: set_host_layer (host layer tracker)

## Authoritative spec

Item contract + PRD §14 + architecture doc §"two independent state planes" +
findings_and_risks.md RISK-2. `set_host_layer` is the **host-side mirror** of the
existing board `activate_layer`/`deactivate_layer` pair (notifier.c:218-235).

## The board pattern to mirror (notifier.c:218-235, current working tree)

```c
void activate_layer(uint8_t layer) {
    #ifdef CONSOLE_ENABLE
    uprintf("Activating layer %d\n", layer);
    #endif
    layer_on(layer);
    activated_layer = layer;
}

void deactivate_layer(void) {
    if (activated_layer == LAYER_UNSET) {
        return;
    }
    #ifdef CONSOLE_ENABLE
    uprintf("Deactivating layer %d\n", activated_layer);
    #endif
    layer_off(activated_layer);
    activated_layer = LAYER_UNSET;
}
```

Style notes: `#ifdef CONSOLE_ENABLE uprintf(...)` logging (compiled out in stubs —
CONSOLE_ENABLE is NOT defined in the stub build); explicit guard before layer_off;
`LAYER_UNSET` macro (not literal 255); assignment last.

## Dependencies that exist (CONTRACT — assume landed)

From P1.M1.T2.S1 (COMPLETE) + P1.M1.T1.S1 (COMPLETE):
- notifier.c:126 `#define LAYER_UNSET 255`
- notifier.c:127 `uint8_t activated_layer = LAYER_UNSET;` (BOARD tracker — must NOT touch)
- notifier.c:143 `static uint8_t host_layer = LAYER_UNSET;` (HOST tracker — what we mutate)
- notifier.h: `HOST_CALLBACK_MAX`, `HOST_LAYER_BASE` (224), `host_callback_t`
- QMK `layer_on(uint8_t)` / `layer_off(uint8_t)` (provided by qmk_stubs in tests)

## set_host_layer logic (per item contract (a)/(b)/(c), §14)

```c
static void set_host_layer(uint8_t layer) {
    if (layer == LAYER_UNSET) {                 /* (a) clear the host layer */
        if (host_layer != LAYER_UNSET) {
            layer_off(host_layer);
        }
        host_layer = LAYER_UNSET;
    } else {                                    /* (b) real host layer (>= 224) */
        if (host_layer != LAYER_UNSET) {
            layer_off(host_layer);              /* turn off old host layer first */
        }
        layer_on(layer);
        host_layer = layer;
    }
    /* (c) touches ONLY host_layer — NEVER activated_layer. No board clear. */
}
```

- Uses `LAYER_UNSET` macro (not literal 0xFF) — matches activate_layer idiom.
- Two-branch structure mirrors the item contract's (a)/(b) exactly (do NOT factor
  out the common `if (host_layer != LAYER_UNSET) layer_off(host_layer);` guard —
  keeping two branches preserves spec fidelity + readability).
- **NO range validation** (RISK-2: "set_host_layer does NOT validate the layer
  range — it trusts the host"). set_host_layer(5) just calls layer_on(5).

## Placement

After `deactivate_layer`'s close brace (notifier.c:235), BEFORE `enable_command`
(notifier.c:237). This keeps the §8.4 "Layer & command state machines" region
contiguous. **Disjoint from P1.M2.T1.S1's edits** (typed_mode fork is in
hid_notify ~L525-620; set_host_layer is ~L236).

## Build behavior (VERIFIED empirically)

### Baseline (current notifier.c) — 4 warnings:
```
board_rules_present defined but not used [-Wunused-function]   (L204)
has_been_queried defined but not used [-Wunused-variable]      (L145)
host_cb_enabled defined but not used [-Wunused-variable]       (L144)
host_layer defined but not used [-Wunused-variable]            (L143)
```

### After set_host_layer — still 4 warnings (swap: host_layer OUT, set_host_layer IN):
```
set_host_layer defined but not used [-Wunused-function]        (NEW, ~L250 -> self-resolves P1.M2.T2.S3)
board_rules_present defined but not used [-Wunused-function]   (L204)
has_been_queried defined but not used [-Wunused-variable]      (L145)
host_cb_enabled defined but not used [-Wunused-variable]       (L144)
```
**`host_layer` warning is GONE** (now read+written by set_host_layer).
**`set_host_layer` warning is NEW** (static, caller is APPLY_HOST_CONTEXT in
P1.M2.T2.S3, not yet implemented). Self-resolves when that handler calls it.
Exit stays 0 (warnings don't fail the non-`-Werror` build). Do NOT suppress with
`__attribute__((unused))` (not this codebase's idiom).

## uprintf / CONSOLE_ENABLE safety (VERIFIED)

- `qmk_stubs/print.h:19: #define uprintf(...) printf(__VA_ARGS__)` — uprintf IS
  available (via print.h, pulled by QMK_KEYBOARD_H), defined as printf.
- `CONSOLE_ENABLE` is NOT defined in the stub build (grep of
  run_notifier_stub_tests.sh + qmk_keyboard_stub.h = empty). So
  `#ifdef CONSOLE_ENABLE uprintf(...)` blocks are compiled OUT in stubs.
- activate_layer (L218) already uses this pattern and stub-compiles fine. Safe to
  mirror. (Omitting the logging is also fine — the item contract doesn't require
  it — but mirroring keeps the §8.4 region visually consistent.)

## Stub semantics (qmk_stubs/qmk_stubs.c) — matters for the test

```c
static uint8_t g_active_layer = 255;
void layer_on(uint8_t layer)  { g_active_layer = layer; }     /* tracks last layer_on */
void layer_off(uint8_t layer) { (void)layer; g_active_layer = 255; }  /* IGNORES arg, resets */
uint8_t stub_get_active_layer(void) { return g_active_layer; }
```
- The stub models a SINGLE active layer (no stacking). layer_off ignores its arg.
- So g_active_layer is NOT a reliable board-vs-host discriminator. The test asserts
  the two STATIC GLOBALS (host_layer, activated_layer) directly via `#include notifier.c`.
- For set_host_layer's logic, what matters: layer_off(old_host) is CALLED before
  layer_on(new). The stub's layer_off resetting g_active_layer=255 then layer_on(new)
  setting it to new is the observable trace.

## Behavioral test results (VERIFIED — 16/16 assertions pass)

Against a temp notifier.c with set_host_layer inserted:
1. Initial: host_layer==LAYER_UNSET, activated_layer==LAYER_UNSET ✓
2. set_host_layer(230): host_layer==230, g_active==230 (layer_on(230)) ✓
3. set_host_layer(240) change: host_layer==240 (layer_off(230)+layer_on(240)) ✓
4. set_host_layer(LAYER_UNSET) clear: host_layer==LAYER_UNSET (layer_off(240)) ✓
5. clear when already unset: no-op (guard skips layer_off) ✓
6. **ORTHOGONALITY (invariant 21)**: activate_layer(5)→activated_layer==5;
   set_host_layer(231)→activated_layer STILL 5, host_layer==231 (board untouched) ✓
   set_host_layer(LAYER_UNSET)→activated_layer STILL 5 ✓
7. RISK-2: set_host_layer(5) works (no range validation) ✓
"ALL CASES CONFIRMED (0 failures)".

The orthogonality test (case 6) is the load-bearing one: it proves set_host_layer
operates ONLY on host_layer and never on activated_layer — the two-plane
independence that is invariant 21.

## Consumer contract (downstream, NOT this task)

From architecture doc §"APPLY_HOST_CONTEXT handler" + item contract §4:
- APPLY_HOST_CONTEXT handler (P1.M2.T2.S3) calls:
  1. (if clear_board flag) deactivate_layer() + disable_command() [board clear]
  2. set_host_layer(layer)            [this task — host layer]
  3. apply_host_callbacks(ids, count) [P1.M2.T1.S3 — host callbacks]
- set_host_layer receives `layer` = the APPLY_HOST_CONTEXT.layer byte (0xFF=LAYER_UNSET
  to clear, else a host layer >= 224 per §14/§4.6).
- Host layers stack above board layers under QMK's highest-layer-wins rule.

## Anti-patterns confirmed NOT to do
- Do NOT touch activated_layer (board tracker) — invariant 21.
- Do NOT validate the layer range — RISK-2 (trust the host).
- Do NOT check/clear board state — that's clear_board's job (APPLY_HOST_CONTEXT).
- Do NOT suppress the set_host_layer unused warning.
- Do NOT use literal 0xFF — use LAYER_UNSET macro.
- Do NOT factor the two branches into one (keep spec (a)/(b) structure).
- Do NOT edit hid_notify / typed_mode (that's P1.M2.T1.S1, parallel, disjoint region).