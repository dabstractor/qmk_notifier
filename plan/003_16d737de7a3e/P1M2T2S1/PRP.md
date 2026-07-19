# PRP — P1.M2.T2.S1: typed response builder + handle_typed_command dispatch + QUERY_INFO + QUERY_CALLBACK handlers

## Goal

**Feature Goal**: Replace the `handle_typed_command` STUB (notifier.c:626, landed
by P1.M2.T1.S1 as a linker placeholder) with the **real typed-command dispatch**:
a `send_typed_response` builder that always emits a 32-byte report with the
`0x51` marker, and the `handle_typed_command` body that switches on `cmd_id` and
services **QUERY_INFO (0x01)** (capability handshake: proto_ver + feature_flags +
callback_count + board_rules_present, and flips `has_been_queried`) and
**QUERY_CALLBACK (0x02)** (name discovery by index), with a safe `default` for
unknown/reserved ids (incl 0x04). The implementation reproduces the PRD §4.6 wire
contract byte-for-byte and never crashes on malformed input. SET_OS (0x03) and
APPLY_HOST_CONTEXT (0x05) are NOT implemented here — they fall through to
`default` and land in P1.M2.T2.S2/S3.

**Deliverable**: ONE file modified — `notifier.c`, changed in exactly ONE region:
the `handle_typed_command` stub (lines ~622-630, the `/* STUB — … */` comment +
the `static bool handle_typed_command(char *buf) { (void)buf; return false; }`
body) is REPLACED with (1) a Mode-A block comment + `send_typed_response` helper,
then (2) a Mode-A block comment + the real `handle_typed_command`. No other file
changes; the `static bool handle_typed_command(char *data)` signature is preserved
(only the param name `buf`→`data` changes to match the item contract).

**Success Definition**:
- `send_typed_response(uint8_t cmd_id, const uint8_t *payload, uint8_t payload_len)`
  present, `static`, immediately before `handle_typed_command`. Fills a 32-byte
  response: `[0x51][cmd_id][payload…][zero-pad]`, caps payload at 30 bytes, calls
  `raw_hid_send(response, RAW_REPORT_SIZE)`.
- `handle_typed_command(char *data)` switches on `(uint8_t)data[1]`; QUERY_INFO
  (0x01) and QUERY_CALLBACK (0x02) cases implemented per §4.6; `default` replies
  `[0x51][cmd_id]` no payload. Returns `true` always.
- `gcc -Wall -Wextra -std=c99 -c notifier.c` (stub flags) → **exit 0** with
  **exactly two** warnings: `apply_host_callbacks` + `set_host_layer` (both
  carried → P1.M2.T2.S2/S3). **`board_rules_present` and `has_been_queried` no
  longer warn** (QUERY_INFO now uses them — they retire this task, as the prior
  PRPs predicted).
- A multi-TU capture harness (strong `DEFINE_HOST_CALLBACKS` overriding the weak
  accessors; capturing `raw_hid_send`) driving the PUBLIC `hid_notify` confirms:
  QUERY_INFO returns `[0x51][0x01][proto=2][flags][count][board_rules]`;
  QUERY_CALLBACK returns the name (NUL-padded) for a valid index and `[index][0x00]`
  for out-of-range; unknown 0x04 returns `[0x51][0x04]`; a legacy string
  (data[2]≠0xF0) still gets the legacy 0/1 ack (backward-compat).
- `test_notifier_dispatch` stays 14/14 and `test_notifier_os` stays 31/31, 0 FAIL.
  `run_notifier_stub_tests.sh` prints "✓ notifier stub-compile gate PASSED".
- The diff is confined to the one region (the stub replacement).

## User Persona (if applicable)

**Target User**: (1) The desktop host (QMKonnect) that, on device (re)connect,
sends `QUERY_INFO` to detect a typed-command-capable firmware, then sweeps
`QUERY_CALLBACK(i)` to build the `name→id` map (§4.6 handshake). (2) The next two
subtasks (P1.M2.T2.S2 SET_OS, P1.M2.T2.S3 APPLY_HOST_CONTEXT) which insert their
own `case` branches before the `default`.

**Use Case**: Host connects → sends `[0x81][0x9F][0xF0][0x01][0x03]` (QUERY_INFO,
no args, ETX) → firmware replies `[0x51][0x01][02][flags][count][board_rules]` and
sets `has_been_queried`. Host sees `response[0]==0x51` ⇒ typed-capable
(`proto_ver==2`); reads flags/count to decide whether to sweep callbacks. For
`i in 0..count`: sends `[0x81][0x9F][0xF0][0x02][i][0x03]` → firmware replies
`[0x51][0x02][i][name bytes]` (or `[i][0x00]` if absent).

**User Journey**: `hid_notify` (typed report) → `data[2]==0xF0` sets typed_mode →
reassembly into msg_buffer (`[0xF0][cmd_id][args…]`) → at ETX,
`handle_typed_command(msg_buffer)` → switch on `data[1]` →
`send_typed_response` → `raw_hid_send` (the `[0x51]` reply) → hid_notify suppresses
the legacy ack via `typed_dispatched`.

**Pain Points Addressed**: Gives the host a deterministic capability/value
handshake over Raw HID without reflashing. `0x51` (≥2) is distinct from the legacy
`0`/`1` match-bool, so the host disambiguates without ambiguity (§4.6). Unknown
cmd_ids get a typed reply (never crash / never silent).

## Why

- **Closes the §4.6 capability handshake.** Until QUERY_INFO/QUERY_CALLBACK are
  answerable, the host cannot detect a typed-capable firmware nor discover
  callback names — the entire host-rules feature is blocked at the handshake step.
- **Retires two carried warnings.** `board_rules_present` (carried since
  P1.M1.T2.S1) and `has_been_queried` (carried since P1.M1.T2.S1) are finally
  *used* by the QUERY_INFO case, so their `-Wunused` warnings disappear. This is
  the predicted self-resolution — the task is the consumer those PRPs pointed to.
- **`handle_typed_command` is `static`; the response is sent inside it.** hid_notify
  sets `typed_dispatched` to suppress the legacy ack, so the `[0x51]` reply MUST be
  emitted within `handle_typed_command` (via `send_typed_response`). The `bool`
  return is vestigial for the typed path but kept `true` per contract.
- **Safe-by-construction.** The 30-byte payload cap in `send_typed_response`
  prevents any response overflow; the `default` case guarantees every cmd_id
  (including the reserved 0x04 and the not-yet-impl 0x03/0x05) gets a typed reply;
  NULL `cbs` / out-of-range `index` / NULL `name` are all guarded (no deref).
- **Rebuild integrity**: the stub replacement is a single-region edit disjoint
  from P1.M2.T1.S3's `apply_host_callbacks` insertion (§8.4) and the typed_mode
  fork (hid_notify). SET_OS/APPLY_HOST_CONTEXT cases are left to S2/S3, which
  insert before `default`.

## What

Modify ONE region in `notifier.c`: replace the `handle_typed_command` STUB (the
`/* STUB — … */` comment + the `static bool handle_typed_command(char *buf) {
(void)buf; return false; }` body, ~lines 622-630) with:

1. A Mode-A block comment + `static void send_typed_response(uint8_t cmd_id,
   const uint8_t *payload, uint8_t payload_len)` — builds the 32-byte response
   (`[0x51][cmd_id][payload capped at 30][zero-pad]`) and calls `raw_hid_send`.
