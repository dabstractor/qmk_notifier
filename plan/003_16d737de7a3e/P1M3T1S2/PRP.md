name: "P1.M3.T1.S2 — test_notifier_host.c: QUERY_INFO + QUERY_CALLBACK test cases"
description: >
  Create `test_notifier_host.c` (a NEW file): the first slice of the typed-command
  host test suite, following the EXACT pattern of `test_notifier_dispatch.c` /
  `test_notifier_os.c` (file-scope `DEFINE_*` macros, `CK(cond,name)` helper,
  `PASS:`/`FAIL:` output, `g_pass`/`g_fail` counters, summary line, `return g_fail?1:0`).
  It stub-links `notifier.c` (same `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I.`
  flags as the existing two notifier suites) and gates the **QUERY_INFO (0x01)** and
  **QUERY_CALLBACK (0x02)** handlers landed in P1.M2.T2.S1, asserting the §4.6 wire
  responses byte-for-byte via the `stub_get_last_response()` accessor shipped by the
  parallel prerequisite P1.M3.T1.S1. SCOPE: ONLY the four contract cases (i) QUERY_INFO
  response layout, (ii) `has_been_queried` board-state-survives, (iii) QUERY_CALLBACK
  name discovery (valid index), (iv) QUERY_CALLBACK out-of-range (name absent).
  SET_OS / APPLY_HOST_CONTEXT / coexistence / multi-report are LATER siblings
  (S3/S4) — this task creates the file's scaffolding + these four blocks ONLY.

---

## Goal

**Feature Goal**: Create `test_notifier_host.c` — the host test suite for the
typed-command namespace (§4.6) — seeded with the QUERY_INFO + QUERY_CALLBACK
test cases. It stub-compiles `notifier.c` (the P1.M2 implementation: routing,
framing, handlers, state machines) and drives typed command reports through the
PUBLIC `hid_notify` entry, asserting the `[0x51][cmd_id][payload…]` responses
via `stub_get_last_response()` (P1.M3.T1.S1). The file follows the established
`test_notifier_dispatch.c` / `test_notifier_os.c` pattern verbatim so it slots
into the §11.2D stub-compile gate (the runner extension is a later sibling,
P1.M3.T2.S1 — NOT this task).

**Deliverable**: ONE new file — `test_notifier_host.c` at repo root (sibling of
`test_notifier_dispatch.c` / `test_notifier_os.c`). It contains:
1. **Includes + manual externs**: `<stdint.h> <stdbool.h> <string.h> <stdio.h>`
   + `"notifier.h"`; manual externs for the non-static `notifier.c` entry points
   (`hid_notify`, `process_full_message`) and the two stub observables
   (`stub_get_active_layer`, `stub_get_last_response` — the latter from S1).
2. **File-scope registry + maps** (the F6 distinguishable-callback precedent):
   `DEFINE_HOST_CALLBACKS({…})` (2 named entries with `on_enable`/`on_disable`
   handlers — S3-ready), plus `DEFINE_SERIAL_COMMANDS({…})` and
   `DEFINE_SERIAL_LAYERS({…})` (make `board_rules_present` true and seed a board
   layer for the (ii) test).
3. **`CK(cond,name)` macro** + `g_pass`/`g_fail` counters (identical to
   `test_notifier_os.c`).
4. **`send_typed(cmd_id, args, nargs)` helper** — builds a single-report
   `[0x81][0x9F][0xF0][cmd_id][args…][0x03]` frame, calls `hid_notify(rep,32)`,
   returns `stub_get_last_response()`.
5. **Four test blocks** in `main()`: (i) QUERY_INFO response layout, (ii)
   `has_been_queried` board-state-survives, (iii) QUERY_CALLBACK(0)/(1) name
   discovery, (iv) QUERY_CALLBACK(out-of-range) name-absent — plus a final
   side-effect-free assertion (no host callback fired by QUERY_*).
6. **Summary line + `return g_fail ? 1 : 0`** (the runner greps `grep -c '^FAIL:'`).

**Success Definition**:
- `test_notifier_host.c` compiles with the contract's exact flags:
  `gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c -std=c99`
  → produces a binary, exit 0.
- The binary prints `PASS:` for every assertion and **0** `FAIL:` lines:
  `./a.out 2>/dev/null | grep -c '^FAIL:'` == 0; the binary exits 0.
- It ALSO compiles cleanly under the runner's **test-TU** standard `-Wall -std=c99`
  (zero warnings) — the same flags `run_notifier_stub_tests.sh` uses for the
  test-link step (matching `test_notifier_dispatch.c` / `test_notifier_os.c`), so
  P1.M3.T2.S1 can wire it in without edits. NOTE: `-Wextra` is `notifier.c`'s
  COMPILE standard only; it is NOT applied to the test TU by the runner, so the
  test follows the siblings and omits the trailing `case_sensitive` field on
  `DEFINE_SERIAL_*` rows (do NOT add `, false` — that deviates from the pattern).
- Mode-A header comment maps each test block to its §4.6 criterion (rides WITH
  the work, DOCS §5).
- No regression: the existing two notifier suites (`test_notifier_dispatch`,
  `test_notifier_os`) and the 9 pattern-match suites are untouched and still
  green (this task only ADDS a file).
- No edits to: `notifier.c`, `notifier.h`, `qmk_stubs/*`, `pattern_match.*`,
  `run_notifier_stub_tests.sh`, `run_all_tests.sh`, any existing `test_*.c`,
  `PRD.md`, `tasks.json`, `rules.mk`, `.gitignore`.

## User Persona (if applicable)

**Target User**: The contributor/maintainer running the §11.2D stub-compile gate
and the future implementer of S3/S4 (who will extend this file). End users and
the desktop host never see this — it is host-side firmware test infrastructure.

**Use Case**: A developer changes `handle_typed_command` / `send_typed_response`
/ the QUERY_INFO or QUERY_CALLBACK handler (or the `board_rules_present` /
`get_host_callbacks_size` accessors). The gate rebuilds `test_notifier_host` and
greps for `^FAIL:`. If any response byte drifted (e.g. `proto_ver`, `feature_flags`,
`callback_count`, the name bytes, the name-absent `0x00`), a `FAIL:` line names
the exact regression. This catches wire-contract drift before it ships to the host.

**User Journey**: `gcc … test_notifier_host.c -o /tmp/t && /tmp/t` → read the
`PASS:`/`FAIL:` lines → `grep -c '^FAIL:'` → 0 means green. Each `CK` line is
self-describing (names the §4.6 criterion it gates).

**Pain Points Addressed**: P1.M2.T2.S1 landed four typed handlers + the
`send_typed_response` builder with NO host test coverage yet (only the matcher/
dispatch/os suites exist). The QUERY_INFO/QUERY_CALLBACK wire bytes — the
capability-handshake and name-discovery contract the desktop host depends on
(§4.6) — were previously un-assertable from the host. `stub_get_last_response`
(S1) opened the window; this task looks through it.

## Why

- **Closes the P1.M2 test gap for the two read-only query handlers.** QUERY_INFO
  (capability handshake: proto_ver/feature_flags/callback_count/board_rules_present)
  and QUERY_CALLBACK (name discovery: `[index][name]` / `[index][0x00]`) are the
  FIRST two typed commands the host sends on every (re)connect (§4.6 handshake).
  They MUST be byte-exact or the host mis-detects capability / mis-builds the
  name→id map. P1.M2.T2.S1 implemented them; this task gates them.
- **Establishes the `test_notifier_host.c` scaffolding for S3/S4.** S3 (SET_OS +
  APPLY_HOST_CONTEXT stack/replace) and S4 (coexistence + multi-report framing)
  both extend THIS file. Defining the includes/externs/`DEFINE_*`/`CK`/`send_typed`
  scaffolding now — with S3-ready host callbacks (real `on_enable`/`on_disable`)
  and board maps — means S3/S4 only append `main()` blocks, no re-architecture.
