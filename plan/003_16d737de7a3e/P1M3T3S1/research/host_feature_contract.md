# Research — Host-Side Rules feature contract (what the README must document)

Sourced from: `notifier.h` (API surface), `notifier.c` (handlers L237-740 +
`hid_notify` dispatch L768-824), PRD §4.6 / §4.7 / §14, and
`plan/003_16d737de7a3e/architecture/host_rules_architecture.md`.

## 1. Opt-in / backward compatibility (the guarantee — state FIRST)

- A keymap WITHOUT `DEFINE_HOST_CALLBACKS` is **byte-for-byte unchanged**.
  - `notifier.c` provides `__attribute__((weak))` defaults:
    `get_host_callbacks(){return NULL;}` / `get_host_callbacks_size(){return 0;}`.
  - So `feature_flags & 0x02` is clear and `callback_count = 0`. The module links
    and behaves identically to the pre-host-rules firmware (prior finding F3:
    structural, no `#ifdef`).
- A host that sends ONLY legacy strings coexists unchanged: `data[2]` for a legacy
  string is a printable char (`0x20–0x7E`), NEVER `0xF0`, so the typed routing
  fork in `hid_notify` (checked only when `msg_index == 0 && data[2] == 0xF0`)
  never fires for legacy strings.

## 2. The public API (notifier.h)

```c
typedef struct {
    const char *name;        // stable identifier the host matches by name
    callback_t  on_enable;   // fired when host adds this id to its desired set
    callback_t  on_disable;  // fired when host removes it; MAY be NULL
} host_callback_t;
```

Accessor pair (user overrides via the macro; module gives weak `{NULL,0}`):
```c
host_callback_t* get_host_callbacks(void);
size_t           get_host_callbacks_size(void);
```

The definition macro (ID = array index, stable per build):
```c
#define DEFINE_HOST_CALLBACKS(...) \
    host_callback_t user_host_callbacks[] = __VA_ARGS__; \
    const size_t user_host_callbacks_size = sizeof(user_host_callbacks)/sizeof(user_host_callbacks[0]); \
    host_callback_t* get_host_callbacks(void) { return user_host_callbacks; } \
    size_t get_host_callbacks_size(void) { return user_host_callbacks_size; }
```
Row shape for the `{ … }`: `{ "name", &on_enable_cb, &on_disable_cb }`
(`on_disable` may be `NULL`). Bounded by `HOST_CALLBACK_MAX` = 32 (static array).

## 3. Typed-command constants (notifier.h) — the at-a-glance table

| Constant | Value | Meaning |
|---|---|---|
| `NOTIFY_CMD_DISCRIMINATOR` | `0xF0` | `data[2] == 0xF0` ⇒ typed command (checked on first report only) |
| `NOTIFY_RESPONSE_MARKER` | `0x51` | typed response prefix; distinct from legacy match-bool `0`/`1` |
| `NOTIFY_CMD_QUERY_INFO` | `0x01` | capability handshake |
| `NOTIFY_CMD_QUERY_CALLBACK` | `0x02` | name discovery by index |
| `NOTIFY_CMD_SET_OS` | `0x03` | host-authoritative OS |
| `NOTIFY_CMD_APPLY_HOST_CONTEXT` | `0x05` | host layer + callbacks + stack/replace |
| (`0x04`) | reserved | VIA-coexist dispatch (not implemented) |
| `NOTIFY_PROTO_VER` | `2` | `1` = legacy string-only; `2` = typed-capable (firmware-owned) |
| `NOTIFY_FEATURE_APPLY_HOST_CONTEXT` | `0x01` | feature_flags bit |
| `NOTIFY_FEATURE_CALLBACK_REGISTRY` | `0x02` | feature_flags bit (set iff `DEFINE_HOST_CALLBACKS` present) |
| `HOST_LAYER_BASE` | `224` | host layers reserved ≥ 224 |
| (`LAYER_UNSET` = 255) | — | defined in notifier.c; `0xFF` = "clear host layer" |

## 4. The four handlers (notifier.c handle_typed_command, L651-740)

**QUERY_INFO (0x01)** — capability handshake. Reply payload (4 bytes):
`[proto_ver=2][feature_flags][callback_count][board_rules_present]`.
- `feature_flags = 0x01 | (get_host_callbacks_size() > 0 ? 0x02 : 0)`.
- `board_rules_present` = `1` iff ANY board map (default or any `DEFINE_SERIAL_*_OS`)
  is non-empty (single bit; per-OS granularity NOT exposed).
- Sets `has_been_queried = true` on first service (§4.6 handshake-timing rule).

**QUERY_CALLBACK (0x02)** — name discovery. args[0]=index. Reply:
`[index][name bytes, NUL-padded…]` for a valid index; `[index][0x00]` (name absent)
for out-of-range / no registry. Host sweeps `i in 0..count` to build name→id.

