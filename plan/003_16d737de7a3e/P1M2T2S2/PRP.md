# PRP — P1.M2.T2.S2: Implement SET_OS handler (host-authoritative OS via apply_os_change seam)

## Goal

**Feature Goal**: Add the **`SET_OS` (cmd_id `0x03`) typed-command handler** to
`handle_typed_command` in `notifier.c`. It lets a connected desktop host declare
its OS authoritatively by routing the wire `os_byte` through the **shared
`apply_os_change` seam** (the same function the keymap's `notifier_set_os` calls),
so an OS **change** triggers the F9 clear (idempotent check → `disable_command()`
→ `deactivate_layer()`) **without duplicating that logic**, then replies
`[0x51][0x03][ack=1]`. This implements §4.6/§4.7 of the wire contract for the one
remaining typed command in this milestone (APPLY_HOST_CONTEXT `0x05` is the next
subtask, P1.M2.T2.S3).

**Deliverable**: A new `case NOTIFY_CMD_SET_OS:` (0x03) block **inserted before
the `default:` case** in the existing `handle_typed_command` switch in
`notifier.c` (which P1.M2.T2.S1 left catching `0x03` via the safe placeholder
`default`). The case is ~7 lines + a Mode-A comment. A one-line **comment-only
update** to the `default:` case is also required (remove the now-stale "0x03"
mention). No new files, no new constants, no new functions, no new globals.

**Success Definition**:
- `case NOTIFY_CMD_SET_OS:` is present, placed immediately before `default:`,
  with `case NOTIFY_CMD_QUERY_CALLBACK:` still before it (switch order:
  `0x01 → 0x02 → 0x03 → default`).
- It reads `uint8_t os_byte = (uint8_t)data[2];`, calls
  `apply_os_change((os_variant_t)os_byte);`, builds `uint8_t payload[1] = {0x01};`,
  and calls `send_typed_response(NOTIFY_CMD_SET_OS, payload, 1);` then `break;`.
- It does **NOT** add its own idempotency check, its own `disable_command()`/
  `deactivate_layer()`, or any `current_os =` write — all of that lives in
  `apply_os_change` (invariant 17; F2 finding).
- `gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c`
  → **exit 0**, **no new warnings** vs the pre-task baseline (the only warnings
  are the carried `-Wunused-function` set from static helpers, unchanged).
- `./run_notifier_stub_tests.sh` → **dispatch fails=0, os fails=0** (no regression).
- A `/tmp` `#include "notifier.c"` harness (the Level-2 gate) confirms: a
  `[0xF0][0x03][os_byte]` buffer makes `handle_typed_command` return `true`,
  `current_os` equals the `os_byte`, and the captured 32-byte response is exactly
  `[0x51][0x03][0x01]`; an OS **change** clears the active layer (F9.1) while an
  **unchanged** OS does **not** (F9.3); all five `os_variant_t` values (0..4) route.
- Mode-A comment present on the case (item-spec §5): "Host-authoritative OS while
  connected (§4.7). Routes through apply_os_change — same seam as notifier_set_os,
  so F9 clear-on-change is not duplicated. os_byte mirrors os_variant_t
  (0=UNSURE..4=IOS)."

## User Persona (if applicable)

**Target User**: The QMKonnect desktop host (the `qmk_notifier` Rust crate). End
users never send typed commands directly; the host sends `SET_OS` once at connect
after the §4.6 capability handshake (`QUERY_INFO` → `proto_ver == 2`).

**Use Case**: Host connects → `QUERY_INFO` (proto=2) → host declares its OS:
`[0x81][0x9F][0xF0][0x03][os_byte][0x03]` → firmware applies it authoritatively
and replies `[0x51][0x03][0x01]`. While the host stays connected, its OS value
governs multi-OS map selection (§2 F8), overriding the firmware's
`OS_DETECTION` heuristic.

**User Journey**: `QUERY_INFO` (P1.M2.T2.S1) detects capability → host sends
`SET_OS` (this task) → `apply_os_change` flips `current_os` (clearing stale
state once) → subsequent focus-change strings dispatch against the new OS's
`DEFINE_SERIAL_*_OS` maps → host later sends `APPLY_HOST_CONTEXT` (P1.M2.T2.S3)
for host-layer/callback rules.

**Pain Points Addressed**: `OS_DETECTION` is a heuristic USB fingerprint that
misdetects (Linux spoofing macOS for media keys, VMs, `OS_UNSURE`). `SET_OS`
makes the OS authoritative when a host is connected, so multi-OS maps resolve
correctly even on hardware that mis-fingerprints.

## Why

- **Completes the typed-command milestone (minus APPLY_HOST_CONTEXT)**: P1.M2.T2.S1
  landed the dispatch skeleton + `QUERY_INFO` + `QUERY_CALLBACK` + a safe
  `default`. `SET_OS` is the third of four handlers; `APPLY_HOST_CONTEXT` (S3) is
  the fourth. This task removes `0x03` from the "not yet implemented" set.
- **Reuses the F9 seam — divergence risk is high if duplicated** (architecture
  findings F2). `apply_os_change` (P1.M1.T2.S2) is the *sole* mutation point for
  `current_os` (invariant 17). Both the keymap path (`notifier_set_os`) and the
  typed path (`SET_OS`) route through it, so F9 clear-on-change (idempotency +
  disable + deactivate) can never diverge between the two OS sources. This task
  does **not** re-implement any F9 logic — it calls the seam.
- **Authoritative OS is a load-bearing orthogonality** (§4.7): `SET_OS` updates
  `current_os` (shared board state) and is therefore the *one* typed command that
  touches board state. It does NOT touch host state (`host_layer`/`host_cb_enabled`);
  `APPLY_HOST_CONTEXT` does. The comment must record this so a future dev does not
  "fix" the apparent leak.
- **No new dependencies**: `apply_os_change`, `send_typed_response`, the switch
  skeleton, `NOTIFY_CMD_SET_OS`, and `current_os`/`os_variant_t` all already exist
  (P1.M1.T2.S2, P1.M2.T2.S1, notifier.h, qmk_stubs/os_detection.h). This is a pure
  insertion + a comment tweak.

## What

Insert one `case` block + one comment edit into the existing
`handle_typed_command` switch in `notifier.c` (currently ~lines 651-697). No
other file changes.

The handler behavior:
1. **Read** `os_byte = (uint8_t)data[2];` — the single request arg (§4.6). Reading
   `data[2]` directly mirrors the `QUERY_CALLBACK` case (which also reads
   `(uint8_t)data[2]` without bounds-checking). A malformed `SET_OS` with no arg
   leaves `data[2] == '\0' == 0 == OS_UNSURE` — a benign, valid value.
2. **Apply** `apply_os_change((os_variant_t)os_byte);` — flips `current_os`
   (clearing state once if it actually changed; no-op if unchanged). The cast
   `uint8_t → os_variant_t` is well-defined for any byte; out-of-range values
   (e.g. 99) just resolve to the default-map fallback in `select_*_map_os()`.
3. **Reply** `uint8_t payload[1] = {0x01}; send_typed_response(NOTIFY_CMD_SET_OS, payload, 1);`
   → on the wire `[0x51][0x03][0x01]` (the `ack=1` = applied, per §4.6).
4. `break;` — `handle_typed_command` then returns `true` (the post-switch
   `return true;` covers every case).

Plus: update the `default:` comment from "0x03/0x05 until P1.M2.T2.S2/S3 land"
to "0x05 until P1.M2.T2.S3 lands" (drop the now-handled `0x03`). The `default:`
**logic** (`send_typed_response(cmd_id, NULL, 0); break;`) is unchanged — it stays
as the safe placeholder for `0x04` (reserved) and `0x05` (pending S3).

### Success Criteria

- [ ] `case NOTIFY_CMD_SET_OS:` block present, placed immediately before
      `default:`, after `case NOTIFY_CMD_QUERY_CALLBACK:`.
- [ ] Body: `uint8_t os_byte = (uint8_t)data[2];` →
      `apply_os_change((os_variant_t)os_byte);` →
      `uint8_t payload[1] = {0x01};` →
      `send_typed_response(NOTIFY_CMD_SET_OS, payload, 1);` → `break;`
- [ ] No `current_os =`, no `disable_command()`, no `deactivate_layer()`, no
      idempotency `if` inside the SET_OS case (all owned by `apply_os_change`).
- [ ] Mode-A comment present (wording in Goal).
- [ ] `default:` comment updated to drop "0x03"; `default:` logic unchanged.
- [ ] Stub-compile of `notifier.c` → exit 0, no new warnings.
- [ ] `./run_notifier_stub_tests.sh` → dispatch fails=0, os fails=0.
- [ ] Level-2 `/tmp` harness: 5 behavioral cases pass (response, OS change, F9.1
      clear-on-change, F9.3 idempotency, all os_variant_t values route).

## All Needed Context

### Context Completeness Check

**Pass.** The handler is a 7-line insertion into a switch whose structure,
signatures, and call targets are all **already present in the current `notifier.c`**
(verified by reading the file): `handle_typed_command` (static, ~line 651) with
its `QUERY_INFO`/`QUERY_CALLBACK`/`default` cases as the exact template to mirror;
`apply_os_change` (static, ~line 595 — P1.M1.T2.S2, COMPLETE) with the F9 body;
`send_typed_response` (static, ~line 628 — P1.M2.T2.S1) with its `(cmd_id, payload,
len)` signature and `[0x51][cmd_id][payload…]` layout; the `current_os` global
(~line 135); the `NOTIFY_CMD_SET_OS` constant (notifier.h:50). The `os_variant_t`
values (0..4) are confirmed in `qmk_stubs/os_detection.h`. The `#include
"notifier.c"` validation harness was **executed against the live `notifier.c`**
during research (calling the existing `QUERY_INFO` case) and proved: it compiles
clean, reaches the static `handle_typed_command`, reads file-scope `current_os`,
and captures the full 32-byte response (`[51][01][02]`). An implementer with only
this PRP + repo access can produce the handler behavior-identically and prove it.

### Documentation & References

```yaml
# MUST READ — the authoritative wire contract for SET_OS
- file: PRD.md
  section: "### 4.6 Typed-command namespace (canonical owner)"
  why: "The command table row: 0x03 SET_OS, Request args [os_byte], Response payload
        after [0x51][cmd_echo] = [ack] (1=applied). And the field def: 'SET_OS.os_byte:
        0 UNSURE · 1 LINUX · 2 WINDOWS · 3 MACOS · 4 IOS (mirrors os_variant_t)'. And
        the response layout: typed response = [0x51][cmd_id_echo][payload…][padding]."
  critical: "data layout after magic-strip: data[0]==0xF0 discriminator, data[1]==cmd_id,
        data[2..]==args. So for SET_OS the os_byte is at data[2]. ack byte is 0x01
        (=applied). This task must echo cmd_id 0x03 in the response."

- file: PRD.md
  section: "### 4.7 OS source: host-authoritative when a host is connected"
  why: "The rationale this handler exists: 'While a host is connected and has sent
        SET_OS, the host's value is authoritative for current_os — it takes precedence
        over the OS_DETECTION heuristic.' And: 'SET_OS updates current_os through the
        same seam as notifier_set_os, so an OS change clears notifier state per F9.'"
  critical: "§4.7 names apply_os_change as the seam. Do NOT introduce a second OS-
        mutation path. The comment in this handler must reference §4.7 (Mode A)."

- file: PRD.md
  section: "### F9 — OS-change state clearing"
  why: "F9.1 (clear on change: disable_command then deactivate_layer), F9.3 (idempotent
        on unchanged value — no flap). This handler triggers these by calling
        apply_os_change; it does NOT re-implement them."
  critical: "If a test asserts F9 behavior for SET_OS, the behavior comes from
        apply_os_change, not from this handler. Adding a redundant clear here would
        double-fire on_disable / double-deactivate."

- file: PRD.md
  section: "## 14. Host-Side Rules & Typed Commands"
  why: "Names SET_OS among the four handlers and the orthogonality rule: multi-OS map
        selection touches ONLY which board map the legacy string path consults; the
        typed namespace + host trackers are independent (except SET_OS updates the
        shared current_os, and APPLY_HOST_CONTEXT.clear_board explicitly clears board)."
  critical: "SET_OS is the ONE typed command that legitimately touches board state
        (current_os). Record this in the comment so it isn't 'fixed' later."

# Architecture — the seam narrative + the SET_OS specifics
- file: plan/003_16d737de7a3e/architecture/host_rules_architecture.md
  section: "### SET_OS (0x03) — host-authoritative OS"
  why: "Spells out the exact behavior: 'Routes through apply_os_change((os_variant_t)os_byte)
        — the SAME function notifier_set_os calls. So an OS change triggers F9 clear
        (disable command + deactivate layer). Idempotent on unchanged value.' And the
        response '[0x51][0x03][ack=1]'."
  critical: "This is the single-paragraph spec for the handler. Mirror its wording."

- file: plan/003_16d737de7a3e/architecture/findings_and_risks.md
  section: "F2 — SET_OS must share the notifier_set_os seam"
  why: "The finding that FORCES the design: 'extract static void apply_os_change(os_variant_t)
        that both call. Do NOT duplicate the F9 logic in the SET_OS handler — divergence
        risk is high.' apply_os_change already exists (P1.M1.T2.S2 COMPLETE); this task
        just calls it."
  critical: "The #1 anti-pattern for this task: do not duplicate F9. Call the seam."

# Dependency PRPs — what exists when this task starts (CONTRACTS)
- file: plan/003_16d737de7a3e/P1M1T2S2/PRP.md
  section: "## Implementation Blueprint" (apply_os_change)
  why: "P1.M1.T2.S2 (COMPLETE) produced apply_os_change(os_variant_t) as the sole
        mutation point for current_os, plus the notifier_set_os one-line forwarder.
        Its contract: static, idempotent (F9.3), clears on change (F9.1), no re-dispatch
        (F9.2). This task CALLS it; it does not modify it."
  critical: "Treat apply_os_change as a black box with signature
        'static void apply_os_change(os_variant_t os);'. Do not read its internals from
        the SET_OS handler — just call it. (It reads/writes current_os for you.)"

- file: plan/003_16d737de7a3e/P1M2T2S1/PRP.md
  section: "## Goal" + "## Implementation Blueprint" (handle_typed_command + send_typed_response)
  why: "P1.M2.T2.S1 (the immediately-preceding task) produced: send_typed_response(cmd_id,
        payload, payload_len) [0x51][cmd_id][payload…]; handle_typed_command(char *data)
        switching on (uint8_t)data[1] with QUERY_INFO (0x01) + QUERY_CALLBACK (0x02) +
        default cases. Its contract explicitly leaves SET_OS/APPLY_HOST_CONTEXT to
        'fall through to default' and states 'S2/S3 insert their case branches BEFORE
        the default.' This task is S2 — it inserts the SET_OS case before default."
  critical: "QUERY_CALLBACK (notifier.c:672) is the template to mirror: it reads
        (uint8_t)data[2], builds a uint8_t payload[], calls send_typed_response, break.
        Copy that STRUCTURE for SET_OS. Also: the default-case comment (notifier.c:686)
        says '0x03/0x05 until P1.M2.T2.S2/S3 land' — S2 updates it to '0x05 until
        P1.M2.T2.S3 lands' (comment-only)."

# The live implementation (the source of truth the implementer edits)
- file: notifier.c
  section: "static bool handle_typed_command(char *data)" (~lines 651-697)
  why: "The exact switch to modify. QUERY_CALLBACK (case at ~672) is the structural
        template; default (case at ~686) is the insertion anchor + the comment to tweak."
  pattern: "case NOTIFY_CMD_QUERY_CALLBACK: { uint8_t index = (uint8_t)data[2]; ...;
              uint8_t payload[..]; ...; send_typed_response(NOTIFY_CMD_QUERY_CALLBACK,
              payload, N); break; }"
  gotcha: "The case must be INSERTED before default, not appended after it — otherwise
           SET_OS never runs (default would catch 0x03 first). C switch cases fall
           through top-to-bottom; place 0x03 between 0x02 and default."

- file: notifier.h
  section: "#define NOTIFY_CMD_SET_OS 0x03" (line 50)
  why: "The constant this case switches on. Already #defined — do not re-define."

# The os_variant_t type (values 0..4)
- file: qmk_stubs/os_detection.h
  section: "typedef enum { OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3, OS_IOS=4 }"
  why: "Confirms os_byte maps 1:1 onto os_variant_t enumerators. Cast is direct."
  critical: "Any uint8_t casts cleanly to os_variant_t (C enums are int-backed);
        out-of-range bytes (e.g. 99) set current_os=99 which select_*_map_os() maps
        to the default-map fallback — benign. No range validation needed in the handler."

# External theory (informational)
- url: https://swtch.com/~rsc/regexp/regexp1.html
  why: "(Not used by SET_OS itself, but referenced by the surrounding NFA matcher.) The
        §4.6/§4.7 wire design is self-contained in the PRD; no external doc is needed
        for this 7-line handler."
```

### Current Codebase tree (run `ls` at repo root)

```bash
notifier.c             # P1.M1..P1.M2.T2.S1 COMPLETE (~33 KB). Contains, in order:
                       #   globals (current_os, activated_layer, host_layer, host_cb_enabled,
                       #            has_been_queried, typed_mode) +
                       #   select_*_map_os() + board_rules_present() +
                       #   layer/command state machines + match_pattern + process_full_message +
                       #   apply_os_change()  [P1.M1.T2.S2 — the seam this task calls] +
                       #   notifier_set_os() [one-line forwarder to apply_os_change] +
                       #   send_typed_response()  [P1.M2.T2.S1] +
                       #   handle_typed_command() [P1.M2.T2.S1 — THIS task adds the 0x03 case] +
                       #   hid_notify() [P1.M2.T1.S1 — typed routing fork]
                       #   THIS task: INSERT case NOTIFY_CMD_SET_OS before default in
                       #   handle_typed_command; UPDATE default's comment (drop "0x03").
notifier.h             # P1.M1.T1.S1 COMPLETE. NOTIFY_CMD_SET_OS=0x03 already #defined.
pattern_match.{c,h}    # Untouched (matcher is the single source of truth for semantics).
qmk_stubs/             # os_detection.h (os_variant_t), qmk_stubs.c (layer_on/off, raw_hid_send,
                       #   stub_get_active_layer), print.h, raw_hid.h, qmk_keyboard_stub.h.
                       #   stub_get_last_response does NOT exist yet (P1.M3.T1.S1).
test_notifier_*.c      # test_notifier_dispatch.c, test_notifier_os.c (exist). test_notifier_host.c
                       #   is P1.M3.T1.S2 (Planned) — NOT this task.
run_notifier_stub_tests.sh  # Builds notifier.o (stub) + links the 2 existing test drivers.
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — write only your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
notifier.c             # THIS task MODIFIES handle_typed_command only:
                       #   - INSERT case NOTIFY_CMD_SET_OS (0x03) before default
                       #   - UPDATE default's comment (drop the now-stale "0x03")
                       # Later subtasks continue editing this same switch:
                       #   P1.M2.T2.S3 -> inserts case NOTIFY_CMD_APPLY_HOST_CONTEXT (0x05)
                       #                  before default; updates default comment to drop "0x05"
# No new files. No new constants. No new functions. No new globals.
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — do NOT duplicate F9. apply_os_change() (notifier.c:595) ALREADY does:
//   if (os == current_os) return;   // F9.3 idempotent
//   current_os = os;
//   disable_command();              // F9.1
//   deactivate_layer();             // F9.1
// SET_OS must ONLY call apply_os_change((os_variant_t)os_byte). Adding a second
// idempotency check, a second current_os=, or a second disable/deactivate here would
// (a) diverge from the keymap path and (b) double-fire on_disable / double-deactivate
// on a real change. (Architecture finding F2; invariant 17.)

// CRITICAL — INSERT BEFORE default, not after. C switches evaluate cases top-to-bottom;
// the default: at the END currently catches 0x03 (SET_OS), 0x04 (reserved), 0x05
// (APPLY_HOST_CONTEXT). If you append the SET_OS case AFTER default, 0x03 still hits
// default and your case is dead code. Place `case NOTIFY_CMD_SET_OS:` BETWEEN
// `case NOTIFY_CMD_QUERY_CALLBACK:` and `default:`.

// GOTCHA — os_byte is at data[2], NOT data[0]. msg_buffer layout after magic-strip
// (hid_notify does data+=2; length-=2;): data[0]==0xF0 discriminator, data[1]==cmd_id,
// data[2..]==args. So the SET_OS arg (os_byte) is data[2]. (Mirror QUERY_CALLBACK,
// which reads index = (uint8_t)data[2].)

// GOTCHA — reading data[2] without a length check is SAFE and INTENTIONAL. The host
// sends [0xF0][0x03][os_byte][ETX]; the reassembly NUL-terminates msg_buffer. A
// malformed SET_OS with no arg ([0xF0][0x03][ETX]) leaves data[2]=='\0'==0==OS_UNSURE
// — a benign valid value. This matches the established (non-validating) style of
// QUERY_CALLBACK; do not add a bounds check (it would diverge from the sibling cases
// and has no failure mode — the NUL terminator is always present).

// GOTCHA — the ack byte is 0x01, NOT 0x03 or true. §4.6: SET_OS response payload is
// [ack] where "1=applied". The payload is a single byte 0x01. (cmd_id 0x03 is echoed
// by send_typed_response in response[1]; the ack is response[2].) Full wire response:
// [0x51][0x03][0x01] then zero-padding to 32 bytes (send_typed_response does the pad).

// GOTCHA — do NOT touch host state. SET_OS updates current_os (board state) via the
// seam. It must NOT touch host_layer or host_cb_enabled (those are APPLY_HOST_CONTEXT's
// job, P1.M2.T2.S3). Orthogonality (§4.7, invariant 21).

// GOTCHA — the default-case comment must be UPDATED, not left stale. notifier.c:686
// currently reads "Default / unknown cmd_id (incl 0x04 reserved for VIA-coexist, and
// 0x03/0x05 until P1.M2.T2.S2/S3 land)". After this task 0x03 is handled, so change
// "0x03/0x05 until P1.M2.T2.S2/S3 land" -> "0x05 until P1.M2.T2.S3 lands". The
// default LOGIC (send_typed_response(cmd_id, NULL, 0); break;) is UNCHANGED — it stays
// as the safe placeholder for 0x04 and the pending 0x05.

// GOTCHA — cast uint8_t -> os_variant_t directly. C enums are int-backed; any uint8_t
// is a valid os_variant_t representation. Out-of-range bytes (e.g. 99) set
// current_os=99, which select_command_map_os/select_layer_map_os map to {NULL,0} =>
// default-map fallback (notifier.c:153 comment). No range guard needed; do not add one.

// GOTCHA — no #include changes. apply_os_change, send_typed_response, NOTIFY_CMD_SET_OS,
// os_variant_t, current_os are all already in scope in notifier.c (the seam + builder +
// constant from earlier subtasks; os_variant_t via notifier.h -> os_detection.h). Adding
// an #include is scope creep and will trip the "no new warnings" gate if it's unused.

// GOTCHA — handle_typed_command already returns true AFTER the switch (notifier.c:695).
// Do NOT add `return true;` inside the SET_OS case — use `break;` so the single
// post-switch `return true;` is the only return path (matches QUERY_INFO/QUERY_CALLBACK).
```

## Implementation Blueprint

### Data models and structure

No new data models. This task consumes the existing types/symbols:

```c
/* Already in scope (notifier.c + notifier.h + os_detection.h): */
typedef enum { OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3, OS_IOS=4 } os_variant_t;
os_variant_t current_os;                              /* file-scope global, notifier.c:135 */
static void apply_os_change(os_variant_t os);         /* notifier.c:595 (P1.M1.T2.S2) */
static void send_typed_response(uint8_t cmd_id,
                                const uint8_t *payload, uint8_t payload_len);   /* notifier.c:628 */
#define NOTIFY_CMD_SET_OS 0x03                         /* notifier.h:50 */
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — insert the SET_OS case in handle_typed_command
  - PLACE: inside `static bool handle_typed_command(char *data)`, in the
    `switch (cmd_id)` block. INSERT the new case BETWEEN the existing
    `case NOTIFY_CMD_QUERY_CALLBACK: { ... }` block and the `default: { ... }` block.
  - TEMPLATE to mirror: the QUERY_CALLBACK case (notifier.c:672) — read (uint8_t)data[2],
    build a uint8_t payload[], call send_typed_response(<this cmd>, payload, N), break.
  - ITEM (the exact block to insert, with its Mode-A comment):
        /* SET_OS (0x03) — host-authoritative OS while connected (§4.7). Routes through
         * apply_os_change — the SAME seam as notifier_set_os, so the F9 clear-on-change
         * is NOT duplicated here. os_byte mirrors os_variant_t (0=UNSURE, 1=LINUX,
         * 2=WINDOWS, 3=MACOS, 4=IOS). Response: [0x51][0x03][ack=1]. */
        case NOTIFY_CMD_SET_OS: {
            uint8_t os_byte = (uint8_t)data[2];
            apply_os_change((os_variant_t)os_byte);
            uint8_t payload[1] = { 0x01 };   /* ack = 1 (applied) */
            send_typed_response(NOTIFY_CMD_SET_OS, payload, 1);
            break;
        }
  - NAMING: os_byte (local, snake_case); payload (matches sibling cases); cmd echo via
    NOTIFY_CMD_SET_OS (the existing constant).
  - DEPENDENCIES: apply_os_change (P1.M1.T2.S2); send_typed_response (P1.M2.T2.S1);
    NOTIFY_CMD_SET_OS (notifier.h); os_variant_t/current_os (in scope).
  - DO NOT: add a `current_os =` write, an idempotency `if`, a disable_command()/
    deactivate_layer() call, a bounds check on data[2], a range guard on os_byte, a
    `return true;` (use break), or any #include.

Task 2: MODIFY notifier.c — update the default-case comment (drop the stale "0x03")
  - PLACE: the comment immediately above `default: {` in the same switch (~notifier.c:686).
  - CHANGE: "Default / unknown cmd_id (incl 0x04 reserved for VIA-coexist, and 0x03/0x05
             until P1.M2.T2.S2/S3 land): reply with just [0x51][cmd_id] ..."
         -> "Default / unknown cmd_id (incl 0x04 reserved for VIA-coexist, and 0x05 until
             P1.M2.T2.S3 lands): reply with just [0x51][cmd_id] ..."
  - PRESERVE: the default-case BODY (send_typed_response(cmd_id, NULL, 0); break;) is
    UNCHANGED — it remains the safe placeholder for 0x04 (reserved) and 0x05 (S3 pending).
  - WHY: keep the comment honest post-S2 (0x03 is now handled); S3 will later drop "0x05".

Task 3: VERIFY the build + behavior gates (run the Validation Loop, Levels 1-4).
```

### Implementation Patterns & Key Details

```c
// PATTERN: route through the shared seam (architecture F2). The handler is a THIN
//   adapter: wire byte -> enum -> seam -> response. Zero domain logic in the handler.
//     uint8_t os_byte = (uint8_t)data[2];                 // wire arg
//     apply_os_change((os_variant_t)os_byte);             // seam owns F9 + current_os
//     uint8_t payload[1] = { 0x01 };                      // ack=applied
//     send_typed_response(NOTIFY_CMD_SET_OS, payload, 1); // [0x51][0x03][0x01]

// PATTERN: mirror the sibling case structure. QUERY_CALLBACK is the template:
//   read (uint8_t)data[2]; build a stack uint8_t payload[]; call send_typed_response
//   with the case's own cmd constant; break; (the post-switch `return true;` covers it).
//   Copy that shape verbatim; only the body differs.

// PATTERN: stack-allocated fixed-size payload. `uint8_t payload[1] = {0x01};` — no
//   malloc, no dynamic size. send_typed_response copies it into the 32-byte response
//   and zero-pads. Matches QUERY_INFO (`uint8_t payload[4]`) and QUERY_CALLBACK
//   (`uint8_t payload[30]`).

// ANTI-PATTERN: do NOT duplicate F9 (idempotency check, current_os=, disable_command,
//   deactivate_layer). apply_os_change owns all of it. (invariant 17, finding F2.)

// ANTI-PATTERN: do NOT append the case after default. INSERT before default, or 0x03
//   still falls through to the placeholder.

// ANTI-PATTERN: do NOT add `return true;` inside the case. Use `break;` so the single
//   post-switch return is the only exit (matches QUERY_INFO/QUERY_CALLBACK).

// ANTI-PATTERN: do NOT validate os_byte range or data[2] bounds. The NUL terminator
//   makes data[2] always readable (malformed -> OS_UNSURE); out-of-range os_byte
//   resolves to the default-map fallback. Adding guards diverges from the sibling
//   cases and has no failure mode to prevent.

// ANTI-PATTERN: do NOT touch host_layer / host_cb_enabled / has_been_queried. SET_OS
//   updates board current_os only. (§4.7 orthogonality, invariant 21.)

// ANTI-PATTERN: do NOT leave the default-case comment stale. Update "0x03/0x05 ...
//   S2/S3" -> "0x05 ... S3" so the code reflects that 0x03 is now handled.
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - MODIFY notifier.c ONLY, inside handle_typed_command's switch:
      * INSERT case NOTIFY_CMD_SET_OS (0x03) between NOTIFY_CMD_QUERY_CALLBACK and default.
      * UPDATE the default-case comment (drop "0x03"; logic unchanged).
  - No new files; no edits to notifier.h, pattern_match.*, qmk_stubs/*, test_*,
    run_notifier_stub_tests.sh, PRD.md, tasks.json, prd_snapshot.md, .gitignore.

CONSUMERS (this task's output):
  - The desktop host (qmk_notifier crate) reads [0x51][0x03][ack] to confirm SET_OS applied.
  - Subsequent legacy-string dispatches consult current_os (now possibly host-set) via
    select_command_map_os / select_layer_map_os (unchanged).

SEAM REUSE (the core integration):
  - SET_OS -> apply_os_change (the SAME function notifier_set_os calls). One mutation
    point for current_os; F9 semantics never diverge between the two OS sources.

DOWNSTREAM HAND-OFFS (NOT this task):
  - P1.M2.T2.S3 (APPLY_HOST_CONTEXT, 0x05): inserts its case AFTER SET_OS, before
    default; updates the default comment to drop "0x05". The default then catches only
    0x04 (reserved) and truly-unknown ids.
  - P1.M3.T1.S1: adds stub_get_last_response() to qmk_stubs.c (my Level-2 /tmp harness
    uses a LOCAL raw_hid_send capture, NOT this accessor — no dependency on S1 of M3).
  - P1.M3.T1.S3: adds the committed SET_OS test cases to test_notifier_host.c.

BUILD:
  - No build-system change. Validate by stub-compiling notifier.c (Level 1), a /tmp
    #include "notifier.c" harness (Level 2), and the existing runner (Level 3).

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module; one switch-case insertion + a comment edit).
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc` with the QMK stub harness.
> `handle_typed_command` and `current_os` are `static`/file-scope (NOT exposed via any
> stub accessor), and `stub_get_last_response` does not exist yet (P1.M3.T1.S1). So the
> SET_OS case is validated by a `/tmp` `#include "notifier.c"` harness that provides its
> OWN local `raw_hid_send` (32-byte capture) + `layer_on`/`layer_off` — the SAME idiom the
> P1.M2.T2.S1 NFA-style research used. The harness skeleton was **compiled and run against
> the live `notifier.c`** during research (via the existing QUERY_INFO case) and is proven
> feasible; once the SET_OS case is added, the identical skeleton with a SET_OS buffer
> validates it.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Stub-compile notifier.c exactly as run_notifier_stub_tests.sh does.
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_s2.o
# Expected: exit 0. The ONLY permitted warnings are the carried -Wunused-function set
# for static helpers (process_escapes / nfa_* / select_*_map_os / match_with_anchors /
# pattern_char_matches / board_rules_present / apply_host_callbacks / set_host_layer /
# find_first_delimiter / split_by_delimiter, etc.) — these are PRE-EXISTING and unchanged.
# FAIL if: exit != 0, OR any NEW warning appears, OR a warning names a symbol this task
#           touched in a way it didn't before.

# 1b. Confirm the SET_OS case is present, before default, after QUERY_CALLBACK.
grep -nE 'case NOTIFY_CMD_SET_OS:' notifier.c
# Expected: exactly ONE line, whose line number is BETWEEN the NOTIFY_CMD_QUERY_CALLBACK
# case and the `default:` in handle_typed_command.

# 1c. Confirm the handler body (os_byte read, seam call, ack payload, response, break).
sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c | grep -qE 'uint8_t os_byte = \(uint8_t\)data\[2\]' \
  && sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c | grep -qE 'apply_os_change\(\(os_variant_t\)os_byte\)' \
  && sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c | grep -qE 'payload\[1\] = \{ 0x01 \}' \
  && sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c | grep -qE 'send_typed_response\(NOTIFY_CMD_SET_OS, payload, 1\)' \
  && echo "SET_OS body correct (ok)" || echo "ERROR: SET_OS body wrong"

# 1d. Confirm NO F9 duplication leaked into the SET_OS case (no current_os=, no disable/
#      deactivate, no idempotency if INSIDE the case).
sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c \
  | grep -qE 'current_os =|disable_command\(\)|deactivate_layer\(\)|if \(os_byte' \
  && echo "ERROR: F9 logic duplicated in SET_OS case (scope creep)" \
  || echo "no F9 duplication in SET_OS case (good)"

# 1e. Confirm the default-case comment was updated (0x03 dropped) AND its logic unchanged.
grep -nE 'default / unknown|0x05 until P1.M2.T2.S3|send_typed_response\(cmd_id, NULL, 0\)' notifier.c
# Expected: a comment line mentioning "0x05 until P1.M2.T2.S3" (NOT "0x03/0x05 ... S2/S3"),
# AND the unchanged default body send_typed_response(cmd_id, NULL, 0).

# 1f. Confirm Mode-A comment references §4.7 + the seam.
sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c | grep -qE '§4.7|host-authoritative' \
  && sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c | grep -qE 'apply_os_change|seam' \
  && echo "Mode-A comment present (ok)" || echo "WARN: Mode-A comment incomplete"

rm -f /tmp/notifier_s2.o
```

### Level 2: Component Tests (THE PRIMARY BEHAVIORAL GATE)

This `/tmp` harness reaches the static `handle_typed_command` + file-scope `current_os`
by `#include`-ing `notifier.c` directly and providing local QMK stubs (including a
`raw_hid_send` that captures the full 32-byte response). Its skeleton was **verified
compilable and runnable against the current `notifier.c`** during research (it exercised
the existing QUERY_INFO case and printed `resp=[51][01][02]`). Create it, run it, require
all-pass.

```bash
cd /home/dustin/projects/qmk-notifier

cat > /tmp/s2_set_os_test.c <<'EOF'
/* Reach the static handle_typed_command + file-scope current_os by including the .c.
 * Provides LOCAL raw_hid_send (32-byte capture) + layer_on/off so we do NOT link
 * qmk_stubs.c (avoids a duplicate raw_hid_send). */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static uint8_t g_resp[32];           /* captured 32-byte response       */
static uint8_t g_active_layer = 255; /* local layer tracker             */
static bool    g_send_called = false;
void raw_hid_send(uint8_t *data, uint8_t length) {
    (void)length; memcpy(g_resp, data, 32); g_send_called = true;
}
void layer_on(uint8_t layer)  { g_active_layer = layer; }
void layer_off(uint8_t layer) { (void)layer; g_active_layer = 255; }

#include "notifier.c"   /* brings handle_typed_command + current_os into scope */

static int failures = 0;
#define CK(cond) do { if (cond) printf("ok   %s\n", desc); \
  else { printf("FAIL %s\n", desc); failures++; } } while(0)

/* Build a reassembled SET_OS msg_buffer: [0xF0][0x03][os_byte][NUL] */
static void make_set_os(char *buf, uint8_t os_byte) {
    buf[0] = (char)0xF0; buf[1] = (char)NOTIFY_CMD_SET_OS; buf[2] = (char)os_byte; buf[3] = '\0';
}

int main(void) {
    /* 1. SET_OS LINUX: current_os changes (OS_UNSURE->LINUX) + response [0x51][0x03][0x01]. */
    { const char *desc = "SET_OS LINUX: current_os=LINUX, resp [0x51][0x03][0x01]";
      char buf[4]; make_set_os(buf, OS_LINUX);
      g_send_called = false;
      bool r = handle_typed_command(buf);
      CK(r && current_os == OS_LINUX && g_send_called
         && g_resp[0]==0x51 && g_resp[1]==0x03 && g_resp[2]==0x01);
    }
    /* 2. F9.1 clear-on-change: changing OS (LINUX->WINDOWS) deactivates the active layer. */
    { const char *desc = "SET_OS change clears active layer (F9.1)";
      g_active_layer = 7;                       /* pretend a board layer is active */
      char buf[4]; make_set_os(buf, OS_WINDOWS);
      handle_typed_command(buf);
      CK(current_os == OS_WINDOWS && g_active_layer == 255);   /* deactivated */
    }
    /* 3. F9.3 idempotent: same OS (WINDOWS again) does NOT clear the active layer. */
    { const char *desc = "SET_OS idempotent: same OS no clear (F9.3)";
      g_active_layer = 5;
      char buf[4]; make_set_os(buf, OS_WINDOWS);
      bool r = handle_typed_command(buf);
      CK(r && current_os == OS_WINDOWS && g_active_layer == 5);  /* NOT cleared */
    }
    /* 4. All five os_variant_t values (0..4) route through the handler. */
    { const char *desc = "SET_OS routes all os_variant_t values (0..4)";
      os_variant_t vals[5] = { OS_UNSURE, OS_LINUX, OS_WINDOWS, OS_MACOS, OS_IOS };
      bool all = true;
      for (int i = 0; i < 5; i++) {
          char buf[4]; make_set_os(buf, (uint8_t)vals[i]);
          g_send_called = false;
          handle_typed_command(buf);
          if (current_os != vals[i]) all = false;
          if (!(g_resp[0]==0x51 && g_resp[1]==0x03 && g_resp[2]==0x01)) all = false;
      }
      CK(all);
    }
    /* 5. Response is ALWAYS sent even when OS is unchanged (idempotent seam still acks). */
    { const char *desc = "SET_OS always replies [0x51][0x03][0x01] (even on no-op)";
      g_send_called = false;
      char buf[4]; make_set_os(buf, (uint8_t)current_os);   /* same as current */
      handle_typed_command(buf);
      CK(g_send_called && g_resp[0]==0x51 && g_resp[1]==0x03 && g_resp[2]==0x01);
    }

    printf("\n%s (%d failures)\n", failures ? "SOME FAILURES" : "ALL CASES CONFIRMED", failures);
    return failures ? 1 : 0;
}
EOF

gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    /tmp/s2_set_os_test.c -o /tmp/s2_set_os_test 2>&1 \
  | grep -vE 'defined but not used' | head -20
/tmp/s2_set_os_test
# Expected: a line of "ok" per check, then "ALL CASES CONFIRMED (0 failures)", exit 0.
# (The only permitted compiler output is the carried -Wunused-function set, filtered above.
#  The CRITICAL gates are case #1 (response + OS change) and case #3 (F9.3 idempotency).)

rm -f /tmp/s2_set_os_test.c /tmp/s2_set_os_test
```

### Level 3: Integration Testing (Regression — the existing runner)

```bash
cd /home/dustin/projects/qmk-notifier

# The existing stub-compile gate. SET_OS touches handle_typed_command (typed path),
# which the legacy dispatch/os tests do NOT exercise — so they must stay byte-for-byte
# green. This proves no regression in the reassembler, F4 delimiter matcher, F5 ordering,
# F8 multi-OS selection, F9 OS-clear, or the hid_notify routing fork.
./run_notifier_stub_tests.sh
# Expected: builds notifier.o + test_notifier_dispatch + test_notifier_os, runs both,
# prints "notifier dispatch fails=0 (exit=0)" and "notifier os fails=0 (exit=0)",
# then "✓ notifier stub-compile gate PASSED", exit 0.

# DO NOT expect test_notifier_host.c to exist or run — it is P1.M3.T1.S2 (Planned).
# The SET_OS behavior is fully covered by the Level-2 /tmp harness above until that lands.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Doc-contract check (item-spec §5 DOCS, Mode A): the SET_OS case carries a comment that
# names §4.7 (host-authoritative), the apply_os_change seam (not duplicating F9), and the
# os_variant_t mirror (0..4).
sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c | grep -qE '§4.7|host-authoritative' \
  && echo "§4.7 / host-authoritative reference present (ok)" \
  || echo "WARN: §4.7 reference missing"
sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c | grep -qE 'apply_os_change|same seam|not.*duplicate|not.*duplicat' \
  && echo "seam / no-F9-duplication rationale present (ok)" \
  || echo "WARN: seam rationale missing"
sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c | grep -qiE 'os_variant_t|0=UNSURE|UNSURE|MACOS|IOS' \
  && echo "os_variant_t mirror documented (ok)" \
  || echo "WARN: os_variant_t mapping not documented"

# Scope-creep guard: the SET_OS case must NOT touch host state or duplicate F9.
sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c \
  | grep -qE 'host_layer|host_cb_enabled|has_been_queried|set_host_layer|apply_host_callbacks' \
  && echo "ERROR: SET_OS touched host state (scope creep)" || echo "no host-state touch (good)"
sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c \
  | grep -qE 'current_os =|disable_command\(\)|deactivate_layer\(\)' \
  && echo "ERROR: F9 logic duplicated in SET_OS (scope creep)" || echo "no F9 duplication (good)"

# Wire-contract check: the response echoes cmd_id 0x03 with ack 0x01.
sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c | grep -qE 'send_typed_response\(NOTIFY_CMD_SET_OS, payload, 1\)' \
  && sed -n '/case NOTIFY_CMD_SET_OS:/,/^        }/p' notifier.c | grep -qE 'payload\[1\] = \{ 0x01 \}' \
  && echo "wire response [0x51][0x03][0x01] constructed (ok)" \
  || echo "ERROR: response construction wrong"

# Orthogonality: SET_OS is the ONLY typed handler that touches board state (current_os).
# Confirm APPLY_HOST_CONTEXT is NOT implemented by this task (S3's job) — it should still
# fall through to default.
grep -nE 'case NOTIFY_CMD_APPLY_HOST_CONTEXT:' notifier.c \
  && echo "ERROR: APPLY_HOST_CONTEXT leaked into this task (S3 scope)" \
  || echo "APPLY_HOST_CONTEXT not implemented here (good — S3's job)"

# default-case comment honesty: 0x03 must be gone, 0x05 must remain (pending S3), logic
# unchanged.
awk '/default: \{/{f=1} f&&/break;/{print; exit} f' notifier.c | grep -q 'send_typed_response(cmd_id, NULL, 0)' \
  && echo "default logic unchanged (ok)" || echo "ERROR: default logic changed"
grep -nE 'Default / unknown cmd_id' notifier.c | grep -q '0x05 until P1.M2.T2.S3' \
  && grep -nE 'Default / unknown cmd_id' notifier.c | grep -qv '0x03/0x05' \
  && echo "default comment honest post-S2 (ok)" || echo "WARN: default comment not updated"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: stub-compile `gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c`
      → exit 0; **no new warnings** vs pre-task baseline (carried `-Wunused-function` set only).
- [ ] Level 1: `case NOTIFY_CMD_SET_OS:` present exactly once, between `NOTIFY_CMD_QUERY_CALLBACK`
      and `default:` in `handle_typed_command`.
- [ ] Level 2: `/tmp/s2_set_os_test` prints "ALL CASES CONFIRMED (0 failures)", including the
      **response `[0x51][0x03][0x01]`** (case #1) and **F9.3 idempotency** (case #3: same OS
      does not clear the active layer).
- [ ] Level 3: `./run_notifier_stub_tests.sh` → dispatch fails=0, os fails=0, exit 0 (no regression).

### Feature Validation

- [ ] SET_OS routes `os_byte` through `apply_os_change((os_variant_t)os_byte)` — the same seam
      `notifier_set_os` uses — so F9 clear-on-change is not duplicated.
- [ ] Response is exactly `[0x51][0x03][0x01]` (ack=1 applied).
- [ ] An OS **change** clears the active layer + fires the prev command's on_disable (F9.1);
      an **unchanged** OS is a no-op (F9.3). (Both via `apply_os_change`, not in the handler.)
- [ ] All five `os_variant_t` values (0..4) set `current_os` correctly.
- [ ] `default:` still replies `[0x51][cmd_id]` (no payload) for 0x04 (reserved) and 0x05
      (pending S3) — logic unchanged.

### Code Quality Validation

- [ ] Mirrors the `QUERY_CALLBACK` case structure (read data[2], build payload, send, break).
- [ ] APPEND/INSERT ONLY into `handle_typed_command` + one comment edit to `default:`; nothing
      else in `notifier.c` is touched.
- [ ] No new files, constants, functions, globals, or `#include`s.
- [ ] Anti-patterns avoided: no F9 duplication, no `return true` inside the case (uses `break`),
      no os_byte range guard, no host-state touch, no bounds check on data[2].
- [ ] APPEND/INSERT only — no modification to `pattern_match.{c,h}`, `notifier.h`, `qmk_stubs/*`,
      `test_*.c`, `run_notifier_stub_tests.sh`, `PRD.md`, `tasks.json`, `prd_snapshot.md`,
      `.gitignore`.

### Documentation & Deployment

- [ ] Mode-A comment on the SET_OS case names §4.7 (host-authoritative), the `apply_os_change`
      seam (no F9 duplication), and the `os_variant_t` mirror (0..4).
- [ ] `default:` comment updated to drop the stale "0x03" (now reads "0x05 until P1.M2.T2.S3 lands").
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't duplicate F9 — no `current_os =`, no `disable_command()`, no `deactivate_layer()`,
  no idempotency `if` in the SET_OS case. `apply_os_change` owns all of it (invariant 17, F2).
- ❌ Don't append the case after `default:` — INSERT before `default:` or 0x03 still hits the
  placeholder. Place it between `NOTIFY_CMD_QUERY_CALLBACK` and `default:`.
- ❌ Don't add `return true;` inside the case — use `break;` (the single post-switch `return true;`
  is the only exit, matching QUERY_INFO/QUERY_CALLBACK).
