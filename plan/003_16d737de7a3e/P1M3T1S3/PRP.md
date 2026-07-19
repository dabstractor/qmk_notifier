name: "P1.M3.T1.S3 — SET_OS + APPLY_HOST_CONTEXT (stack/replace) test cases for test_notifier_host.c"
description: >
  EXTEND `test_notifier_host.c` (created by the parallel prerequisite P1.M3.T1.S2)
  with the SET_OS (0x03) and APPLY_HOST_CONTEXT (0x05) test slices. This task
  EDITS that one file: (a) ADD file-scope `DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…)` +
  `DEFINE_SERIAL_LAYERS_OS(OS_MACOS,…)` with distinguishable mac callbacks (for the
  SET_OS OS-map-selection test), (b) ADD sequence-stamping to the existing `cb_*`
  host callbacks (backward-compatible — for the callback-diff ORDERING test), and
  (c) APPEND the eight contract test blocks (SET_OS i-iv, APPLY_HOST_CONTEXT v-viii)
  to `main()`, each commented with its §4.6/§4.7/§14 criterion (Mode-A docs).

  🚨 CRITICAL — VERIFIED BLOCKER (full diagnosis in research/findings.md): the
  SET_OS cmd_id is `0x03`, which EQUALS the ETX terminator `0x03`. `hid_notify`'s
  byte loop treats the cmd_id byte as a premature ETX, so `handle_typed_command`
  receives `cmd_id=0` (default case) and the SET_OS handler NEVER runs. Worse,
  `OS_MACOS==3` (`0x03`), so the os_byte argument ALSO collides with ETX. The four
  SET_OS tests (i-iv) CANNOT pass against the current `notifier.c` without a
  framing fix (length-prefix or escaping for binary typed payloads) — a notifier.c /
  PRD-§4.6 change OWNED BY P1.M2, OUT OF SCOPE for this test-only task. The four
  APPLY_HOST_CONTEXT tests (v-viii) PASS (cmd 0x05 ≠ ETX, and none of their args
  are 0x03). This PRP writes ALL eight tests correctly per the contract; it does
  NOT weaken the SET_OS assertions to mask the firmware flaw. See "CRITICAL
  BLOCKER" section.

---

## Goal

**Feature Goal**: Extend `test_notifier_host.c` with the SET_OS and
APPLY_HOST_CONTEXT test slices, exercising the §4.6/§4.7/§14 contract for these
two typed commands against the stub-compiled `notifier.c` (P1.M2 handlers). The
tests drive typed command reports through the public `hid_notify` entry and
assert the `[0x51][cmd_id][payload…]` responses via `stub_get_last_response()`
(P1.M3.T1.S1, LANDED) plus board-state observables (`stub_get_active_layer`,
distinguishable callback flags) — following the EXACT pattern S2 established
(file-scope `DEFINE_*`, `CK` macro, `PASS:`/`FAIL:`, summary, `return g_fail?1:0`).

**Deliverable**: ONE file MODIFIED — `test_notifier_host.c` (repo root, created by
S2). Three edits:
1. ADD `DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…)` + `DEFINE_SERIAL_LAYERS_OS(OS_MACOS,…)`
   + `mac_cmd_en/dis` flags (SET_OS OS-map-selection test ii).
2. ADD sequence-stamping (`g_seq` + `cb_*_on/off_seq`) to the existing host
   callbacks (backward-compatible; enables the callback-diff ORDERING test vii).
3. APPEND eight test blocks to `main()`: SET_OS (i-iv) + APPLY_HOST_CONTEXT (v-viii).

**Success Definition**:
- `test_notifier_host.c` compiles clean with the contract flags (`-Wall -Wextra
  -std=c99`, zero warnings) and the S2 flags.
- The **four APPLY_HOST_CONTEXT tests (v-viii) PASS** (`PASS:` for every assertion;
  0 `FAIL:` lines attributable to them).
- The **four SET_OS tests (i-iv) are present and CORRECT per contract**; against the
  current `notifier.c` they produce `FAIL:` lines that reveal the VERIFIED upstream
  framing flaw (SET_OS cmd_id `0x03` == ETX `0x03`) — this is an EXPECTED, DOCUMENTED
  outcome, NOT a test defect. See "CRITICAL BLOCKER" + "Expected Test Results".
- Mode-A comments map every block to its §4.6/§4.7/§14 criterion.
- No regression: S2's QUERY_INFO/QUERY_CALLBACK blocks still pass; the other two
  notifier suites + 9 pattern-match suites untouched; no other file modified.

## User Persona (if applicable)

**Target User**: The contributor running the §11.2D stub-compile gate and the
P1.M2 owner responsible for the typed-command framing. End users / the desktop
host never see this — it is host-side firmware test infrastructure.

**Use Case**: (1) A developer changes the APPLY_HOST_CONTEXT handler
(`clear_board`, `set_host_layer`, `apply_host_callbacks`) — the gate rebuilds
`test_notifier_host` and the AHC tests catch stack/replace/callback-diff regressions.
(2) A developer working on typed framing sees the SET_OS tests fail and, via this
PRP's BLOCKER section, immediately understands it is a wire-protocol ETX-collision
flaw (not a test bug) and knows the exact fix path.

**Pain Points Addressed**: P1.M2.T2.S2 (SET_OS) and P1.M2.T2.S3 (APPLY_HOST_CONTEXT)
landed handlers with NO host test coverage for them (S2 covered only the read-only
QUERY_INFO/QUERY_CALLBACK). The stack/replace decision (§14) — the load-bearing
host-orchestration mechanism — was previously un-assertable from the host. These
tests gate it. (They also surface, with full diagnosis, that SET_OS is currently
unreachable — a finding the acceptance gate MUST capture rather than hide.)

## Why

- **Closes the P1.M2 test gap for the two state-mutating typed handlers.** SET_OS
  (host-authoritative OS, §4.7) and APPLY_HOST_CONTEXT (per-window stack/replace,
  §14) are the typed commands that change firmware state. APPLY_HOST_CONTEXT in
  particular carries the `clear_board` flag that lets the host choose, per window,
  whether board rules run — the core of host-side rules. P1.M2 implemented them;
  this task gates the APPLY_HOST_CONTEXT half and WRITES (correct) coverage for the
  SET_OS half.
- **APPLY_HOST_CONTEXT stack/replace is the highest-risk typed behavior.** The
  board/host orthogonality (invariant 21), the `clear_board` board-teardown, the
  `0xFF` host-layer clear, and the disable-before-enable callback diff all need
  byte/behavior-level assertions. All four (v-viii) are verified PASSING.
