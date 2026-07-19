# Research Findings — P1.M2.T1.S3: apply_host_callbacks

## Item contract recap

Implement `static void apply_host_callbacks(const uint8_t *ids, uint8_t count)` in
notifier.c — the host-callback diff against `host_cb_enabled[]` (P1.M1.T2.S1).
Two-phase, disable-before-enable (§13 invariant 4 / architecture "Host callback
diff algorithm"). Defensive id range check (RISK-3). NULL on_enable/on_disable
guarded (enable_command/disable_command pattern). Sole caller: APPLY_HOST_CONTEXT
handler (P1.M2.T2.S3).

## Verified facts (from current working copy)

- **`host_cb_enabled[]`** is at notifier.c (P1.M1.T2.S1 landed):
  `static bool host_cb_enabled[HOST_CALLBACK_MAX] = {false};` — currently emits
  `-Wunused-variable` (consumed by THIS task → warning disappears).
- **`HOST_CALLBACK_MAX`** = 32 (notifier.h:60). So Phase 1 loops `id` 0..31 (uint8_t OK).
- **`callback_t`** = `typedef void (*callback_t)(void);` (notifier.h:5).
- **`host_callback_t`** = `{ const char *name; callback_t on_enable; callback_t on_disable; }`
  (notifier.h:17-21). Comment says `on_disable; /* may be NULL */`.
- **Accessors** (P1.M1.T2.S1, weak): `get_host_callbacks()` → NULL;
  `get_host_callbacks_size()` → 0 when no `DEFINE_HOST_CALLBACKS`.
- **Board NULL-guard pattern** (enable_command/disable_command, notifier.c:267-285):
  ```c
  void enable_command(command_map_t *command) {
      current_command = command;
      if (command->on_enable != NULL) command->on_enable();
  }
  void disable_command(void) {
      if (current_command != NULL && current_command->on_disable != NULL)
          current_command->on_disable();
      current_command = NULL;
  }
  ```
- **Anchor**: insert between `set_host_layer`'s closing `}` (~L265) and
  `void enable_command(command_map_t *command) {` (L267). S2 (set_host_layer)
  ALREADY LANDED in the working copy (notifier.c:252). The two host state
  machines (set_host_layer + apply_host_callbacks) become contiguous.
- **Baseline warning set (pre-this-task, 4)**: set_host_layer (-Wunused-function),
  board_rules_present (-Wunused-function), has_been_queried (-Wunused-variable),
  host_cb_enabled (-Wunused-variable).

## Algorithm (from architecture + item contract)

```c
static void apply_host_callbacks(const uint8_t *ids, uint8_t count) {
    host_callback_t *cbs     = get_host_callbacks();     /* NULL when no registry */
    size_t           cb_size = get_host_callbacks_size(); /* 0 when no registry */
    /* PHASE 1 — DISABLE (newly-out-of-set, fires on_disable FIRST) */
    for (uint8_t id = 0; id < HOST_CALLBACK_MAX; id++) {
        if (!host_cb_enabled[id]) continue;
        bool still_desired = false;
        for (uint8_t i = 0; i < count; i++) if (ids[i] == id) { still_desired = true; break; }
        if (still_desired) continue;
        if (id < cb_size && cbs[id].on_disable != NULL) cbs[id].on_disable();   /* RISK-3 guard */
        host_cb_enabled[id] = false;    /* clear UNCONDITIONALLY for newly-out (deref is what's guarded) */
    }
    /* PHASE 2 — ENABLE (newly-in-set, fires on_enable AFTER all disables) */
    for (uint8_t i = 0; i < count; i++) {
        uint8_t id = ids[i];
        if (id >= HOST_CALLBACK_MAX) continue;     /* RISK-3: array bounds */
        if (id >= cb_size) continue;               /* RISK-3: registry bounds */
        if (host_cb_enabled[id]) continue;         /* already enabled (diff) */
        if (cbs[id].on_enable != NULL) cbs[id].on_enable();   /* NULL guard */
        host_cb_enabled[id] = true;
    }
}
```

### RISK-3 nuance (load-bearing)
- **Phase 1**: the `id < cb_size` guard protects ONLY the dereference (`cbs[id]`).
  The `host_cb_enabled[id] = false` clear runs UNCONDITIONALLY for any newly-out
  id (an enabled id that's >= cb_size shouldn't exist in normal operation, but if
  it did, it's still newly-out and must be cleared — without a dangerous deref).
- **Phase 2**: ALL THREE guards (`id < HOST_CALLBACK_MAX && id < cb_size && !enabled`)
  gate the entire operation INCLUDING the set, per the item contract verbatim.
  An out-of-range id is skipped entirely (never set).

### NULL-registry safety
When `DEFINE_HOST_CALLBACKS` is absent, `cbs==NULL` and `cb_size==0`. Then:
- Phase 1: `host_cb_enabled[]` is all false → no disable branch runs; even if it
  did, `id < cb_size` → `id < 0` false → no deref.
- Phase 2: `id >= cb_size` (0) → every id continues → no deref.
So `cbs` (NULL) is NEVER dereferenced. Safe. (VERIFIED by smoke test.)

## Empirical validation (research, against temp notifier.c copies)

1. **Stub-compile** (`gcc -Wall -Wextra -std=c99 ... -c`): exit 0. Warning set is
   EXACTLY: `apply_host_callbacks` (NEW -Wunused-function), `set_host_layer`
   (carried), `board_rules_present` (carried), `has_been_queried` (carried).
   **`host_cb_enabled` warning is GONE** (now read+written by apply_host_callbacks).

2. **Algorithm logic** (standalone verbatim-body mock, 4-entry recording registry,
   8 transitions + RISK-3 + NULL guards): **ALL CASES CONFIRMED (0 failures)**.
   Confirmed specifically:
   - enable {0,1} from empty → en0,en1 in id order
   - idempotent re-apply → nothing fires
   - {0,1}→{1,2} → **di0 fires BEFORE en2** (disable-before-enable ordering ✓)
   - {1,2}→{2,3} → di1 before en3
   - {2,3}→{0,3} → id2 disabled but on_disable NULL → guarded (no fire); en0 fires
   - →{} → di0 fires (id3 on_disable NULL → guarded)
   - RISK-3: ids {3,5,99} → only id3 (valid) enabled; 5,99 skipped, no deref/crash
   - clean slate verified

3. **Real-function NULL-safety** (#include temp notifier.c, weak {NULL,0} registry):
   `apply_host_callbacks({0,3,7}, 3)` → no crash, host_cb_enabled[] stays all false.
   **ALL CASES CONFIRMED (0 failures)**.

## Test strategy decision (matches S1 precedent)

`apply_host_callbacks` + `host_cb_enabled[]` are BOTH `static` (file-local in
notifier.c). The registry override (`DEFINE_HOST_CALLBACKS`) needs a SEPARATE
translation unit (strong beats weak at link time). This creates the classic
static-vs-weak tension: you can't #include notifier.c (to reach the static fn)
AND override its weak accessor in the same TU (redefinition error).

**Resolution (exactly what S1's board_rules_present did):**
- **2a — real-function NULL-safety smoke** (`#include "notifier.c"`): the REAL
  apply_host_callbacks is callable + safe with the weak NULL registry (RISK-3
  fires: no deref, no crash, host_cb_enabled[] stays false). Tests the literal fn.
- **2b — standalone verbatim-body logic mock**: copies the function body VERBATIM
  into a standalone program with a controlled recording registry. Exhaustively
  verifies disable-before-enable ORDER, newly-in/out, idempotency, RISK-3, NULL
  guards. (The body MUST match notifier.c — a comment asserts this.)
- **3 — regression**: dispatch 14/14 + os 31/31 stay green (apply_host_callbacks
  is static-unused in the legacy path; those suites never call it).

This mirrors S1 (board_rules_present): 2a `#include` with weak defaults + 2b
standalone verbatim-body logic mock. Proven pattern in this codebase.

## Sibling/consumer boundaries

- **set_host_layer (S2)** — LANDED (notifier.c:252). Disjoint: it touches
  `host_layer`; apply_host_callbacks touches `host_cb_enabled[]`. No overlap.
- **APPLY_HOST_CONTEXT handler (P1.M2.T2.S3)** — the sole caller. Calls
  `set_host_layer(layer)` then `apply_host_callbacks(ids, count)`. This task makes
  apply_host_callbacks EXIST; the caller lands later (→ the -Wunused-function
  warning self-resolves there, same convention as set_host_layer/board_rules_present).
- **QUERY_INFO/QUERY_CALLBACK/SET_OS handlers (P1.M2.T2)** — do NOT touch
  host_cb_enabled[]; disjoint.