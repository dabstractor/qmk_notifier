# DELTA PRD — Host-Side Rules & Typed Commands (003)

**Target repo:** `qmk-notifier` (firmware C module). Builds on the multi-OS
codebase shipped by plan 002 (P1 complete). This delta promotes the previously
**HELD/planned** host-rules work to **implemented**.

---

## 1. Diff Analysis — what ACTUALLY changed

Comparing the Previous PRD (plan 002, post-multi-OS) to the Current PRD, the
substantive changes cluster in five places. Two are **doc-only truth-ups with
zero code/test impact**; three are a **single large feature** implemented end to end.

### Doc-only / awareness (NO tasks — code already correct)

- **§4.4 + F6.1 — the acknowledgement is received, not dropped.** Previous PRD
  said QMK silently drops the ack (`length == RAW_EPSIZE` guard); Current PRD
  corrects this: the reply is a full 32-byte report and the host receives it,
  with a historical note pinning the resolution to commit `01a51935`. **Verified:
  `notifier.c:539-540` already sends `response[0] = match; raw_hid_send(response,
  RAW_REPORT_SIZE)` with `RAW_REPORT_SIZE = 32`.** No code change. Spec text only.
- **§11.2C — `user_host` → `user@host`.** The Previous PRD's `/tmp/nfa_real`
  probe had a wrong example (`^\w+@\w+$` cannot match `user_host` — `_` is `\w`,
  there is no literal `@`; result would be `0`, not the annotated `1`). Current
  PRD fixes the example to `user@host`. **Verified: the committed
  `test_pattern_match.c:565-568` already encodes the CORRECT semantics**
  (`user@host` matches; `user_host` does not). The /tmp probe is throwaway; the
  committed corpus is already right. Spec text only.

### The feature delta (LARGE — this is the work)

The Previous PRD held three areas as "planned / next cycle / NOT implemented".
The Current PRD promotes all three to **canonical, implemented specifications**:

1. **§4.6 — Typed-command namespace** (was "Planned (v0.3.0, NOT now)"). Now the
   canonical wire contract: `data[2] == 0xF0` discriminator; ETX-framed commands
   that may span reports; typed responses `[0x51][cmd_echo][payload]`; the command
   table `QUERY_INFO=0x01 / QUERY_CALLBACK=0x02 / SET_OS=0x03 / APPLY_HOST_CONTEXT=0x05`;
   field definitions (`proto_ver`, `feature_flags`, `callback_count`,
   `board_rules_present`, `os_byte`, host `layer`/`clear_board`, callback-id set);
   the capability handshake + `has_been_queried` timing rule; legacy-fallback
   behavior.
2. **§4.7 — Host-authoritative OS via `SET_OS`** (was "RESERVED next cycle").
   While a host is connected and has sent `SET_OS`, the host's value is
   authoritative for `current_os`, taking precedence over the `OS_DETECTION`
   heuristic. `SET_OS` updates `current_os` through the same seam as
   `notifier_set_os` (so an OS change clears notifier state per F9).
