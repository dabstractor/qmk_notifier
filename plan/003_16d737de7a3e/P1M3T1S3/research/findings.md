# Research Findings — P1.M3.T1.S3 (SET_OS + APPLY_HOST_CONTEXT host tests)

## Methodology

Read in full: `notifier.c` (SET_OS + APPLY_HOST_CONTEXT handlers, `hid_notify`
reassembly, `apply_os_change`, `apply_host_callbacks`, `set_host_layer`),
`notifier.h` (NOTIFY_* constants, DEFINE_* macros), `qmk_stubs/qmk_stubs.c`
(stub layer tracking + `stub_get_last_response`), `test_notifier_os.c` (F9
clear-on-change pattern), `test_notifier_dispatch.c` (precedent), the S2 PRP
(contract for the file being extended), the S1 PRP (`stub_get_last_response`
contract, LANDED), and the architecture doc. Then **empirically verified** every
assertion with two gcc probe binaries stub-linked against the REAL `notifier.c`.

---

## VERIFIED BYTE LAYOUTS (traced from notifier.c + confirmed by probe)

### SET_OS (cmd 0x03) — handler reads `data[2]` = os_byte
- Frame: `[0x81][0x9F][0xF0][0x03][os_byte][0x03]`
- Expected response: `[0x51][0x03][0x01]` (r[0]=0x51, r[1]=0x03, r[2]=1 ack)
- **ACTUAL response (unmodified notifier.c): `[0x51][0x00][0x00]`** — see BLOCKER below.

### APPLY_HOST_CONTEXT (cmd 0x05) — handler reads `data[2]=layer, data[3]=flags, data[4]=count, data[5..]=ids`
- Frame: `[0x81][0x9F][0xF0][0x05][layer][flags][count][id0][id1]…[0x03]`
- Response (VERIFIED): `[0x51][0x05][0x01]` (r[0]=0x51, r[1]=0x05, r[2]=1 ack) ✓

### `send_typed(cmd, args, nargs)` arg placement (from S2 PRP, reused)
- `rep[0]=0x81, rep[1]=0x9F, rep[2]=0xF0(discrim), rep[3]=cmd, rep[4+i]=args[i], rep[4+nargs]=0x03(ETX)`.
- After magic strip in `hid_notify`, the byte loop accumulates `[0xF0, cmd, args…]`
  into `msg_buffer` until ETX, then `handle_typed_command(msg_buffer)`:
  `data[0]=0xF0, data[1]=cmd_id, data[2..]=args`.

---

## 🚨 CRITICAL BLOCKER — SET_OS cmd_id (0x03) == ETX terminator (0x03)

### The flaw (VERIFIED by probe against real notifier.c)

`hid_notify`'s byte loop treats EVERY `0x03` byte as the ETX terminator:

```c
for (uint8_t i = 0; i < length; i++) {
    char c = (char)data[i];        // data points PAST the 2-byte magic header
    if (c == ETX_TERMINATOR[0]) {  // 0x03 -> dispatch immediately
        ... handle_typed_command(msg_buffer) ...
        break;
    } else { msg_buffer[msg_index++] = c; }
}
```

For SET_OS the post-magic byte stream is `[0xF0, 0x03(cmd_id), os_byte, 0x03(ETX)]`.
The loop appends `0xF0` (msg_index 0→1), then sees `0x03` (the **cmd_id**) at
msg_index 1 and **dispatches prematurely** with `msg_buffer=[0xF0, 0, 0…]`.
`handle_typed_command` reads `cmd_id = data[1] = 0` → **default case** →
`send_typed_response(0, NULL, 0)` → response `[0x51][0x00][0x00…]`.

**Probe evidence (unmodified notifier.c):**
```
SET_OS(OS_MACOS=3): r[0]=51 r[1]=00 r[2]=00     <- BROKEN (want 51 03 01)
AHC{224,0,0}:        r[0]=51 r[1]=05 r[2]=01     <- OK (cmd 0x05 != ETX)
```

### It is WORSE than just the cmd_id

`OS_MACOS == 3` (`os_variant_t` enum, confirmed in `qmk_stubs/os_detection.h`), so
the **os_byte argument** for macOS is ALSO `0x03`. Even a fix that only exempts the
cmd_id byte from ETX (e.g. `!(typed_mode && msg_index < 2)`) leaves SET_OS(OS_MACOS)
broken: the os_byte `0x03` would still terminate the message before being appended.
Probe with that partial fix confirmed: response went `[0x51][0x00]`→`[0x51][0x03]`
(cmd_id now delivered) but `current_os` did NOT become MACOS (os_byte lost to ETX),
so the OS-map-selection test (ii) still failed.

