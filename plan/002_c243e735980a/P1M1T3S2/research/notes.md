# Research Notes — P1.M1.T3.S2

## Status of upstream dependency (P1.M1.T3.S1)

**S1 has ALREADY LANDED in the repo** (verified: `git status` shows `M notifier.c`;
`grep -c` for S1 markers == 17; file is 462 lines). The post-S1 `notifier.c`
contains, between `command_map_t *current_command = {0};` and
`void activate_layer(...)`:
- `os_variant_t current_os = OS_UNSURE;` (+ §8.1 push-only comment)
- 16 per-OS weak accessors (`_notifier_get_{command,layer}_map_OS_{LINUX,WINDOWS,MACOS,IOS}`[+`_size`])
- 2 `static` selectors (`select_command_map_os`, `select_layer_map_os`)

S1 did NOT touch `process_full_message` (still at line ~334, byte-identical to
pre-S1). **Therefore S2's edits are additive/in-place to `process_full_message`
plus one new function `notifier_set_os`** — no conflict with S1's region.

## Empirical validation performed (end-state = post-S1 + S2)

Constructed `/tmp/t3s2_val/notifier.c` = repo-post-S1 + my two S2 edits, then:

### Level 1 — stub-compile (mirrors run_notifier_stub_tests.sh [1/3])
```
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o notifier.o
```
**Result: 0 warnings, rc=0.** (The 2 `-Wunused-function` warnings S1 left are
CLEARED — `process_full_message` now calls both selectors. This is the
forward-contract payoff S1 documented.)

### Level 2a — backward-compat canary (test_notifier_dispatch, 11/11)
```
gcc -Wall -std=c99 -Iqmk_stubs -I. notifier.o qmk_stubs/qmk_stubs.c \
    test_notifier_dispatch.c -o t_dispatch && ./t_dispatch
```
**Result: `Total tests run: 11 / passed: 11 / failed: 0`.**
(No `_OS` macros in this TU ⇒ current_os stays OS_UNSURE ⇒ selectors return
{NULL,0} ⇒ OS scans run 0 iterations ⇒ default maps scanned ⇒ byte-identical to
pre-multi-OS. Proves invariant 19 / F3 / F5 structurally.)

### Level 2b — functional multi-OS validation (research/test_notifier_os_val.c)
A focused test TU defining `DEFINE_SERIAL_COMMANDS`/`DEFINE_SERIAL_LAYERS`
(defaults) + `DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…)`/`DEFINE_SERIAL_LAYERS_OS(OS_MACOS,…)`
(strong overrides) with **distinguishable callbacks** (findings F6) and a
test-only `stub_get_active_layer()` added to a /tmp copy of qmk_stubs.c. Exercises
the six §11.2D categories end-to-end through `process_full_message`.

**Result: `Total tests run: 30 / passed: 30 / failed: 0`.** Covers:
- **F8.4 cmd** — "blender" matches OS_MACOS cmd (os_cmd_on) AND default cmd
  (def_cmd_on); OS scanned first ⇒ `os_cmd_en==1`, `def_cmd_en==0` (OS match
  PREVENTS default scan — the core rule).
- **F8.4 layer** — "blender" OS layer=11 vs default=22; OS wins ⇒ active layer 11.
- **F8.4 fallback** — "calculator" (no OS entry) ⇒ default layer 33 activated.
- **F8.5 independent** — "iTerm": OS layer 44 fires, command track resolves to
  nothing (independent); "neovide": default cmd fires, layer track resolves to
  nothing. Proves the two tracks decide independently (F5.5 preserved).
- **F8.6 OS_UNSURE** — at boot, OS-only pattern "iTerm" does NOT match (OS map
  inert); default "calculator" matches via default map.
- **F9.1 clear-on-change** — `notifier_set_os(OS_LINUX)` after blender active ⇒
  prev `on_disable` fired, active layer → 255 (cleared).
- **F9.2 no-re-dispatch** — after OS change, no `on_enable` re-fired, no layer
  re-activated by `notifier_set_os` itself.
- **F9.3 idempotent** — `notifier_set_os(OS_MACOS)` while already OS_MACOS ⇒
  no `on_disable`, layer unchanged (no-op).
- **F8.6/F8.4 OS_LINUX (no overrides)** — after switching to OS_LINUX, "blender"
  falls back to default cmd + default layer 22 (no OS_LINUX map defined).

> Link emitted 7 `-Wmissing-field-initializers` warnings — ALL from the TEST TU
> (map rows omitting trailing `case_sensitive`), NOT from notifier.c. The official
> `test_notifier_os.c` (P1.M2.T1.S1) can add `, false` or compile without that
> flag. Irrelevant to notifier.c correctness.