3. **§14 — Host-Side Rules & Typed Commands** (was "Planned Future, NOT
   implemented"). The firmware's role: receive typed commands, expose a named
   callback registry (`DEFINE_HOST_CALLBACKS`), track a `host_layer` + host-callback
   set **separate from board state**, and honor a per-window **stack/replace**
   decision via the `clear_board` flag on `APPLY_HOST_CONTEXT`.

**Magnitude:** ~130 lines of spec rewritten across §4.4/§4.6/§4.7/§14/§16/DoD +
~2 intro lines. The implementation adds a typed-command dispatch layer, four
command handlers, host-side state machines (host layer + callback diff), a new
header API, host test coverage, and README docs. **This is a large feature →
full PRD structure** (1 phase, 3 milestones).

### Scope NOT touched by this delta (preserve as-is)

- `pattern_match.{c,h}` and `rules.mk` — untouched (the matcher is the single
  source of truth for match semantics; the host-side matcher lives in
  `qmkonnect`, §14 note).
- The wire framing for **legacy strings** (§4.1–4.3, §4.5) — unchanged. Host rules
  stack/replace *on top of* the unchanged legacy string path.
- The multi-OS selection (§2 F8/F9, plan 002) — `current_os`/`notifier_set_os`/
  per-OS maps are reused unchanged; `SET_OS` only changes the *source* of
  `current_os` via the existing seam.
- The 9 `pattern_match` test suites — untouched regression gate.

---

## 2. Scope of This Delta

### IN scope (tasks below)
- `notifier.h`: `host_callback_t` struct, `DEFINE_HOST_CALLBACKS` macro +
  accessor decls, and the typed-command constants (discriminator `0xF0`,
  response marker `0x51`, command IDs, `proto_ver`/`feature_flags` semantics,
  `HOST_CALLBACK_MAX`, host-layer block ≥ 224).
- `notifier.c`: `host_layer` tracker + `host_cb_enabled[]` set + `has_been_queried`
  bool; weak-default host-callback accessors; `board_rules_present` helper;
  `set_host_layer()` (host tracker only); `apply_host_callbacks()` (disable-before-
  enable diff); `hid_notify()` typed routing + multi-report framing;
  `handle_typed_command()` + the four handlers + typed-response builder; `SET_OS`
  authoritative-OS path.
- `qmk_stubs/`: extend `raw_hid_send` to capture the full 32-byte response so host
  tests can verify the `0x51` marker + payload (precedent: the existing
  `stub_get_active_layer()` test-harness observable).
- New host test suite `test_notifier_host.c` (typed commands + host rules).
- `run_notifier_stub_tests.sh`: build+run the new suite.
- `README.md`: changeset-level docs (Mode B).

### Awareness / no-op (no tasks)
- §4.4 / F6.1 ack correction — code already correct (verified §1).
- §11.2C `user_host`→`user@host` — committed corpus already correct (verified §1).

### Removed requirements (none)
The Previous PRD's §14.1 "divergence (stack vs replace) — OPEN" is **resolved**
by the Current PRD §14: it is per-window via `clear_board`, driven by the host's
per-rule `disable_firmware_config`. The old "global replace mode" and the
`dabstractor/qmkonnect/PRPs/002` artifact are explicitly superseded (Current §14
"Resolved design decisions"). No firmware work for the *removal* — just implement
the per-window design.

---

## 3. Backlog (Phase / Milestones / Tasks)

> **Prior research reuse (plan 002):** `plan/002_c243e735980a/architecture/`
> findings still apply and are NOT re-derived here:
> - **F1** (the `##os` / weak-symbol naming contract) → the new
>   `DEFINE_HOST_CALLBACKS` follows the SAME weak-default override pattern.
> - **F3** (backward-compat is structural, no `#ifdef`) → a keymap with no
>   `DEFINE_HOST_CALLBACKS` links against the weak `{NULL,0}` accessors; legacy
>   strings never have `data[2]==0xF0` so typed routing is transparent.
> - **F4** (stub `os_detection.h` stays minimal) → unchanged.
> - **F6** (distinguishable callback flags + a stub accessor for observation) →
>   the host-rules tests reuse this observation strategy and extend the stub's
>   `raw_hid_send` capture.
> - **external_deps.md** (`os_variant_t` enum values, QMK hook signatures) →
>   reused verbatim for `SET_OS.os_byte` mapping.
>
> **NEW research for this delta** is minimal because the Current PRD §4.6/§4.7/§14
> is now the **canonical, authoritative** spec — the breakdown agent implements to
> it directly. The only genuinely new ground is the stub response-capture pattern
> (extending the `stub_get_active_layer` precedent to the full 32-byte response).

---

### Phase P1 — Host-Side Rules & Typed Commands

**Goal:** Implement the typed-command namespace (§4.6), host-authoritative `SET_OS`
(§4.7), and the host-side-rules machinery (§14) in `notifier.{c,h}`, with host
test coverage and README docs — while leaving the legacy string path, the pattern
matcher, multi-OS selection, and all prior tests byte-for-byte unchanged.

#### Milestone P1.M1 — Public API surface + state scaffolding

The header additions (`host_callback_t`, `DEFINE_HOST_CALLBACKS`, typed-command
constants) and the `notifier.c` globals/weak-accessors that the dispatch logic
(P1.M2) consumes. After this milestone the module compiles with the new surface
but typed commands are not yet dispatched.

**Task P1.M1.T1 — `notifier.h` host-rules API surface**
- **S1 — Add `host_callback_t`, host-callback accessors, `DEFINE_HOST_CALLBACKS`
  macro, and typed-command constants to `notifier.h`** (story_points: 1; deps: none)
  - **INPUT:** Current PRD §5 (header), §14 (firmware requirements), §4.6 (command
    table / field defs), §16 (constants). The existing `notifier.h` (91 lines) has
    `command_map_t`/`layer_map_t`, `DEFINE_SERIAL_*(_OS)`, `notifier_set_os`, `WT`.
  - **LOGIC:** (a) Add `typedef struct { const char *name; callback_t on_enable;
    callback_t on_disable; } host_callback_t;` and the accessor declarations
    `host_callback_t* get_host_callbacks(void); size_t get_host_callbacks_size(void);`
    (PRD §14). (b) Add `DEFINE_HOST_CALLBACKS(...)` macro mirroring
    `DEFINE_SERIAL_COMMANDS`: defines `user_host_callbacks[]` + the matching
    accessor pair, overriding the weak defaults in `notifier.c` (§14). (c) Add the
    typed-command constants block: `#define NOTIFY_CMD_DISCRIMINATOR 0xF0`,
    `#define NOTIFY_RESPONSE_MARKER 0x51`, `#define NOTIFY_CMD_QUERY_INFO 0x01`,
    `#define NOTIFY_CMD_QUERY_CALLBACK 0x02`, `#define NOTIFY_CMD_SET_OS 0x03`,
    `#define NOTIFY_CMD_APPLY_HOST_CONTEXT 0x05`, `#define NOTIFY_PROTO_VER 2`,
    feature-flag bits (`0x01` = APPLY_HOST_CONTEXT supported, `0x02` = callback
    registry present, `0x04` reserved), `#define HOST_CALLBACK_MAX 32` (static
    array bound; size per `QUERY_INFO.callback_count` reporting the true count),
    `#define HOST_LAYER_BASE 224` (host layers ≥ 224 resolve above board layers;
    255 = `LAYER_UNSET`). `callback_t` is already typedef'd in this header.
  - **OUTPUT:** A keymap writing `DEFINE_HOST_CALLBACKS({ … })` at file scope
    generates `user_host_callbacks[]` + the strong accessors that override
    `notifier.c`'s weak defaults at link time. A keymap that omits it links and
    behaves identically to today (structural backward-compat, prior finding F3).
  - **DOCS (Mode A — rides with this task):** inline comments on each constant
    block citing its PRD section; a comment on `DEFINE_HOST_CALLBACKS` noting
    `ID = array index`, stable per build, and that the host re-queries names on
    reconnect so renumbering across flashes is harmless (§14).
  - **MOCKING:** pure header — compiles in the QMK build and the `-Iqmk_stubs` host
    harness unchanged.

**Task P1.M1.T2 — `notifier.c` state scaffolding + weak host-callback accessors**
- **S1 — Add `host_layer`, `host_cb_enabled[]`, `has_been_queried`, weak
  host-callback accessors, and `board_rules_present` helper** (story_points: 2;
  deps: P1.M1.T1.S1)
  - **INPUT:** The `host_callback_t` type + accessors + constants from P1.M1.T1.S1.
    Current PRD §14 (firmware requirements), §8.1 (globals region), §4.6
    (`board_rules_present`, `has_been_queried`).
  - **LOGIC:** (a) Globals near `activated_layer`/`current_os` (§8.1 region):
    `static uint8_t host_layer = LAYER_UNSET;`, `static bool host_cb_enabled[HOST_CALLBACK_MAX];`
    (zero-initialized), `static bool has_been_queried = false;`. (b) Weak-default
    host-callback accessors mirroring the existing board-map weak block (§8.3 /
    prior finding F1 pattern): `__attribute__((weak)) host_callback_t*
    get_host_callbacks(void) { return NULL; }` and `__attribute__((weak)) size_t
    get_host_callbacks_size(void) { return 0; }`. (c) A `static bool
    board_rules_present(void)` helper returning true iff **any** board map (default
    command/layer OR any per-OS command/layer) is non-empty — drives the
    `QUERY_INFO.board_rules_present` bit (§4.6). Implementation: `get_command_map_size()>0
    || get_layer_map_size()>0 || <each OS accessor size>0`. (Do NOT expose per-OS
    granularity — §4.6 "a single bit suffices".)
  - **OUTPUT:** The globals/accessors exist and compile; `host_cb_enabled[]` is the
    single source of truth for which host callbacks are currently enabled (the diff
    target of `apply_host_callbacks` in P1.M2.T1). `board_rules_present()` is
    callable by `QUERY_INFO` (P1.M2.T2). Weak defaults keep a no-callback keymap
    linking + behaving identically (prior finding F3).
  - **DOCS (Mode A):** comment each global with its PRD anchor (`host_layer`
    independent of board `activated_layer`, §14; `has_been_queried` per §4.6
    handshake-timing rule). Comment the weak accessor block: "overridden by
    `DEFINE_HOST_CALLBACKS`; `{NULL,0}` ⇒ no registry ⇒ `feature_flags & 0x02` clear".
  - **MOCKING:** host stub harness already provides `layer_on/off`, `raw_hid_send`,
    `os_variant_t`. No new stub needed here.

#### Milestone P1.M2 — Typed dispatch + host state machines

The meaty implementation: route `0xF0` commands in `hid_notify`, the host state
machines (`set_host_layer`, `apply_host_callbacks`), and the four typed-command
handlers + response builder.

**Task P1.M2.T1 — `hid_notify` typed routing + multi-report framing +
`set_host_layer` + `apply_host_callbacks`**
- **S1 — Typed routing/framing in `hid_notify`, plus the host layer/callback state
  machines** (story_points: 3; deps: P1.M1.T2.S1)
  - **INPUT:** Current PRD §4.6 (framing: ETX-framed, may span reports,
    discriminator `0xF0` in the first report; bypass of `process_full_message`),
    §8.8 (`hid_notify` entry point), §14 (`set_host_layer`, `apply_host_callbacks`,
    `clear_board`, host-layer independence). Current `hid_notify` (notifier.c:~490-540):
    coexistence guard → `data+=2; length-=2` → byte loop into `msg_buffer` until
    ETX → `process_full_message` → 32-byte ack.
  - **LOGIC:**
    (a) **Routing + framing.** The discriminator lives at `data[2]` (the byte after
      the magic header) on the **first** report of a message. Track a per-message
      `static bool typed_mode` flag. At the top of `hid_notify`, after the
      coexistence guard and **before** stripping, when starting a fresh message
      (`msg_index == 0`): if `length >= 3 && data[2] == 0xF0`, set `typed_mode =
      true`. Strip the 2-byte magic header as today and accumulate payload bytes
      into `msg_buffer` until ETX (same reassembly + overflow/`dropping` rules,
      prior finding: the existing reassembly already handles multi-report). On ETX:
      if `typed_mode`, dispatch to `handle_typed_command(msg_buffer)` (P1.M2.T2) and
      send the **typed** response; else run the unchanged legacy
      `process_full_message` path and send the legacy `0`/`1` ack. Reset
      `typed_mode = false` at the ETX message boundary. The response-sending for
      the typed path is owned by P1.M2.T2 (the handler builds `[0x51][…]`);
      `hid_notify` here just chooses the branch.
    (b) **`set_host_layer(uint8_t layer)`** (§14): operates on `host_layer` ONLY —
      `layer_on`/`layer_off` the host tracker, never the board `activated_layer`.
      `0xFF` ⇒ clear (`layer_off` the old `host_layer`; `host_layer = LAYER_UNSET`).
      Otherwise `layer_off(host_layer)` (if set) then `layer_on(layer)`; record
      `host_layer = layer`. Guard `LAYER_UNSET`/no-op.
    (c) **`apply_host_callbacks(const uint8_t *ids, uint8_t count)`** (§14): mirror
      the board's disable-before-enable ordering. Diff `ids[0..count)` against
      `host_cb_enabled[]`: for each currently-enabled id NOT in the desired set,
      call its `on_disable` and clear the flag (**disable phase first**); then for
      each desired id NOT currently enabled, call its `on_enable` and set the flag
      (**enable phase**). Resolve id → `host_callback_t*` via `get_host_callbacks()`
      (skip ids `>= get_host_callbacks_size()` defensively). NULL `on_enable`/
      `on_disable` guarded (same pattern as `enable_command`/`disable_command`,
      notifier.c current code).
  - **OUTPUT:** `0xF0` messages bypass `process_full_message` (no board
    disable/deactivate side effects on a capable firmware — §4.6 / §14). Legacy
    string messages (`data[2]` printable, never `0xF0`) take the unchanged path —
    `test_notifier_dispatch` and `test_notifier_os` pass **unchanged** (prior
    finding F5 extended: existing suites never send `0xF0`, verified). Multi-report
    typed commands reassemble via the existing `msg_buffer` machinery + the
    `typed_mode` flag.
  - **DOCS (Mode A):** comment the routing branch citing §4.6 (discriminator, ETX
    framing, bypass of board side effects) and the `typed_mode`/`msg_index==0`
    first-report rule. Comment `set_host_layer` (host tracker only, independent of
    board layer, 0xFF clears) and `apply_host_callbacks` (disable-before-enable
    diff, §14).
  - **MOCKING:** stub harness unchanged; the typed tests (P1.M3.T1) drive
    `hid_notify` with `0xF0` reports and read the captured response.

**Task P1.M2.T2 — `handle_typed_command` + the four handlers + typed responses**
- **S1 — Implement `handle_typed_command` + `QUERY_INFO`/`QUERY_CALLBACK`/`SET_OS`/
  `APPLY_HOST_CONTEXT` + the `[0x51]` response builder** (story_points: 3; deps:
  P1.M2.T1.S1)
  - **INPUT:** Current PRD §4.6 (command table, field definitions, responses,
    handshake, `has_been_queried`, legacy fallback), §4.7 (`SET_OS` authoritative
    while host connected; same seam as `notifier_set_os` ⇒ F9 clear on change),
    §14 (`clear_board` + `APPLY_HOST_CONTEXT`). The reassembled `msg_buffer`
    layout for a typed command: `msg_buffer[0]==0xF0`, `[1]==cmd_id`, `[2..]==args`,
    NUL-terminated after ETX strip (built by P1.M2.T1).
  - **LOGIC:**
    (a) **Response builder:** a helper that fills a 32-byte `response` with
      `response[0]=0x51`, `response[1]=cmd_id_echo`, `response[2..]=payload` (zero-
      padded), then `raw_hid_send(response, RAW_REPORT_SIZE)`. The `0x51` marker
      (≥2) is distinct from the legacy `0`/`1` ack (§4.6 responses).
    (b) **`QUERY_INFO` (0x01):** set `has_been_queried = true` on first service
      (§4.6 handshake timing). Respond `[0x51][0x01][proto_ver=NOTIFY_PROTO_VER][feature_flags][callback_count][board_rules_present]`.
      `feature_flags`: `0x01` if APPLY_HOST_CONTEXT supported (always set for this
      build), `0x02` if `get_host_callbacks_size()>0`, `0x04` reserved(0).
      `callback_count = get_host_callbacks_size()`. `board_rules_present` from the
      P1.M1.T2 helper.
    (c) **`QUERY_CALLBACK` (0x02):** args `[index]`. If `index <
      get_host_callbacks_size()`, respond `[0x51][0x02][index][name bytes,
      NUL-padded to fill]`; else `[0x51][0x02][index][0x00]` (name absent, §4.6).
    (d) **`SET_OS` (0x03):** args `[os_byte]` (`0 UNSURE · 1 LINUX · 2 WINDOWS · 3
      MACOS · 4 IOS`, mirrors `os_variant_t`). Authoritative while host connected
      (§4.7). Update `current_os` **through the same seam as `notifier_set_os`** so
      an OS **change** clears notifier state per F9 (disable command + deactivate
      layer) before recording the new OS. Simplest: a single internal
      `apply_os_change(os_variant_t)` that both `notifier_set_os` and `SET_OS` call
      (idempotent + clear-on-change) — do NOT duplicate the F9 logic. Respond
      `[0x51][0x03][ack=1]`.
    (e) **`APPLY_HOST_CONTEXT` (0x05):** args `[layer][flags][count][id0][id1]…`.
      If `flags & 0x01` (**`clear_board`**): `deactivate_layer()` (board) +
      `disable_command()` (board) BEFORE applying — this is the per-window "replace"
      decision (§14); else ("stack") leave board state untouched. Then
      `set_host_layer(layer)` (`0xFF` clears the host layer) and
      `apply_host_callbacks(&ids, count)`. Respond `[0x51][0x05][ack=1]`.
    (f) **Unknown `cmd_id` / `0x04` (reserved):** respond `[0x51][cmd_id]` with no
      payload (or a defined no-op ack); never crash.
  - **OUTPUT:** All four handlers implemented; typed responses carry the `0x51`
    marker; `QUERY_INFO` advertises `proto_ver=2` + feature flags;
    `has_been_queried` flips on first `QUERY_INFO`; `SET_OS` is host-authoritative
    and clears state on change; `APPLY_HOST_CONTEXT` honors `clear_board`
    (replace) vs stack. Against a legacy (string-only) caller, `data[2]` is never
    `0xF0`, so none of this path runs — full backward compatibility (§4.6 capability
    handshake note: the host treats `response[0] != 0x51` / timeout as legacy).
  - **DOCS (Mode A):** comment `handle_typed_command` and each handler with its
    §4.6/§14 anchor, the response byte layout, and the `clear_board`/stack-vs-
    replace semantics. Comment the `SET_OS` seam-sharing with `notifier_set_os`
    (so F9 clear is not duplicated).
  - **MOCKING:** stub harness; typed tests (P1.M3.T1) assert response bytes.

#### Milestone P1.M3 — Host tests + acceptance gate + changeset docs

Dedicated host test coverage for the typed-command namespace + host rules, the
extended stub harness, the runner, the full §11.2 acceptance gate, and the
README sync.

**Task P1.M3.T1 — Stub response capture + `test_notifier_host.c`**
- **S1 — Extend `qmk_stubs.c` to capture the full 32-byte response; add
  `stub_get_last_response()`** (story_points: 1; deps: P1.M2.T2.S1)
  - **INPUT:** Current `qmk_stubs/qmk_stubs.c` (26 lines): `raw_hid_send` prints
    only `response[0]`. Prior finding F6 precedent: `stub_get_active_layer()` was
    added as a test-harness observable.
  - **LOGIC:** Add a file-static `g_last_response[32]` in `qmk_stubs.c`; have
    `raw_hid_send` `memcpy` its `data[0..31]` into it; expose `const uint8_t*
    stub_get_last_response(void)` (and/or `stub_get_response_byte(idx)`). This is a
    **test-harness enhancement**, NOT production code (Mode A doc, mirrors
    `stub_get_active_layer`).
  - **OUTPUT:** Host tests can assert the typed-response marker `0x51`, the
    `cmd_echo`, and the payload bytes (e.g. `QUERY_INFO`'s
    `proto_ver/flags/count/board_rules_present`).
  - **DOCS (Mode A):** comment the capture as a test-harness observable (not
    production); cite the F6 precedent.
  - **MOCKING:** this IS the stub enhancement.

- **S2 — Write `test_notifier_host.c` covering the typed-command namespace +
  host rules** (story_points: 3; deps: P1.M3.T1.S1)
  - **INPUT:** The full `notifier.c` implementation from P1.M2 (routing, framing,
    handlers, state machines). `DEFINE_HOST_CALLBACKS` from P1.M1.T1. The stub
    response capture from S1.
  - **LOGIC:** Create `test_notifier_host.c` that stub-links `notifier.c` (same
    `-DQMK_KEYBOARD_H / -Iqmk_stubs` flags) and follows the EXACT
    `ck(...)`/`PASS:`/`FAIL:`/summary pattern of `test_notifier_dispatch.c` /
    `test_notifier_os.c` (so the runner's `grep -c '^FAIL:'` works). Define a
    `DEFINE_HOST_CALLBACKS({ … })` at file scope (distinguishable callbacks that
    set global flags, prior finding F6). Cover, at minimum:
    (i) **`QUERY_INFO`** returns `proto_ver=2`, correct `feature_flags`
      (`0x01|0x02`), `callback_count`, `board_rules_present` (1 with the test's
      `DEFINE_SERIAL_*`, 0 without); `has_been_queried` flips; response marker `0x51`,
      cmd_echo `0x01`.
    (ii) **`QUERY_CALLBACK`** returns the correct name bytes (NUL-padded) for valid
      indices and `[index][0x00]` for out-of-range.
    (iii) **`SET_OS`** updates `current_os` authoritatively — verify via a subsequent
      legacy string dispatch that the OS map for the SET_OS value is selected (the
      `DEFINE_*_OS` maps from the existing pattern); verify a **change** fires F9
      clear (prev command's `on_disable`, board layer off) and an unchanged value is
      idempotent (no spurious clear). Response marker `0x51`, cmd_echo `0x03`, ack=1.
    (iv) **`APPLY_HOST_CONTEXT` stack vs replace:** with `clear_board=0` (stack), a
      board layer/command set by a prior legacy string remains active and the host
      layer stacks above (distinct layer index ≥ 224 via `stub_get_active_layer`);
      with `clear_board=1` (replace), the board layer is deactivated and board
      command disabled before the host layer applies. Verify
      `apply_host_callbacks` disable-before-enable diff ordering (newly-out ids'
      `on_disable` fire before newly-in ids' `on_enable`).
    (v) **Coexistence / backward-compat:** a legacy string report (`data[2]`
      printable, never `0xF0`) still routes to `process_full_message` and produces
      the legacy `0`/`1` ack (not `0x51`) — i.e. `test_notifier_dispatch`-style
      behavior is intact alongside the typed path.
    (vi) **Multi-report typed framing:** a typed command split across two 32-byte
      reports (discriminator `0xF0` only in the first) reassembles and dispatches
      correctly (exercised with a large `APPLY_HOST_CONTEXT` id list or a long
      `QUERY_CALLBACK` echo).
  - **OUTPUT:** `test_notifier_host.c` compiles with `gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"'
    -Iqmk_stubs -I. notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c -std=c99`
    and all categories pass with 0 `FAIL:` lines; summary prints totals.
  - **DOCS (Mode A):** header comment mapping each test block to its §4.6/§4.7/§14
    criterion.
  - **MOCKING:** the stub harness IS the mock; `DEFINE_HOST_CALLBACKS` at file scope
    overrides the weak default for this test TU only.

**Task P1.M3.T2 — Extend runner + verify full §11.2 acceptance gate**
- **S1 — Extend `run_notifier_stub_tests.sh` to build+run `test_notifier_host`;
  verify §11.2A-D green + no regressions** (story_points: 1; deps: P1.M3.T1.S2)
  - **INPUT:** Current `run_notifier_stub_tests.sh` (55 lines): compiles a shared
    `notifier_stub.o`, links+runs `test_notifier_dispatch` and `test_notifier_os`.
  - **LOGIC:** (a) Add a step to link `test_notifier_host` from the SAME shared
    `.o` (do not compile `notifier.c` thrice). Run all three; grep `FAIL:` for
    each; print `notifier host fails=N`; end `✓ notifier stub-compile gate PASSED`
    iff all three have 0 `FAIL:` and exit 0. Renumber steps (now ~[1/5] compile,
    [2/5]–[4/5] link, [5/5] run all). (b) Run the FULL acceptance gate per §11.2:
    A) `./run_all_tests.sh` — 9 `pattern_match` suites, 0 failures; B) pathological
    NFA stress `<50ms`, `result=0`; C) six realistic patterns print `1`; D)
    `./run_notifier_stub_tests.sh` — all three notifier binaries 0 `FAIL:`.
    (c) Confirm `test_notifier_dispatch` and `test_notifier_os` pass **unchanged**
    (legacy path untouched; `current_os`/multi-OS logic intact).
  - **OUTPUT:** the runner builds+runs three notifier binaries; the full gate is
    green; no regressions.
  - **DOCS (Mode A):** update the runner header comment to mention the third binary.
  - **MOCKING:** stub harness for all three binaries.