- **Proven pattern, zero design risk.** The architecture doc (§Test strategy)
  mandates "follow the EXACT pattern of `test_notifier_dispatch.c`". The `CK`
  macro, file-scope `DEFINE_*`, manual-extern stub accessors, and board-state
  setup via `process_full_message` are all lifted verbatim from
  `test_notifier_os.c`. Findings F6 blesses the stub-observable consumption.
- **Pure additive test infrastructure.** A single new test file. No production
  code touched. Cannot regress any existing suite (it is a separate TU/binary).

## What

Create ONE new file `test_notifier_host.c` (repo root) containing the QUERY_INFO +
QUERY_CALLBACK host tests. It is compiled and linked against the same
stub-compiled `notifier.c` + `qmk_stubs.c` as the two existing notifier suites.

**What the tests assert** (byte-exact, traced from `notifier.c` — see "Verified
response byte layouts" in research/findings.md):

1. **(i) QUERY_INFO response layout [§4.6]** — send `[0x81][0x9F][0xF0][0x01][0x03]`;
   assert `r[0]==0x51`, `r[1]==0x01`, `r[2]==2` (proto_ver), `r[3]` has bit 0 AND
   bit 1 set (feature_flags == `0x01|0x02 == 0x03` with callbacks present),
   `r[4]==callback_count` (== 2 from DEFINE_HOST_CALLBACKS), `r[5]==1`
   (board_rules_present, true because DEFINE_SERIAL_* defined).

2. **(ii) `has_been_queried` — board state survives QUERY_INFO [§4.6 handshake
   timing]** — `has_been_queried` is a `static bool` in notifier.c with NO
   accessor, so verify its OBSERVABLE consequence: the typed path bypasses
   `process_full_message`, so QUERY_INFO never clears board state. Dispatch a
   legacy `"neovide"` (sets board layer 5 + board command), assert
   `stub_get_active_layer()==5`; send QUERY_INFO; assert layer STILL 5 and board
   command NOT disabled (no spurious `on_disable`); send a SECOND QUERY_INFO;
   assert layer STILL 5. (This is the contract's "doesn't clear board state on a
   second QUERY_INFO".)

3. **(iii) QUERY_CALLBACK name discovery [§4.6]** — QUERY_CALLBACK(0) →
   `r[0]==0x51, r[1]==0x02, r[2]==0` (index echo), `r[3..]=='mute'` (first
   callback name bytes), `r[7]==0` (NUL pad). QUERY_CALLBACK(1) → `r[2]==1`,
   `r[3..]=='layout'`, `r[9]==0`.

4. **(iv) QUERY_CALLBACK out-of-range [§4.6]** — QUERY_CALLBACK(2) (count==2 ⇒
   out of range) → `r[2]==2` (index echo), `r[3]==0x00` (name absent).

Plus a final **side-effect-free** assertion: across all QUERY_INFO/QUERY_CALLBACK
sends, no host callback `on_enable`/`on_disable` fired (QUERY_* are read-only; only
APPLY_HOST_CONTEXT, in S3, fires callbacks).

**DO NOT** (out of scope — later siblings): SET_OS (0x03) tests → P1.M3.T1.S3;
APPLY_HOST_CONTEXT (0x05) stack/replace tests → P1.M3.T1.S3; coexistence /
backward-compat (legacy string + typed interleave) → P1.M3.T1.S4; multi-report
typed framing → P1.M3.T1.S4; extending `run_notifier_stub_tests.sh` → P1.M3.T2.S1.

### Success Criteria

- [ ] `test_notifier_host.c` exists at repo root and compiles with the contract
      flags → binary, exit 0.
- [ ] Running the binary prints `PASS:` for all assertions; `grep -c '^FAIL:'` == 0;
      exit 0.
- [ ] Clean compile of the test TU under `-Wall -std=c99` (zero warnings) — the
      runner's test-link standard (NOT `-Wextra`; see Success Definition note).
      (`notifier.c` itself stays clean under its own `-Wall -Wextra` — unchanged.)
- [ ] The four contract cases (i)-(iv) are all present with the exact byte
      assertions listed above.
- [ ] File-scope `DEFINE_HOST_CALLBACKS` (2 entries, with `on_enable`/`on_disable`),
      `DEFINE_SERIAL_COMMANDS`, `DEFINE_SERIAL_LAYERS` all present.
- [ ] `send_typed(cmd_id,args,nargs)` helper present and used by all four blocks.
- [ ] `CK(cond,name)` macro + `g_pass`/`g_fail` + summary + `return g_fail?1:0`
      (matches `test_notifier_os.c`).
- [ ] Mode-A header comment maps each block to its §4.6 criterion.
- [ ] No file other than `test_notifier_host.c` is created or modified.

## All Needed Context

### Context Completeness Check

**Pass.** The exact response byte layouts are traced directly from the landed
`notifier.c` `handle_typed_command` / `send_typed_response` (not guessed — see
"Verified response byte layouts" in research/findings.md and the Implementation
Blueprint below). The test pattern (file-scope `DEFINE_*`, `CK` macro,
manual-extern stub accessors, board-state setup via `process_full_message`) is
copied verbatim from `test_notifier_os.c` (read in full). The build chain and
`grep -c '^FAIL:'` gate are taken from `run_notifier_stub_tests.sh`. The
`stub_get_last_response()` contract (signature, last-write-wins semantics,
manual-extern declaration) is taken from the parallel predecessor PRP
P1.M3.T1.S1. The wire input frames are traced through `hid_notify`'s magic-strip
+ discriminator + ETX logic. A complete reference test file is given in the
Implementation Blueprint. An implementer with this PRP + repo access can write
the file and prove 0 `FAIL:` lines.

### Documentation & References

```yaml
# MUST READ — the wire contract this test gates (canonical owner)
- file: PRD.md
  section: "### 4.6 Typed-command namespace (canonical owner)"
  why: "Defines the discriminator 0xF0, the [0x51][cmd_id][payload] response
        format, the command table (0x01 QUERY_INFO, 0x02 QUERY_CALLBACK), the
        field definitions (proto_ver=2, feature_flags bits 0x01/0x02,
        callback_count, board_rules_present), and the has_been_queried
        handshake-timing rule. Every assertion maps to a field here."
  critical: "QUERY_INFO response payload is EXACTLY
        [proto_ver][feature_flags][callback_count][board_rules_present] (4 bytes
        at r[2..5]). QUERY_CALLBACK valid = [index][name bytes NUL-padded];
        out-of-range = [index][0x00]. feature_flags with a callback registry
        present = 0x01 | 0x02 = 0x03 (bits 0 AND 1)."

# MUST READ — the implementation under test (landed in P1.M2)
- file: notifier.c
  section: "handle_typed_command (QUERY_INFO + QUERY_CALLBACK cases) +
            send_typed_response + hid_notify typed fork"
  why: "send_typed_response builds response[0]=0x51, response[1]=cmd_id, memcpy
        payload into response[2..] (capped 30), zero-pads, raw_hid_send(32). The
        QUERY_INFO case sets has_been_queried=true then payload=[2,flags,count,
        board]. The QUERY_CALLBACK case reads data[2]=index; valid→payload[0]=
        index + name bytes (n<29); oob→payload={index,0x00}. The typed fork in
        hid_notify sets typed_mode on data[2]==0xF0 (msg_index==0 only), strips
        the 2-byte magic, reassembles, dispatches at ETX, suppresses the legacy
        ack (typed_dispatched)."
  critical: "has_been_queried is a file-static bool SET in the QUERY_INFO handler
        but NEVER READ by any gate — it cannot be asserted directly. Assert its
        observable consequence instead: QUERY_INFO bypasses process_full_message,
        so board state (activated_layer / current_command) survives QUERY_INFO.
        The QUERY_INFO handler reads NO args (data[2] unused) — the report is
        [0x81][0x9F][0xF0][0x01][0x03] with the ETX right after the cmd byte."

# MUST READ — the API surface (constants + macros consumed by the test)
- file: notifier.h
  why: "Provides NOTIFY_CMD_DISCRIMINATOR(0xF0), NOTIFY_RESPONSE_MARKER(0x51),
        NOTIFY_CMD_QUERY_INFO(0x01), NOTIFY_CMD_QUERY_CALLBACK(0x02),
        NOTIFY_PROTO_VER(2), NOTIFY_FEATURE_APPLY_HOST_CONTEXT(0x01),
        NOTIFY_FEATURE_CALLBACK_REGISTRY(0x02), and the DEFINE_HOST_CALLBACKS /
        DEFINE_SERIAL_COMMANDS / DEFINE_SERIAL_LAYERS macros. The test uses the
        NOTIFY_* constants directly (no magic numbers) and the macros at file
        scope to override notifier.c's weak {NULL,0}/{empty,0} defaults."
  pattern: "DEFINE_HOST_CALLBACKS({...}) generates user_host_callbacks[] +
        get_host_callbacks()/get_host_callbacks_size() (strong, overriding the
        weak defaults). DEFINE_SERIAL_COMMANDS/LAYERS similarly override the
        board map accessors. Each test TU is a separate binary, so defining
        these here does NOT clash with test_notifier_dispatch.c/os.c."

# MUST READ — the EXACT pattern to follow (copy the shape verbatim)
- file: test_notifier_os.c
  why: "The closest sibling. Copy its: include block (<stdint.h><stdbool.h>
        <string.h><stdio.h> + notifier.h); manual externs for hid_notify /
        process_full_message / stub_get_active_layer (line 37); file-scope
        DEFINE_SERIAL_* + distinguishable callbacks (F6); the CK(cond,name)
        macro; g_pass/g_fail counters; block-scoped {…} per category; the final
        printf summary + return g_fail?1:0."
  pattern: "CK macro:
        #define CK(cond, name) do { \\
            if (cond) { g_pass++; printf(\"PASS: %s\\n\", name); } \\
            else      { g_fail++; printf(\"FAIL: %s\\n\", name); } \\
        } while (0)
        Use it for EVERY assertion so the runner's `grep -c '^FAIL:'` works."
  gotcha: "manual extern for stub observables — NOT a header #include. Add:
        const uint8_t *stub_get_last_response(void);  (the S1 accessor)
        uint8_t        stub_get_active_layer(void);   (existing)"

# MUST READ — the second precedent (file-scope DEFINE_* + ck helper + hid_notify driving)
- file: test_notifier_dispatch.c
  why: "Shows DEFINE_SERIAL_COMMANDS/LAYERS at file scope with simple
        on_en_cmd/on_dis_cmd callbacks, a hid_notify-driven ack observation,
        and the PASS:/FAIL:/summary/return shape. The architecture doc says
        test_notifier_host.c follows this pattern EXACTLY."
  pattern: "Driving hid_notify: build a 32-byte report (memset 0), set rep[0]=
        0x81, rep[1]=0x9F, then the payload, then 0x03 ETX; call hid_notify(rep,
        32). The send_typed helper in this test generalizes that for typed cmds."

# MUST READ — the stub observable the test reads (S1 contract — treat as shipped)
- file: plan/003_16d737de7a3e/P1M3T1S1/PRP.md
  why: "Defines stub_get_last_response(): 'const uint8_t *stub_get_last_response(
        void) { return g_last_response; }' — a pointer into a file-static 32-byte
        buffer captured by raw_hid_send (memcpy before the debug print).
        last-write-wins: read it IMMEDIATELY after the hid_notify that triggers
        the response. This test consumes it via a manual extern (no header)."
  critical: "S1 runs in parallel; assume it ships EXACTLY this signature. The
        accessor returns const uint8_t* into a static buffer (do not free/write).
        Every raw_hid_send (typed [0x51]… AND legacy [0|1] ack) overwrites it —
        but the typed path sends ONLY the [0x51] response (typed_dispatched
        suppresses the legacy ack in hid_notify), so after a QUERY_* the buffer
        holds exactly the typed response."

# REFERENCE — the build/link/gate chain (DO NOT modify — P1.M3.T2.S1 extends it)
- file: run_notifier_stub_tests.sh
  why: "Shows the exact flags: compile notifier.c with
        -DQMK_KEYBOARD_H='\"qmk_keyboard_stub.h\"' -Iqmk_stubs -I. -Wall -Wextra
        -std=c99; link the test with -Wall -std=c99; run; grep -c '^FAIL:'.
        This task's contract OUTPUT gate is the same flags WITHOUT -Wall -Wextra;
        the stricter flags are the runner-readiness gate (zero warnings)."
  critical: "DO NOT edit this script — extending it for test_notifier_host is
        P1.M3.T2.S1. This task only proves the test compiles + runs green
        standalone."

# REFERENCE — test strategy (mandates the test_notifier_dispatch.c pattern)
- file: plan/003_16d737de7a3e/architecture/host_rules_architecture.md
  section: "## Test strategy"
  why: "'The new test_notifier_host.c follows the EXACT pattern of
        test_notifier_dispatch.c: file-scope DEFINE_* macros, ck() helper,
        PASS:/FAIL: output, summary line, return non-zero on failure. The runner
        greps grep -c ^FAIL:.' This PRP implements that mandate for the
        QUERY_INFO + QUERY_CALLBACK slice."

# REFERENCE — F6 stub-observable precedent (Risk: none)
- file: plan/003_16d737de7a3e/architecture/findings_and_risks.md
  section: "### F6 — Stub observables precedent established"
  why: "Blesses stub_get_active_layer() and, by extension, stub_get_last_response()
        as the test-harness observable pattern. Distinguishable callbacks (F6)
        reveal which path fired — used here for the host-callback flags."
```

### Current Codebase tree (relevant subset — run `ls` at repo root)

```bash
notifier.c              # implementation under test (P1.M2 — typed handlers live here)
notifier.h              # API: NOTIFY_* constants + DEFINE_* macros
qmk_stubs/
  qmk_keyboard_stub.h   # layer_on/off decls, RAW_EPSIZE
  qmk_stubs.c           # stubs: layer_on/off, raw_hid_send, stub_get_active_layer
                         #   S1 (parallel) adds: g_last_response + stub_get_last_response
  os_detection.h        # os_variant_t type stub
  raw_hid.h             # raw_hid_send decl
test_notifier_dispatch.c   # PRECEDENT — file-scope DEFINE_* + hid_notify driving
test_notifier_os.c         # PRECEDENT — CK macro + manual-extern stub accessors + board-state setup
run_notifier_stub_tests.sh # build/gate chain (extended in P1.M3.T2.S1 — NOT here)
run_all_tests.sh           # top-level runner (untouched)
```

### Desired Codebase tree with files to be added

```bash
test_notifier_host.c   # NEW — QUERY_INFO + QUERY_CALLBACK host test suite (this task)
# nothing else changes
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL: stub_get_last_response() does NOT exist yet in qmk_stubs.c at this
// writing — it ships with the PARALLEL prerequisite P1.M3.T1.S1. Treat S1's PRP
// as a contract: declare it via MANUAL EXTERN in test_notifier_host.c:
//     const uint8_t *stub_get_last_response(void);
// (NOT a header #include — matches stub_get_active_layer at test_notifier_os.c:37).
// If S1 hasn't landed when you build, the link fails on this symbol — that is
// expected until S1 merges; the compile still proves the TU is well-formed.

// CRITICAL: last-write-wins. stub_get_last_response() returns a pointer into a
// file-static buffer overwritten on EVERY raw_hid_send. Read it IMMEDIATELY after
// the hid_notify that triggers the response, before any other hid_notify. (The
// typed path sends ONLY the [0x51] response — hid_notify suppresses the legacy
// ack via typed_dispatched — so after a QUERY_* the buffer holds exactly that.)

// GOTCHA: has_been_queried is a file-static bool in notifier.c that is SET in the
// QUERY_INFO handler but NEVER READ by any gate. It has NO accessor. You CANNOT
// assert it directly. Assert its OBSERVABLE consequence: QUERY_INFO bypasses
// process_full_message (architecture invariant 21), so board state set by a prior
// legacy dispatch (activated_layer via stub_get_active_layer, current_command via
// a distinguishable on_disable flag) SURVIVES QUERY_INFO — even a second one.
// This is exactly the contract's "(ii) verify indirectly — e.g. by checking it
// doesn't clear board state on a second QUERY_INFO".

// GOTCHA: the QUERY_INFO report has NO args — the ETX comes right after the cmd
// byte: [0x81][0x9F][0xF0][0x01][0x03]. After magic strip the reassembled buffer
// is [0xF0, 0x01]; handle_typed_command reads cmd_id=data[1]=0x01 and ignores
// data[2] (no args). Do NOT add a phantom arg byte before the ETX.

// GOTCHA: file-scope `static int` flags that are written-but-never-read do NOT
// trigger -Wall (that warning is -Wunused-but-set-variable, a -Wextra check for
// LOCALS only). The runner links the test TU with -Wall (not -Wextra) — matching
// test_notifier_dispatch.c/os.c — so omitted case_sensitive fields are fine too.
// STILL, to be safe AND meaningful: the (ii) test READS the board-command flags
// (assert no spurious on_disable across QUERY_INFO), and a final assertion READS
// the host-callback flags (assert QUERY_* fired no host callback). This both
// strengthens the tests and guarantees no unused-symbol surprises.

// GOTCHA: avoid C99 compound literals for the arg arrays — use a local uint8_t
// var and pass its address. Maximally clean under -Wextra (no pedantic edge cases).

// GOTCHA: memset needs <string.h>. Include it (mirror test_notifier_os.c's
// include block). notifier.h gives you the NOTIFY_* constants and the DEFINE_*
// macros — use the constants, do NOT hardcode 0x51/0x01/0x02/0xF0/2.

// GOTCHA: this task writes ONLY QUERY_INFO + QUERY_CALLBACK cases. SET_OS /
// APPLY_HOST_CONTEXT / coexistence / multi-report belong to S3/S4 — leave them
// out. The file structure (scaffolding + main with these 4 blocks) is what S3/S4
// extend by appending blocks; do not pre-build hooks for them.

// GOTCHA: do NOT modify run_notifier_stub_tests.sh (P1.M3.T2.S1), qmk_stubs.c
// (S1), or notifier.* (P1.M2). This task is ONE new file.
```

## Implementation Blueprint

### Data models and structure

**None created.** The test consumes existing types: `host_callback_t` /
`command_map_t` / `layer_map_t` (from `notifier.h`) via the `DEFINE_*` macros,
and the `NOTIFY_*` constants. It declares only counters (`g_pass`/`g_fail` plus
distinguishable callback flags) and a 32-byte report buffer inside the helper.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: CREATE test_notifier_host.c — header comment + includes + externs
  - WRITE a Mode-A header comment (DOCS §5) at the top mapping each test block
    to its §4.6 criterion (QUERY_INFO layout → §4.6 capability handshake;
    has_been_queried → §4.6 handshake timing; QUERY_CALLBACK valid → §4.6 name
    discovery; QUERY_CALLBACK oob → §4.6 name absent). Mirror the header style
    of test_notifier_os.c.
  - INCLUDE: <stdint.h>, <stdbool.h>, <string.h>, <stdio.h>, "notifier.h"
    (identical to test_notifier_os.c).
  - DECLARE manual externs (NON-static notifier.c entries + the two stub
    observables — F6 manual-extern convention, NOT a header):
      void  hid_notify(uint8_t *data, uint8_t length);
      bool  process_full_message(char *data);
      uint8_t        stub_get_active_layer(void);    /* qmk_stubs.c */
      const uint8_t *stub_get_last_response(void);   /* qmk_stubs.c (P1.M3.T1.S1) */
  - NAMING/PLACEMENT: copy test_notifier_os.c's block exactly (only ADD the
    stub_get_last_response line).

Task 2: CREATE test_notifier_host.c — file-scope registry + maps (F6 distinguishable callbacks)
  - DEFINE distinguishable callback flags + functions for the HOST callback
    registry (F6 precedent; S3-ready with on_enable/on_disable):
      static int cb_mute_en=0, cb_mute_dis=0, cb_layout_en=0, cb_layout_dis=0;
      static void cb_mute_on(void)   { cb_mute_en++; }
      static void cb_mute_off(void)  { cb_mute_dis++; }
      static void cb_layout_on(void) { cb_layout_en++; }
      static void cb_layout_off(void){ cb_layout_dis++; }
  - DEFINE_HOST_CALLBACKS({ { "mute", cb_mute_on, cb_mute_off },
                             { "layout", cb_layout_on, cb_layout_off } });
    → callback_count = 2 (asserted in (i) r[4]==2); names "mute"/"layout"
    (asserted in (iii)); S3 will fire cb_*_on/off via APPLY_HOST_CONTEXT.
  - DEFINE board command callbacks (F6; read in (ii) to prove no spurious disable):
      static int board_cmd_en=0, board_cmd_dis=0;
      static void board_cmd_on(void)  { board_cmd_en++; }
      static void board_cmd_off(void) { board_cmd_dis++; }
  - DEFINE_SERIAL_COMMANDS({ { "neovide", board_cmd_on, board_cmd_off } });
  - DEFINE_SERIAL_LAYERS({ { "neovide", 5 } });
    → board_rules_present() == 1 (asserted in (i) r[5]==1); the (ii) test
    dispatches "neovide" to set board layer 5 + board command.
  - WHY: these override notifier.c's weak {NULL,0}/{empty,0} defaults for THIS
    binary only (separate TU — no clash with the other test binaries).

Task 3: CREATE test_notifier_host.c — CK macro + counters + send_typed helper
  - COPY the CK macro verbatim from test_notifier_os.c:
      static int g_pass = 0, g_fail = 0;
      #define CK(cond, name) do { \
          if (cond) { g_pass++; printf("PASS: %s\n", name); } \
          else      { g_fail++; printf("FAIL: %s\n", name); } \
      } while (0)
  - DEFINE send_typed(cmd_id, args, nargs):
      static const uint8_t *send_typed(uint8_t cmd_id, const uint8_t *args, uint8_t nargs) {
          uint8_t rep[32];
          memset(rep, 0, sizeof(rep));
          rep[0] = 0x81; rep[1] = 0x9F; rep[2] = NOTIFY_CMD_DISCRIMINATOR; /* 0xF0 */
          rep[3] = cmd_id;
          for (uint8_t i = 0; i < nargs; i++) rep[4 + i] = args[i];
          rep[4 + nargs] = ETX_TERMINATOR[0];  /* 0x03 — §4.6 ETX framing */
          hid_notify(rep, 32);
          return stub_get_last_response();
      }
  - WHY: one helper for all four blocks. Single-report framing (all QUERY_* args
    fit in 30 payload bytes). nargs==0 ⇒ args unused (loop doesn't run; NULL ok);
    rep[4] becomes the ETX right after the cmd byte (QUERY_INFO case).
  - GOTCHA: pass NULL,0 for QUERY_INFO (no args). For QUERY_CALLBACK pass &idx
    (a local uint8_t), 1 — avoid compound literals.

Task 4: CREATE test_notifier_host.c — main() block (i) QUERY_INFO response layout
  - BLOCK (i): §4.6 capability handshake.
      const uint8_t *r = send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);
      CK(r[0] == NOTIFY_RESPONSE_MARKER,                "(i) QUERY_INFO r[0]=0x51 marker [§4.6]");
      CK(r[1] == NOTIFY_CMD_QUERY_INFO,                 "(i) QUERY_INFO r[1]=0x01 cmd echo [§4.6]");
      CK(r[2] == NOTIFY_PROTO_VER,                      "(i) QUERY_INFO r[2]=proto_ver=2 [§4.6]");
      CK((r[3] & NOTIFY_FEATURE_APPLY_HOST_CONTEXT) && (r[3] & NOTIFY_FEATURE_CALLBACK_REGISTRY),
                                                          "(i) QUERY_INFO r[3]=feature_flags bits 0,1 set [§4.6]");
      CK(r[4] == 2,                                     "(i) QUERY_INFO r[4]=callback_count=2 [§4.6]");
      CK(r[5] == 1,                                     "(i) QUERY_INFO r[5]=board_rules_present=1 [§4.6]");
  - WHY: gates every QUERY_INFO field. feature_flags==0x03 (0x01|0x02) because
    DEFINE_HOST_CALLBACKS present ⇒ cb_size>0. board_rules_present==1 because
    DEFINE_SERIAL_* present.

