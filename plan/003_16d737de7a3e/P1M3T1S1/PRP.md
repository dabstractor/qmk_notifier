name: "P1.M3.T1.S1 — stub_get_last_response accessor (qmk_stubs.c response capture)"
description: >
  Add a test-harness observable to qmk_stubs/qmk_stubs.c: a file-static
  g_last_response[32] buffer captured by raw_hid_send (memcpy before the debug
  print) and exposed via a new const uint8_t* stub_get_last_response() accessor.
  Mirrors the stub_get_active_layer() precedent (findings F6). Pure additive
  test infrastructure — enables the test_notifier_host.c suite (P1.M3.T1.S2/S3/S4)
  to assert typed-response bytes ([0]=0x51 marker, [1]=cmd echo, [2..]=payload).
  NOT production code; qmk_stubs.c never links into firmware.

---

## Goal

**Feature Goal**: Extend `qmk_stubs/qmk_stubs.c` so the host stub harness
**captures the full 32-byte `raw_hid_send` response** into a file-static buffer
and **exposes it to tests** via a new `stub_get_last_response()` accessor. Today
`raw_hid_send` only prints `response[0]` to stderr and discards the rest, so no
host test can assert the typed-response wire bytes. This accessor is the
**infrastructure prerequisite** for the `test_notifier_host.c` suite
(P1.M3.T1.S2/S3/S4), which must verify the `[0x51][cmd_id][payload…]` responses
emitted by `QUERY_INFO` / `QUERY_CALLBACK` / `SET_OS` / `APPLY_HOST_CONTEXT`
(§4.6 typed-command namespace). It follows the `stub_get_active_layer()`
precedent (findings **F6**) exactly: a file-static observable + a thin accessor,
test-harness-only, never compiled into firmware.

**Deliverable**: ONE file modified — `qmk_stubs/qmk_stubs.c`. Four edits:
1. **ADD** `#include <string.h>` to the include block (CRITICAL — `memcpy`
   needs it; the contract omits this and the build fails under `-std=c99`
   without it).
2. **ADD** `static uint8_t g_last_response[32];` file-static, immediately after
   `g_active_layer` (with the Mode-A F6 comment).
3. **MODIFY** `raw_hid_send`: insert
   `memcpy(g_last_response, data, (length < 32) ? length : 32);` as the **first**
   statement (before the existing `fprintf` debug print), and drop the now-redundant
   `(void)length;` line (length is now consumed by the memcpy). Keep the `fprintf`
   print unchanged.
4. **ADD** `const uint8_t *stub_get_last_response(void) { return g_last_response; }`
   immediately after `stub_get_active_layer` (with the Mode-A F6 comment).

No new files, no header changes (accessor is declared via manual extern in test
TUs, matching the `stub_get_active_layer` convention), no `notifier.c`/`notifier.h`
edits, no test files written by this task.

**Success Definition**:
- `qmk_stubs/qmk_stubs.c` compiles **clean standalone** under
  `gcc -Wall -Wextra -std=c99 -c qmk_stubs/qmk_stubs.c` → exit 0, **zero warnings**
  (the `<string.h>` addition is what makes this pass — without it `memcpy` is an
  implicit-declaration error in C99).
- `./run_notifier_stub_tests.sh` → **dispatch fails=0, os fails=0**, both binaries
  exit 0, `✓ notifier stub-compile gate PASSED` (the new symbol is purely
  additive; baseline at research time is 31/31 PASS). **No regression.**
- A small driver linked against the modified `qmk_stubs.o` confirms
  `stub_get_last_response()` exposes `[0]`/`[1]`/`[2..]` of the last
  `raw_hid_send` for BOTH a typed `[0x51]…` response AND a legacy `[0|1]` ack,
  and that a short `length < 32` send is clamped (no over-read). **(Empirically
  verified during research — all assertions passed.)**