2. A Mode-A block comment + the real `static bool handle_typed_command(char *data)`
   — switches on `(uint8_t)data[1]`:
   - `case NOTIFY_CMD_QUERY_INFO` (0x01): `has_been_queried = true`; build a 4-byte
     payload `[NOTIFY_PROTO_VER][feature_flags][(uint8_t)get_host_callbacks_size()][board_rules_present()?1:0]`
     where `feature_flags = NOTIFY_FEATURE_APPLY_HOST_CONTEXT | (cb_size>0 ?
     NOTIFY_FEATURE_CALLBACK_REGISTRY : 0)`; `send_typed_response(0x01, payload, 4)`.
   - `case NOTIFY_CMD_QUERY_CALLBACK` (0x02): `index = (uint8_t)data[2]`; if
     `cbs && index < cb_size && cbs[index].name`: build `[index][name ≤29 bytes]`
     (payload_len = 1+n) else `[index][0x00]` (payload_len 2);
     `send_typed_response(0x02, payload, len)`.
   - `default`: `send_typed_response(cmd_id, NULL, 0)`.
   - `return true;`.

**msg_buffer layout (why data[1] is cmd_id)**: `hid_notify` strips the 2-byte
magic header (`data += 2`) before reassembly, so `msg_buffer = [0xF0][cmd_id][args…]`.
Thus `data[0]=0xF0`, `data[1]=cmd_id`, `data[2..]=args`. (The fork guarantees
`msg_index >= 2` for typed commands.)

**Response byte layouts** (per §4.6):
- QUERY_INFO → `[0x51][0x01][proto=2][feature_flags][callback_count][board_rules_present][zero-pad]`.
- QUERY_CALLBACK valid → `[0x51][0x02][index][name bytes][NUL-pad]`.
- QUERY_CALLBACK out-of-range → `[0x51][0x02][index][0x00]`.
- default/unknown (incl 0x04; also 0x03/0x05 until S2/S3) → `[0x51][cmd_id][zero-pad]`.

### Success Criteria

- [ ] `send_typed_response` present, static, immediately before `handle_typed_command`;
      caps payload at `RAW_REPORT_SIZE - 2` (30); zero-pads via `{0}` init.
- [ ] `handle_typed_command(char *data)` switches on `(uint8_t)data[1]`; QUERY_INFO
      (0x01) + QUERY_CALLBACK (0x02) + default cases implemented; returns `true`.
- [ ] QUERY_INFO: `has_been_queried = true`; payload = `[2][flags][count][board_rules]`;
      `feature_flags = 0x01 | (cb_size>0 ? 0x02 : 0)`.
- [ ] QUERY_CALLBACK: valid index → `[index][name]`; out-of-range/NULL-name → `[index][0x00]`.
- [ ] default → `send_typed_response(cmd_id, NULL, 0)`.
- [ ] `gcc -Wall -Wextra -std=c99 -c notifier.c` → exit 0; **exactly two** warnings
      (`apply_host_callbacks`, `set_host_layer`); `board_rules_present` and
      `has_been_queried` NOT among them.
- [ ] Multi-TU capture harness (via `hid_notify`): all QUERY_INFO/QUERY_CALLBACK/
      unknown/legacy assertions pass (0 failures).
- [ ] `test_notifier_dispatch` 14/14 + `test_notifier_os` 31/31, 0 FAIL; runner
      prints "✓ notifier stub-compile gate PASSED".
- [ ] Diff confined to the one stub-replacement region.

## All Needed Context

### Context Completeness Check

**Pass.** The exact code to write (the `send_typed_response` helper + the
`handle_typed_command` body, verbatim) and its exact replacement anchor (the stub
at notifier.c:622-630) are specified inline below and were **empirically validated
during research** by replacing the stub in a /tmp copy of notifier.c: stub-compiles
exit 0 with **exactly two** warnings (`apply_host_callbacks`, `set_host_layer`;
**`board_rules_present` and `has_been_queried` retired**); a multi-TU capture
harness driving the PUBLIC `hid_notify` passes ALL cases (QUERY_INFO proto=2/
flags=0x03/count=2/board_rules=0; QUERY_CALLBACK names + out-of-range; unknown
0x04; legacy-string backward-compat); both regression suites stay 0 FAIL. An
implementer with only this PRP + repo access can make the one replacement and
prove it green.

### Documentation & References