Task 5: CREATE test_notifier_host.c — main() block (ii) has_been_queried (board state survives)
  - BLOCK (ii): §4.6 handshake timing (indirect — has_been_queried has no accessor).
      board_cmd_en = board_cmd_dis = 0;
      char m[] = "neovide";
      process_full_message(m);                       /* set board layer 5 + board cmd */
      CK(stub_get_active_layer() == 5,               "(ii) setup: legacy dispatch set board layer 5");
      CK(board_cmd_en == 1,                          "(ii) setup: board command enabled");
      board_cmd_dis = 0;
      (void)send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);   /* 1st QUERY_INFO */
      CK(stub_get_active_layer() == 5,               "(ii) 1st QUERY_INFO: board layer NOT cleared (typed path side-effect-free) [§4.6]");
      CK(board_cmd_dis == 0,                         "(ii) 1st QUERY_INFO: board command NOT disabled [§4.6]");
      (void)send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);   /* 2nd QUERY_INFO */
      CK(stub_get_active_layer() == 5,               "(ii) 2nd QUERY_INFO: board layer still NOT cleared [§4.6 handshake timing]");
  - WHY: has_been_queried is set-but-never-read; its observable face is that the
    typed path (handle_typed_command) bypasses process_full_message, so board
    state is untouched. Two QUERY_INFOs prove the "second QUERY_INFO" half of
    the contract. The (void) cast discards the unused response pointer cleanly.