**Root cause:** ETX-termination is fundamentally incompatible with **binary** typed
payloads. Legacy strings are text (0x20–0x7E), so `0x03` never appears in them —
ETX-framing is safe for strings only. Typed commands carry arbitrary binary
(cmd_id, os_byte, layer, flags, count, ids) which CAN be `0x03`. The PRD §4.6
framing (`[0x81][0x9F][0xF0][cmd_id][args…][0x03]`) reuses string ETX-framing for
binary, which is self-contradictory whenever any payload byte == 0x03:
- `cmd_id 0x03` (SET_OS)  → cmd byte terminates the message.
- `os_byte 0x03` (MACOS)  → arg byte terminates the message.
- any `layer/flags/count/id == 0x03` in APPLY_HOST_CONTEXT → same.

### Scope of impact on THIS task

| Test | cmd/args contain 0x03? | Verdict |
|------|------------------------|---------|
| (i) SET_OS response            | cmd_id=0x03            | **BLOCKED** — response is [0x51][0x00] |
| (ii) SET_OS changes current_os | cmd_id=0x03            | **BLOCKED** — current_os never changes |
| (iii) SET_OS F9 clear-on-change| cmd_id=0x03            | **BLOCKED** — apply_os_change never called |
| (iv) SET_OS idempotent         | cmd_id=0x03            | **BLOCKED** |
| (v) AHC stack {224,0,0}        | no                     | **PASS** ✓ |
| (vi) AHC replace {224,1,0}     | no                     | **PASS** ✓ |
| (vii) AHC callback diff [0]→[1]| no (ids 0,1)           | **PASS** ✓ |
| (viii) AHC layer=0xFF clear    | no                     | **PASS** ✓ |

**The 4 APPLY_HOST_CONTEXT tests pass against the current firmware. The 4 SET_OS
tests CANNOT pass without a notifier.c framing fix (out of scope for this test-only task).**

### Required upstream fix (in notifier.c `hid_notify` — NOT this task)

Binary typed payloads must NOT be ETX-terminated. Two viable designs:
1. **Length-prefixed typed framing**: the host sends a byte count; the firmware
   reads exactly that many bytes (no ETX scan). Cleanest; changes the wire format.
2. **Escape 0x03 in typed payloads** (e.g. `0x1B 0x03`), or reserve cmd_ids/arg
   values to never be 0x03 (but `OS_MACOS=3` violates this, so escaping is required).

A MINIMAL (but INCOMPLETE — does not fix os_byte=3) patch that only unblocks the
cmd_id byte, verified to fix the response layout but NOT OS_MACOS dispatch:
```c
// in hid_notify byte loop — exempts discriminator+cmd_id (first 2 typed bytes):
if (c == ETX_TERMINATOR[0] && !(typed_mode && msg_index < 2)) { /* dispatch */ }
```
**This is NOT sufficient for SET_OS(OS_MACOS).** The real fix is framing redesign
(length-prefix or full escaping) — a notifier.c / PRD-§4.6 change owned by P1.M2,
not by this test task. This PRP documents it; the implementer must NOT silently
weaken the SET_OS assertions to mask it.

---

## STUB LAYER-TRACKING GOTCHA (critical for stack/replace assertions)

`qmk_stubs.c` models QMK layers as a SINGLE `g_active_layer` (last-write-wins), NOT
a bitmask like real `layer_state`:
```c
void layer_on(uint8_t layer)  { g_active_layer = layer; }   // overwrites
void layer_off(uint8_t layer) { (void)layer; g_active_layer = 255; }  // ALWAYS 255
```