- **Proven pattern, zero design risk.** Identical to S2: file-scope `DEFINE_*`,
  `CK` macro, `send_typed` helper, `stub_get_last_response`/`stub_get_active_layer`
  via manual extern. The F9 clear-on-change assertions mirror `test_notifier_os.c`
  (v)/(iv) verbatim. The stub single-layer tracking gotcha is documented (see
  Context) so stack/replace assertions use the correct distinguisher
  (`board_cmd_dis`), not a false layer-number check.
- **Surfaces a real, verified wire-protocol flaw (research value).** The SET_OS
  tests, written correctly, EXPOSE that `cmd_id 0x03 == ETX 0x03` makes SET_OS
  unreachable. This is exactly the defect TDD exists to catch; this PRP documents
  it with probe evidence + the fix path rather than masking it.

## What

EDIT `test_notifier_host.c` (the S2 file). The eight contract test blocks to append
to `main()` (each with a Mode-A §4.6/§4.7/§14 comment):

**SET_OS (0x03)** — *(see CRITICAL BLOCKER: these are CORRECT tests that will FAIL
against current notifier.c until the ETX framing flaw is fixed upstream)*
1. **(i) SET_OS response layout [§4.6]** — send `SET_OS(OS_MACOS)`; assert
   `r[0]==0x51, r[1]==0x03, r[2]==1` (ack).
2. **(ii) SET_OS changes current_os [§4.7]** — before SET_OS, a legacy string
   matching ONLY the OS_MACOS map ("iTerm") does NOT fire; after `SET_OS(OS_MACOS)`,
   the same string fires the OS_MACOS rule (`mac_cmd_en==1`, default NOT scanned),
   proving `current_os` changed and the OS map is now selected.
3. **(iii) SET_OS change fires F9 clear [§4.7 / F9]** — establish a board layer +
   command via a legacy string; `SET_OS` to a DIFFERENT OS → assert the board layer
   was deactivated (`stub_get_active_layer()==255`) and the previous command's
   `on_disable` fired (`board_cmd_dis==1`); no re-dispatch.
4. **(iv) SET_OS idempotent [§4.7 / F9.3]** — `SET_OS` to the SAME OS → no spurious
   `on_disable`/deactivate.

**APPLY_HOST_CONTEXT (0x05)** — *(all four VERIFIED PASSING)*
5. **(v) Stack (clear_board=0) [§14]** — set board layer via legacy string, then
   `APPLY_HOST_CONTEXT{layer=224, flags=0, count=0}` → assert host layer 224 active
   (`stub_get_active_layer()==224`) AND board command NOT torn down
   (`board_cmd_dis==0`).
6. **(vi) Replace (clear_board=1) [§14]** — set board layer via legacy string, then
   `APPLY_HOST_CONTEXT{layer=224, flags=0x01, count=0}` → assert board torn down
   (`board_cmd_dis==1`) and host layer 224 active (`stub_get_active_layer()==224`).
7. **(vii) Callback diff ordering (disable-before-enable) [§14]** —
   `APPLY_HOST_CONTEXT{224,0,1,0}` → `cb_mute_on` fires; then
   `APPLY_HOST_CONTEXT{224,0,1,1}` → assert `cb_mute_off` (disable id 0) fires
   BEFORE `cb_layout_on` (enable id 1): `cb_mute_off_seq==1 && cb_layout_on_seq==2`.
8. **(viii) APPLY_HOST_CONTEXT{layer=0xFF} clears host layer [§14]** — after a host
   layer is set, `APPLY_HOST_CONTEXT{0xFF,0,0}` → `stub_get_active_layer()==255`.

**DO NOT** (out of scope — siblings): coexistence/backward-compat + multi-report
typed framing → P1.M3.T1.S4; `run_notifier_stub_tests.sh` extension → P1.M3.T2.S1;
any `notifier.c`/`notifier.h`/`qmk_stubs/*` change → P1.M2 / P1.M3.T1.S1.

### Success Criteria

- [ ] `test_notifier_host.c` compiles clean (`-Wall -Wextra -std=c99`, zero warnings).
- [ ] APPLY_HOST_CONTEXT tests (v-viii) all PASS (0 `FAIL:` lines from them).
- [ ] SET_OS tests (i-iv) present with the EXACT contract assertions (NOT weakened).
- [ ] File-scope `DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…)` +
      `DEFINE_SERIAL_LAYERS_OS(OS_MACOS,…)` + `mac_cmd_*` flags added.
- [ ] `cb_*` callbacks sequence-stamped (`g_seq` + `cb_*_on/off_seq`); S2's
      QUERY_INFO/QUERY_CALLBACK assertions still pass (backward-compatible).
- [ ] Mode-A comments map each block to §4.6/§4.7/§14.
- [ ] No file other than `test_notifier_host.c` is modified.

## 🚨 CRITICAL BLOCKER — SET_OS cmd_id (0x03) == ETX terminator (0x03)

**STATUS: VERIFIED by empirical probe against the real `notifier.c` (see
research/findings.md). The four SET_OS tests CANNOT pass without an upstream
notifier.c framing fix that is OUT OF SCOPE for this test-only task.**

**The flaw.** `hid_notify`'s reassembly byte loop treats EVERY `0x03` byte as ETX:

```c
for (uint8_t i = 0; i < length; i++) {
    char c = (char)data[i];                 // data is past the 2-byte magic header
    if (c == ETX_TERMINATOR[0]) {           // 0x03 -> dispatch NOW
        ... handle_typed_command(msg_buffer); break;
    } else { msg_buffer[msg_index++] = c; }
}
```

For SET_OS the post-magic byte stream is `[0xF0, 0x03(cmd_id), os_byte, 0x03(ETX)]`.
The loop appends `0xF0`, then sees `0x03` (the **cmd_id**) and dispatches with
`msg_buffer=[0xF0, 0, 0…]`. `handle_typed_command` reads `cmd_id=data[1]=0` →
**default case** → response `[0x51][0x00][0x00]`, NOT `[0x51][0x03][0x01]`.

**Probe evidence (unmodified notifier.c):**
```
SET_OS(OS_MACOS=3): r[0]=51 r[1]=00 r[2]=00      <- BROKEN (cmd_id 0x03 == ETX)
AHC{224,0,0}:        r[0]=51 r[1]=05 r[2]=01      <- OK (cmd 0x05 != ETX)
```

**Doubly broken for macOS.** `OS_MACOS == 3` (`os_variant_t`, confirmed in
`qmk_stubs/os_detection.h`), so the **os_byte argument is ALSO `0x03`**. Even a
partial fix that only exempts the cmd_id byte (`!(typed_mode && msg_index < 2)`)
leaves SET_OS(OS_MACOS) broken — the os_byte `0x03` still terminates the message
before being appended (probe-confirmed: response fixed to `[0x51][0x03]` but
`current_os` never became MACOS).