Task 6: CREATE test_notifier_host.c — main() block (iii) QUERY_CALLBACK name discovery
  - BLOCK (iii): §4.6 name discovery (two indices for coverage).
      /* index 0 → "mute" */
      uint8_t idx0 = 0;
      const uint8_t *r0 = send_typed(NOTIFY_CMD_QUERY_CALLBACK, &idx0, 1);
      CK(r0[0] == NOTIFY_RESPONSE_MARKER,            "(iii) QUERY_CALLBACK(0) r[0]=0x51 marker [§4.6]");
      CK(r0[1] == NOTIFY_CMD_QUERY_CALLBACK,         "(iii) QUERY_CALLBACK(0) r[1]=0x02 cmd echo [§4.6]");
      CK(r0[2] == 0,                                 "(iii) QUERY_CALLBACK(0) r[2]=index echo 0 [§4.6]");
      CK(r0[3]=='m' && r0[4]=='u' && r0[5]=='t' && r0[6]=='e',
                                                       "(iii) QUERY_CALLBACK(0) r[3..]='mute' [§4.6]");
      CK(r0[7] == 0,                                 "(iii) QUERY_CALLBACK(0) name NUL-padded after 'mute' [§4.6]");
      /* index 1 → "layout" */
      uint8_t idx1 = 1;
      const uint8_t *r1 = send_typed(NOTIFY_CMD_QUERY_CALLBACK, &idx1, 1);
      CK(r1[0] == NOTIFY_RESPONSE_MARKER,            "(iii) QUERY_CALLBACK(1) r[0]=0x51 marker [§4.6]");
      CK(r1[1] == NOTIFY_CMD_QUERY_CALLBACK,         "(iii) QUERY_CALLBACK(1) r[1]=0x02 cmd echo [§4.6]");
      CK(r1[2] == 1,                                 "(iii) QUERY_CALLBACK(1) r[2]=index echo 1 [§4.6]");
      CK(r1[3]=='l' && r1[4]=='a' && r1[5]=='y' && r1[6]=='o' && r1[7]=='u' && r1[8]=='t',
                                                       "(iii) QUERY_CALLBACK(1) r[3..]='layout' [§4.6]");
      CK(r1[9] == 0,                                 "(iii) QUERY_CALLBACK(1) name NUL-padded after 'layout' [§4.6]");