```yaml
# MUST READ — the canonical wire contract
- file: PRD.md   (snapshot: plan/003_16d737de7a3e/prd_snapshot.md)
  section: "### 4.6 Typed-command namespace (canonical owner)"
  why: "The EXACT command table + field definitions + response layouts this task
        implements: QUERY_INFO response [proto_ver][feature_flags][callback_count]
        [board_rules_present]; QUERY_CALLBACK response [index][name bytes, NUL-padded]
        (name absent => [index][0x00]); response marker 0x51 (distinct from legacy 0/1);
        proto_ver=2 owned by firmware; feature_flags 0x01=APPLY_HOST_CONTEXT, 0x02=
        CALLBACK_REGISTRY; has_been_queried set on first QUERY_INFO (handshake timing)."
  critical: "0x51 (>=2) is DISTINCT from legacy 0/1 — the host disambiguates on
        response[0]. Typed commands BYPASS process_full_message (no board side effects,
        invariant 21). The [0x51] response is sent INSIDE handle_typed_command."

- file: PRD.md
  section: "## 16 Appendix B — Constants Reference"
  why: "Locks the values: discriminator 0xF0, response marker 0x51, proto_ver 2,
        SET_OS 0x03, RAW_REPORT_SIZE 32. The constants themselves are #define'd in
        notifier.h (LANDED); this task consumes them by name."
  critical: "Do NOT hardcode 0x51/0x01/0x02/2 — use NOTIFY_RESPONSE_MARKER,
        NOTIFY_CMD_QUERY_INFO, NOTIFY_CMD_QUERY_CALLBACK, NOTIFY_PROTO_VER (notifier.h).
        Do NOT redefine them."

# Architecture — the four-handler contract + response builder intent
- file: plan/003_16d737de7a3e/architecture/host_rules_architecture.md
  section: "## The four typed-command handlers" (QUERY_INFO / QUERY_CALLBACK / SET_OS / APPLY_HOST_CONTEXT)
  why: "Spells out each handler's request/response byte layout. QUERY_INFO: feature_flags
        = 0x01 | (cb_size>0 ? 0x02 : 0); sets has_been_queried. QUERY_CALLBACK: [index][name]
        valid, [index][0x00] out-of-range. SET_OS/APPLY_HOST_CONTEXT are the OTHER two
        handlers (NOT this task — they land in S2/S3)."
  critical: "This task implements ONLY QUERY_INFO + QUERY_CALLBACK + the default.
        SET_OS (0x03) and APPLY_HOST_CONTEXT (0x05) MUST fall through to default for now
        (safe placeholder). S2/S3 insert their cases before default."

- file: plan/003_16d737de7a3e/architecture/external_deps.md
  section: "### Command table (§4.6)" + "### Field definitions (§4.6)"
  why: "The authoritative command table: 0x01 QUERY_INFO, 0x02 QUERY_CALLBACK, 0x03 SET_OS,
        0x04 reserved, 0x05 APPLY_HOST_CONTEXT. And field defs: proto_ver (firmware-owned),
        feature_flags bitmask, callback_count = get_host_callbacks_size(), board_rules_present."
  critical: "board_rules_present = '1 iff ANY board map (default or OS-specific) is non-empty'.
        board_rules_present() (LANDED, P1.M1.T2.S1) already computes this — call it, do NOT
        recompute. callback_count = (uint8_t)get_host_callbacks_size() (0 when no registry)."

# The file being modified — exact anchor
- file: notifier.c
  section: "handle_typed_command STUB (lines ~622-630)"
  why: "The STUB to REPLACE: '/* STUB — real handle_typed_command lands in P1.M2.T2 ... */
        static bool handle_typed_command(char *buf) { (void)buf; return false; }'. Replace
        it (comment + body) with send_typed_response + the real handle_typed_command."
  pattern: "Keep the signature 'static bool handle_typed_command(char *data)' (only buf->data
        rename). Insert send_typed_response IMMEDIATELY BEFORE it (handle_typed_command calls
        it, so it must be declared first). Keep the surrounding code (hid_notify below it)
        untouched."
  gotcha: "handle_typed_command is called by hid_notify ('match = handle_typed_command(msg_buffer);'
        then 'typed_dispatched = true;'). The [0x51] response is sent INSIDE handle_typed_command
        (via send_typed_response -> raw_hid_send); hid_notify's typed_dispatched guard then
        suppresses the legacy 0/1 ack. So the bool return is vestigial for the typed path —
        return true per contract, do NOT try to send the response from hid_notify."

# How hid_notify consumes the work (the wiring contract)
- file: notifier.c
  section: "hid_notify typed/response region (lines ~660-708)"
  why: "Shows 'if (typed_mode) { match = handle_typed_command(msg_buffer); typed_dispatched = true; }'
        and the later 'if (!typed_dispatched) { /* legacy ack */ }'. Confirms handle_typed_command
        OWNS the [0x51] response and the legacy ack is suppressed for typed commands."
  critical: "Do NOT edit hid_notify. The wiring is already correct (landed by P1.M2.T1.S1).
        This task only makes handle_typed_command actually send the response."

# The constants + types (LANDED in notifier.h)
- file: notifier.h
  section: "Host-Side Rules & Typed Commands (§4.6 / §14)"
  why: "NOTIFY_CMD_DISCRIMINATOR 0xF0, NOTIFY_RESPONSE_MARKER 0x51, NOTIFY_CMD_QUERY_INFO 0x01,
        NOTIFY_CMD_QUERY_CALLBACK 0x02, NOTIFY_CMD_SET_OS 0x03, NOTIFY_CMD_APPLY_HOST_CONTEXT 0x05,
        NOTIFY_PROTO_VER 2, NOTIFY_FEATURE_APPLY_HOST_CONTEXT 0x01, NOTIFY_FEATURE_CALLBACK_REGISTRY
        0x02, HOST_CALLBACK_MAX 32. host_callback_t {name, on_enable, on_disable}. RAW_REPORT_SIZE
        is in notifier.c (=32)."
  critical: "Use these #defines BY NAME; do NOT hardcode the literals. host_callback_t.name is
        'const char *' (a C string) — scan to NUL when copying. get_host_callbacks()/
        get_host_callbacks_size() are the weak {NULL,0} accessors (overridden by
        DEFINE_HOST_CALLBACKS in a keymap/test). ALL LANDED — do NOT redefine."

# The helpers this task consumes (LANDED)
- file: notifier.c
  section: "board_rules_present (:204), has_been_queried (:145), weak get_host_callbacks (:123-124)"
  why: "board_rules_present() returns bool (checks all default + per-OS maps). has_been_queried is
        a static bool init false. get_host_callbacks()/get_host_callbacks_size() are weak, return
        NULL/0 when no DEFINE_HOST_CALLBACKS."
  critical: "QUERY_INFO must SET has_been_queried = true (the §4.6 handshake-timing rule) and CALL
        board_rules_present() and get_host_callbacks_size(). Doing so makes both formerly-unused
        symbols USED -> their -Wunused warnings disappear (the predicted self-resolution)."

# Dependency PRPs (CONTRACTS)
- file: plan/003_16d737de7a3e/P1M2T1S1/PRP.md
  why: "LANDED the typed_mode flag + the 0xF0 discriminator fork in hid_notify + the
        handle_typed_command STUB this task replaces. Confirms the wiring (typed_dispatched
        suppresses the legacy ack; handle_typed_command owns the [0x51] response)."
  critical: "Do NOT re-add the fork or typed_mode — they are LANDED. This task ONLY replaces
        the stub body. The stub's 'return false' is a placeholder; the real handler returns true."

- file: plan/003_16d737de7a3e/P1M1T2S1/PRP.md
  why: "LANDED board_rules_present(), has_been_queried, host_cb_enabled[], the weak
        get_host_callbacks/_size accessors. This task is their first CONSUMER (for QUERY_INFO)."
  critical: "board_rules_present + has_been_queried currently warn 'defined but not used'. This
        task's QUERY_INFO case references both, RETIRING those warnings. Do NOT redeclare them."

- file: plan/003_16d737de7a3e/P1M2T1S3/PRP.md   (parallel — landed/landing)
  why: "LANDED apply_host_callbacks (host callback diff) in §8.4, between set_host_layer and
        enable_command. Its edit region is DISJOINT from this task's (the stub at ~L622)."
  critical: "Do NOT touch apply_host_callbacks or set_host_layer. They remain 'defined but not
        used' (carried -> P1.M2.T2.S2/S3). This task does NOT call apply_host_callbacks
        (APPLY_HOST_CONTEXT is S3, not this task)."

# The build/test gate
- file: run_notifier_stub_tests.sh
  why: "Object-compiles notifier.c (-Wall -Wextra -std=c99 -DQMK_KEYBOARD_H=... -Iqmk_stubs -I.),
        links test_notifier_dispatch + test_notifier_os, runs both, asserts 0 FAIL. The 2 carried
        -Wunused warnings are non-fatal (NOT -Werror). Must print '✓ notifier stub-compile gate PASSED'."
  critical: "This task does NOT touch the runner. The official host test (test_notifier_host.c) and
        the stub response-capture accessor (stub_get_last_response) land in P1.M3.T1.S1/S2 — NOT
        this task. This task's Level-2 gate uses a throwaway /tmp multi-TU capture harness."

# External (informational)
- url: https://swtch.com/~rsc/regexp/regexp1.html
  why: "(N/A for this task — included only for matcher-context continuity.)"
```

### Current Codebase tree (relevant slice — POST P1.M2.T1.S3 state)

```bash
notifier.c                # ← MODIFY (1 region: replace handle_typed_command STUB ~L622-630 with
                          #   send_typed_response + real handle_typed_command). NOTHING ELSE.
                          # Current state: host globals (L143-145) + board_rules_present (L204)
                          # + apply_host_callbacks (L283, parallel S3) + typed_mode fork in
                          # hid_notify (L644) + the STUB handle_typed_command (L626).
notifier.h                # LANDED: NOTIFY_* constants + host_callback_t + HOST_CALLBACK_MAX + DEFINE_HOST_CALLBACKS. DO NOT TOUCH.
pattern_match.{c,h}       # unaffected. DO NOT TOUCH.
qmk_stubs/                # stub harness. DO NOT TOUCH (the response-capture accessor is P1.M3.T1.S1).
test_notifier_dispatch.c  # regression (14 cases). DO NOT TOUCH.
test_notifier_os.c        # regression (31 cases). DO NOT TOUCH.
run_notifier_stub_tests.sh# gate. DO NOT TOUCH.
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be changed

```bash
notifier.c                # MODIFIED: stub replaced by send_typed_response + real handle_typed_command (QUERY_INFO + QUERY_CALLBACK + default).
# (no new files; no header change)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — the [0x51] response is sent INSIDE handle_typed_command, NOT by hid_notify.
//   hid_notify does 'match = handle_typed_command(msg_buffer); typed_dispatched = true;'
//   and then 'if (!typed_dispatched) { /* legacy 0/1 ack */ }'. So for a typed command
//   the ONLY response is the one handle_typed_command sends via send_typed_response ->
//   raw_hid_send. The bool return is captured into 'match' but is VESTIGIAL (the legacy
//   ack is suppressed). Return true per the contract; do NOT also send a response from
//   hid_notify and do NOT change typed_dispatched.

