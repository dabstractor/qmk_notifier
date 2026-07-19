# Findings & Risks — Plan 003

## Research findings

### F1 — Weak-symbol override pattern is proven (reuse)
The existing `__attribute__((weak))` + `DEFINE_SERIAL_*` macro pattern is well-tested
(2023 test cases + 31 stub cases green). The new `DEFINE_HOST_CALLBACKS` follows
the exact same pattern: macro at file scope generates a strong accessor pair that
overrides the weak `{NULL, 0}` defaults at link time. **Risk: none** — proven pattern.

### F2 — `SET_OS` must share the `notifier_set_os` seam
The current `notifier_set_os` (notifier.c:~470-485) contains the F9 clear-on-change
logic (idempotent check → `disable_command()` → `deactivate_layer()`). `SET_OS`
must trigger the same logic. **Finding:** extract `static void apply_os_change(os_variant_t)`
that both call. Do NOT duplicate the F9 logic in the `SET_OS` handler — divergence
risk is high. This is a refactor, not a rewrite — the existing `notifier_set_os`
body moves into `apply_os_change` and `notifier_set_os` becomes a one-line forwarder.

### F3 — Backward compatibility is structural, not conditional
No `#ifdef` is needed for backward compat. Three structural guarantees:
1. A keymap without `DEFINE_HOST_CALLBACKS` links against weak `{NULL, 0}` accessors.
2. Legacy string sends have `data[2]` printable (`0x20–0x7E`), never `0xF0`.
3. `process_full_message` is untouched.
**Risk: none** — the typed path is transparently absent for legacy callers.

### F4 — Stub `os_detection.h` stays minimal
The stub provides ONLY the `os_variant_t` type. It does NOT declare `detected_host_os()`.
The module never calls it. This is unchanged from plan 002. **Risk: none.**

### F5 — Existing tests never send `0xF0`
Verified by reading `test_notifier_dispatch.c` and `test_notifier_os.c`: all test
cases send legacy strings with printable `data[2]` bytes. The typed routing branch
never fires for them. They will pass unchanged. **Risk: none.**

### F6 — Stub observables precedent established
`stub_get_active_layer()` was added in plan 002 as a test-harness observable. The
new `stub_get_last_response()` follows the same precedent. **Risk: none.**

### F7 — Multi-report typed framing uses existing reassembly
The existing `msg_buffer` + `msg_index` + `dropping` overflow machinery already
handles multi-report reassembly (30 payload bytes/report, ETX terminates). Typed
commands reuse this — the discriminator `0xF0` is at `data[2]` only in the FIRST
report; subsequent reports are pure payload. The `typed_mode` flag (checked when
`msg_index == 0`) governs routing. **Risk: low** — the reassembly is proven; the
`typed_mode` flag is the only new state.

### F8 — `board_rules_present` must check ALL maps
The `QUERY_INFO.board_rules_present` bit is `1` iff ANY board map (default command,
default layer, or any per-OS command/layer map) is non-empty. Implementation calls
all the `get_*_size()` and `select_*_map_os()` accessors. Per-OS granularity is NOT
exposed (§4.6: "a single bit suffices"). **Risk: none** — straightforward
aggregation.

### F9 — Ecosystem repos have spec but no implementation
The researcher confirmed: `qmk_notifier` (Rust crate) and `qmkonnect` (Rust daemon)
have complete typed-command SPECIFICATIONS in their docs but no CODE. This delta
implements the firmware side only. The host/transport repos will implement
separately. **Implication:** the firmware must be ready per §4.6 but cannot be
end-to-end tested against a real host yet — only via host-test stubs. **Risk: low**
— the stub harness fully simulates the wire protocol.

## Risks

### RISK-1: `typed_mode` flag lifecycle (LOW)
The `typed_mode` bool persists across `hid_notify` calls (it's set on the first
report and read on ETX which may be a later report). It must be:
- Set only when `msg_index == 0` (first report of a message).
- Reset to `false` at every ETX boundary (both typed and legacy paths).
- Reset to `false` on overflow/`dropping` (an oversized typed message is dropped,
  and `typed_mode` must clear so the next message starts fresh).

**Mitigation:** Reset `typed_mode = false` alongside `msg_index = 0` in all three
ETX branches (normal dispatch, dropping, typed dispatch).

### RISK-2: Host layer / board layer collision (LOW)
Host layers are reserved ≥ 224 (`HOST_LAYER_BASE`). Board layers use QMK's
conventional layer indices (typically 0–31). `LAYER_UNSET = 255` is the sentinel
for both trackers. There is no collision because:
- Board `activated_layer` is set by `activate_layer()` from user-defined layer
  indices (0–31 typically).
- Host `host_layer` is set by `set_host_layer()` from values ≥ 224 (pushed by host).
- They are different variables tracking different QMK layers.

**Mitigation:** Document the ≥ 224 reservation in code comments. The `set_host_layer`
function does NOT validate the layer range — it trusts the host (the host-side
spec reserves ≥ 224).

### RISK-3: `apply_host_callbacks` id range validation (LOW)
The host sends callback ids; the firmware must defensively skip ids ≥
`get_host_callbacks_size()` (malformed host data should not crash). Both the
disable and enable phases must range-check.

**Mitigation:** Explicit `if (id < get_host_callbacks_size())` guard before
dereferencing `get_host_callbacks()[id]`.

### RISK-4: Response builder must always send 32 bytes (LOW)
The typed-response builder fills a 32-byte buffer with `response[0]=0x51`,
`response[1]=cmd_echo`, payload at `[2..]`, zero-padded. Must always call
`raw_hid_send(response, RAW_REPORT_SIZE)` with `RAW_REPORT_SIZE = 32`.

**Mitigation:** Single response-builder helper that always sends 32 bytes;
constant `RAW_REPORT_SIZE` reused from existing code.