Task 7: CREATE test_notifier_host.c — main() block (iv) QUERY_CALLBACK out-of-range + side-effect-free
  - BLOCK (iv): §4.6 name absent. count==2 ⇒ index 2 is out of range.
      uint8_t idx2 = 2;
      const uint8_t *r2 = send_typed(NOTIFY_CMD_QUERY_CALLBACK, &idx2, 1);
      CK(r2[0] == NOTIFY_RESPONSE_MARKER,            "(iv) QUERY_CALLBACK(OOB) r[0]=0x51 marker [§4.6]");
      CK(r2[1] == NOTIFY_CMD_QUERY_CALLBACK,         "(iv) QUERY_CALLBACK(OOB) r[1]=0x02 cmd echo [§4.6]");
      CK(r2[2] == 2,                                 "(iv) QUERY_CALLBACK(OOB) r[2]=index echo 2 [§4.6]");
      CK(r2[3] == 0x00,                              "(iv) QUERY_CALLBACK(OOB) r[3]=0x00 name absent [§4.6]");
  - FINAL side-effect-free assertion (reads the host-callback flags ⇒ no unused,
    AND proves QUERY_* fired no host callback — only APPLY_HOST_CONTEXT (S3) does):
      CK(cb_mute_en==0 && cb_mute_dis==0 && cb_layout_en==0 && cb_layout_dis==0,
                                                       "QUERY_INFO/QUERY_CALLBACK fired no host callback (read-only queries) [§4.6]");

Task 8: CREATE test_notifier_host.c — summary + return
  - COPY the closing from test_notifier_os.c verbatim:
      printf("\nTotal tests run: %d / passed: %d / failed: %d\n", g_pass + g_fail, g_pass, g_fail);
      return g_fail ? 1 : 0;
  - WHY: the runner greps `grep -c '^FAIL:'` AND checks exit==0; both must hold.