// CRITICAL — msg_buffer layout: data[0]=0xF0, data[1]=cmd_id, data[2..]=args. hid_notify
//   strips the 2-byte magic header (data += 2) BEFORE reassembly, so the discriminator
//   0xF0 lands at msg_buffer[0], cmd_id at [1], args at [2..]. Switch on (uint8_t)data[1].
//   data[1] is ALWAYS valid (the fork guarantees msg_index >= 2 for typed commands).

// CRITICAL — do NOT implement SET_OS (0x03) or APPLY_HOST_CONTEXT (0x05) here. They land
//   in P1.M2.T2.S2 and P1.M2.T2.S3. Until then they fall through to 'default'
//   ([0x51][cmd_id] no payload) — a SAFE placeholder. S2/S3 will INSERT their 'case' branches
//   BEFORE the 'default' in this same switch. Leave the switch structured so insertion is
//   trivial (one case per cmd_id, default last).

// CRITICAL — board_rules_present and has_been_queried RETIRE this task. QUERY_INFO references
//   both (board_rules_present()?1:0; has_been_queried=true). After this task their
//   -Wunused warnings DISAPPEAR. The remaining warnings are EXACTLY apply_host_callbacks +
//   set_host_layer (carried -> S2/S3). If board_rules_present or has_been_queried STILL warn,
//   the QUERY_INFO case didn't reference them — fix it.

// GOTCHA — cap payload at RAW_REPORT_SIZE - 2 = 30 bytes. response[RAW_REPORT_SIZE=32]:
// [0x51][cmd_id] = 2 bytes, then 30 bytes for payload. send_typed_response MUST cap
// (payload_len < cap ? payload_len : cap) so a too-large payload never overflows. For
// QUERY_CALLBACK the name is copied up to 29 bytes (payload[1+n], n<=29), payload_len=1+n<=30.

// GOTCHA — the response is '{0}'-init, so the unused tail is ALREADY zero (NUL-pad). Do NOT
//   write an explicit pad loop. memcpy the payload into response+2; the rest stays 0.

// GOTCHA — QUERY_CALLBACK name copy: scan cbs[index].name to NUL, copy up to 29 bytes into
//   payload[1..]. Guard 'cbs != NULL && index < cb_size && cbs[index].name != NULL' before
//   deref. If any fails -> out-of-range branch ([index][0x00]). host_callback_t.name is
//   'const char *' — a NULL name would crash the scan, so the NULL guard is MANDATORY
//   (not over-engineering).

// GOTCHA — feature_flags: NOTIFY_FEATURE_APPLY_HOST_CONTEXT (0x01) is ALWAYS set (this
//   firmware implements the namespace). NOTIFY_FEATURE_CALLBACK_REGISTRY (0x02) is set iff
//   get_host_callbacks_size() > 0. So flags = 0x01 | (cb_size>0 ? 0x02 : 0). Do NOT set 0x04
//   (reserved VIA-coexist). With a 2-entry registry -> flags = 0x03.

// GOTCHA — callback_count is (uint8_t)get_host_callbacks_size(). HOST_CALLBACK_MAX is 32, so
//   it always fits in uint8_t. 0 when no DEFINE_HOST_CALLBACKS (weak default).

// GOTCHA — the 'bool' return is vestigial but keep it 'true'. The signature 'static bool
//   handle_typed_command(char *data)' is fixed (hid_notify's call site). Changing it would
//   break the wiring landed by P1.M2.T1.S1. Only the param name buf->data changes.

// GOTCHA — do NOT add #includes. send_typed_response uses memcpy (<string.h>, already included
//   by notifier.c) and uint8_t (already in scope). handle_typed_command uses the NOTIFY_*
//   constants and host_callback_t (notifier.h, already included). No <stddef.h>/<stdint.h>/etc.

// GOTCHA — do NOT run the "normal build" to validate; notifier.c cannot compile standalone
//   (#include QMK_KEYBOARD_H). Validate with the STUB build (run_notifier_stub_tests.sh flags)
//   + the throwaway /tmp multi-TU capture harness for behavior.

// GOTCHA — multi-TU test tension: handle_typed_command/send_typed_response are static, and
//   DEFINE_HOST_CALLBACKS (strong) conflicts with the weak accessor if both are in one TU
//   ('redefinition of get_host_callbacks'). So the behavior test MUST be multi-TU: compile
//   notifier.c to an object, link a separate test TU that defines DEFINE_HOST_CALLBACKS +
//   a capturing raw_hid_send, and drive the PUBLIC hid_notify. (The official test_notifier_host.c
//   in P1.M3.T1.S2 will do exactly this.) This task's Level-2 gate uses the throwaway harness.

// GOTCHA — placement: send_typed_response goes IMMEDIATELY BEFORE handle_typed_command (which
//   calls it). Replace the stub's comment+body in place; do NOT move handle_typed_command out
//   of its current location (hid_notify, directly below, calls it).
```

## Implementation Blueprint

### Data models and structure

No new types, no new globals, no new includes. This task consumes:
- `NOTIFY_RESPONSE_MARKER` (0x51), `NOTIFY_CMD_QUERY_INFO` (0x01),
  `NOTIFY_CMD_QUERY_CALLBACK` (0x02), `NOTIFY_PROTO_VER` (2),
  `NOTIFY_FEATURE_APPLY_HOST_CONTEXT` (0x01), `NOTIFY_FEATURE_CALLBACK_REGISTRY` (0x02)
  — all in notifier.h (LANDED).
- `RAW_REPORT_SIZE` (32) — notifier.c.
- `board_rules_present()`, `has_been_queried`, `get_host_callbacks()`,
  `get_host_callbacks_size()` — notifier.c (LANDED).
- `host_callback_t` (`{name, on_enable, on_disable}`) — notifier.h (LANDED).
- `raw_hid_send(uint8_t*, uint8_t)` — external (QMK / stub).

And adds two `static` functions: `send_typed_response` + the real `handle_typed_command`.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY notifier.c — REPLACE the handle_typed_command STUB
  - LOCATE: grep -n 'STUB — real handle_typed_command' notifier.c -> the comment at ~L622,
    immediately followed by 'static bool handle_typed_command(char *buf) { (void)buf; return false; }'.
  - REPLACE the ENTIRE stub (comment + function, ~L622-630) with the two Mode-A-commented
    functions in "The exact code to write" below (send_typed_response, then handle_typed_command).
  - send_typed_response: signature 'static void send_typed_response(uint8_t cmd_id,
    const uint8_t *payload, uint8_t payload_len)'. Body: {0}-init response[RAW_REPORT_SIZE];
    response[0]=NOTIFY_RESPONSE_MARKER; response[1]=cmd_id; if payload&&len>0: cap=RAW_REPORT_SIZE-2,
    n=min(len,cap), memcpy(response+2,payload,n); raw_hid_send(response,RAW_REPORT_SIZE).
  - handle_typed_command: signature 'static bool handle_typed_command(char *data)' (buf->data).
    Body: cmd_id=(uint8_t)data[1]; switch(cmd_id): QUERY_INFO case (has_been_queried=true;
    4-byte payload; send_typed_response(0x01,payload,4)); QUERY_CALLBACK case (index=data[2];
    valid->name payload, else [index][0x00]; send_typed_response(0x02,...)); default
    (send_typed_response(cmd_id,NULL,0)); return true.
  - PRESERVE: the signature 'static bool handle_typed_command(char *)' (hid_notify calls it);
    all surrounding code (hid_notify below; the §8.4 state machines; the typed_mode fork).
  - DO NOT: implement SET_OS (0x03) or APPLY_HOST_CONTEXT (0x05) cases (-> default for now);
    add #includes; touch hid_notify/typed_mode/apply_host_callbacks/set_host_layer; suppress
    warnings; change the public API.

Task 2: VERIFY (no edit) — compile + behavior + regression
  - Run Validation Level 1 (stub-compile; exit 0; exactly 2 warnings; board_rules_present +
    has_been_queried retired).
  - Run Validation Level 2 (multi-TU capture harness via hid_notify: QUERY_INFO/QUERY_CALLBACK/
    unknown/legacy).
  - Run Validation Level 3 (dispatch 14/14 + os 31/31, 0 FAIL; runner PASSED).
  - Run Level 4 (Mode-A doc anchors; diff confined to notifier.c).
```