- ❌ Don't validate `os_byte` range or `data[2]` bounds — the NUL terminator makes `data[2]`
  always readable (malformed → OS_UNSURE), and out-of-range bytes resolve to the default-map
  fallback. Guards diverge from the sibling cases and prevent nothing.
- ❌ Don't touch host state (`host_layer`, `host_cb_enabled`, `has_been_queried`,
  `set_host_layer`, `apply_host_callbacks`) — that's APPLY_HOST_CONTEXT's job (§4.7 orthogonality).
- ❌ Don't implement APPLY_HOST_CONTEXT (0x05) here — it's P1.M2.T2.S3. Leave it falling through
  to `default:` (safe placeholder).
- ❌ Don't add `stub_get_last_response` to `qmk_stubs.c` — that's P1.M3.T1.S1. The Level-2 gate
  uses a LOCAL `raw_hid_send` capture in a `/tmp` throwaway harness.
- ❌ Don't leave the `default:` comment stale — update "0x03/0x05 ... S2/S3" → "0x05 ... S3".
- ❌ Don't add `#include`s — `apply_os_change`, `send_typed_response`, `NOTIFY_CMD_SET_OS`,
  `os_variant_t`, `current_os` are all already in scope.
- ❌ Don't touch `notifier.h`, `pattern_match.*`, `qmk_stubs/*`, `test_*.c`,
  `run_notifier_stub_tests.sh`, `PRD.md`, `tasks.json`, `prd_snapshot.md`, or `.gitignore`.