**Root cause (wire-protocol design flaw, PRD §4.6).** ETX-termination is safe for
legacy *strings* (text 0x20–0x7E never contains 0x03) but FUNDAMENTALLY incompatible
with *binary* typed payloads (cmd_id/os_byte/layer/flags/count/ids can all be 0x03).
The PRD §4.6 framing reuses string ETX-framing for binary, which is
self-contradictory for SET_OS (cmd_id 0x03), SET_OS(OS_MACOS) (os_byte 0x03), and
any typed command carrying a 0x03 byte.

**Required upstream fix (OWNED BY P1.M2 — NOT this task).** Binary typed payloads
must use length-prefixed framing OR an escape scheme for 0x03. A partial
cmd_id-only patch is INSUFFICIENT (does not fix os_byte=3). This is a notifier.c /
PRD-§4.6 change.

**What this PRP does about it.** It writes the SET_OS tests (i-iv) CORRECTLY per
the contract. Against the current `notifier.c` they will produce `FAIL:` lines
(response[1]==0x00, current_os unchanged). **This is the CORRECT, EXPECTED outcome**
— the tests are right; the firmware has a flaw. The implementer MUST NOT weaken the
SET_OS assertions (e.g. do NOT change `r[1]==0x03` to `r[1]==0x00`) to mask it.
Instead: implement the tests as written, confirm AHC passes, document the SET_OS
failures as the known upstream blocker, and escalate the framing fix to P1.M2.

## All Needed Context

### Context Completeness Check

**Pass (with the documented blocker).** Every assertion is traced from the landed
`notifier.c` handlers and CONFIRMED by empirical probe (research/findings.md). The
test pattern (file-scope `DEFINE_*`, `CK`, `send_typed`, manual-extern stub
accessors) is copied verbatim from the S2 file / `test_notifier_os.c`. The stub
single-layer tracking gotcha (stack/replace distinguisher = `board_cmd_dis`, NOT a
layer number) is documented. The F9 clear-on-change assertions mirror
`test_notifier_os.c` (v)/(iv). The callback-diff ordering observation (sequence
stamps) is specified precisely. The SET_OS blocker is fully diagnosed with the fix
path. An implementer with this PRP + repo access can write the file, prove AHC
passes, and correctly attribute the SET_OS failures.

### Documentation & References

```yaml
# MUST READ — the wire contract this test gates (canonical owner)
- file: PRD.md
  section: "### 4.6 Typed-command namespace (canonical owner)"
  why: "Discriminator 0xF0, [0x51][cmd_id][payload] response, command table
        (0x03 SET_OS, 0x05 APPLY_HOST_CONTEXT), field definitions (os_byte
        0=UNSURE/1=LINUX/2=WINDOWS/3=MACOS/4=IOS; layer 0xFF=LAYER_UNSET clear;
        flags bit0=clear_board), apply_host_callbacks disable-before-enable diff."
  critical: "SET_OS response = [0x51][0x03][ack=1]. APPLY_HOST_CONTEXT response =
        [0x51][0x05][ack=1]. layer>=224 reserved for host. clear_board=1 => replace."

# MUST READ — host-authoritative OS semantics (SET_OS)
- file: PRD.md
  section: "### 4.7 OS source: host-authoritative when a host is connected"
  why: "SET_OS makes the host authoritative for current_os while connected; routes
        through the same seam as notifier_set_os, so an OS CHANGE clears state per
        F9 (disable+deactivate) and is IDEMPOTENT on an unchanged value."

# MUST READ — stack/replace design (APPLY_HOST_CONTEXT)
- file: PRD.md
  section: "## 14. Host-Side Rules & Typed Commands"
  why: "clear_board flag selects stack (0, board untouched) vs replace (1, board
        deactivate_layer+disable_command then apply). set_host_layer(0xFF) clears.
        apply_host_callbacks disable-before-enable. host_cb_enabled[] diff."

# MUST READ — the implementation under test (P1.M2 — LANDED)
- file: notifier.c
  section: "handle_typed_command (SET_OS + APPLY_HOST_CONTEXT cases) +
            apply_os_change + set_host_layer + apply_host_callbacks + hid_notify
            typed fork"
  why: "SET_OS case: data[2]=os_byte -> apply_os_change((os_variant_t)os_byte) ->
        [0x51][0x03][1]. APPLY_HOST_CONTEXT case: data[2]=layer, data[3]=flags,
        data[4]=count, data[5..]=ids; if(flags&0x01){deactivate_layer();
        disable_command();} set_host_layer(layer); apply_host_callbacks(ids,count);
        -> [0x51][0x05][1]. apply_os_change: idempotent (os==current_os -> return);
        else current_os=os; disable_command(); deactivate_layer(). set_host_layer:
        0xFF => clear (layer_off old if set, host_layer=255); else layer_off old +
        layer_on new. apply_host_callbacks: Phase1 disable out-of-set (on_disable),
        Phase2 enable in-set (on_enable)."
  critical: "SEE BLOCKER: hid_notify's byte loop `if (c == ETX_TERMINATOR[0])`
        dispatches on ANY 0x03, including the SET_OS cmd_id (0x03) and OS_MACOS
        os_byte (0x03). SET_OS handler is UNREACHABLE via hid_notify. AHC (0x05)
        is reachable. ids[] tail is `&data[5]`; count clamped to MSG_BUFFER_SIZE-5."

# MUST READ — the API surface (constants + macros consumed by the test)
- file: notifier.h
  why: "NOTIFY_CMD_SET_OS(0x03), NOTIFY_CMD_APPLY_HOST_CONTEXT(0x05),
        NOTIFY_RESPONSE_MARKER(0x51), NOTIFY_CMD_DISCRIMINATOR(0xF0),
        ETX_TERMINATOR[0](0x03), HOST_LAYER_BASE(224). DEFINE_HOST_CALLBACKS,
        DEFINE_SERIAL_COMMANDS/LAYERS, DEFINE_SERIAL_COMMANDS_OS/LAYERS_OS macros.
        Use the NOTIFY_* constants (no magic numbers except 0x81/0x9F magic header,
        layer 224, and char literals)."

# MUST READ — the file being EXTENDED (the S2 contract — treat as shipped)
- file: plan/003_16d737de7a3e/P1M3T1S2/PRP.md
  why: "Defines test_notifier_host.c's exact scaffolding: include block, manual
        externs (hid_notify/process_full_message/stub_get_active_layer/
        stub_get_last_response), DEFINE_HOST_CALLBACKS{mute,layout},
        DEFINE_SERIAL_COMMANDS{neovide->board_cmd}, DEFINE_SERIAL_LAYERS{neovide->5},
        cb_mute_*/cb_layout_* flags, board_cmd_en/dis flags, CK macro, g_pass/g_fail,
        send_typed(cmd,args,nargs) helper, the QUERY_INFO/QUERY_CALLBACK main()
        blocks, summary + return g_fail?1:0. THIS TASK EDITS that file: ADDS the
        OS_MACOS maps + sequence stamps + the eight new blocks."
  critical: "S2 is IMPLEMENTING in parallel; assume it ships EXACTLY this. The cb_*
        callbacks in S2 are plain counters (cb_*_en++); S3 adds sequence stamps
        (cb_*_seq = ++g_seq) — BACKWARD COMPATIBLE (counters still increment, so
        S2's `cb_*_en==0` assertions after QUERY_* still hold)."

# MUST READ — the F9 clear-on-change + idempotent pattern (test iii/iv template)
- file: test_notifier_os.c
  section: "main() blocks (iv) idempotent and (v) clear-on-change"
  why: "The EXACT pattern: establish board state via process_full_message, call
        notifier_set_os(CHANGED) -> assert on_disable fired + layer==255 + no
        re-dispatch; and notifier_set_os(SAME) -> assert no disable + layer
        unchanged. SET_OS uses the SAME apply_os_change seam, so the SAME
        observables apply. Copy the assertion shapes verbatim."

# MUST READ — the stub observables (LANDED: S1 + existing)
- file: qmk_stubs/qmk_stubs.c
  why: "stub_get_last_response() returns const uint8_t* into a file-static 32-byte
        buffer captured by raw_hid_send (last-write-wins). stub_get_active_layer()
        returns g_active_layer. CRITICAL GOTCHA: the stub models layers as a SINGLE
        g_active_layer (layer_on(X)->X; layer_off(X)->255 ALWAYS), NOT a bitmask.
        So after board layer + host layer, g_active_layer==host layer (board
        shadowed); the stack/replace DISTINGUISHER is board_cmd_dis, NOT a layer
        number. See 'Known Gotchas'."

# REFERENCE — test strategy (mandates the test_notifier_dispatch.c pattern)
- file: plan/003_16d737de7a3e/architecture/host_rules_architecture.md
  section: "## Test strategy / The four typed-command handlers (SET_OS, APPLY_HOST_CONTEXT)"
  why: "test_notifier_host.c follows the EXACT pattern of test_notifier_dispatch.c;
        the runner greps grep -c '^FAIL:'. Documents SET_OS->apply_os_change seam
        and APPLY_HOST_CONTEXT clear_board semantics."

# MUST READ — THIS TASK'S verified research (the blocker + byte layouts + stub gotcha)
- file: plan/003_16d737de7a3e/P1M3T1S3/research/findings.md
  why: "Empirical probe evidence for every assertion; the full SET_OS/ETX blocker
        diagnosis; the stub single-layer tracking gotcha; the callback-diff ordering
        observation; the F9 pattern template; file-scope additions needed. READ THIS
        BEFORE implementing — it is the authoritative basis for the assertions."
```