**The exact code to write** — replace the stub (`/* STUB — … */` comment + the
`static bool handle_typed_command(char *buf) { (void)buf; return false; }` body)
with (validated verbatim during research):

```c
/* send_typed_response (§4.6) — Always sends exactly 32 bytes (RAW_REPORT_SIZE)
 * with the 0x51 NOTIFY_RESPONSE_MARKER in byte 0 (distinct from the legacy 0/1
 * match-bool ack, §4.6), the cmd_id echo in byte 1, the payload in bytes [2..],
 * and zero-padding the remainder. payload_len is capped at RAW_REPORT_SIZE-2 (30)
 * so a too-large payload can never overflow the 32-byte report. Called by every
 * handle_typed_command branch, so every typed command elicits exactly one reply. */
static void send_typed_response(uint8_t cmd_id, const uint8_t *payload, uint8_t payload_len) {
    uint8_t response[RAW_REPORT_SIZE] = {0};   /* zero-pads the unused tail */
    response[0] = NOTIFY_RESPONSE_MARKER;      /* 0x51 */
    response[1] = cmd_id;                      /* echo */
    if (payload != NULL && payload_len > 0) {
        uint8_t cap = (uint8_t)(RAW_REPORT_SIZE - 2);   /* 30 bytes available after [0x51][cmd_id] */
        uint8_t n = (payload_len < cap) ? payload_len : cap;
        memcpy(response + 2, payload, n);
    }
    raw_hid_send(response, RAW_REPORT_SIZE);
}

/* handle_typed_command (§4.6) — dispatches a reassembled typed command. msg_buffer
 * layout (magic header already stripped by hid_notify): data[0]==0xF0 discriminator,
 * data[1]==cmd_id, data[2..]==args. Switches on (uint8_t)data[1]; each case builds
 * its response payload and calls send_typed_response, then returns true (the typed
 * path always replies — it sent a [0x51] response). hid_notify sets typed_dispatched
 * so the legacy 0/1 ack is suppressed. Bypasses process_full_message => no board
 * disable/deactivate side effects (§4.6, invariant 21).
 *
 * QUERY_INFO (0x01) and QUERY_CALLBACK (0x02) are implemented here. SET_OS (0x03)
 * and APPLY_HOST_CONTEXT (0x05) land in P1.M2.T2.S2/S3; until then they fall through
 * to the default ([0x51][cmd_id] no payload) — a safe placeholder. */
static bool handle_typed_command(char *data) {
    uint8_t cmd_id = (uint8_t)data[1];

    switch (cmd_id) {
        /* QUERY_INFO (0x01) — capability handshake (§4.6). The host sends this once
         * per board boot to detect a typed-command-capable firmware. Reply payload:
         * [proto_ver][feature_flags][callback_count][board_rules_present]. */
        case NOTIFY_CMD_QUERY_INFO: {
            has_been_queried = true;   /* §4.6 handshake-timing: set on first QUERY_INFO service */
            uint8_t payload[4];
            payload[0] = NOTIFY_PROTO_VER;   /* 2 = typed-command capable (firmware-owned, §4.6) */
            payload[1] = NOTIFY_FEATURE_APPLY_HOST_CONTEXT
                       | (get_host_callbacks_size() > 0 ? NOTIFY_FEATURE_CALLBACK_REGISTRY : 0);
            payload[2] = (uint8_t)get_host_callbacks_size();          /* 0 when no DEFINE_HOST_CALLBACKS */
            payload[3] = board_rules_present() ? 1 : 0;               /* single bit (§4.6) */
            send_typed_response(NOTIFY_CMD_QUERY_INFO, payload, 4);
            break;
        }
        /* QUERY_CALLBACK (0x02) — name discovery (§4.6). args[0]=index. The host
         * sweeps i in 0..count to build name->id. Reply: [index][name bytes, NUL-
         * padded] for a valid index; [index][0x00] (name absent) for out-of-range. */
        case NOTIFY_CMD_QUERY_CALLBACK: {
            uint8_t index = (uint8_t)data[2];
            size_t cb_size = get_host_callbacks_size();
            host_callback_t *cbs = get_host_callbacks();
            if (cbs != NULL && index < cb_size && cbs[index].name != NULL) {
                uint8_t payload[30];               /* [index] + up to 29 name bytes (fits the 30-byte response tail) */
                payload[0] = index;
                const char *name = cbs[index].name;
                uint8_t n = 0;
                while (n < 29 && name[n] != '\0') { payload[1 + n] = (uint8_t)name[n]; n++; }
                send_typed_response(NOTIFY_CMD_QUERY_CALLBACK, payload, (uint8_t)(1 + n));
            } else {
                uint8_t payload[2] = { index, 0x00 };   /* name absent (§4.6) */
                send_typed_response(NOTIFY_CMD_QUERY_CALLBACK, payload, 2);
            }
            break;
        }
        /* Default / unknown cmd_id (incl 0x04 reserved for VIA-coexist, and 0x03/0x05
         * until P1.M2.T2.S2/S3 land): reply with just [0x51][cmd_id] (no payload) so
         * the host always gets a typed response and never crashes on an unknown command. */
        default: {
            send_typed_response(cmd_id, NULL, 0);
            break;
        }
    }
    return true;   /* typed path always succeeds — it sent a [0x51] response */
}
```

### Implementation Patterns & Key Details

```c
// PATTERN: response builder centralizes the [0x51] layout + the 30-byte cap. Every
//   handler calls send_typed_response(cmd_id, payload, len) — one place enforces the
//   32-byte report, the marker, the echo, and the cap. The {0}-init zero-pads the tail.

// PATTERN: switch on (uint8_t)data[1] with default last. SET_OS/APPLY_HOST_CONTEXT
//   cases will be INSERTED before default in S2/S3 — leave the switch structured so
//   that is a one-case insertion (no fall-through tricks).

// PATTERN: guard before every registry deref. cbs != NULL (weak default is NULL) AND
//   index < cb_size (RISK-3 spirit) AND cbs[index].name != NULL (name scan would crash
//   on NULL). Any failure -> the out-of-range branch. Mirrors apply_host_callbacks'
//   RISK-3 guards.

// PATTERN: use the NOTIFY_* constants by name, not literals. NOTIFY_RESPONSE_MARKER,
//   NOTIFY_CMD_QUERY_INFO, NOTIFY_CMD_QUERY_CALLBACK, NOTIFY_PROTO_VER,
//   NOTIFY_FEATURE_APPLY_HOST_CONTEXT, NOTIFY_FEATURE_CALLBACK_REGISTRY are all #define'd
//   in notifier.h. Hardcoding 0x51/0x01/0x02/2 would diverge if the contract changes.

// ANTI-PATTERN: do NOT send the response from hid_notify. handle_typed_command OWNS the
//   [0x51] reply (hid_notify's typed_dispatched suppresses the legacy ack). The bool
//   return is vestigial; return true.

// ANTI-PATTERN: do NOT implement SET_OS (0x03) or APPLY_HOST_CONTEXT (0x05). They are
//   S2/S3. Letting them hit default ([0x51][cmd_id]) is the intended safe placeholder.

// ANTI-PATTERN: do NOT hardcode payload sizes without the cap. send_typed_response MUST
//   cap at RAW_REPORT_SIZE-2. A payload_len of 31 (uncapped) would write response[2..32]
//   — a stack buffer overflow.

// ANTI-PATTERN: do NOT add an explicit NUL-pad loop. The response is '{0}'-init; memcpy
//   only the payload bytes; the tail is already zero.

// ANTI-PATTERN: do NOT change the signature 'static bool handle_typed_command(char *)'.
//   hid_notify's call site (landed by P1.M2.T1.S1) depends on it. Only buf->data renames.

// ANTI-PATTERN: do NOT suppress the apply_host_callbacks/set_host_layer warnings. They
//   are carried (-> S2/S3) and self-resolve then. This task retires board_rules_present +
//   has_been_queried (by USING them in QUERY_INFO) — that is the intended resolution.

// ANTI-PATTERN: do NOT touch hid_notify, typed_mode, apply_host_callbacks, set_host_layer,
//   notifier.h, or any test/stub file. The ONLY edit is the stub replacement in notifier.c.
```