### Diff hygiene
`diff repo-post-S1.c /tmp/target.c`: ONLY the two S2 regions change — the
`process_full_message` body (declarations → return) and the inserted
`notifier_set_os`. No S1 code, no other region touched.

## Key design decisions (rationale)

### D1. Drop the `found_command_match` / `found_layer_match` index variables

**Decision:** REMOVE both `signed int` index vars; use `command_found != NULL`
(command track) and `layer_found != LAYER_UNSET` (layer track) as the sole
match-found signals, exactly per PRD §8.6 pseudocode
(`command_found = &it; … return (command_found || layer_found)`).

**Rationale:**
1. **Equivalence (proven):** in the original code both signals are set together
   inside the same loop iteration, so `found_command_match != -1` ⟺
   `command_found != NULL` and `found_layer_match != -1` ⟺
   `layer_found != LAYER_UNSET`. The conditions, the return, and the
   enable/activate guards are all behaviorally IDENTICAL. (Validated: dispatch
   11/11 + 30/30 functional.)
2. **Eliminates the F2 footgun structurally:** after the OS/default split, an
   index `i` is ambiguous (indexes into OS map OR default map — which?). The
   pointer `command_found` is unambiguous (points at the matched entry in
   whichever map). Keeping a redundant index var invites exactly the
   `cmd_map[found_command_match]` re-indexing bug F2 warns about, in any future
   edit. Dropping it makes the bug structurally impossible.
3. **Spec-faithful:** PRD §8.6 pseudocode uses only `command_found`/`layer_found`.
4. **"keep steps 6-8 unchanged"** is respected at the SEMANTIC level
   (disable-before-scan, deactivate-before-activate, enable-if-found,
   activate-if-found, first-match-wins, exactly-one-active-layer all preserved);
   the boolean expression form is an implementation detail the item does not
   constrain (its GOTCHA explicitly directs toward `command_found->pattern`).

**Alternative considered (rejected):** keep the index vars as a boolean and only
fix the CONSOLE print. Equivalent behavior, but leaves the ambiguity landmine in
place and deviates from PRD §8.6 pseudocode. Not recommended.

### D2. CONSOLE block: fix command print only; do NOT add a layer print

**Decision:** keep ONLY the existing command-track debug print, fixed to use
`command_found->pattern` (the F2 fix). Do NOT add a layer-track pattern print.

**Rationale:**
- Item point 3's GOTCHA makes the layer print OPTIONAL ("Same care IF a
  layer-track pattern print is added").
- Adding debug prints is scope creep under `CONSOLE_ENABLE` and a (small) risk
  surface for a subtle bug. The existing command print + the stub's
  `layer_on`/`raw_hid_send` stderr traces already give per-track observability.
- PRD §8.6 step 9 "print per-track match/miss" is satisfied by the command print
  (one track) + the existing return-value/layer traces. If the official
  test_notifier_os.c (P1.M2.T1.S1) or a later task wants a layer print, the F2
  gotcha tells them to use the matched layer entry's pattern via the same
  pointer discipline.

### D3. Placement of `notifier_set_os`

**Decision:** insert between `process_full_message`'s closing brace and
`hid_notify` (matches PRD section order §8.6 → §8.7 → §8.8).

**Rationale:**
- `notifier_set_os` calls `disable_command()` / `deactivate_layer()`, both
  defined ~line 178–209 (well before process_full_message at ~334) ⇒
  definition-before-use satisfied; no forward declaration needed.
- Independent of S1's insertion region (before activate_layer) ⇒ no merge
  conflict with S1.
- Logically groups with the dispatcher (it is the OS selector; process_full_message
  is the OS-map consumer).

### D4. The `_OS` macro symbol-name parity is S1's contract, not S2's

`notifier_set_os` only flips `current_os`; it does NOT reference any
`_notifier_get_*_OS_*` symbol directly (that's `select_*_map_os`, S1's code).
S2's `process_full_message` calls `select_command_map_os`/`select_layer_map_os`
(S1's static helpers), which internally dispatch to the right accessor. So S2 has
NO direct dependency on the `##os` mangled names — only on the selector
signatures, which are `static` and already in `notifier.c` (file-local). This is
why S2 compiles cleanly against the post-S1 file with no new externs.

## Validation commands (post-S1 + S2, reproduce verbatim)

All three were executed during research and PASSED. Use the **corrected-flag
harness** (`-Iqmk_stubs` on BOTH steps) — the current `run_notifier_stub_tests.sh`
[2/3] link step still lacks it (pre-existing, fixed by P1.M2.T2.S1; same caveat as
S1's PRP). See PRP Validation Loop for the exact commands.