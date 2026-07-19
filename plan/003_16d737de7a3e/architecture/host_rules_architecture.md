# Host-Rules Architecture — Plan 003

## Overview

The host-side rules feature adds a **typed-command namespace** to the firmware
that lets a desktop host (QMKonnect) push layer/callback decisions over Raw HID
without reflashing. The firmware's role: receive typed commands, expose a named
callback registry, track a host layer + host-callback state **separate from board
state**, and honor a per-window stack/replace decision via `clear_board`.

## Architecture: two independent state planes

```
┌─────────────────────────────────────────────────────────┐
│                    notifier.c                           │
│                                                         │
│  ┌─────────────────────┐    ┌─────────────────────────┐ │
│  │   BOARD STATE        │    │   HOST STATE (NEW)      │ │
│  │                      │    │                         │ │
│  │  activated_layer     │    │  host_layer             │ │
│  │  current_command     │    │  host_cb_enabled[]      │ │
│  │  current_os          │    │  has_been_queried       │ │
│  │                      │    │                         │ │
│  │  Defined via:        │    │  Defined via:           │ │
│  │  DEFINE_SERIAL_*     │    │  DEFINE_HOST_CALLBACKS  │ │
│  │  DEFINE_SERIAL_*_OS  │    │                         │ │
│  │                      │    │                         │ │
│  │  Driven by:          │    │  Driven by:             │ │
│  │  legacy string path  │    │  typed commands (0xF0)  │ │
│  │  process_full_message│    │  handle_typed_command   │ │
│  └─────────────────────┘    └─────────────────────────┘ │
│          │                           │                  │
│          ▼                           ▼                  │
│     layer_on/off               layer_on/off             │
│     (QMK layers)              (QMK layers ≥224)         │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │           SHARED OS SEAM                         │   │
│  │  apply_os_change(os) ← notifier_set_os()         │   │
│  │                      ← SET_OS (typed 0x03)       │   │
│  │  Idempotent + F9 clear-on-change                 │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

**Key insight:** Board and host state are orthogonal. `process_full_message`
(legacy string path) touches ONLY board state. `handle_typed_command` touches
ONLY host state (except `clear_board` which explicitly clears board state, and
`SET_OS` which updates the shared `current_os`). This orthogonality is invariant 21.

## Dispatch fork in `hid_notify`

```c
void hid_notify(uint8_t *data, uint8_t length) {
    // 1. Coexistence guard (unchanged)
    if (length < 2 || data[0] != 0x81 || data[1] != 0x9F) return;

    // 2. Typed discriminator check (NEW) — on first report of a message
    if (msg_index == 0 && length >= 3 && data[2] == NOTIFY_CMD_DISCRIMINATOR) {
        typed_mode = true;
    }

    // 3. Strip magic header, accumulate into msg_buffer (unchanged)
    data += 2; length -= 2;
    // ... byte loop until ETX ...

    // 4. On ETX:
    if (typed_mode) {
        typed_response = handle_typed_command(msg_buffer);
        // send [0x51][...] response
    } else {
        match = process_full_message(msg_buffer);
        // send [0|1] response (unchanged)
    }
    typed_mode = false;  // reset for next message
}
```

**Why `data[2]` and not `data[0]`:** After the coexistence guard, `data` still
points at byte 0. The discriminator is at byte index 2 (after magic bytes 0 and 1).
Legacy strings have a printable char at `data[2]` (`0x20–0x7E`), which is never
`0xF0`, so the routing is transparent (invariant: §4.6, §13).

**Why `msg_index == 0`:** The discriminator appears only in the FIRST report of a
multi-report message. Subsequent reports have payload bytes at `data[2]`, which may
coincidentally be `0xF0`. Only check on the first report.

## The four typed-command handlers

### QUERY_INFO (0x01) — capability handshake
```
Response: [0x51][0x01][proto_ver=2][feature_flags][callback_count][board_rules_present]
```
- Sets `has_been_queried = true` on first service (§4.6 handshake timing).
- `feature_flags = 0x01 | (get_host_callbacks_size() > 0 ? 0x02 : 0)`.
- The host sends this once per board boot to detect capability.

### QUERY_CALLBACK (0x02) — name discovery
```
Request:  [index]
Response: [0x51][0x02][index][name bytes, NUL-padded...]  (valid index)
Response: [0x51][0x02][index][0x00]                        (out-of-range)
```
- The host sweeps `i in 0..count` to build a `name→id` map.
- `id = array index`, stable per build.

### SET_OS (0x03) — host-authoritative OS
```
Request:  [os_byte]   (0=UNSURE, 1=LINUX, 2=WINDOWS, 3=MACOS, 4=IOS)
Response: [0x51][0x03][ack=1]
```
- Routes through `apply_os_change((os_variant_t)os_byte)` — the SAME function
  `notifier_set_os` calls. So an OS change triggers F9 clear (disable command +
  deactivate layer). Idempotent on unchanged value.
- **Authoritative while host connected** (§4.7): the host's value takes precedence
  over the `OS_DETECTION` heuristic.

### APPLY_HOST_CONTEXT (0x05) — layer + callbacks + stack/replace
```
Request:  [layer][flags][count][id0][id1]...
Response: [0x51][0x05][ack=1]
```
- `flags & 0x01` = `clear_board`: if set, `deactivate_layer()` (board) +
  `disable_command()` (board) BEFORE applying — "replace" mode.
- If `clear_board` not set: "stack" mode — board state untouched.
- Then: `set_host_layer(layer)` (0xFF clears host layer) +
  `apply_host_callbacks(ids, count)`.

## The `apply_os_change` seam refactor

Current `notifier_set_os`:
```c
void notifier_set_os(os_variant_t os) {
    if (os == current_os) return;       // idempotent (F9.3)
    current_os = os;
    disable_command();                  // F9.1 clear
    deactivate_layer();                 // F9.1 clear
}
```

Refactored to extract the shared logic:
```c
static void apply_os_change(os_variant_t os) {
    if (os == current_os) return;       // idempotent
    current_os = os;
    disable_command();
    deactivate_layer();
}