Consequence for APPLY_HOST_CONTEXT stack/replace:
- After board layer 5 (legacy) + host layer 224 (AHC stack): `stub_get_active_layer()==224`
  (host layer was `layer_on`'d last; board layer number is "shadowed").
- After board layer 5 + AHC replace (deactivate→layer_off(5)→255, then set_host_layer(224)→224):
  `stub_get_active_layer()==224` too.

**So `stub_get_active_layer()==224` in BOTH stack and replace.** The DISTINGUISHER
between stack and replace is the **board-command `on_disable` flag**:
- stack (clear_board=0): `board_cmd_dis==0` (board command NOT torn down).
- replace (clear_board=1): `board_cmd_dis==1` (`disable_command()` fired on_disable).

VERIFIED by probe:
```
[AHC{224,0,0} stack]   layer=224 def_dis=0   ✓ (board preserved)
[AHC{224,1,0} replace] layer=224 def_dis=1   ✓ (board torn down)
[AHC{0xFF,0,0} clear]  layer=255             ✓ (host layer cleared)
```

Implication for the PRP assertions:
- (v)  stack:   assert `stub_get_active_layer()==224 && board_cmd_dis==0`.
- (vi) replace: assert `stub_get_active_layer()==224 && board_cmd_dis==1`.
- (viii) clear: assert `stub_get_active_layer()==255` (set up with NO competing board layer).

---

## CALLBACK-DIFF ORDERING OBSERVATION (test vii)

`apply_host_callbacks` does Phase 1 (disable out-of-set, fires `on_disable`) THEN
Phase 2 (enable in-set, fires `on_enable`). To OBSERVE ordering (disable-before-
enable) — plain counters can't — the host callbacks must STAMP a monotonic sequence:

```c
static int g_seq = 0;
static int cb_mute_on_seq=0, cb_mute_off_seq=0, cb_layout_on_seq=0, cb_layout_off_seq=0;
static void cb_mute_on(void)   { cb_mute_en++;   cb_mute_on_seq   = ++g_seq; }
static void cb_mute_off(void)  { cb_mute_dis++;  cb_mute_off_seq  = ++g_seq; }
static void cb_layout_on(void) { cb_layout_en++; cb_layout_on_seq = ++g_seq; }
static void cb_layout_off(void){ cb_layout_dis++;cb_layout_off_seq= ++g_seq; }
```

Sequence-stamping is BACKWARD-COMPATIBLE with S2's tests (S2 asserts
`cb_*_en==0`/`cb_*_dis==0` after QUERY_*; the `_seq` vars stay 0 there too).

VERIFIED logic (probe counters): AHC{224,0,1,0} → cb0_en=1; AHC{224,0,1,1} →
cb0_dis=1, cb1_en=1 (disable + enable both fire on the transition). With sequence
stamps: cb_mute_off_seq==1 (disable first), cb_layout_on_seq==2 (enable second).

---

## F9 CLEAR-ON-CHANGE PATTERN (test iii template — test_notifier_os.c)

test_notifier_os.c (v) block is the exact template:
```c
char m[] = "blender"; process_full_message(m);   /* establish board state */
reset_flags();
notifier_set_os(OS_LINUX);                        /* CHANGED os => clear */
CK(os_cmd_dis == 1, "prev on_disable fired [F9.1]");
CK(stub_get_active_layer() == 255, "layer deactivated [F9.1]");
CK(os_cmd_en == 0 && def_cmd_en == 0, "no re-dispatch [F9.2]");
```
SET_OS routes through the SAME `apply_os_change` seam, so the SAME observables
apply (board_cmd_dis, stub_get_active_layer). Idempotent (iv) mirrors
test_notifier_os.c (iv): SET_OS to the SAME os → no disable, no deactivate.

---

## FILE-SCOPE REGISTRY ADDITIONS NEEDED (extending the S2 file)

S2's `test_notifier_host.c` (treat as the contract) defines, at file scope:
- `DEFINE_HOST_CALLBACKS({{"mute",cb_mute_on,cb_mute_off},{"layout",cb_layout_on,cb_layout_off}})`
- `DEFINE_SERIAL_COMMANDS({{"neovide",board_cmd_on,board_cmd_off}})`
- `DEFINE_SERIAL_LAYERS({{"neovide",5}})`
- `send_typed`, `CK`, `g_pass`/`g_fail`, `board_cmd_en/dis`, `cb_*_en/dis`.

S3 MUST ADD (for the SET_OS OS-map-selection test ii):
- `DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {{"iTerm", mac_cmd_on, mac_cmd_off}})`
- `DEFINE_SERIAL_LAYERS_OS(OS_MACOS, {{"iTerm", 44}})`
- distinguishable `mac_cmd_en`/`mac_cmd_dis` flags + functions.

S3 MUST ALSO MODIFY the `cb_*` callbacks to add sequence stamps (ordering test vii)
— backward-compatible (counters still increment).

S3 APPENDS the SET_OS (i-iv) + APPLY_HOST_CONTEXT (v-viii) blocks to `main()`.

NOTE: SET_OS tests (i-iv) are BLOCKED (see BLOCKER); they are written correctly
per contract and serve as regression tests once the framing flaw is fixed. They
will FAIL against the current notifier.c — this is expected and DOCUMENTED, not a
test bug. The implementer must NOT weaken them.

---

## OS ENUM VALUES (confirmed: qmk_stubs/os_detection.h)
`OS_UNSURE=0, OS_LINUX=1, OS_WINDOWS=2, OS_MACOS=3, OS_IOS=4`.

Note the collision: OS_MACOS=3=0x03=ETX. This is why SET_OS(OS_MACOS) is doubly
broken (cmd_id AND os_byte both 0x03).