### Current Codebase tree (relevant subset)

```bash
notifier.c              # implementation under test (P1.M2 — SET_OS + AHC handlers; hid_notify ETX flaw lives here)
notifier.h              # NOTIFY_* constants + DEFINE_* / DEFINE_*_OS macros
qmk_stubs/
  qmk_stubs.c           # stubs: layer_on/off (SINGLE g_active_layer!), raw_hid_send,
                         #   stub_get_active_layer, stub_get_last_response (S1, LANDED)
  os_detection.h        # os_variant_t: OS_UNSURE=0,LINUX=1,WINDOWS=2,MACOS=3,IOS=4
test_notifier_host.c    # CREATED BY S2 (parallel) — THIS TASK EDITS IT (not a new file)
test_notifier_os.c      # PRECEDENT — F9 clear-on-change (v) + idempotent (iv) patterns
test_notifier_dispatch.c# PRECEDENT — file-scope DEFINE_* + hid_notify driving
run_notifier_stub_tests.sh # build/gate chain (extended in P1.M3.T2.S1 — NOT here)
```

### Desired Codebase tree with files to be added/modified

```bash
test_notifier_host.c   # MODIFIED — +OS_MACOS maps, +sequence stamps, +8 test blocks
# nothing else changes
```

### Known Gotchas of our codebase & Library Quirks

```c
// 🚨 CRITICAL BLOCKER (SEE ABOVE): SET_OS cmd_id 0x03 == ETX 0x03. hid_notify's
// byte loop dispatches on the cmd_id byte, so handle_typed_command gets cmd_id=0
// (default case) -> response [0x51][0x00], NOT [0x51][0x03]. OS_MACOS=3 makes the
// os_byte ALSO collide. The SET_OS tests (i-iv) WILL FAIL against current
// notifier.c — EXPECTED, DOCUMENTED, NOT a test bug. Do NOT weaken the assertions.
// The fix is a notifier.c framing change (P1.M2 owned) — out of scope here.

// CRITICAL (stub layers): qmk_stubs models QMK layers as a SINGLE g_active_layer:
//   layer_on(X) -> g_active_layer = X        (overwrites)
//   layer_off(X) -> g_active_layer = 255     (ALWAYS, ignores X)
// So after board layer 5 + host layer 224: g_active_layer==224 (board shadowed).
// After replace (deactivate->255 then set_host_layer->224): g_active_layer==224 too.
// => stack vs replace are distinguished by board_cmd_dis (0=stack, 1=replace),
//    NOT by stub_get_active_layer() (==224 in BOTH). VERIFIED by probe.

// GOTCHA: apply_host_callbacks Phase 1 disables BEFORE Phase 2 enables, but plain
// counters can't show ORDER. Use a monotonic sequence stamp (++g_seq) captured at
// callback time; assert cb_mute_off_seq < cb_layout_on_seq for disable-before-enable.

// GOTCHA: send_typed places cmd_id at rep[3] and the ETX at rep[4+nargs]. For
// APPLY_HOST_CONTEXT: rep[3]=0x05, rep[4]=layer, rep[5]=flags, rep[6]=count,
// rep[7..]=ids, rep[7+count]=0x03. handle_typed_command reads data[2]=layer,
// data[3]=flags, data[4]=count, data[5..]=ids (data[0]=0xF0, data[1]=cmd). Use a
// local uint8_t args[] and pass it (no compound literals — clean under -Wextra).

// GOTCHA: layer 224 is HOST_LAYER_BASE; pass it as a literal 224 (or define a local
// const). 0xFF (255) is LAYER_UNSET (host-layer clear). Avoid 0x03 as ANY arg byte
// (ETX collision — do not use layer/flags/count/id == 3 in AHC tests; the contract's
// values 224/0/1/0xFF and ids 0/1 are all safe).

// GOTCHA: reset state between blocks. board_cmd_en/dis, mac_cmd_en/dis, cb_* flags,
// and g_seq must be zeroed at the start of each block that reads them, so blocks are
// independent. (S2's board_cmd flags are reused; S3's mac_cmd flags + g_seq are new.)

// GOTCHA: do NOT modify run_notifier_stub_tests.sh (P1.M3.T2.S1), qmk_stubs.c (S1),
// notifier.* (P1.M2), or any other test file. This task edits ONE file only.

// GOTCHA: avoid -Wmissing-field-initializers on the map rows by including the
// trailing case_sensitive field (false) in EVERY row, including the new OS_MACOS
// rows. S2's rows already include it; mirror them.
```