Task 9: VALIDATE — compile (bare + strict) + run + grep
  - RUN (contract OUTPUT gate — bare flags):
      gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
          notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c -std=c99 \
          -o /tmp/test_notifier_host && echo "COMPILE OK"
  - RUN (runner-readiness gate — the runner's TEST-LINK standard, zero warnings):
      gcc -Wall -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
          -Iqmk_stubs -I. notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c \
          -o /tmp/test_notifier_host_strict && echo "STRICT COMPILE OK"
      # NOTE: -Wall (not -Wextra) for the test TU — matches the runner's test-link
      # step and test_notifier_dispatch.c/os.c (which omit case_sensitive on
      # DEFINE_SERIAL_* rows; -Wmissing-field-initializers is -Wextra only).
      # notifier.c's own compile uses -Wextra (the runner's -c step) — unchanged.
  - RUN + COUNT:
      /tmp/test_notifier_host
      echo "fails=$(/tmp/test_notifier_host 2>/dev/null | grep -c '^FAIL:' || true)"
  - EXPECT: both compiles exit 0 (strict with ZERO warnings); the binary prints
      all PASS: lines, fails==0, exit 0.
  - CLEAN: rm -f /tmp/test_notifier_host /tmp/test_notifier_host_strict
```

> **Tasks 1-8 are one `write` call** (a single new file). Assemble them in order
> into one `test_notifier_host.c`. Task 9 is validation only.

### Reference implementation (assemble into the single file)

```c
/* test_notifier_host.c — Host-Side Rules & Typed Commands (§4.6) host test.
 *
 * Stub-compiles notifier.c (P1.M2 implementation) and drives typed command
 * reports through the PUBLIC hid_notify entry, asserting the [0x51][cmd_id]
 * [payload…] responses (§4.6) via stub_get_last_response() (P1.M3.T1.S1).
 * Follows the EXACT pattern of test_notifier_dispatch.c / test_notifier_os.c
 * (file-scope DEFINE_*, CK helper, PASS:/FAIL:, summary, return g_fail?1:0);
 * the runner greps `grep -c '^FAIL:'`.
 *
 * This slice (P1.M3.T1.S2) gates the two READ-ONLY query handlers:
 *   (i)   QUERY_INFO (0x01) response layout — §4.6 capability handshake:
 *         [proto_ver][feature_flags][callback_count][board_rules_present].
 *   (ii)  has_been_queried — §4.6 handshake timing: QUERY_INFO bypasses
 *         process_full_message, so board state set by a prior legacy dispatch
 *         survives QUERY_INFO (even a second one). has_been_queried is a
 *         file-static with no accessor; assert its observable consequence.
 *   (iii) QUERY_CALLBACK (0x02) valid index — §4.6 name discovery:
 *         [index][name bytes, NUL-padded].
 *   (iv)  QUERY_CALLBACK out-of-range — §4.6 name absent: [index][0x00].
 * Siblings P1.M3.T1.S3 (SET_OS + APPLY_HOST_CONTEXT) and S3.T1.S4 (coexistence +
 * multi-report) APPEND blocks to this file; this task seeds the scaffolding.
 *
 * Build (PRD §11.1):
 *   gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
 *       notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c -std=c99
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"

/* Non-static entry points implemented in notifier.c (which #includes pattern_match.c). */
void hid_notify(uint8_t *data, uint8_t length);
bool process_full_message(char *data);

/* Test-harness observables in qmk_stubs.c — MANUAL EXTERN (F6 convention; NOT a
 * header). stub_get_last_response ships with the parallel P1.M3.T1.S1. */
uint8_t        stub_get_active_layer(void);
const uint8_t *stub_get_last_response(void);

/* --- HOST callback registry (F6 distinguishable callbacks; S3-ready) ---
 * Two named entries so QUERY_INFO reports callback_count=2 and QUERY_CALLBACK
 * can discover both names. on_enable/on_disable set flags S3 (APPLY_HOST_CONTEXT)
 * will assert; here the flags also prove QUERY_* fired NO host callback. */
static int cb_mute_en = 0, cb_mute_dis = 0, cb_layout_en = 0, cb_layout_dis = 0;
static void cb_mute_on(void)   { cb_mute_en++; }
static void cb_mute_off(void)  { cb_mute_dis++; }
static void cb_layout_on(void) { cb_layout_en++; }
static void cb_layout_off(void){ cb_layout_dis++; }
DEFINE_HOST_CALLBACKS({
    { "mute",   cb_mute_on,  cb_mute_off  },
    { "layout", cb_layout_on, cb_layout_off },
});

/* --- BOARD maps (make board_rules_present==1; seed board state for (ii)) --- */
static int board_cmd_en = 0, board_cmd_dis = 0;
static void board_cmd_on(void)  { board_cmd_en++; }
static void board_cmd_off(void) { board_cmd_dis++; }
DEFINE_SERIAL_COMMANDS({
    { "neovide", board_cmd_on, board_cmd_off },
});
DEFINE_SERIAL_LAYERS({
    { "neovide", 5 },
});

static int g_pass = 0, g_fail = 0;
#define CK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS: %s\n", name); } \
    else      { g_fail++; printf("FAIL: %s\n", name); } \
} while (0)

/* send a single-report typed command [0x81][0x9F][0xF0][cmd_id][args…][0x03],
 * drive it through hid_notify, and return the captured 32-byte response.
 * §4.6 ETX framing; single-report (all QUERY_* args fit in 30 payload bytes). */
static const uint8_t *send_typed(uint8_t cmd_id, const uint8_t *args, uint8_t nargs) {
    uint8_t rep[32];
    memset(rep, 0, sizeof(rep));
    rep[0] = 0x81; rep[1] = 0x9F; rep[2] = NOTIFY_CMD_DISCRIMINATOR; /* 0xF0 */
    rep[3] = cmd_id;
    for (uint8_t i = 0; i < nargs; i++) rep[4 + i] = args[i];
    rep[4 + nargs] = ETX_TERMINATOR[0];                              /* 0x03 */
    hid_notify(rep, 32);
    return stub_get_last_response();
}