- Mode-A comment ("Test-harness observable (not production); mirrors
  stub_get_active_layer precedent (F6).") present on BOTH `g_last_response` and
  `stub_get_last_response`.
- No edits to: `notifier.c`, `notifier.h`, `pattern_match.*`, any other
  `qmk_stubs/*` file, any `test_*.c`, `run_*.sh`, `PRD.md`, `tasks.json`,
  `rules.mk`, `.gitignore`.

## User Persona (if applicable)

**Target User**: The contributor writing `test_notifier_host.c` (P1.M3.T1.S2/S3/S4)
and the maintainer running the §11.2D stub-compile gate. End users / the desktop
host never see this — it is test infrastructure.

**Use Case**: A test drives a typed command through the PUBLIC `hid_notify`
entry (e.g. a `[0x81][0x9F][0xF0][0x01][0x03]` QUERY_INFO report). The firmware
calls `send_typed_response` → `raw_hid_send(response, 32)`. The stub captures
all 32 bytes into `g_last_response`. The test then reads
`stub_get_last_response()` and asserts `r[0]==0x51`, `r[1]==0x01`, `r[2]==2`
(proto_ver), etc. Without this accessor, the test could only see `response[0]`
on stderr — useless for payload assertions.

**User Journey**: test author adds `const uint8_t *stub_get_last_response(void);`
manual extern to their TU (precedent: `stub_get_active_layer`) → calls
`hid_notify(rep, 32)` → `const uint8_t *r = stub_get_last_response();` → asserts
the marker / echo / payload bytes. Each `hid_notify` that triggers a response
leaves a fresh capture for the immediately-following assertions.

**Pain Points Addressed**: `raw_hid_send`'s 32-byte response was opaque to host
tests (only `[0]` printed). The typed-command milestone (P1.M2) emits rich
`[0x51][cmd][payload]` responses that MUST be asserted to gate the handlers —
this accessor is the only host-testable window onto those bytes.

## Why

- **Unblocks the typed-command test suite.** P1.M2.T2.S1/S2/S3 landed four typed
  handlers + the `send_typed_response` builder; P1.M3.T1.S2/S3/S4 must write the
  tests that gate them. Those tests cannot assert response bytes without this
  capture. This task is the explicit S1 prerequisite named in the plan
  (`P1.M3.T1.S1` → `S2/S3/S4`).
- **Established precedent (F6), zero design risk.** `stub_get_active_layer()`
  (plan 002) already proved the "file-static observable + thin accessor"
  pattern works cleanly in this exact harness and is consumed by
  `test_notifier_os.c` via a manual extern. `stub_get_last_response()` is the
  same shape, returning a `const uint8_t*` instead of a `uint8_t`. Findings F6:
  "Risk: none."
- **Pure additive test infrastructure — no production impact.** `qmk_stubs.c` is
  NEVER compiled into firmware; only the host harness links it. Adding a capture
  buffer + accessor changes nothing about the real `raw_hid_send` (which QMK
  provides) or any firmware behavior.
- **Contained and surgical.** One file, four small edits, ~12 added lines. No
  new mechanism, no interface change, no linkable symbol that could clash (the
  `stub_*` namespace is test-harness-only by convention).

## What

Modify ONE file: `qmk_stubs/qmk_stubs.c`. Four edits (exact text in
Implementation Tasks):

1. **`#include <string.h>`** — add to the include block. (`memcpy` requires it;
   without it `-std=c99` rejects the implicit declaration. The item contract's
   snippet omits this — it is the implementer's responsibility to add it.)
2. **`static uint8_t g_last_response[32];`** — file-static, after
   `g_active_layer`, with the Mode-A F6 comment.
3. **`raw_hid_send` capture** — insert the `memcpy` as the first statement,
   drop `(void)length;`, keep the `fprintf` debug print:
   ```c
   void raw_hid_send(uint8_t *data, uint8_t length) {
       memcpy(g_last_response, data, (length < 32) ? length : 32);
       fprintf(stderr, "[stub] raw_hid_send response[0]=%u\n", data[0]);
   }
   ```
4. **`stub_get_last_response` accessor** — after `stub_get_active_layer`, with
   the Mode-A F6 comment:
   ```c
   const uint8_t *stub_get_last_response(void) { return g_last_response; }
   ```

**What the accessor exposes** (from `send_typed_response`, notifier.c:628-637 —
already landed in P1.M2.T2.S1):
- `r[0]` = `0x51` `NOTIFY_RESPONSE_MARKER` (distinct from the legacy `0`/`1`
  match-bool ack — the host disambiguates on `response[0]`).
- `r[1]` = `cmd_id` echo (`0x01` QUERY_INFO, `0x02` QUERY_CALLBACK, `0x03`
  SET_OS, `0x05` APPLY_HOST_CONTEXT).
- `r[2..]` = payload (capped at 30 bytes), remainder zero-padded.
- It ALSO captures the legacy string ack (notifier.c:824, `[0|1][zero-pad]`) —
  last-write-wins, so a test asserts immediately after the triggering `hid_notify`.

**Do NOT**: add a header declaration for `stub_get_last_response` (the convention
is a manual extern in each test TU, matching `stub_get_active_layer`); modify
`notifier.c`/`notifier.h`; write any test file; change `run_notifier_stub_tests.sh`
(that's P1.M3.T2.S1); add any other stub observable.

### Success Criteria

- [ ] `#include <string.h>` added to qmk_stubs.c include block.
- [ ] `static uint8_t g_last_response[32];` present after `g_active_layer`.
- [ ] `raw_hid_send` does `memcpy(g_last_response, data, (length < 32) ? length : 32);`
      as its first statement, then the unchanged `fprintf`; `(void)length;` removed.
- [ ] `const uint8_t *stub_get_last_response(void) { return g_last_response; }`
      present after `stub_get_active_layer`.
- [ ] Mode-A F6 comment on both `g_last_response` and `stub_get_last_response`.
- [ ] `gcc -Wall -Wextra -std=c99 -c qmk_stubs/qmk_stubs.c` → exit 0, 0 warnings.
- [ ] `./run_notifier_stub_tests.sh` → both binaries 0 FAIL, exit 0, `PASSED`.
- [ ] No file other than `qmk_stubs/qmk_stubs.c` is modified.

## All Needed Context

### Context Completeness Check

**Pass.** The exact four edits are given verbatim in Implementation Tasks and
were **empirically validated during research** by applying them to a /tmp copy
of `qmk_stubs.c`: standalone compile clean under `-Wall -Wextra -std=c99`
(0 warnings); a linked driver confirmed the accessor exposes `[0]`/`[1]`/`[2..]`
for a typed `0x51` response, a legacy `0x01` ack, and a clamped short send. The
baseline `./run_notifier_stub_tests.sh` passes 31/31. The `<string.h>`
requirement (the one thing the contract omits) was proven necessary and
sufficient. The response-byte layout the accessor exposes was read directly from
`send_typed_response` (notifier.c:628-637). An implementer with only this PRP +
repo access can make the four edits and prove both gates green.

### Documentation & References

```yaml
# MUST READ — the F6 precedent this task follows (Risk: none)
- file: plan/003_16d737de7a3e/architecture/findings_and_risks.md
  section: "### F6 — Stub observables precedent established"
  why: "Establishes that stub_get_active_layer() (plan 002) is the pattern to
        mirror, and explicitly states stub_get_last_response() follows it with
        'Risk: none.' This task IS that prescription."
  critical: "F6 blesses the file-static-observable + thin-accessor pattern.
        Match stub_get_active_layer's shape exactly (only the return type differs:
        const uint8_t* vs uint8_t)."

# MUST READ — the ONLY file being modified
- file: qmk_stubs/qmk_stubs.c
  why: "Contains g_active_layer (insert g_last_response after it), raw_hid_send
        (insert the memcpy + drop (void)length), and stub_get_active_layer
        (insert stub_get_last_response after it)."
  pattern: "Mirror stub_get_active_layer's block-comment + one-liner-accessor
        style for the new observable. Keep raw_hid_send's fprintf debug print
        unchanged (it is useful stderr trace during test runs)."
  gotcha: "The file includes ONLY <stdint.h> and <stdio.h>. memcpy needs
        <string.h> — ADD IT or the -std=c99 build fails (implicit declaration).
        The item contract's snippet omits this; the implementer MUST add it."

# MUST READ — the response format the accessor exposes (already-landed code)
- file: notifier.c
  section: "send_typed_response (lines ~622-637) + the legacy ack (line ~824)"
  why: "send_typed_response builds uint8_t response[RAW_REPORT_SIZE=32]={0},
        sets response[0]=NOTIFY_RESPONSE_MARKER (0x51), response[1]=cmd_id,
        memcpy's the payload into response[2..] (capped at 30), then calls
        raw_hid_send(response, RAW_REPORT_SIZE). So stub_get_last_response()[0]
        is 0x51, [1] is the cmd echo, [2..] is the payload. The legacy ack at
        ~824 also calls raw_hid_send(response, 32) with [0]=0/1."
  critical: "Both raw_hid_send call sites pass exactly 32 bytes (verified), so
        the (length<32)?length:32 clamp always copies all 32 in practice; the
        clamp is a defensive bound only. This task does NOT touch notifier.c."

# MUST READ — the manual-extern convention for stub observables (downstream)
- file: test_notifier_os.c
  section: "line 37: 'uint8_t stub_get_active_layer(void);' (manual extern)"
  why: "Shows how test TUs declare stub observables: a manual extern, NOT a
        header #include. There is NO header declaring stub_get_active_layer.
        Downstream test_notifier_host.c (S2/S3/S4) will declare
        'const uint8_t *stub_get_last_response(void);' the same way. This PRP
        does NOT write that test and does NOT need to add any header."
  pattern: "When documenting the accessor for downstream, point test authors at
        this precedent: manual extern + immediate post-hid_notify read."

# REFERENCE — the build/link chain that must keep passing (DO NOT modify it)
- file: run_notifier_stub_tests.sh
  why: "Links notifier.o + qmk_stubs/qmk_stubs.c + test_*.c. Confirms the new
        stub_get_last_response symbol is available to every test binary that
        links qmk_stubs.c. This task adds a symbol; it cannot break linking."
  critical: "Do NOT edit this script here — extending it for test_notifier_host
        is P1.M3.T2.S1. This task only ships the accessor."

# REFERENCE — the parallel task (zero file overlap, safe to parallelize)
- file: plan/003_16d737de7a3e/P1M2T2S3/PRP.md
  why: "P1.M2.T2.S3 (running in parallel) modifies ONLY notifier.c (adds the
        APPLY_HOST_CONTEXT case). It does NOT touch qmk_stubs.c. Its research
        /tmp harness 'captured raw_hid_send' as a throwaway shim; THIS task is
        the committed capture living in qmk_stubs.c. Zero file overlap."
```

### Current Codebase tree (relevant subset)

```bash
qmk_stubs/
  qmk_keyboard_stub.h   # (untouched) layer_on/off decls, RAW_EPSIZE
  raw_hid.h             # (untouched) raw_hid_send decl
  os_detection.h        # (untouched) os_variant_t enum
  qmk_stubs.c           # MODIFY — add g_last_response, capture, accessor, <string.h>
notifier.c              # (untouched — owned by parallel P1.M2.T2.S3) defines send_typed_response
notifier.h              # (untouched)
run_notifier_stub_tests.sh  # (untouched — extended in P1.M3.T2.S1)
test_notifier_os.c      # (untouched — precedent for the manual-extern pattern)
test_notifier_dispatch.c # (untouched)
```

### Desired Codebase tree with files to be added/modified

```bash
qmk_stubs/qmk_stubs.c   # MODIFIED: +<string.h>, +g_last_response[32], +memcpy in raw_hid_send,
                        #            +stub_get_last_response() accessor (+~12 lines, 4 edits)
# nothing else changes
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL: qmk_stubs.c includes ONLY <stdint.h> and <stdio.h> today.
// The contract's memcpy REQUIRES <string.h>. ADD #include <string.h> or the
// -std=c99 build fails with an implicit-declaration error on memcpy. (Verified:
// without it, fail; with it, clean.) The item contract's snippet omits this —
// it is the ONE gotcha the implementer must not miss.

// CRITICAL: drop the `(void)length;` line in raw_hid_send when adding the
// memcpy. length is now consumed by memcpy (the (length<32)?length:32 clamp),
// so the (void)length cast is no longer needed — leaving BOTH would be dead
// code (harmless, but the memcpy makes (void)length pointless). Remove it.

// GOTCHA: last-write-wins. Each raw_hid_send overwrites g_last_response
// entirely (memcpy, no merge). Downstream tests must read
// stub_get_last_response() IMMEDIATELY after the hid_notify that triggers the
// response, before any other raw_hid_send. (Verified: a legacy ack after a
// typed response overwrites [0] from 0x51 to 0x01.) This is the intended,
// point-in-time semantics — not a bug.

// GOTCHA: the (length<32)?length:32 clamp is a DEFENSIVE bound only. Both
// raw_hid_send call sites in notifier.c (send_typed_response ~637, legacy ack
// ~824) pass exactly RAW_REPORT_SIZE (32). So in practice all 32 bytes are
// always copied. The clamp guards a hypothetical length>32 caller (uint8_t
// length maxes at 255) from overrunning the 32-byte g_last_response. Keep the
// clamp verbatim — it is the contract's literal expression.

// GOTCHA: do NOT add a header declaration for stub_get_last_response. The
// convention (stub_get_active_layer, test_notifier_os.c:37) is a MANUAL EXTERN
// in each test TU. Adding it to raw_hid.h or a new header would break the
// established pattern. Test authors redeclare it locally.

// GOTCHA: return type is `const uint8_t *` (a POINTER into the file-static
// buffer), NOT a copy. Downstream tests must NOT free() it or write through it.
// The pointer remains valid for the program's lifetime (g_last_response is
// static). Tests read individual bytes: r[0], r[1], r[2]...
```

## Implementation Blueprint

### Data models and structure

**None.** This task adds no types or structs. It adds one file-static buffer
(`uint8_t g_last_response[32]`) and one accessor returning `const uint8_t*` into
it. Both are test-harness observables, not part of any data model.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY qmk_stubs/qmk_stubs.c — add #include <string.h>
  - EDIT: the include block at the top of the file.
  - oldText:
        #include <stdint.h>
        #include <stdio.h>
  - newText:
        #include <stdint.h>
        #include <stdio.h>
        #include <string.h>
  - WHY: memcpy (added in Task 3) requires <string.h>; without it -std=c99
        rejects the implicit declaration. (Contract omits this; verified necessary.)
  - DEPENDENCIES: none (do this FIRST so the file compiles after Task 3).

Task 2: MODIFY qmk_stubs/qmk_stubs.c — add g_last_response[32] file-static
  - EDIT: insert immediately AFTER `static uint8_t g_active_layer = 255;`.
  - oldText:
        static uint8_t g_active_layer = 255;
  - newText:
        static uint8_t g_active_layer = 255;

        /* Test-harness observable (NOT production); mirrors stub_get_active_layer
         * precedent (F6). Captures the full 32-byte raw_hid_send response so host
         * tests can assert the typed-response marker 0x51 ([0]), the cmd echo ([1]),
         * and payload bytes ([2..]) for QUERY_INFO/QUERY_CALLBACK/SET_OS/
         * APPLY_HOST_CONTEXT responses. */
        static uint8_t g_last_response[32];
  - NAMING: g_last_response (g_ prefix matches g_active_layer convention).
  - PLACEMENT: file scope, next to g_active_layer (group the observables).

Task 3: MODIFY qmk_stubs/qmk_stubs.c — capture in raw_hid_send
  - EDIT: the raw_hid_send function body.
  - oldText:
        void raw_hid_send(uint8_t *data, uint8_t length) {
            (void)length;
            fprintf(stderr, "[stub] raw_hid_send response[0]=%u\n", data[0]);
        }
  - newText:
        void raw_hid_send(uint8_t *data, uint8_t length) {
            memcpy(g_last_response, data, (length < 32) ? length : 32);
            fprintf(stderr, "[stub] raw_hid_send response[0]=%u\n", data[0]);
        }
  - WHY: memcpy FIRST (before the debug print) so the capture always happens.
        Drop (void)length; — length is now consumed by the clamp. Keep fprintf
        unchanged (useful stderr trace). The (length<32)?length:32 clamp is the
        contract's literal expression — keep it verbatim.

Task 4: MODIFY qmk_stubs/qmk_stubs.c — add stub_get_last_response accessor
  - EDIT: insert immediately AFTER the stub_get_active_layer definition.
  - oldText:
        uint8_t stub_get_active_layer(void) { return g_active_layer; }
  - newText:
        uint8_t stub_get_active_layer(void) { return g_active_layer; }

        /* Test-harness observable (NOT production); mirrors stub_get_active_layer
         * precedent (F6). Returns a pointer to the last 32-byte raw_hid_send
         * response captured by raw_hid_send. Host tests read [0]=marker,
         * [1]=cmd echo, [2..]=payload. */
        const uint8_t *stub_get_last_response(void) { return g_last_response; }
  - NAMING: stub_get_last_response (stub_ prefix matches stub_get_active_layer).
  - SIGNATURE: const uint8_t * (pointer into file-static buffer; do not free/write).
  - PLACEMENT: immediately after stub_get_active_layer (group the accessors).

Task 5: VALIDATE — standalone compile + full gate + accessor driver
  - RUN: gcc -Wall -Wextra -std=c99 -Iqmk_stubs -c qmk_stubs/qmk_stubs.c -o /tmp/s.o
        → expect exit 0, 0 warnings.
  - RUN: ./run_notifier_stub_tests.sh → expect dispatch fails=0, os fails=0,
        "✓ notifier stub-compile gate PASSED".
  - RUN: the accessor driver (Validation Loop Level 3) → expect all assertions pass.
  - READ: confirm all three green; if any warning/error, fix root cause first.
```

> **Edits 1-4 can be a single `edit` call** with four entries (they target four
> disjoint, unique regions of the same file). Do NOT emit overlapping edits.

### Implementation Patterns & Key Details

```c
// PATTERN: mirror stub_get_active_layer EXACTLY (only return type differs).
// Existing (do not change):
uint8_t stub_get_active_layer(void) { return g_active_layer; }
// New (this task):
const uint8_t *stub_get_last_response(void) { return g_last_response; }

// PATTERN: file-static observable + capture-at-the-seam.
// g_active_layer is mutated inside layer_on/layer_off; g_last_response is
// captured inside raw_hid_send. Both are exposed by a thin getter. This is the
// F6-sanctioned shape — do not invent a different mechanism (no ring buffer,
// no history, no struct). One buffer, last-write-wins, point-in-time reads.

// PATTERN: the memcpy clamp (contract-literal):
memcpy(g_last_response, data, (length < 32) ? length : 32);
// length is uint8_t (max 255); g_last_response is fixed 32 bytes. The clamp
// copies min(length,32). Both real call sites pass 32, so all 32 copy in
// practice; the clamp is purely defensive. Keep verbatim.

// PATTERN: Mode-A comment rides WITH the work (DOCS §5). Put the F6 comment on
// BOTH g_last_response and stub_get_last_response (mirroring how
// stub_get_active_layer carries its own explanatory block comment). No README
// change in this task — README sync is P1.M3.T3.S1.
```

### Integration Points

```yaml
NO database / config / route / migration / firmware changes. This is pure
test-harness infrastructure.

BUILD/LINK:
  - qmk_stubs.c is linked into every host test binary by
    run_notifier_stub_tests.sh (and will be by the extended runner in
    P1.M3.T2.S1). The new stub_get_last_response symbol is therefore available
    to test_notifier_dispatch, test_notifier_os, and (future)
    test_notifier_host with NO build-flag or linkage change. Purely additive.
  - qmk_stubs.c is NEVER compiled into firmware (the QMK build provides the real
    raw_hid_send). So this change has zero production footprint.

SYMBOL NAMESPACE:
  - stub_get_last_response joins the stub_* test-harness namespace
    (alongside stub_get_active_layer). No collision risk: production code never
    defines stub_* symbols.

HEADER SURFACE:
  - NONE. Do NOT add stub_get_last_response to raw_hid.h or any header. Test TUs
    declare it via a manual extern (precedent: test_notifier_os.c:37). This is
    the established convention; follow it.
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)

```bash
# After the four edits to qmk_stubs/qmk_stubs.c:
gcc -Wall -Wextra -std=c99 -Iqmk_stubs -c qmk_stubs/qmk_stubs.c -o /tmp/qmk_stubs_check.o
echo "standalone compile exit=$?"
# Expected: exit 0, ZERO warnings. If you see "implicit declaration of function
# 'memcpy'" you forgot Task 1 (#include <string.h>) — add it and rebuild.
rm -f /tmp/qmk_stubs_check.o
```

### Level 2: Unit Tests (Component Validation)

```bash
# There is no unit test for the stub itself (it IS test infrastructure). The
# "component test" is that the committed stub-compile gate still passes — it
# links the modified qmk_stubs.c into BOTH existing test binaries, proving the
# new symbol does not break linking and qmk_stubs.c still compiles in-context:
./run_notifier_stub_tests.sh
# Expected: exit 0; output includes:
#   notifier dispatch fails=0  (exit=0)
#   notifier os fails=0  (exit=0)
#   ✓ notifier stub-compile gate PASSED
# Baseline at research time: "Total tests run: 31 / passed: 31 / failed: 0".
# (The count is from the existing suites; this task adds no test cases.)
```

### Level 3: Integration Testing (System Validation) — the accessor driver

```bash
# Prove stub_get_last_response actually exposes the captured bytes, across a
# typed (0x51) response, a legacy ack, and a clamped short send. This is the
# core functional proof that downstream tests (S2/S3/S4) can rely on.
cat > /tmp/stub_resp_driver.c <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
void raw_hid_send(uint8_t *data, uint8_t length);          /* qmk_stubs.c */
const uint8_t *stub_get_last_response(void);                /* qmk_stubs.c */
int main(void) {
    /* 1. typed response: [0x51][0x01][proto=2][flags=3][count=2][board=1] + pad */
    uint8_t typed[32] = {0};
    typed[0]=0x51; typed[1]=0x01; typed[2]=2; typed[3]=3; typed[4]=2; typed[5]=1;
    raw_hid_send(typed, 32);
    const uint8_t *r = stub_get_last_response();
    assert(r[0]==0x51 && r[1]==0x01 && r[2]==2 && r[3]==3 && r[4]==2 && r[5]==1 && r[6]==0);
    printf("PASS: typed [0x51][0x01][02][03][02][01] captured; pad [6]=0\n");

    /* 2. legacy ack overwrites (last-write-wins) */
    uint8_t legacy[32] = {0}; legacy[0]=0x01;
    raw_hid_send(legacy, 32);
    r = stub_get_last_response();
    assert(r[0]==0x01 && r[1]==0);
    printf("PASS: legacy ack [0x01] captured (overwrote prior)\n");

    /* 3. short send clamped (no over-read) */
    uint8_t short_buf[4] = {0x51, 0x02, 0x05, 0x41};
    raw_hid_send(short_buf, 4);
    r = stub_get_last_response();
    assert(r[0]==0x51 && r[1]==0x02 && r[2]==0x05 && r[3]==0x41 && r[4]==0);
    printf("PASS: short send (len=4) clamped, tail zero\n");

    printf("ALL STUB RESPONSE-ACCESSOR ASSERTIONS PASSED\n");
    return 0;
}
EOF
gcc -Wall -Wextra -std=c99 -Iqmk_stubs \
    /tmp/stub_resp_driver.c qmk_stubs/qmk_stubs.c -o /tmp/stub_resp_driver && \
    /tmp/stub_resp_driver
# Expected: all three PASS lines + "ALL STUB RESPONSE-ACCESSOR ASSERTIONS PASSED".
rm -f /tmp/stub_resp_driver.c /tmp/stub_resp_driver
```

### Level 4: Creative & Domain-Specific Validation

```bash
# (Optional) Prove the accessor works end-to-end through the REAL notifier.c
# typed path — i.e. a QUERY_INFO report driven through the PUBLIC hid_notify
# lands a [0x51][0x01][...] capture. This requires linking the stub-compiled
# notifier.o; it is a stronger integration check than Level 3 (which uses a
# synthetic raw_hid_send call). NOTE: this mirrors what P1.M3.T1.S2 will do as a
# committed test; here it is a one-off confidence check.
# (If the exact QUERY_INFO report layout is not at hand, SKIP Level 4 — Level 3
#  already proves the capture mechanism; Level 4 is a bonus, not a gate.)
echo "Level 4 is optional confidence check — Level 3 is the required gate."
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 standalone compile (`gcc -Wall -Wextra -std=c99 -c qmk_stubs.c`) →
      0 warnings (confirms `#include <string.h>` is present).
- [ ] Level 2 `./run_notifier_stub_tests.sh` → exit 0, both binaries 0 FAIL,
      `✓ notifier stub-compile gate PASSED`.
- [ ] Level 3 accessor driver → all three capture assertions PASS (typed,
      legacy-overwrite, short-send-clamp).

### Feature Validation

- [ ] `g_last_response[32]` present, file-static, after `g_active_layer`.
- [ ] `raw_hid_send` captures via `memcpy(g_last_response, data, (length<32)?length:32);`
      as its first statement; `(void)length;` removed; `fprintf` unchanged.
- [ ] `const uint8_t *stub_get_last_response(void) { return g_last_response; }`
      present after `stub_get_active_layer`.
- [ ] Mode-A F6 comment present on both `g_last_response` and the accessor.
- [ ] Diff is purely additive to qmk_stubs.c (no other file touched).

### Code Quality Validation

- [ ] `stub_get_last_response` mirrors `stub_get_active_layer`'s comment + one-liner
      style (F6 precedent), differing only in return type.
- [ ] No header declaration added (manual-extern convention preserved).
- [ ] No new mechanism invented (no ring buffer / history / struct).
- [ ] The `memcpy` clamp is the contract's literal expression (verbatim).

### Documentation & Deployment

- [ ] Mode-A F6 comment rides with the work (DOCS §5) — done as part of Tasks 2 & 4.
- [ ] No README change in this task (README sync is P1.M3.T3.S1).
- [ ] No new environment variables / config / firmware build flags.

---

## Anti-Patterns to Avoid

- ❌ Don't forget `#include <string.h>` — the contract's `memcpy` snippet omits it
  and the `-std=c99` build will fail. (Verified: this is the one omission to fix.)
- ❌ Don't leave `(void)length;` in `raw_hid_send` after adding the memcpy —
  `length` is now consumed by the clamp; the cast is dead code. Remove it.
- ❌ Don't add `stub_get_last_response` to a header (`raw_hid.h` or a new one) —
  the convention is a manual extern in each test TU (see `stub_get_active_layer`
  at test_notifier_os.c:37). Adding a header breaks the established pattern.
- ❌ Don't invent a richer capture mechanism (ring buffer, history, struct, count
  of sends). F6 blessed the simple file-static + thin-accessor shape; mirror it.
- ❌ Don't return a copy or a malloc'd buffer — return `const uint8_t *` into the
  file-static `g_last_response` (callers read individual bytes; never free/write).
- ❌ Don't place the `memcpy` after the `fprintf` — capture FIRST, so the buffer
  is always populated even if the print were ever changed/removed.
- ❌ Don't touch `notifier.c`, `notifier.h`, any other `qmk_stubs/*`, any test
  file, or `run_notifier_stub_tests.sh` — those are other tasks' scope (the
  runner extension is P1.M3.T2.S1; the tests are P1.M3.T1.S2/S3/S4).
- ❌ Don't write a test case for the stub in this task — this ships the accessor
  ONLY; downstream tasks consume it.