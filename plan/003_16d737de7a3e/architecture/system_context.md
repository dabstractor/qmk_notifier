# System Context — Plan 003 (Host-Side Rules & Typed Commands)

## What this plan is

This is a **delta** on a green, production-ready codebase. Plan `001` built the full
qmk-notifier firmware module (pattern matcher, receiver/dispatcher, 9-suite test
corpus, stub harness). Plan `002` added **per-OS command/layer map selection**
(opt-in overlay with `notifier_set_os`). Plan `003` adds the **host-side rules &
typed-command namespace** — the v0.3.0 feature that lets a desktop host
(QMKonnect) push layer/callback decisions over the wire without reflashing.

## Verified baseline (2025-07-19)

| Gate | Result |
|---|---|
| `./run_all_tests.sh` (9 pattern_match suites) | **2023/2023 passing, 0 failures** |
| `./run_notifier_stub_tests.sh` (dispatch + OS stub) | **31/31 passing, `✓ gate PASSED`** |
| Performance micro-benchmark | ~0.10 µs per `pattern_match` call |

The pattern matcher (`pattern_match.c/h`), `rules.mk`, the reassembly/ETX/sanitize
logic, `match_pattern` (F4 delimiter wrapper), the multi-OS dispatch
(`process_full_message` OS-first scan, `notifier_set_os` F9 clear-on-change), and
the full test corpus are all **untouched** by this delta and must stay green.

## What exists today vs. what this delta adds

| Component | Current state | Delta impact |
|---|---|---|
| `notifier.h` (91 lines) | structs, `DEFINE_SERIAL_*(_OS)` macros, `notifier_set_os`, `WT`, constants | **GROWS**: add `host_callback_t`, `DEFINE_HOST_CALLBACKS`, typed-command constants (§4.6/§14/§16) |
| `notifier.c` (541 lines) | receiver, reassembler, multi-OS dispatcher, `hid_notify` (legacy only), `notifier_set_os` | **GROWS** ~150-200 lines: typed routing in `hid_notify`, `handle_typed_command`, 4 handlers, host state machines, `apply_os_change` seam refactor |
| `pattern_match.{c,h}` | Thompson NFA matcher | **UNTOUCHED** |
| `rules.mk` (2 lines) | `RAW_ENABLE` + `SRC +=` | **UNTOUCHED** |
| `qmk_stubs/qmk_stubs.c` | `layer_on/off` + `raw_hid_send` (prints `response[0]`) + `stub_get_active_layer()` | **GROWS**: add `g_last_response[32]` capture + `stub_get_last_response()` accessor |
| `qmk_stubs/qmk_keyboard_stub.h` | `layer_on/off`, `RAW_EPSIZE` | **UNTOUCHED** |
| `qmk_stubs/os_detection.h` | header-only `os_variant_t` enum | **UNTOUCHED** |
| `qmk_stubs/raw_hid.h` | `raw_hid_send` decl | **UNTOUCHED** |
| `qmk_stubs/print.h` | `uprintf` macro | **UNTOUCHED** |
| `test_notifier_dispatch.c` (104 lines) | F4 matrix, ordering, reassembly, ack | **UNTOUCHED** (must re-pass — legacy path intact) |
| `test_notifier_os.c` (203 lines) | F8/F9 host tests | **UNTOUCHED** (must re-pass — multi-OS intact) |
| `test_notifier_host.c` | **DOES NOT EXIST** | **NEW** — typed-command + host-rules host tests |
| `run_notifier_stub_tests.sh` (55 lines) | compiles shared `.o`, links+runs dispatch + OS tests | **GROWS**: build+run a 3rd binary (`test_notifier_host`) |
| `run_all_tests.sh` (181 lines) | builds+runs 9 pattern_match suites + perf bench | **UNTOUCHED** |
| `README.md` | multi-OS section (plan 002), no host-rules | **GROWS**: host-rules section (Mode B docs) |

## The existing dispatch flow (what the delta modifies)

Current `hid_notify` (notifier.c:~490-540):
```
1. Coexistence guard (length < 2 || data[0] != 0x81 || data[1] != 0x9F) → return
2. Strip 2-byte magic header: data += 2; length -= 2
3. Byte loop: accumulate into msg_buffer until ETX (0x03)
   - Overflow → dropping=true, skip until ETX
4. On ETX: sanitize_string(msg_buffer, msg_index); process_full_message(msg_buffer)
5. Send 32-byte response: response[0] = match ? 1 : 0
```

**Delta change:** After step 1 and before step 2, when starting a fresh message
(`msg_index == 0`), check `data[2] == 0xF0` (the typed-command discriminator). If
set, route to `handle_typed_command()` on ETX instead of `process_full_message()`.
Legacy strings never have `data[2] == 0xF0` (sanitizer allows only `0x20-0x7E`),
so the routing is transparent.

## Key seams the delta hooks into

1. **`notifier_set_os` → `apply_os_change`** (refactor): The current
   `notifier_set_os` contains the F9 clear-on-change logic (idempotent check,
   disable_command, deactivate_layer). `SET_OS` (typed cmd 0x03) must route
   through the SAME logic. Extract a `static void apply_os_change(os_variant_t os)`
   that both call. This avoids duplicating F9 semantics.

2. **`hid_notify` dispatch fork**: The single point where legacy vs. typed routing
   is decided. The `typed_mode` flag (per-message, reset at ETX) governs which path
   the ETX handler takes.

3. **Weak-default pattern** (prior finding F1): `DEFINE_HOST_CALLBACKS` follows the
   exact same `__attribute__((weak))` override pattern as `DEFINE_SERIAL_COMMANDS`.
   A keymap without it links against weak `{NULL, 0}` accessors.

4. **Stub observable pattern** (prior finding F6): The `stub_get_active_layer()`
   test-harness observable precedent is extended to `stub_get_last_response()` for
   typed-response byte assertions.