**Task P1.M3.T3 — Sync changeset-level documentation (README.md)** (Mode B)
- **S1 — Add a "Host-Side Rules & Typed Commands" section to README.md; update
  Setup / Running Tests / What-this-does-NOT-change** (story_points: 1; deps:
  P1.M1.T1.S1, P1.M1.T2.S1, P1.M2.T1.S1, P1.M2.T2.S1, P1.M3.T1.S1, P1.M3.T1.S2,
  P1.M3.T2.S1)
  - **INPUT:** All implementing subtasks complete and verified green. Current
    README.md (396 lines) has a "Multi-OS Configuration" section (plan 002); the
    host-rules feature is entirely absent.
  - **LOGIC (Mode B — cross-cutting summary):** (a) Add a "Host-Side Rules & Typed
    Commands" section documenting: the opt-in nature (a keymap without
    `DEFINE_HOST_CALLBACKS` is unchanged); `DEFINE_HOST_CALLBACKS` + the
    `host_callback_t` row shape; the typed-command namespace at a glance
    (`QUERY_INFO`/`QUERY_CALLBACK`/`SET_OS`/`APPLY_HOST_CONTEXT`, the `0xF0`
    discriminator, `0x51` responses, capability handshake + `proto_ver=2`); the
    per-window **stack vs replace** model via `clear_board` (driven by the host's
    per-rule `disable_firmware_config`, pointing to `qmkonnect/spec/HOST_RULES.md`
    for the host-side design); host-authoritative `SET_OS` (host value wins over
    `OS_DETECTION` while connected); host layers reserved ≥ 224. (b) Setup note:
    host-rules users add `DEFINE_HOST_CALLBACKS({ … })` and the host (QMKonnect)
    negotiates via `QUERY_INFO`. (c) Running Tests: mention `test_notifier_host`
    is now built+run by `run_notifier_stub_tests.sh`. (d) "What this does NOT
    change": the legacy string wire protocol and the pattern matcher are untouched;
    board `DEFINE_*`/`DEFINE_*_OS` rules keep working (stacked or replaced per the
    host's per-window decision).
  - **OUTPUT:** README documents the host-rules feature end-to-end + the backward-
    compat guarantee; a user understands how a host ruleset coexists with board
    rules.
  - **DOCS:** this IS the changeset-level documentation task (depends on every
    implementing subtask).
  - **MOCKING:** documentation only.

---

## 4. Key Invariants This Delta Must Preserve

1. **Legacy strings are transparent.** `data[2]` for a legacy string is a printable
   char (`0x20–0x7E`), never `0xF0`. The typed routing branch never fires for them;
   `process_full_message` runs unchanged. `test_notifier_dispatch` and
   `test_notifier_os` pass byte-for-byte unchanged (verified: neither sends `0xF0`).
2. **Typed commands bypass board side effects** on a capable firmware — no
   `disable_command()`/`deactivate_layer()` from `QUERY_INFO`/`QUERY_CALLBACK`/
   `SET_OS`/`APPLY_HOST_CONTEXT` themselves. (`clear_board` on
   `APPLY_HOST_CONTEXT` is the *explicit* board-clear, §14 — that is intended, not
   a side effect of routing.)
3. **`0x51` response marker is distinct** from the legacy `0`/`1` ack (§4.6). The
   host disambiguates without ambiguity; `response[0] != 0x51` / timeout ⇒ legacy.
4. **`SET_OS` shares the `notifier_set_os` seam** so F9 (idempotent + clear-on-
   change) is not duplicated. Host value is authoritative while connected; the
   `OS_DETECTION` heuristic is the no-host fallback.
5. **`host_layer` is independent of board `activated_layer`.** `set_host_layer`
   touches only the host tracker; board layer state is separate. Host layers are
   reserved ≥ 224 (`LAYER_UNSET = 255`).
6. **`apply_host_callbacks` is disable-before-enable** (mirrors the board ordering).
7. **Structural backward compatibility:** no `DEFINE_HOST_CALLBACKS` ⇒ weak
   `{NULL,0}` accessors ⇒ no registry ⇒ `feature_flags & 0x02` clear; the module
   links and behaves identically to today (prior finding F3). A capable firmware
   still services legacy strings exactly as before.
8. **`has_been_queried` + at-most-once handshake** (§4.6) prevents a mid-session
   reconnect against legacy firmware from clearing an active board layer.
9. **Matcher + wire framing untouched.** `pattern_match.{c,h}`, `rules.mk`, §4.1–
   4.3/§4.5 are unchanged. The host-side matcher lives in `qmkonnect` (§14 note).

---

## 5. Definition of Done (this delta)

- [ ] `notifier.h` exposes `host_callback_t`, `get_host_callbacks{,_size}`,
      `DEFINE_HOST_CALLBACKS`, and the typed-command constants (§4.6/§14).
- [ ] `notifier.c` dispatches `0xF0` commands via `handle_typed_command`
      (bypassing `process_full_message`), implements all four handlers + `0x51`
      responses, tracks `host_layer`/`host_cb_enabled[]`/`has_been_queried`,
      and implements `set_host_layer` + `apply_host_callbacks` + `clear_board`.
- [ ] `SET_OS` updates `current_os` authoritatively (host > heuristic) via the
      shared seam (F9 clear on change, idempotent on unchanged).
- [ ] `test_notifier_host.c` passes 0 `FAIL:` (all §4.6/§4.7/§14 categories);
      `run_notifier_stub_tests.sh` builds+runs all THREE notifier binaries green.
- [ ] The full §11.2A–D acceptance gate is green; no regressions in the 9
      `pattern_match` suites or the two existing notifier suites.
- [ ] README documents the host-rules feature + backward-compat guarantee.
- [ ] No new compiler warnings beyond pre-existing ones.
- [ ] A keymap without `DEFINE_HOST_CALLBACKS` links and behaves byte-identically
      to the pre-delta firmware; legacy string dispatch is unchanged.