## Implementation Blueprint

### Data models and structure

**None created.** Consumes existing types (`host_callback_t`, `command_map_t`,
`layer_map_t`, `os_variant_t`) via the `DEFINE_*` macros + `NOTIFY_*` constants.
Adds only: `mac_cmd_en/dis` + `mac_cmd_on/off`, a `g_seq` counter + `cb_*_on/off_seq`
stamps, and local `uint8_t args[]` buffers inside the test blocks.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY test_notifier_host.c — ADD OS_MACOS maps + mac callbacks (for SET_OS test ii)
  - ADD distinguishable mac command flags + functions (F6 precedent; test_notifier_os.c shape):
      static int mac_cmd_en = 0, mac_cmd_dis = 0;
      static void mac_cmd_on(void)  { mac_cmd_en++; }
      static void mac_cmd_off(void) { mac_cmd_dis++; }
  - ADD (file scope, after S2's DEFINE_SERIAL_*):
      DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, { { "iTerm", mac_cmd_on, mac_cmd_off, false } });
      DEFINE_SERIAL_LAYERS_OS(OS_MACOS,   { { "iTerm", 44, false } });
  - WHY: "iTerm" exists ONLY in the OS_MACOS maps (not in S2's default "neovide"
    maps), so it matches the OS map iff current_os==OS_MACOS. This is the SET_OS
    (ii) OS-map-selection probe. (These maps are inert until SET_OS changes
    current_os — exactly what test ii asserts.)
  - GOTCHA: include the trailing `false` (case_sensitive) on every row to avoid
    -Wmissing-field-initializers under -Wextra.

Task 2: MODIFY test_notifier_host.c — ADD sequence stamps to cb_* callbacks (ordering test vii)
  - ADD a monotonic sequence counter + per-callback stamps:
      static int g_seq = 0;
      static int cb_mute_on_seq=0, cb_mute_off_seq=0, cb_layout_on_seq=0, cb_layout_off_seq=0;
  - MODIFY the four cb_* functions (from S2) to ALSO stamp the sequence:
      static void cb_mute_on(void)   { cb_mute_en++;   cb_mute_on_seq   = ++g_seq; }
      static void cb_mute_off(void)  { cb_mute_dis++;  cb_mute_off_seq  = ++g_seq; }
      static void cb_layout_on(void) { cb_layout_en++; cb_layout_on_seq = ++g_seq; }
      static void cb_layout_off(void){ cb_layout_dis++;cb_layout_off_seq= ++g_seq; }
  - WHY: plain counters can't show disable-before-enable ORDER; the stamps let
    test vii assert cb_mute_off_seq < cb_layout_on_seq.
  - BACKWARD COMPATIBILITY: the counters (cb_*_en/dis) STILL increment, so S2's
    `cb_*_en==0` assertions after QUERY_INFO/QUERY_CALLBACK still hold (stamps stay
    0 there). No change to S2's behavior.

Task 3: MODIFY test_notifier_host.c — APPEND SET_OS blocks (i-iv) to main()
  - BLOCK (i) §4.6 SET_OS response layout. (🚨 BLOCKED — see Expected Test Results)
      uint8_t os = OS_MACOS;
      const uint8_t *r = send_typed(NOTIFY_CMD_SET_OS, &os, 1);
      CK(r[0] == NOTIFY_RESPONSE_MARKER,        "(i) SET_OS r[0]=0x51 marker [§4.6]");
      CK(r[1] == NOTIFY_CMD_SET_OS,             "(i) SET_OS r[1]=0x03 cmd echo [§4.6]");
      CK(r[2] == 1,                             "(i) SET_OS r[2]=ack=1 [§4.6]");
  - BLOCK (ii) §4.7 SET_OS changes current_os (OS_MACOS map now selected).
      /* before SET_OS (current_os==OS_UNSURE): "iTerm" matches ONLY the OS_MACOS
       * map, so it must NOT fire at OS_UNSURE. */
      mac_cmd_en = mac_cmd_dis = 0;
      { char m[] = "iTerm"; process_full_message(m); }
      CK(mac_cmd_en == 0,                       "(ii) pre-SET_OS: OS_MACOS-only pattern does not match at OS_UNSURE [§4.7]");
      /* SET_OS(OS_MACOS) -> current_os=OS_MACOS */
      uint8_t os = OS_MACOS; (void)send_typed(NOTIFY_CMD_SET_OS, &os, 1);
      mac_cmd_en = mac_cmd_dis = 0;
      { char m[] = "iTerm"; process_full_message(m); }
      CK(mac_cmd_en == 1,                       "(ii) post-SET_OS(OS_MACOS): OS_MACOS command fired (current_os changed) [§4.7]");
      CK(stub_get_active_layer() == 44,         "(ii) post-SET_OS(OS_MACOS): OS_MACOS layer 44 selected [§4.7]");
  - BLOCK (iii) §4.7/F9 SET_OS change fires F9 clear (mirror test_notifier_os.c (v)).
      /* establish board layer 5 + board command via default map */
      board_cmd_en = board_cmd_dis = 0;
      { char m[] = "neovide"; process_full_message(m); }
      CK(stub_get_active_layer() == 5,          "(iii) setup: board layer 5 active");
      CK(board_cmd_en == 1,                     "(iii) setup: board command enabled");
      board_cmd_dis = 0;
      uint8_t os = OS_LINUX; (void)send_typed(NOTIFY_CMD_SET_OS, &os, 1); /* CHANGED (MACOS->LINUX) */
      CK(board_cmd_dis == 1,                    "(iii) SET_OS change: prev command on_disable fired [§4.7/F9.1]");
      CK(stub_get_active_layer() == 255,        "(iii) SET_OS change: board layer deactivated (cleared) [§4.7/F9.1]");
      CK(board_cmd_en == 0 || board_cmd_en == 1,"(iii) SET_OS change: no re-dispatch (on_enable not re-fired) [§4.7/F9.2]");
      /* (board_cmd_en is unchanged by apply_os_change; the no-re-dispatch point is
         that disable_command fired once, not that enable re-fired. The layer==255
         above is the no-re-activate proof.) */
  - BLOCK (iv) §4.7/F9.3 SET_OS idempotent (mirror test_notifier_os.c (iv)).
      board_cmd_dis = 0;
      uint8_t os = OS_LINUX; (void)send_typed(NOTIFY_CMD_SET_OS, &os, 1); /* SAME os */
      CK(board_cmd_dis == 0,                    "(iv) SET_OS idempotent: no spurious on_disable on same-OS [§4.7/F9.3]");
      CK(stub_get_active_layer() == 255,        "(iv) SET_OS idempotent: no layer change on same-OS [§4.7/F9.3]");