### Integration Points

```yaml
STUB REPLACEMENT (notifier.c ~L622-630):
  - remove: the '/* STUB — real handle_typed_command lands in P1.M2.T2 ... */' comment +
    'static bool handle_typed_command(char *buf) { (void)buf; return false; }'
  - add: send_typed_response (static) + handle_typed_command (static, real body)
  - invariant: signature 'static bool handle_typed_command(char *)' preserved; hid_notify
    (directly below) still calls it; typed_dispatched still suppresses the legacy ack.
CONSUMES (LANDED — unchanged):
  - notifier.h: NOTIFY_* constants, host_callback_t, HOST_CALLBACK_MAX.
  - notifier.c: board_rules_present(), has_been_queried, get_host_callbacks()/_size() (weak),
    RAW_REPORT_SIZE.
  - external: raw_hid_send (QMK / stub).
GLOBALS (written):
  - has_been_queried = true on first QUERY_INFO (§4.6 handshake).
DOWNSTREAM (NOT this task):
  - SET_OS case (0x03) -> P1.M2.T2.S2; APPLY_HOST_CONTEXT case (0x05) -> P1.M2.T2.S3.
    Both insert their 'case' before 'default' in this switch.
  - Official test_notifier_host.c + stub_get_last_response accessor -> P1.M3.T1.S1/S2.
BUILD / CONFIG / DATABASE / ROUTES:
  - none. No rules.mk, no wire change, no header change, no new files.
```

## Validation Loop

> Toolchain: gcc (C project; no ruff/mypy/pytest). notifier.c is stub-compiled via
> `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I.`. All commands were
> **executed during research** against a /tmp copy of notifier.c with the stub
> replaced by the real implementation and **PASSED**.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Stub-compile notifier.c. Expect exit 0 AND EXACTLY TWO -Wunused warnings.
#     CRITICAL: 'board_rules_present' and 'has_been_queried' must NO LONGER warn
#     (QUERY_INFO now uses them); the remaining two are apply_host_callbacks +
#     set_host_layer (carried -> P1.M2.T2.S2/S3).
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
echo "compile exit=$?  (expect 0)"
echo "-- warnings (expect EXACTLY apply_host_callbacks + set_host_layer; NO board_rules_present/has_been_queried) --"
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o 2>&1 | grep 'warning:' | sed 's/^[^:]*:notifier.c://'
# Expected: exactly 2 lines (apply_host_callbacks, set_host_layer — both -Wunused-function).
# FAIL if: exit != 0, OR a 3rd warning appears, OR 'board_rules_present' or 'has_been_queried' is present.

# 1b. Confirm send_typed_response + handle_typed_command landed with exact signatures.
grep -nE 'static void send_typed_response\(uint8_t cmd_id, const uint8_t \*payload, uint8_t payload_len\)' notifier.c
grep -nE 'static bool handle_typed_command\(char \*data\)' notifier.c
# Expected: each prints exactly ONE line; send_typed_response's line < handle_typed_command's.

# 1c. Confirm the STUB is GONE and the cases are present.
! grep -q 'STUB — real handle_typed_command' notifier.c && echo "✓ stub comment gone"
grep -q 'case NOTIFY_CMD_QUERY_INFO' notifier.c && echo "✓ QUERY_INFO case present"
grep -q 'case NOTIFY_CMD_QUERY_CALLBACK' notifier.c && echo "✓ QUERY_CALLBACK case present"
grep -q 'has_been_queried = true' notifier.c && echo "✓ has_been_queried set in QUERY_INFO"
grep -q 'board_rules_present()' notifier.c && echo "✓ board_rules_present() called"

# 1d. Confirm NO new #includes and SET_OS/APPLY_HOST_CONTEXT are NOT yet cases.
grep -nE '^#include' notifier.c | head   # expect the existing set, nothing new
! grep -qE 'case NOTIFY_CMD_SET_OS|case NOTIFY_CMD_APPLY_HOST_CONTEXT' notifier.c \
  && echo "✓ SET_OS/APPLY_HOST_CONTEXT not implemented (correct — S2/S3)" \
  || echo "ERROR: SET_OS/APPLY_HOST_CONTEXT implemented out of scope"

rm -f /tmp/notifier_stub.o
```

### Level 2: Component Validation (behavior — THE PRIMARY GATE)

Multi-TU capture harness (strong `DEFINE_HOST_CALLBACKS` overriding the weak
accessors at link time; capturing `raw_hid_send`; driving the PUBLIC `hid_notify`).
This was **verified during research** (ALL CASES CONFIRMED):

```bash
cd /home/dustin/projects/qmk-notifier

# 2a. Object-compile notifier.c (the file under test).
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o