int main(void) {
    /* ===== (i) QUERY_INFO response layout — §4.6 capability handshake ===== */
    {
        const uint8_t *r = send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);
        CK(r[0] == NOTIFY_RESPONSE_MARKER,                                              "(i) QUERY_INFO r[0]=0x51 marker [§4.6]");
        CK(r[1] == NOTIFY_CMD_QUERY_INFO,                                               "(i) QUERY_INFO r[1]=0x01 cmd echo [§4.6]");
        CK(r[2] == NOTIFY_PROTO_VER,                                                    "(i) QUERY_INFO r[2]=proto_ver=2 [§4.6]");
        CK((r[3] & NOTIFY_FEATURE_APPLY_HOST_CONTEXT) && (r[3] & NOTIFY_FEATURE_CALLBACK_REGISTRY), "(i) QUERY_INFO r[3]=feature_flags bits 0,1 set [§4.6]");
        CK(r[4] == 2,                                                                   "(i) QUERY_INFO r[4]=callback_count=2 [§4.6]");
        CK(r[5] == 1,                                                                   "(i) QUERY_INFO r[5]=board_rules_present=1 [§4.6]");
    }

    /* ===== (ii) has_been_queried — board state survives QUERY_INFO — §4.6 handshake timing =====
     * has_been_queried is a file-static set in the QUERY_INFO handler but never
     * read by any gate (no accessor). Its observable consequence: the typed path
     * bypasses process_full_message, so board state is untouched by QUERY_INFO. */
    {
        board_cmd_en = board_cmd_dis = 0;
        char m[] = "neovide";
        process_full_message(m);                              /* board layer 5 + board cmd */
        CK(stub_get_active_layer() == 5,                      "(ii) setup: legacy dispatch set board layer 5");
        CK(board_cmd_en == 1,                                 "(ii) setup: board command enabled");

        board_cmd_dis = 0;
        (void)send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);     /* 1st QUERY_INFO */
        CK(stub_get_active_layer() == 5,                      "(ii) 1st QUERY_INFO: board layer NOT cleared (typed path side-effect-free) [§4.6]");
        CK(board_cmd_dis == 0,                                "(ii) 1st QUERY_INFO: board command NOT disabled [§4.6]");

        (void)send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);     /* 2nd QUERY_INFO */
        CK(stub_get_active_layer() == 5,                      "(ii) 2nd QUERY_INFO: board layer still NOT cleared [§4.6 handshake timing]");
    }

    /* ===== (iii) QUERY_CALLBACK valid index — §4.6 name discovery ===== */
    {
        uint8_t idx0 = 0;
        const uint8_t *r0 = send_typed(NOTIFY_CMD_QUERY_CALLBACK, &idx0, 1);
        CK(r0[0] == NOTIFY_RESPONSE_MARKER,                  "(iii) QUERY_CALLBACK(0) r[0]=0x51 marker [§4.6]");
        CK(r0[1] == NOTIFY_CMD_QUERY_CALLBACK,               "(iii) QUERY_CALLBACK(0) r[1]=0x02 cmd echo [§4.6]");
        CK(r0[2] == 0,                                       "(iii) QUERY_CALLBACK(0) r[2]=index echo 0 [§4.6]");
        CK(r0[3]=='m' && r0[4]=='u' && r0[5]=='t' && r0[6]=='e', "(iii) QUERY_CALLBACK(0) r[3..]='mute' [§4.6]");
        CK(r0[7] == 0,                                       "(iii) QUERY_CALLBACK(0) name NUL-padded after 'mute' [§4.6]");

        uint8_t idx1 = 1;
        const uint8_t *r1 = send_typed(NOTIFY_CMD_QUERY_CALLBACK, &idx1, 1);
        CK(r1[0] == NOTIFY_RESPONSE_MARKER,                  "(iii) QUERY_CALLBACK(1) r[0]=0x51 marker [§4.6]");
        CK(r1[1] == NOTIFY_CMD_QUERY_CALLBACK,               "(iii) QUERY_CALLBACK(1) r[1]=0x02 cmd echo [§4.6]");
        CK(r1[2] == 1,                                       "(iii) QUERY_CALLBACK(1) r[2]=index echo 1 [§4.6]");
        CK(r1[3]=='l' && r1[4]=='a' && r1[5]=='y' && r1[6]=='o' && r1[7]=='u' && r1[8]=='t', "(iii) QUERY_CALLBACK(1) r[3..]='layout' [§4.6]");
        CK(r1[9] == 0,                                       "(iii) QUERY_CALLBACK(1) name NUL-padded after 'layout' [§4.6]");
    }

    /* ===== (iv) QUERY_CALLBACK out-of-range — §4.6 name absent =====
     * callback_count==2 ⇒ index 2 is out of range ⇒ [index][0x00]. */
    {
        uint8_t idx2 = 2;
        const uint8_t *r2 = send_typed(NOTIFY_CMD_QUERY_CALLBACK, &idx2, 1);
        CK(r2[0] == NOTIFY_RESPONSE_MARKER,                  "(iv) QUERY_CALLBACK(OOB) r[0]=0x51 marker [§4.6]");
        CK(r2[1] == NOTIFY_CMD_QUERY_CALLBACK,               "(iv) QUERY_CALLBACK(OOB) r[1]=0x02 cmd echo [§4.6]");
        CK(r2[2] == 2,                                       "(iv) QUERY_CALLBACK(OOB) r[2]=index echo 2 [§4.6]");
        CK(r2[3] == 0x00,                                    "(iv) QUERY_CALLBACK(OOB) r[3]=0x00 name absent [§4.6]");
    }

    /* ===== side-effect-free: QUERY_* fired NO host callback (read-only) — §4.6 =====
     * Only APPLY_HOST_CONTEXT (P1.M3.T1.S3) calls apply_host_callbacks. Reading
     * these flags also guarantees no unused-symbol warnings under -Wextra. */
    CK(cb_mute_en == 0 && cb_mute_dis == 0 && cb_layout_en == 0 && cb_layout_dis == 0,
                                                              "QUERY_INFO/QUERY_CALLBACK fired no host callback (read-only queries) [§4.6]");

    printf("\nTotal tests run: %d / passed: %d / failed: %d\n", g_pass + g_fail, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
```

### Implementation Patterns & Key Details

```c
// PATTERN: file-scope DEFINE_* override the weak defaults FOR THIS BINARY ONLY.
// Each test_notifier_*.c is a separate TU linked with its own notifier.o, so
// defining DEFINE_HOST_CALLBACKS / DEFINE_SERIAL_* here does NOT clash with
// test_notifier_dispatch.c or test_notifier_os.c. (F1: weak-symbol override is
// the proven pattern.)

// PATTERN: send_typed builds the §4.6 single-report frame and returns the capture.
// [0x81][0x9F] magic, [0xF0] discriminator (typed_mode set on msg_index==0),
// [cmd_id], [args…], [0x03] ETX. hid_notify strips the 2-byte magic, reassembles,
// and at ETX dispatches to handle_typed_command (data[1]=cmd_id, data[2..]=args).
// stub_get_last_response() then holds the [0x51][cmd_id][payload…] response.

// PATTERN: CK(cond, name) — every assertion goes through CK so the runner's
// `grep -c '^FAIL:'` counts failures and the summary/return are uniform. Copy
// verbatim from test_notifier_os.c.

// PATTERN: use the NOTIFY_* constants from notifier.h, NOT magic numbers.
// NOTIFY_RESPONSE_MARKER (0x51), NOTIFY_CMD_QUERY_INFO (0x01),
// NOTIFY_CMD_QUERY_CALLBACK (0x02), NOTIFY_PROTO_VER (2),
// NOTIFY_FEATURE_APPLY_HOST_CONTEXT (0x01), NOTIFY_FEATURE_CALLBACK_REGISTRY
// (0x02), NOTIFY_CMD_DISCRIMINATOR (0xF0), ETX_TERMINATOR[0] (0x03). Only the
// raw magic header 0x81/0x9F and the name char literals are literals.

// CRITICAL: has_been_queried has NO accessor — assert board-state survival, not
// the bool itself. process_full_message(m) sets activated_layer (observable via
// stub_get_active_layer) and current_command (observable via the board_cmd_dis
// flag). QUERY_INFO must not clear either, twice.

// CRITICAL: the (ii) board-state block must run its process_full_message AFTER
// any prior block that might have changed g_active_layer. Ordering (i) then
// (ii) is safe: (i) only reads the response buffer (board state starts at
// LAYER_UNSET=255, untouched by QUERY_INFO). (ii) explicitly establishes layer 5.
```

### Integration Points

```yaml
NO database / config / route / migration / firmware changes. Pure test file.

BUILD/LINK:
  - test_notifier_host.c is a NEW sibling of test_notifier_dispatch.c /
    test_notifier_os.c at repo root. It compiles + links with the SAME flags the
    runner uses for the other two (P1.M3.T2.S1 will add the 3rd binary line).
  - It consumes stub_get_last_response (P1.M3.T1.S1) + stub_get_active_layer
    (existing) via MANUAL EXTERN — no header, no build-flag change.

SYMBOL NAMESPACE:
  - DEFINE_HOST_CALLBACKS / DEFINE_SERIAL_COMMANDS / DEFINE_SERIAL_LAYERS generate
    strong user_host_callbacks / user_command_map / user_layer_map + their
    accessors, overriding notifier.c's weak defaults in THIS binary only. No
    collision with other test binaries (separate TUs).

SCOPE BOUNDARY (do NOT cross — later siblings own these):
  - SET_OS (0x03) + APPLY_HOST_CONTEXT (0x05) tests  → P1.M3.T1.S3
  - coexistence/backward-compat + multi-report typed framing → P1.M3.T1.S4
  - run_notifier_stub_tests.sh extension (3rd binary)      → P1.M3.T2.S1
  - qmk_stubs.c (stub_get_last_response)                   → P1.M3.T1.S1 (parallel)
  - notifier.c / notifier.h                                → P1.M2 (landed)
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# Contract OUTPUT gate — bare flags (must produce a binary, exit 0):
gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c -std=c99 \
    -o /tmp/test_notifier_host && echo "COMPILE OK (bare)"
# Expected: "COMPILE OK (bare)". If it fails on an undefined
# stub_get_last_response, that is the S1 prerequisite not yet merged — the TU is
# still well-formed; re-run after S1 lands.

# Runner-readiness gate — the runner's TEST-LINK standard (P1.M3.T2.S1 uses these
# for the test link step; matches test_notifier_dispatch.c/os.c):
gcc -Wall -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c \
    -o /tmp/test_notifier_host_strict && echo "STRICT COMPILE OK"
# Expected: "STRICT COMPILE OK" with NO warning lines. Use -Wall (NOT -Wextra) for
# the test TU: -Wextra is notifier.c's -c standard only, and -Wmissing-field-
# initializers (-Wextra) would flag the intentional omission of case_sensitive on
# DEFINE_SERIAL_* rows that the sibling tests also omit. (notifier.c is unchanged
# and stays clean under its own -Wall -Wextra runner -c step.)
```

### Level 2: Unit Tests (Component Validation)

```bash
# Run the binary and count FAIL: lines (the runner's gate):
/tmp/test_notifier_host
rc=$?
fails=$(/tmp/test_notifier_host 2>/dev/null | grep -c '^FAIL:' || true)
echo "exit=$rc fails=$fails"
# Expected: every assertion prints PASS:, the summary prints "failed: 0",
# exit==0, fails==0. If any FAIL: appears, read its [§4.6] tag to see which
# response byte drifted, then re-check notifier.c's handler / your byte index.
rm -f /tmp/test_notifier_host /tmp/test_notifier_host_strict
```

### Level 3: Integration Testing (System Validation)

```bash
# Confirm NO regression in the existing two notifier suites (this task only
# ADDS a file, so they must stay green):
./run_notifier_stub_tests.sh
# Expected: "notifier dispatch fails=0", "notifier os fails=0",
# "✓ notifier stub-compile gate PASSED", exit 0. (test_notifier_host is NOT yet
# wired into this script — that is P1.M3.T2.S1. Here we only prove the new file
# does not break the existing two via shared notifier.c/qmk_stubs.c compiles.)

# Confirm the 9 pattern-match suites are untouched (smoke):
./run_all_tests.sh
# Expected: all suites green; this task changed no matcher code.
```

### Level 4: Creative & Domain-Specific Validation

```bash
# (Optional confidence) Dump the raw QUERY_INFO response bytes to eyeball the
# §4.6 layout end-to-end through the REAL notifier.c typed path:
cat > /tmp/host_dump.c <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include "notifier.h"
void hid_notify(uint8_t *data, uint8_t length);
const uint8_t *stub_get_last_response(void);
int main(void){
    uint8_t rep[32] = {0};
    rep[0]=0x81; rep[1]=0x9F; rep[2]=NOTIFY_CMD_DISCRIMINATOR;
    rep[3]=NOTIFY_CMD_QUERY_INFO; rep[4]=0x03;          /* ETX */
    hid_notify(rep, 32);
    const uint8_t *r = stub_get_last_response();
    printf("QUERY_INFO resp:");
    for (int i = 0; i < 8; i++) printf(" %02X", r[i]);
    printf("\n  expect: 51 01 02 03 00 00 00 00  (count/board depend on THIS tu's maps)\n");
    return 0;
}
EOF
gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    notifier.c qmk_stubs/qmk_stubs.c /tmp/host_dump.c -std=c99 -o /tmp/host_dump \
    && /tmp/host_dump
# NOTE: this dump TU has NO DEFINE_HOST_CALLBACKS/DEFINE_SERIAL_* (weak defaults
# ⇒ cb_size=0, board_rules_present=0), so expect 51 01 02 01 00 00 … — i.e. the
# same LAYOUT the test asserts, just with the no-registry field values. This
# proves the path end-to-end; the committed test pins the WITH-registry values.
rm -f /tmp/host_dump.c /tmp/host_dump
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 bare compile (`gcc … test_notifier_host.c -std=c99`) → binary, exit 0.
- [ ] Level 1 strict compile of the test TU (`-Wall -std=c99`) → ZERO warnings
      (the runner's test-link standard; `-Wextra` is `notifier.c`'s `-c` standard).
- [ ] Level 2 run → all `PASS:`, summary `failed: 0`, `grep -c '^FAIL:'` == 0, exit 0.
- [ ] Level 3 `./run_notifier_stub_tests.sh` still PASSED (no regression; new file
      is additive) and `./run_all_tests.sh` still green.

### Feature Validation

- [ ] (i) QUERY_INFO asserts r[0]=0x51, r[1]=0x01, r[2]=2, r[3] bits 0&1, r[4]=2,
      r[5]=1 — all six fields of the §4.6 capability handshake.
- [ ] (ii) board layer 5 + board command survive TWO QUERY_INFOs (the indirect
      has_been_queried check the contract specifies).
- [ ] (iii) QUERY_CALLBACK(0)→"mute" and QUERY_CALLBACK(1)→"layout" with correct
      index echoes and NUL padding.
- [ ] (iv) QUERY_CALLBACK(2) out-of-range → r[2]=2, r[3]=0x00 name absent.
- [ ] Side-effect-free assertion: no host callback fired by any QUERY_*.
- [ ] Mode-A header comment maps each block to its §4.6 criterion.

### Code Quality Validation

- [ ] Follows `test_notifier_os.c` pattern verbatim (CK macro, manual externs,
      file-scope DEFINE_*, summary, return).
- [ ] Uses NOTIFY_* constants from notifier.h (no magic numbers except 0x81/0x9F
      magic header and name char literals).
- [ ] `send_typed` helper is the single entry for all four blocks.
- [ ] No compound literals for arg arrays (local uint8_t vars).
- [ ] File is S3/S4-extensible (scaffolding + 4 blocks; no pre-built hooks).
- [ ] Diff is purely the one new file (no other file touched).

### Documentation & Deployment

- [ ] Mode-A header comment rides WITH the work (DOCS §5) — done as the file header.
- [ ] No README change in this task (README sync is P1.M3.T3.S1).
- [ ] No new environment variables / config / firmware build flags.

---

## Anti-Patterns to Avoid

- ❌ Don't add SET_OS / APPLY_HOST_CONTEXT / coexistence / multi-report test cases —
  those are P1.M3.T1.S3 / S4. This task is ONLY QUERY_INFO + QUERY_CALLBACK.
- ❌ Don't try to assert `has_been_queried` directly — it's a file-static with no
  accessor and is never read by any gate. Assert board-state survival instead
  (the contract's "(ii) verify indirectly").
- ❌ Don't add a phantom arg byte before the ETX in the QUERY_INFO report — QUERY_INFO
  reads NO args; the frame is `[0x81][0x9F][0xF0][0x01][0x03]`.
- ❌ Don't `#include` a header for `stub_get_last_response` / `stub_get_active_layer`
  — use a MANUAL EXTERN (F6 convention; test_notifier_os.c:37). There is no header.
- ❌ Don't read `stub_get_last_response()` after a different `hid_notify` — it is
  last-write-wins. Read it immediately after the triggering `send_typed`.
- ❌ Don't hardcode 0x51/0x01/0x02/0xF0/2 — use the NOTIFY_* constants from notifier.h.
- ❌ Don't modify `run_notifier_stub_tests.sh` (P1.M3.T2.S1), `qmk_stubs.c`
  (P1.M3.T1.S1), `notifier.*` (P1.M2), or any existing test file — this task is
  ONE new file only.
- ❌ Don't leave file-scope callback flags unread — the (ii) block reads the
  board-command flags and the final assertion reads the host-callback flags (both
  meaningful, not `(void)` hacks; also harmless under the runner's -Wall test-link
  standard).
- ❌ Don't use compound literals for the arg arrays — pass a local `uint8_t` by
  address (maximally clean under `-Wall`).