Task 4: MODIFY test_notifier_host.c — APPEND APPLY_HOST_CONTEXT blocks (v-viii) to main()
  - BLOCK (v) §14 STACK (clear_board=0): board preserved + host layer active.
      board_cmd_en = board_cmd_dis = 0;
      { char m[] = "neovide"; process_full_message(m); }     /* board layer 5 + board cmd */
      CK(stub_get_active_layer() == 5,          "(v) setup: board layer 5 active");
      board_cmd_dis = 0;
      uint8_t a[] = { 224, 0x00, 0 };           /* layer=224, flags=0 (clear_board=0), count=0 */
      (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 3);
      CK(stub_get_active_layer() == 224,        "(v) stack: host layer 224 active (highest-layer-wins) [§14]");
      CK(board_cmd_dis == 0,                    "(v) stack: board command NOT torn down (clear_board=0) [§14]");
  - BLOCK (vi) §14 REPLACE (clear_board=1): board torn down + host layer active.
      board_cmd_en = board_cmd_dis = 0;
      { char m[] = "neovide"; process_full_message(m); }     /* board layer 5 + board cmd */
      CK(stub_get_active_layer() == 5,          "(vi) setup: board layer 5 active");
      board_cmd_dis = 0;
      uint8_t a[] = { 224, 0x01, 0 };           /* layer=224, flags=0x01 (clear_board=1), count=0 */
      (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 3);
      CK(board_cmd_dis == 1,                    "(vi) replace: board command torn down (clear_board=1) [§14]");
      CK(stub_get_active_layer() == 224,        "(vi) replace: host layer 224 active [§14]");
  - BLOCK (vii) §14 callback diff ordering (disable-before-enable).
      /* enable id 0 only */
      g_seq = 0; cb_mute_en = cb_mute_dis = cb_layout_en = cb_layout_dis = 0;
      cb_mute_on_seq = cb_mute_off_seq = cb_layout_on_seq = cb_layout_off_seq = 0;
      { uint8_t a[] = { 224, 0x00, 1, 0 }; (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 4); }
      CK(cb_mute_en == 1,                       "(vii) AHC{[0]}: on_enable fired for id 0 [§14]");
      /* switch to id 1 only -> id 0 disabled, id 1 enabled */
      g_seq = 0; cb_mute_on_seq = cb_mute_off_seq = cb_layout_on_seq = cb_layout_off_seq = 0;
      { uint8_t a[] = { 224, 0x00, 1, 1 }; (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 4); }
      CK(cb_mute_dis == 1,                      "(vii) AHC{[1]}: on_disable fired for id 0 [§14]");
      CK(cb_layout_en == 1,                     "(vii) AHC{[1]}: on_enable fired for id 1 [§14]");
      CK(cb_mute_off_seq == 1 && cb_layout_on_seq == 2,
                                               "(vii) AHC{[1]}: on_disable(id0) BEFORE on_enable(id1) [§14 disable-before-enable]");
  - BLOCK (viii) §14 APPLY_HOST_CONTEXT{layer=0xFF} clears host layer.
      /* establish a host layer with NO competing board layer (fresh: board is LAYER_UNSET) */
      { uint8_t a[] = { 224, 0x00, 0 }; (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 3); }
      CK(stub_get_active_layer() == 224,        "(viii) setup: host layer 224 active");
      { uint8_t a[] = { 0xFF, 0x00, 0 }; (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 3); }
      CK(stub_get_active_layer() == 255,        "(viii) AHC{layer=0xFF}: host layer cleared (LAYER_UNSET) [§14]");

Task 5: VALIDATE — compile (strict) + run + interpret results (see Validation Loop)
  - COMPILE strict: gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
        -Iqmk_stubs -I. notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c -o /tmp/tnh
  - RUN: /tmp/tnh ; fails=$(/tmp/tnh 2>/dev/null | grep -c '^FAIL:' || true)
  - EXPECT: AHC blocks (v-viii) all PASS; SET_OS blocks (i-iv) FAIL with the
        documented blocker signatures (r[1]==0x00; current_os unchanged). The S2
        QUERY_INFO/QUERY_CALLBACK blocks still PASS (no regression). See "Expected
        Test Results".
  - CLEAN: rm -f /tmp/tnh
```

> Tasks 1-4 are EDITS to the single file (use the `edit` tool against the S2 file's
> scaffolding: insert the OS_MACOS maps + mac callbacks after S2's DEFINE_SERIAL_*,
> modify the four cb_* functions to add sequence stamps, and append the eight blocks
> before the summary/return). Task 5 is validation.

### Reference: the APPLY_HOST_CONTEXT arg layout (do not get this wrong)

```c
/* send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, args, nargs) builds:
 *   rep = [0x81, 0x9F, 0xF0, 0x05, args[0], args[1], ..., 0x03]
 * After magic strip + reassembly, handle_typed_command sees msg_buffer:
 *   data[0]=0xF0, data[1]=0x05(cmd), data[2]=args[0]=layer,
 *   data[3]=args[1]=flags, data[4]=args[2]=count, data[5..]=ids
 * So for a 3-arg call {layer, flags, count}:    args[]={layer,flags,count}, nargs=3
 * For a 4-arg call {layer, flags, count, id0}:  args[]={layer,flags,count,id0}, nargs=4
 * VERIFIED: AHC{224,0,0}->r=[51 05 01]; stack def_dis=0; replace def_dis=1; 0xFF->255.
 */