# 2b. The capture harness TU.
cat > /tmp/htc_test.c <<'EOF'
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"
void hid_notify(uint8_t *data, uint8_t length);
static uint8_t g_resp[32]; static int g_send_count = 0;
void raw_hid_send(uint8_t *data, uint8_t length) { (void)length; memcpy(g_resp, data, 32); g_send_count++; }
void layer_on(uint8_t l){ (void)l; } void layer_off(uint8_t l){ (void)l; }
static void cb0_en(void){} static void cb0_di(void){} static void cb1_en(void){}
DEFINE_HOST_CALLBACKS({ {"mute-discord", cb0_en, cb0_di}, {"open-terminal", cb1_en, NULL} });
static int fails = 0;
#define CK(cond, fmt, ...) do{ if(cond){printf("ok   %s\n",desc);} else {printf("FAIL %s " fmt "\n",desc,##__VA_ARGS__); fails++;} }while(0)
static void mk(uint8_t *rep, uint8_t cmd, const uint8_t *args, uint8_t alen){
    memset(rep,0,32); rep[0]=0x81; rep[1]=0x9F; rep[2]=0xF0; rep[3]=cmd;
    for(uint8_t i=0;i<alen;i++) rep[4+i]=args[i]; rep[4+alen]=0x03;
}
int main(void){
    uint8_t rep[32];
    { const char *desc="QUERY_INFO: [0x51][0x01], 1 response"; mk(rep,0x01,NULL,0);
      g_send_count=0; memset(g_resp,0xFF,32); hid_notify(rep,32);
      CK(g_send_count==1 && g_resp[0]==0x51 && g_resp[1]==0x01, "sends=%d m=%02X e=%02X", g_send_count, g_resp[0], g_resp[1]); }
    { const char *desc="QUERY_INFO: proto=2 flags=0x03 count=2 board=0"; mk(rep,0x01,NULL,0); hid_notify(rep,32);
      CK(g_resp[2]==2 && g_resp[3]==0x03 && g_resp[4]==2 && g_resp[5]==0, "p=%d f=%02X c=%d b=%d", g_resp[2],g_resp[3],g_resp[4],g_resp[5]); }
    { const char *desc="QUERY_CALLBACK(0): 'mute-discord' + NUL pad"; uint8_t a[1]={0}; mk(rep,0x02,a,1); hid_notify(rep,32);
      CK(g_resp[1]==0x02 && g_resp[2]==0 && memcmp(g_resp+3,"mute-discord",12)==0 && g_resp[15]==0, "name='%s'", g_resp+3); }
    { const char *desc="QUERY_CALLBACK(1): 'open-terminal'"; uint8_t a[1]={1}; mk(rep,0x02,a,1); hid_notify(rep,32);
      CK(g_resp[2]==1 && memcmp(g_resp+3,"open-terminal",13)==0, "name='%s'", g_resp+3); }
    { const char *desc="QUERY_CALLBACK(99): out-of-range [index][0x00]"; uint8_t a[1]={99}; mk(rep,0x02,a,1); hid_notify(rep,32);
      CK(g_resp[2]==99 && g_resp[3]==0x00, "idx=%d nb=%02X", g_resp[2], g_resp[3]); }
    { const char *desc="unknown 0x04: [0x51][04] no payload"; mk(rep,0x04,NULL,0); hid_notify(rep,32);
      CK(g_resp[0]==0x51 && g_resp[1]==0x04 && g_resp[2]==0, "m=%02X e=%02X p=%02X", g_resp[0],g_resp[1],g_resp[2]); }
    { const char *desc="legacy string: data[2]!='F0' -> legacy 0/1 ack";
      memset(rep,0,32); rep[0]=0x81; rep[1]=0x9F; memcpy(rep+2,"hi\x03",3);
      g_send_count=0; memset(g_resp,0xFF,32); hid_notify(rep,32);
      CK(g_send_count==1 && (g_resp[0]==0||g_resp[0]==1), "ack=%02X (not 0x51)", g_resp[0]); }
    printf("\n%s (%d failures)\n", fails?"SOME FAILURES":"ALL CASES CONFIRMED", fails);
    return fails?1:0;
}
EOF
gcc -Wall -std=c99 -Iqmk_stubs -I. /tmp/htc_test.c /tmp/notifier_stub.o -o /tmp/htc_test 2>&1 | grep -iE 'error|warning' | grep -vE 'defined but not used' | head
/tmp/htc_test; echo "behavior exit=$? (expect 0)"
# Expected: a line of "ok" per check, then "ALL CASES CONFIRMED (0 failures)", exit 0.
rm -f /tmp/htc_test.c /tmp/htc_test /tmp/notifier_stub.o
```

### Level 3: Integration Testing (Regression — the primary gate)

```bash
cd /home/dustin/projects/qmk-notifier

# 3a. Full existing gate (both notifier suites via the runner). MUST end
#     "✓ notifier stub-compile gate PASSED" with 0 FAIL: for both suites.
./run_notifier_stub_tests.sh > /tmp/ns.out 2>&1; echo "stub-tests exit=$?  (expect 0)"
tail -n 4 /tmp/ns.out
# Expected: "notifier dispatch fails=0 (exit=0)", "notifier os fails=0 (exit=0)",
#           "✓ notifier stub-compile gate PASSED".

# 3b. Explicit per-suite regression. Neither suite sends 0xF0, so handle_typed_command
#     is never reached -> byte-identical behavior.
gcc -Wall -std=c99 -Iqmk_stubs -I. -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -o /tmp/td
gcc -Wall -std=c99 -Iqmk_stubs -I. -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -o /tmp/tos
echo "dispatch fails=$(/tmp/td 2>/dev/null | grep -c '^FAIL:')  (expect 0)"
echo "os fails=$(/tmp/tos 2>/dev/null | grep -c '^FAIL:')  (expect 0)"
rm -f /tmp/ns.out /tmp/td /tmp/tos
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Mode-A doc anchors (item-spec §5 DOCS).
grep -q 'Always sends exactly 32 bytes' notifier.c && echo "✓ send_typed_response doc (32-byte/0x51)"
grep -q 'capability handshake' notifier.c && echo "✓ QUERY_INFO §4.6 anchor"
grep -q 'name discovery' notifier.c && echo "✓ QUERY_CALLBACK §4.6 anchor"
grep -q 'distinct from the legacy 0/1' notifier.c && echo "✓ 0x51-vs-legacy distinction documented"
grep -q 'handshake-timing' notifier.c && echo "✓ has_been_queried handshake-timing documented"

# 4b. Response correctness via a quick inline check (rebuild the capture harness if needed,
#     or trust Level 2). Confirm QUERY_INFO feature_flags is 0x01 | (cb_size>0 ? 0x02 : 0):
#     with no DEFINE_HOST_CALLBACKS (weak default), flags must be 0x01 (only APPLY_HOST_CONTEXT).
cat > /tmp/qi_weak.c <<'EOF'
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"
void hid_notify(uint8_t *data, uint8_t length);
static uint8_t g[32];
void raw_hid_send(uint8_t *d, uint8_t l){ (void)l; memcpy(g,d,32); }
void layer_on(uint8_t x){(void)x;} void layer_off(uint8_t x){(void)x;}
/* NO DEFINE_HOST_CALLBACKS -> weak {NULL,0} -> feature_flags must be 0x01, count 0. */
int main(void){
    uint8_t rep[32]={0}; rep[0]=0x81; rep[1]=0x9F; rep[2]=0xF0; rep[3]=0x01; rep[4]=0x03;
    hid_notify(rep,32);
    printf("weak-registry QUERY_INFO: proto=%d flags=%02X count=%d board=%d\n", g[2], g[3], g[4], g[5]);
    return (g[2]==2 && g[3]==0x01 && g[4]==0) ? 0 : 1;
}
EOF
gcc -Wall -std=c99 -Iqmk_stubs -I. -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    notifier.c qmk_stubs/qmk_stubs.c /tmp/qi_weak.c -o /tmp/qi_weak 2>&1 | grep -iE 'error' | head
/tmp/qi_weak; echo "weak-registry exit=$? (0 => flags=0x01/count=0 when no registry)"
rm -f /tmp/qi_weak.c /tmp/qi_weak

# 4c. Diff hygiene: ONLY notifier.c changed (plus plan/ PRP/research).
git status --porcelain | grep -vE '^\?\? plan/'
# Expected: ` M notifier.c` only. NOTHING else.

# 4d. The switch is structured for S2/S3 insertion (QUERY_INFO + QUERY_CALLBACK + default,
#     no SET_OS/APPLY_HOST_CONTEXT cases yet).
awk '/static bool handle_typed_command/{f=1} f&&/^}/{exit} f' notifier.c \
  | grep -cE 'case NOTIFY_CMD_'   # expect 2 (QUERY_INFO, QUERY_CALLBACK)
echo "(expect 2 cases; SET_OS/APPLY_HOST_CONTEXT added by S2/S3)"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `notifier.c` stub-compiles (`-Wall -Wextra -std=c99`); exit 0; **exactly two**
      warnings (`apply_host_callbacks`, `set_host_layer`); `board_rules_present` and
      `has_been_queried` NOT among them.