---

## Confidence Score: 10/10

The handler is a 7-line insertion + a one-line comment edit into a switch whose structure,
call targets, constants, and types are **all already present in the current `notifier.c`**
(verified by direct read): `handle_typed_command` (static) with `QUERY_INFO`/`QUERY_CALLBACK`
as the exact structural template and `default:` as the insertion anchor; `apply_os_change`
(static, P1.M1.T2.S2 COMPLETE) as the F9 seam to call; `send_typed_response` (static,
P1.M2.T2.S1) as the response builder; the `NOTIFY_CMD_SET_OS`/`os_variant_t`/`current_os`
symbols all in scope. The `os_variant_t` mapping (0..4) is confirmed in
`qmk_stubs/os_detection.h`; the wire response `[0x51][0x03][0x01]` follows directly from §4.6
+ the `send_typed_response` layout. The **single non-obvious requirement** — that `SET_OS`
must route through `apply_os_change` rather than re-implementing F9 (architecture finding F2,
invariant 17) — is stated in the PRD (§4.7), the architecture doc (SET_OS section), the
findings (F2), and the seam's own PRP (P1.M1.T2.S2). The **Level-2 `#include "notifier.c"`
validation harness was compiled and run against the live `notifier.c` during research**
(exercising the existing QUERY_INFO case: it printed `resp=[51][01][02]`, proving the harness
reaches the static dispatcher, reads file-scope `current_os`, and captures the full 32-byte
response) — so the identical skeleton with a SET_OS buffer is proven feasible. Dependencies on
P1.M1.T2.S2 (`apply_os_change` — treated as a CONTRACT, called not modified) and P1.M2.T2.S1
(the switch skeleton + `send_typed_response` — treated as a CONTRACT, inserted into not
rewritten) are explicit, and the hand-off to P1.M2.T2.S3 (APPLY_HOST_CONTEXT inserts after
SET_OS) and P1.M3.T1.S1 (`stub_get_last_response`, not depended on) is documented. No external
dependencies are needed (libc + the in-repo stubs only).