/* send_typed(NOTIFY_CMD_SET_OS, &os_byte, 1) builds:
 *   rep = [0x81, 0x9F, 0xF0, 0x03, os_byte, 0x03]
 * 🚨 BLOCKED: cmd_id 0x03 == ETX 0x03 -> dispatches on the cmd byte -> cmd_id=0.
 */
```

### Implementation Patterns & Key Details

```c
// PATTERN: reuse S2's send_typed + CK + manual externs verbatim (no new scaffolding).
// The eight new blocks go inside main(), BEFORE the S2 summary line, each in its own
// { } scope so local `char m[]` / `uint8_t a[]` don't collide across blocks.

// PATTERN: distinguishable callbacks reveal which map/path fired (F6). board_cmd_*
// (default board map) vs mac_cmd_* (OS_MACOS map) reveal OS-map selection; board_cmd_dis
// reveals stack vs replace; cb_*_seq reveals callback-diff ordering.

// CRITICAL (stack/replace): the stub's single g_active_layer means stub_get_active_layer()
// is 224 in BOTH stack and replace. The DISTINGUISHER is board_cmd_dis (0 vs 1). Do NOT
// assert stub_get_active_layer()==5 after a stack APPLY_HOST_CONTEXT — it is 224 (host
// layer won; board shadowed in the stub). Assert board_cmd_dis==0 instead.

// CRITICAL (SET_OS BLOCKED): the four SET_OS blocks will FAIL. That is correct. Do NOT
// "fix" them by relaxing r[1]==0x03 to r[1]==0x00 or skipping the OS-map assertion —
// that would hide the firmware flaw. Implement them EXACTLY as specified.

// PATTERN: reset flags + g_seq at the start of each block that reads them, so blocks
// are independent and asserts are unambiguous.

// PATTERN: every CK name carries its [§4.6]/[§4.7]/[§14]/[F9.x] tag (Mode-A docs).
```

### Integration Points

```yaml
NO database / config / route / migration / firmware changes. One test file edited.

BUILD/LINK:
  - test_notifier_host.c is MODIFIED (S2 created it). It compiles + links with the
    SAME flags S2 used. No new build flag, no new TU.

SYMBOL NAMESPACE:
  - DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…)/DEFINE_SERIAL_LAYERS_OS(OS_MACOS,…)
    generate strong _notifier_*_map_OS_MACOS[_size] symbols overriding notifier.c's
    weak defaults in THIS binary only (separate TU; no clash with test_notifier_os.c,
    which defines its OWN OS_MACOS maps).

SCOPE BOUNDARY (do NOT cross — siblings own these):
  - coexistence/backward-compat + multi-report typed framing -> P1.M3.T1.S4
  - run_notifier_stub_tests.sh extension (3rd binary)              -> P1.M3.T2.S1
  - qmk_stubs.c / notifier.c / notifier.h                          -> P1.M2 / S1
  - The SET_OS ETX-collision FIX                                   -> P1.M2 (BLOCKER)
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Strict compile (the runner-readiness gate S2 targets; ZERO warnings required):
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c \
    -o /tmp/test_notifier_host && echo "STRICT COMPILE OK"
# Expected: "STRICT COMPILE OK" with NO warnings. If you see
# -Wmissing-field-initializers, add the trailing `false` to the offending map row.
# If you see -Wunused-but-set-variable, a flag you set isn't read — read it in an
# assertion (don't (void) it).
```

### Level 2: Run + Interpret (Component Validation)

```bash
/tmp/test_notifier_host
echo "exit=$?"
fails=$(/tmp/test_notifier_host 2>/dev/null | grep -c '^FAIL:' || true)
echo "total FAIL: lines = $fails"
# EXPECTED (against current notifier.c, with the SET_OS blocker):
#   - S2 QUERY_INFO/QUERY_CALLBACK blocks: PASS (no regression)
#   - APPLY_HOST_CONTEXT blocks (v)-(viii): PASS
#   - SET_OS blocks (i)-(iv): FAIL with the documented signatures:
#       (i)   r[1]==0x00 not 0x03          (cmd_id 0x03 == ETX)
#       (ii)  mac_cmd_en==0 after SET_OS   (current_os never changed)
#       (iii) board_cmd_dis==0, layer!=255 (apply_os_change never called)
#       (iv)  (passes vacuously-ish, but (iii) already failed)
# The AHC PASS + SET_OS FAIL pattern CONFIRMS the tests are correct and the flaw
# is upstream (notifier.c). See "Expected Test Results" below.
rm -f /tmp/test_notifier_host
```

### Level 3: Regression (no other suite touched)

```bash
# This task edits ONE test file (additive within it). The existing suites must stay green:
./run_notifier_stub_tests.sh
# Expected: "notifier dispatch fails=0", "notifier os fails=0",
# "✓ notifier stub-compile gate PASSED", exit 0. (test_notifier_host is NOT yet wired
# into this script — that is P1.M3.T2.S1.)
./run_all_tests.sh
# Expected: all 9 pattern-match suites green (this task changed no matcher code).
```

### Level 4: Blocker confirmation (proves the SET_OS failures are the ETX flaw, not a test bug)

```bash
# Confirm the SET_OS failures vanish ONLY with a notifier.c framing fix (out of scope
# to apply — this is a READ-ONLY diagnostic proving the diagnosis). Build a patched
# COPY in /tmp (do NOT modify repo notifier.c):
sed 's/if (c == ETX_TERMINATOR\[0\]) {/if (c == ETX_TERMINATOR[0] \&\& !(typed_mode \&\& msg_index < 2)) {/' \
    notifier.c > /tmp/notifier_patched.c