- [ ] Level 1: `send_typed_response` + `handle_typed_command` present with exact signatures;
      stub comment gone; QUERY_INFO/QUERY_CALLBACK/default cases present; SET_OS/APPLY_HOST_CONTEXT
      NOT yet cases.
- [ ] Level 2: multi-TU capture harness (via `hid_notify`) → ALL CASES CONFIRMED (QUERY_INFO
      proto/flags/count/board; QUERY_CALLBACK names + out-of-range; unknown 0x04; legacy 0/1 ack).
- [ ] Level 3: `run_notifier_stub_tests.sh` → dispatch 14/14 + os 31/31, 0 FAIL,
      "✓ notifier stub-compile gate PASSED".
- [ ] Level 4: Mode-A doc anchors present; weak-registry QUERY_INFO → flags=0x01/count=0;
      diff confined to notifier.c; switch has exactly 2 cases (QUERY_INFO + QUERY_CALLBACK).

### Feature Validation

- [ ] QUERY_INFO returns `[0x51][0x01][proto=2][feature_flags][callback_count][board_rules]`;
      feature_flags = `0x01 | (cb_size>0 ? 0x02 : 0)`; sets `has_been_queried = true`.
- [ ] QUERY_CALLBACK returns `[index][name]` (valid) or `[index][0x00]` (out-of-range/NULL-name).
- [ ] default returns `[0x51][cmd_id]` (no payload) — safe for unknown/reserved/0x03/0x05.
- [ ] Every typed command elicits exactly ONE 32-byte response (sent inside handle_typed_command);
      hid_notify suppresses the legacy ack.
- [ ] Legacy string path (data[2]≠0xF0) unchanged — backward-compat preserved.

### Code Quality Validation

- [ ] Uses NOTIFY_* constants by name (no hardcoded 0x51/0x01/0x02/2 literals).
- [ ] `send_typed_response` centralizes the [0x51] layout + the 30-byte payload cap.
- [ ] Guards before every registry deref (cbs!=NULL, index<cb_size, name!=NULL).
- [ ] `{0}`-init zero-pads the response tail (no explicit pad loop).
- [ ] No anti-patterns (see below): no response from hid_notify, no SET_OS/APPLY_HOST_CONTEXT,
      no signature change, no new #includes.

### Documentation & Deployment

- [ ] Mode-A comments cite §4.6 for each handler + the response byte layout + the 0x51-vs-legacy
      distinction + the has_been_queried handshake-timing rule.
- [ ] No new env vars / config / build-system / runner changes.
- [ ] README host-rules section is P1.M3.T3.S1 (NOT this task — out of scope).

---

## Anti-Patterns to Avoid

- ❌ Don't send the typed response from `hid_notify` — `handle_typed_command` OWNS the `[0x51]`
  reply (hid_notify's `typed_dispatched` suppresses the legacy ack). The `bool` return is
  vestigial; return `true`.
- ❌ Don't implement SET_OS (0x03) or APPLY_HOST_CONTEXT (0x05) — they are P1.M2.T2.S2/S3.
  Let them hit `default` (`[0x51][cmd_id]`) as a safe placeholder; S2/S3 insert their cases.
- ❌ Don't change the signature `static bool handle_typed_command(char *)` — hid_notify's call
  site (landed by P1.M2.T1.S1) depends on it. Only `buf`→`data` renames.
- ❌ Don't skip the 30-byte payload cap in `send_typed_response` — an uncapped `payload_len`
  could overflow the 32-byte response. Cap at `RAW_REPORT_SIZE - 2`.
- ❌ Don't add an explicit NUL-pad loop — the response is `{0}`-init; `memcpy` the payload,
  the tail is already zero.
- ❌ Don't dereference `cbs[index].name` without guarding `cbs != NULL && index < cb_size &&
  cbs[name] != NULL` — a NULL name would crash the NUL-scan.
- ❌ Don't hardcode `0x51`/`0x01`/`0x02`/`2` — use `NOTIFY_RESPONSE_MARKER`/
  `NOTIFY_CMD_QUERY_INFO`/`NOTIFY_CMD_QUERY_CALLBACK`/`NOTIFY_PROTO_VER` (notifier.h).
- ❌ Don't recompute `board_rules_present` or `callback_count` — call `board_rules_present()`
  and `(uint8_t)get_host_callbacks_size()` (both LANDED).
- ❌ Don't suppress the `apply_host_callbacks`/`set_host_layer` warnings — they are carried
  (→ S2/S3) and self-resolve then. This task RETIRES `board_rules_present` + `has_been_queried`
  by using them in QUERY_INFO — that is the intended resolution, not suppression.
- ❌ Don't touch `hid_notify`, `typed_mode`, `apply_host_callbacks`, `set_host_layer`,
  `notifier.h`, `qmk_stubs/*`, `test_notifier_*`, `run_*.sh`, `PRD.md`, `tasks.json`,
  `prd_snapshot.md`, `rules.mk`, or `.gitignore`. Only the `handle_typed_command` stub
  region in `notifier.c` changes.
- ❌ Don't write the official `test_notifier_host.c` or the `stub_get_last_response` accessor
  here — those are P1.M3.T1.S1/S2. This task's validation uses a throwaway `/tmp` capture harness.
- ❌ Don't run the "normal build" to validate — `notifier.c` can't compile standalone
  (`#include QMK_KEYBOARD_H`). Use the stub build + the multi-TU capture harness.

---

## Confidence Score: 10/10

The deliverable is a single-region edit to `notifier.c`: replace the
`handle_typed_command` STUB (landed by P1.M2.T1.S1) with (1) a
`send_typed_response` helper (`[0x51][cmd_id][payload capped at 30][zero-pad]`,
32 bytes via `raw_hid_send`) and (2) the real `handle_typed_command` body
(switch on `(uint8_t)data[1]`: QUERY_INFO 0x01 + QUERY_CALLBACK 0x02 + default).
The exact code is given verbatim above and was **empirically validated during
research** by replacing the stub in a /tmp copy of notifier.c: stub-compiles exit 0
with **exactly two** warnings (`apply_host_callbacks`, `set_host_layer` — both
carried → S2/S3) and **`board_rules_present` + `has_been_queried` retired** (QUERY_INFO
now uses them); a multi-TU capture harness (strong `DEFINE_HOST_CALLBACKS`
overriding the weak accessor at link time; capturing `raw_hid_send`) driving the
PUBLIC `hid_notify` passes ALL cases — QUERY_INFO `proto=2/flags=0x03/count=2/
board_rules=0`, QUERY_CALLBACK names + out-of-range `[index][0x00]`, unknown 0x04
`[0x51][04]`, and legacy-string backward-compat (data[2]≠0xF0 → 0/1 ack); the
weak-registry variant confirms `flags=0x01/count=0` when no `DEFINE_HOST_CALLBACKS`;
both regression suites stay 0 FAIL (dispatch 14/14, os 31/31). The msg_buffer layout
(`data[0]=0xF0, data[1]=cmd_id, data[2..]=args` — magic stripped by hid_notify), the
"response sent inside handle_typed_command / typed_dispatched suppresses legacy ack"
wiring, the 30-byte payload cap, and the NULL-guard-before-deref pattern were all
confirmed by reading notifier.c and the prototype run. SET_OS/APPLY_HOST_CONTEXT are
explicitly deferred to S2/S3 (they hit `default` for now). Dependencies (the NOTIFY_*
constants, board_rules_present, has_been_queried, the weak accessors, the typed_mode
fork, apply_host_callbacks) are all LANDED; boundaries with S2/S3 (the two remaining
cases) and P1.M3.T1 (the official host test) are explicit. No external dependencies
are added.