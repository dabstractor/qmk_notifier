# Research Notes — P1.M1.T2.S2

**Work item:** Refactor `notifier_set_os` into a shared `apply_os_change` seam so
`SET_OS` (typed cmd, P1.M2.T2.S2) can reuse the same OS-change logic.

## 1. What exists when S2 starts (dependency: P1.M1.T2.S1, landing in parallel)

P1.M1.T2.S1 adds to `notifier.c` (verified present in the working tree):
- Host state globals after `current_os` (~L137-139): `host_layer = LAYER_UNSET`,
  `host_cb_enabled[HOST_CALLBACK_MAX] = {false}`, `has_been_queried = false`.
- `static bool board_rules_present(void)` after the selectors (~L198).
- Weak host-callback accessors in §8.3.

These are **unused scaffolding** until P1.M2 and emit 4 expected `-Wunused`
warnings under `-Wall -Wextra`. **S2 does not touch any of them.** S2's refactor
is orthogonal to S1's additions.

## 2. The exact function being refactored (notifier.c L493-523, verified verbatim)

```c
/* notifier_set_os — the OS selector (§8.7). Sole mutation point for current_os
 * ... [big §8.7 comment: invariant 17, F9 contract, symbol-name parity] ... */
void notifier_set_os(os_variant_t os) {
    if (os == current_os) return;                 /* idempotent: no flap on repeat (F9.3) */
    #ifdef CONSOLE_ENABLE
    uprintf("notifier: OS %u -> %u; clearing state\n", (unsigned)current_os, (unsigned)os);
    #endif
    current_os = os;
    disable_command();      /* fires prev on_disable if a command was active (F9.1) */
    deactivate_layer();     /* turns off the active notifier layer if any (F9.1)    */
    /* Intentionally do NOT re-dispatch the last message. The next focus-change
     * message from the host re-establishes state under the new maps (F9.2). */
}
```

`disable_command()` (L244) and `deactivate_layer()` (L220) are both defined
**above** this point, so a new `apply_os_change` placed immediately before
`notifier_set_os` can call them with no forward declaration.

## 3. Sole-mutation-point invariant (verified)

`grep -nE 'current_os[[:space:]]*=' notifier.c` shows `current_os` is WRITTEN at
exactly two places:
- L129: `os_variant_t current_os = OS_UNSURE;` (init)
- L518: `current_os = os;` (inside notifier_set_os)

Moving L518 into `apply_os_change` keeps `current_os` mutated only in
`apply_os_change` + init → the "sole mutation point" invariant (item §3, §17
invariant 17) holds. No other write site exists.

## 4. The target refactor (architecture doc §'apply_os_change seam refactor')

```c
static void apply_os_change(os_variant_t os) {   /* holds the logic + the uprintf */
    if (os == current_os) return;
    current_os = os;
    disable_command();
    deactivate_layer();
}
void notifier_set_os(os_variant_t os) {          /* one-line forwarder */
    apply_os_change(os);
}
```

Item §3 specifics:
- `apply_os_change` is `static` (internal; only notifier.c callers).
- The `#ifdef CONSOLE_ENABLE` `uprintf` **stays in apply_os_change** (item §3 —
  "The CONSOLE_ENABLE uprintf stays in apply_os_change").
- The §8.7 contract comment (F9 idempotent/clear/no-re-dispatch) moves onto
  `apply_os_change`; `notifier_set_os` keeps a reduced comment (keymap entry
  point, symbol-name parity).
- Mode-A comment on apply_os_change (item §5, verbatim text).
- `notifier_set_os` stays `void`/public/unchanged signature — it's declared in
  notifier.h (§5.2/§8.7) and called by the keymap; tests call it unchanged.

## 5. Empirical validation (executed against a temp notifier.c copy)

Applied the exact old→new block replacement to `/tmp/notifier_refactor.c` and ran
the real gate commands:

| Check | Command | Result |
|---|---|---|
| stub-compile | `gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c` | **exit 0**; ONLY the 4 S1 `-Wunused` warnings; **NO** apply_os_change warning (static + used) |
| OS regression | link `test_notifier_os.c` + run | **31/31 passed**, exit 0 (idempotency iv + clear-on-change v both green) |
| dispatch regression | link `test_notifier_dispatch.c` + run | **14/14 passed**, exit 0 |

→ Behavior is identical (tests pass unchanged), and no new warning is introduced.

## 6. SET_OS consumer contract (P1.M2.T2.S2 — NOT this task, but the seam's purpose)

`apply_os_change` takes `os_variant_t` directly. The SET_OS handler (P1.M2.T2.S2)
will call `apply_os_change((os_variant_t)os_byte)`. The cast is sound: the stub
`os_detection.h` defines `OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3,
OS_IOS=4`, which matches the §4.6 SET_OS `os_byte` mapping (`0 UNSURE · 1 LINUX ·
2 WINDOWS · 3 MACOS · 4 IOS`) exactly. The host's value is authoritative while a
host is connected (§4.7). S2's only job is to EXPOSE `apply_os_change` as the
shared seam; P1.M2.T2.S2 wires the call. Do NOT duplicate the F9 logic in the
SET_OS handler — that divergence risk is the reason this seam exists (findings F2).

## 7. Scope boundaries (do not violate)

- MODIFY ONLY `notifier.c` (the single notifier_set_os block ~L493-523).
- Do NOT touch: notifier.h (notifier_set_os already declared), pattern_match.*,
  qmk_stubs/*, test_*.c, run_*.sh, README.md, PRD.md, tasks.json, prd_snapshot.md,
  .gitignore.
- Do NOT add forward declarations beyond what's needed — apply_os_change placed
  immediately above notifier_set_os needs none (disable_command/deactivate_layer
  are already defined above).
- Do NOT change notifier_set_os's signature or its presence in notifier.h.
- Do NOT move/touch S1's host globals or board_rules_present (orthogonal).