gcc -Wall -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    /tmp/notifier_patched.c qmk_stubs/qmk_stubs.c test_notifier_host.c -o /tmp/tnh_patched
/tmp/tnh_patched 2>/dev/null | grep -E 'SET_OS|current_os|F9'
# Expected: with the partial (cmd_id-only) fix, SET_OS response becomes [0x51][0x03]
# (test i passes) BUT (ii) still fails for OS_MACOS (os_byte 0x03 still collides) —
# proving the cmd_id fix ALONE is insufficient and the real fix is framing redesign.
# This diagnostic DOCUMENTS why the SET_OS tests are blocked; it is NOT a fix to ship.
rm -f /tmp/notifier_patched.c /tmp/tnh_patched
```

### Expected Test Results (authoritative — read before judging pass/fail)

| Block | Command | Expected against CURRENT notifier.c | Why |
|-------|---------|--------------------------------------|-----|
| S2 (i)-(iv) | QUERY_INFO/QUERY_CALLBACK | **PASS** | no regression; S2 unchanged |
| (i)   | SET_OS response | **FAIL** (r[1]==0x00) | cmd_id 0x03 == ETX (BLOCKER) |
| (ii)  | SET_OS changes os | **FAIL** (mac_cmd_en==0) | SET_OS never ran (BLOCKER) |
| (iii) | SET_OS F9 clear | **FAIL** (board_cmd_dis==0) | apply_os_change never called (BLOCKER) |
| (iv)  | SET_OS idempotent | **FAIL/pass** | depends on prior current_os; moot under blocker |
| (v)   | AHC stack | **PASS** | clear_board=0; cmd 0x05 != ETX |
| (vi)  | AHC replace | **PASS** | clear_board=1; board_cmd_dis==1 |
| (vii) | AHC callback diff | **PASS** | disable-before-enable ordering |
| (viii)| AHC clear 0xFF | **PASS** | set_host_layer(LAYER_UNSET) |

**The task's OUTPUT criterion ("SET_OS and APPLY_HOST_CONTEXT tests pass with 0 FAIL")
CANNOT be fully met against the current notifier.c because SET_OS is unreachable
(VERIFIED BLOCKER). The APPLY_HOST_CONTEXT half (v-viii) DOES pass. The SET_OS half
is written correctly and will pass once the P1.M2 framing flaw is fixed.**

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 strict compile (`-Wall -Wextra -std=c99`) → ZERO warnings.
- [ ] Level 2 run: APPLY_HOST_CONTEXT blocks (v)-(viii) all PASS.
- [ ] Level 2 run: SET_OS blocks (i)-(iv) present with EXACT contract assertions
      (NOT weakened) and FAIL with the documented blocker signatures.
- [ ] Level 2 run: S2 QUERY_INFO/QUERY_CALLBACK blocks still PASS (no regression).
- [ ] Level 3 `./run_notifier_stub_tests.sh` still PASSED; `./run_all_tests.sh` green.
- [ ] Level 4 blocker diagnostic confirms the SET_OS failures are the ETX flaw.

### Feature Validation

- [ ] (i) SET_OS asserts r[0]=0x51, r[1]=0x03, r[2]=1 (§4.6).
- [ ] (ii) SET_OS(OS_MACOS) makes "iTerm" fire mac_cmd (OS map selected) (§4.7).
- [ ] (iii) SET_OS change fires board on_disable + deactivates layer + no re-dispatch (F9).
- [ ] (iv) SET_OS same-OS is idempotent (F9.3).
- [ ] (v) AHC stack: host layer 224 + board_cmd_dis==0 (§14).
- [ ] (vi) AHC replace: host layer 224 + board_cmd_dis==1 (§14).
- [ ] (vii) AHC callback diff: disable(id0) BEFORE enable(id1) via sequence stamps (§14).
- [ ] (viii) AHC{0xFF}: stub_get_active_layer()==255 (§14 host-layer clear).
- [ ] Mode-A comments map each block to §4.6/§4.7/§14.

### Code Quality Validation

- [ ] Follows S2's pattern verbatim (CK macro, send_typed, manual externs, summary, return).
- [ ] Uses NOTIFY_* constants (no magic numbers except 0x81/0x9F, layer 224, 0xFF).
- [ ] OS_MACOS maps added with distinguishable mac_cmd callbacks; trailing `false` on rows.
- [ ] cb_* callbacks sequence-stamped (backward-compatible with S2's assertions).
- [ ] stack/replace distinguished by board_cmd_dis (not a false layer-number check).
- [ ] Diff is purely the one edited file (no other file touched).

### Documentation & Deployment

- [ ] Mode-A comments ride WITH the work (DOCS §5) — each block's [§x.y] tag.
- [ ] The SET_OS BLOCKER is surfaced (tests fail correctly; not masked).
- [ ] No README change (README sync is P1.M3.T3.S1). No new env vars / config.

---

## Anti-Patterns to Avoid

- ❌ Don't WEAKEN the SET_OS assertions to make them pass (e.g. r[1]==0x00, or skip
  the OS-map assertion). The cmd_id 0x03 == ETX flaw is REAL; the correct tests
  expose it. Masking it defeats the purpose of the test suite.
- ❌ Don't assert `stub_get_active_layer()==5` after a STACK APPLY_HOST_CONTEXT — the
  stub's single g_active_layer is 224 (host layer won; board shadowed). Use
  `board_cmd_dis==0` as the "board preserved" proof.
- ❌ Don't try to observe callback-diff ORDERING with plain counters — they can't show
  sequence. Use the `g_seq`/`cb_*_seq` stamps.
- ❌ Don't use any arg byte == 0x03 in an APPLY_HOST_CONTEXT test (ETX collision). The
  contract's values (224/0/1/0xFF, ids 0/1) are all safe; keep them.
- ❌ Don't add coexistence / multi-report test cases — those are P1.M3.T1.S4.
- ❌ Don't modify `notifier.c`/`notifier.h`/`qmk_stubs/*`/`run_notifier_stub_tests.sh`/
  any other test file — this task edits ONE file. The SET_OS fix is P1.M2's job.
- ❌ Don't `#include` a header for the stub observables — use MANUAL EXTERNs (S2's
  convention). `stub_get_last_response`/`stub_get_active_layer` already declared by S2.
- ❌ Don't hardcode 0x51/0x03/0x05/0xF0/0xFF/224 — use NOTIFY_* constants (224 and 0xFF
  are the only literals; 224 = HOST_LAYER_BASE, 0xFF = LAYER_UNSET).

---

## Confidence Score & Blocker Summary

**Confidence: 9/10 for the APPLY_HOST_CONTEXT half (v-viii) — all four VERIFIED
PASSING by empirical probe. 0/10 for the SET_OS half (i-iv) reaching "0 FAIL"
against the current notifier.c — the cmd_id 0x03 == ETX 0x03 collision (doubly so
for OS_MACOS, whose os_byte is also 0x03) makes the SET_OS handler unreachable
via hid_notify. This is a VERIFIED, DOCUMENTED upstream framing flaw (PRD §4.6 /
notifier.c) OWNED BY P1.M2 and OUT OF SCOPE for this test-only task.**

**The PRP provides correct, complete tests for all eight contract cases. The
implementer should: (1) implement all eight, (2) confirm AHC (v-viii) passes, (3)
confirm SET_OS (i-iv) fails with the documented signatures (proving the tests are
correct), (4) escalate the SET_OS framing flaw to P1.M2 with this PRP's diagnosis +
fix path. The task's "0 FAIL" OUTPUT criterion cannot be fully met until P1.M2 fixes
the typed framing; this PRP makes that fact explicit rather than hiding it.**