**SET_OS (0x03)** — host-authoritative OS while connected. args[0]=os_byte
(`0 UNSURE · 1 LINUX · 2 WINDOWS · 3 MACOS · 4 IOS`, mirrors `os_variant_t`).
- Routes through `apply_os_change((os_variant_t)os_byte)` — the SAME static seam
  `notifier_set_os` calls — so an OS **change** triggers the F9 clear (disable
  command + deactivate layer); same OS is idempotent.
- While a host is connected and has sent SET_OS, the host value is AUTHORITATIVE
  for `current_os`, taking precedence over the `OS_DETECTION` heuristic.
- Reply: `[0x51][0x03][ack=1]`.

**APPLY_HOST_CONTEXT (0x05)** — per-window stack/replace. args:
`[layer][flags][count][id0][id1]…`.
- `flags & 0x01` = **`clear_board`**: if set, firmware first
  `deactivate_layer()`s the board `activated_layer` AND `disable_command()`s the
  board command → **REPLACE** mode (board rules inert for this window). If clear →
  **STACK** mode (board state untouched).
- Then `set_host_layer(layer)` (`0xFF`/`LAYER_UNSET` clears the host layer) and
  `apply_host_callbacks(ids, count)` diffs the host-callback enable set
  (disable-before-enable; `count==0` disables all).
- Reply: `[0x51][0x05][ack=1]`.

## 5. Two independent state planes (architecture invariant 21)

- **BOARD state**: `activated_layer`, `current_command`, `current_os`. Driven by
  the legacy string path (`process_full_message`). Defined via
  `DEFINE_SERIAL_*` / `DEFINE_SERIAL_*_OS`.
- **HOST state**: `host_layer` (independent of board `activated_layer`),
  `host_cb_enabled[HOST_CALLBACK_MAX]`, `has_been_queried`. Driven by typed
  commands (`handle_typed_command`). Defined via `DEFINE_HOST_CALLBACKS`.
- They touch each other ONLY at: `clear_board` (explicit board teardown in
  APPLY_HOST_CONTEXT) and `SET_OS` (shared `current_os` via `apply_os_change`).

## 6. Stack vs replace — driven by the HOST's per-rule `disable_firmware_config`

The firmware offers BOTH; the **host chooses per window** via the `clear_board`
flag, which it computes from its own per-rule `disable_firmware_config` flag in
`rules.toml` (default `false`):
- A matched rule with `disable_firmware_config = true` contributes to a REPLACE.
- The window is REPLACE iff **every** matched rule is disabling.
- The legacy string is sent iff the board has rules AND ≥1 matched rule is
  non-disabling (else the host sends only APPLY_HOST_CONTEXT{clear_board=1}).
- The host-side design (matcher + rules.toml + the per-window decision) is
  CANONICAL in the `qmkonnect` repo: `qmkonnect/spec/HOST_RULES.md`. The firmware
  matcher (`pattern_match.c`) + its test corpus are the single source of truth for
  match semantics; the crate is transport-only.

## 7. Capability handshake & legacy fallback (§4.6)

On device (re)connect the host sends `QUERY_INFO`:
1. Typed-capable firmware → `[0x51][0x01][proto=2][flags][count][…]`.
2. Legacy (string-only) firmware walks the typed bytes as a no-match string →
   `[0x00…]` (or times out). Host treats `response[0] != 0x51` / timeout as
   **legacy ⇒ string-only**: keeps sending the legacy string, NEVER sends typed
   commands (board rules still work).
3. If capable and `flags & 0x01`: host sweeps QUERY_CALLBACK(i) for
   `i in 0..count`, builds name→id, validates `rules.toml` names (warn+skip unknown).
- `has_been_queried` + host handshakes **at most once per board boot** (never on a
  mere HID re-enumeration) so a mid-session reconnect against legacy firmware
  cannot clear an active board layer. Host rules gated on `proto_ver == 2`.

## 8. Typed framing (§4.6) — for the "what does NOT change" precision

- Typed commands are **ETX-framed and may span multiple 32-byte reports**, exactly
  like legacy strings: `[0x81][0x9F][0xF0][cmd_id][args…][0x03]`, chunked at 30
  payload bytes/report.
- Responses are 32-byte reports. Legacy: `[matched(0|1)][pad]`. Typed:
  `[0x51][cmd_echo][payload…][pad]`. No reply in timeout ⇒ host treats as offline.

## 9. Setup note for the user (item_description (b))

Host-rules users:
1. `DEFINE_HOST_CALLBACKS({ … })` in `keymap.c` (registers named callbacks).
2. The host (QMKonnect) negotiates capability at connect via `QUERY_INFO` and (if
   capable) `QUERY_CALLBACK`, then drives `SET_OS` + `APPLY_HOST_CONTEXT`.
3. No `rules.mk` change is needed for host rules (it is wire-level, feature is
   always compiled in; the registry is simply empty/absent if the macro is omitted).