void notifier_set_os(os_variant_t os) {       // keymap calls this
    apply_os_change(os);
}

// SET_OS handler calls apply_os_change((os_variant_t)os_byte);
```

**Critical:** Only `apply_os_change` mutates `current_os` (besides init). Both
sources (keymap `notifier_set_os`, typed `SET_OS`) route through it. This ensures
F9 semantics are never duplicated or diverged.

## Host callback diff algorithm (`apply_host_callbacks`)

```
Input: desired[] (the host's full desired enabled set), count

PHASE 1 — DISABLE (newly-out-of-set):
  for each id in 0..HOST_CALLBACK_MAX:
    if host_cb_enabled[id] AND id NOT in desired[]:
      call on_disable (if non-NULL)
      host_cb_enabled[id] = false

PHASE 2 — ENABLE (newly-in-set):
  for each id in desired[0..count):
    if NOT host_cb_enabled[id] AND id < get_host_callbacks_size():
      call on_enable (if non-NULL)
      host_cb_enabled[id] = true
```

**Ordering:** disable-before-enable mirrors the board's
`disable_command()`/`enable_command()` ordering (invariant 4, §13). This prevents
a callback being briefly in both states during a transition.

## Backward compatibility guarantees

1. **No `DEFINE_HOST_CALLBACKS`** → weak `{NULL, 0}` accessors → no registry →
   `feature_flags & 0x02` clear → `callback_count = 0`. Module links and behaves
   identically to today. (Prior finding F3: structural, no `#ifdef`.)

2. **Legacy string sends** → `data[2]` is printable (`0x20–0x7E`), never `0xF0` →
   typed routing never fires → `process_full_message` runs unchanged. All existing
   tests pass byte-for-byte.

3. **`process_full_message`** → untouched. Multi-OS dispatch, F4 delimiter matching,
   F5 ordering, F6 ack all unchanged.

4. **`pattern_match.{c,h}`** → untouched. The matcher is the single source of truth
   for match semantics.

## Test strategy

| Test suite | Scope | New/changed? |
|---|---|---|
| 9 `test_*pattern*.c` suites | Pattern matcher regression | **Unchanged** — must stay 2023/2023 green |
| `test_notifier_dispatch.c` | Legacy reassembly, F4, ordering, ack | **Unchanged** — must stay green |
| `test_notifier_os.c` | Multi-OS F8/F9 | **Unchanged** — must stay green |
| `test_notifier_host.c` | Typed commands + host rules | **NEW** — stub-compiled `notifier.c` |
| `qmk_stubs/qmk_stubs.c` | Stub harness | **Extended** — add response capture |
| `run_notifier_stub_tests.sh` | Runner | **Extended** — build+run 3rd binary |

The new `test_notifier_host.c` follows the EXACT pattern of `test_notifier_dispatch.c`:
file-scope `DEFINE_*` macros, `ck()` helper, `PASS:`/`FAIL:` output, summary line,
return non-zero on failure. The runner greps `grep -c '^FAIL:'`.

### Stub harness enhancement

Current `raw_hid_send` only prints `response[0]`. For typed-command tests, we need
the full 32-byte response. Extend with:
```c
static uint8_t g_last_response[32];
void raw_hid_send(uint8_t *data, uint8_t length) {
    memcpy(g_last_response, data, (length < 32) ? length : 32);
    // ... existing debug print ...
}
const uint8_t* stub_get_last_response(void) { return g_last_response; }
```

This follows the `stub_get_active_layer()` precedent (test-harness observable, not
production code).