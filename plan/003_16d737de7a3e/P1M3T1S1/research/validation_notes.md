# Research Notes — P1.M3.T1.S1 (stub_get_last_response accessor in qmk_stubs.c)

## What this task is
Add a **test-harness observable** to `qmk_stubs/qmk_stubs.c` that captures the
full 32-byte `raw_hid_send` response, exposed via a new
`stub_get_last_response()` accessor. This mirrors the `stub_get_active_layer()`
precedent (findings F6) and is the **infrastructure prerequisite** for the
`test_notifier_host.c` suite (P1.M3.T1.S2/S3/S4), which must assert the typed
response bytes (`[0x51][cmd_id][payload…]`). NOT production code.

## Current qmk_stubs.c (26 lines — the ONLY file this task touches)
```c
#include <stdint.h>
#include <stdio.h>

static uint8_t g_active_layer = 255;

void layer_on(uint8_t layer) { ... }
void layer_off(uint8_t layer) { ... }
void raw_hid_send(uint8_t *data, uint8_t length) {
    (void)length;
    fprintf(stderr, "[stub] raw_hid_send response[0]=%u\n", data[0]);
}

uint8_t stub_get_active_layer(void) { return g_active_layer; }
```

## CRITICAL GOTCHA the contract omits: `#include <string.h>`
The contract says to add `memcpy(g_last_response, data, ...)`. But qmk_stubs.c
currently includes ONLY `<stdint.h>` and `<stdio.h>`. Under
`-Wall -Wextra -std=c99`, calling `memcpy` without `<string.h>` is an
**implicit-declaration error** (C99 removed implicit int/decl). The implementer
MUST add `#include <string.h>`. **Verified by prototype**: without it the build
fails; with it, clean. The PRP lists this as edit #1.

## The four edits (exact, prototype-validated)
1. **ADD** `#include <string.h>` to the include block (after `<stdio.h>`).
2. **ADD** `static uint8_t g_last_response[32];` file-static, immediately after
   `static uint8_t g_active_layer = 255;`. (With the Mode-A F6 comment.)
3. **MODIFY** `raw_hid_send`: add
   `memcpy(g_last_response, data, (length < 32) ? length : 32);` as the FIRST
   statement (before the existing `fprintf` debug print). Drop the `(void)length;`
   line (length is now used by the memcpy). Keep the `fprintf` print unchanged.
4. **ADD** `const uint8_t *stub_get_last_response(void) { return g_last_response; }`
   immediately after `stub_get_active_layer`. (With the Mode-A F6 comment.)

## Why `(length < 32) ? length : 32` (the contract's clamp)
`raw_hid_send(uint8_t *data, uint8_t length)` — `length` is a `uint8_t`, max 255.
Both call sites in notifier.c pass exactly `RAW_REPORT_SIZE` (32) (verified:
notifier.c:637 typed response, notifier.c:824 legacy ack). `g_last_response` is
fixed 32 bytes, so the clamp guards a defensive bound: if a future/malformed
caller passed `length > 32`, we copy only 32 (no overrun); if it passed
`length < 32`, we copy only `length` bytes (the tail of g_last_response retains
its previous value — acceptable; tests assert only the bytes they sent). The
clamp is the contract's literal expression; keep it verbatim.

## Response format the accessor exposes (what downstream tests assert)
From `send_typed_response` (notifier.c:628-637, landed P1.M2.T2.S1):
- `response[0] = NOTIFY_RESPONSE_MARKER` = **0x51**
- `response[1] = cmd_id` (the "echo": 0x01 QUERY_INFO, 0x02 QUERY_CALLBACK,
  0x03 SET_OS, 0x05 APPLY_HOST_CONTEXT)
- `response[2..] = payload` (capped at RAW_REPORT_SIZE-2 = 30 bytes)
- remainder zero-padded (`uint8_t response[RAW_REPORT_SIZE] = {0}`)

Per-handler payloads (what stub_get_last_response()[2..] holds after each):
- QUERY_INFO  → `[0x51][0x01][proto=2][feature_flags][callback_count][board_rules_present]`
- QUERY_CALLBACK valid   → `[0x51][0x02][index][name bytes][NUL-pad]`
- QUERY_CALLBACK oob     → `[0x51][0x02][index][0x00]`
- SET_OS      → `[0x51][0x03][ack=1]`
- APPLY_HOST_CONTEXT → `[0x51][0x05][ack=1]`  (P1.M2.T2.S3, parallel/landing)
- default/unknown → `[0x51][cmd_id][zero-pad]`
- legacy string ack (notifier.c:824) → `[0|1][zero-pad]`  (also captured)

## Behavior note: last-write-wins
Each `raw_hid_send` overwrites `g_last_response` entirely (memcpy, no merge).
Downstream tests must read `stub_get_last_response()` IMMEDIATELY after the
`hid_notify(...)` call that triggers the response, before any other
`raw_hid_send` runs. (Verified in prototype: legacy ack after typed response
overwrites [0] from 0x51 to 0x01.) This is fine — each test case is
point-in-time.

## Downstream consumer contract (for P1.M3.T1.S2/S3/S4 test authors)
The accessor is declared in test files via a **manual extern** (precedent:
`uint8_t stub_get_active_layer(void);` at test_notifier_os.c:37 — there is NO
header that declares these stub observables; each test TU re-declares it).
So test_notifier_host.c will contain:
```c
const uint8_t *stub_get_last_response(void);   /* test-harness observable in qmk_stubs.c */
...
hid_notify(rep, 32);
const uint8_t *r = stub_get_last_response();
CK(r[0] == 0x51, "QUERY_INFO response marker 0x51");
CK(r[1] == 0x01, "QUERY_INFO cmd echo");
CK(r[2] == 2,    "proto_ver == 2");
```
There is NO need to add a header declaration anywhere — the manual-extern
pattern is the established convention. (This PRP does NOT write any test; it
only ships the accessor. S2/S3/S4 write the tests.)

## No conflict with the parallel task (P1.M2.T2.S3)
P1.M2.T2.S3 modifies ONLY `notifier.c` (adds the APPLY_HOST_CONTEXT `case`).
It does NOT touch qmk_stubs.c. Its research `/tmp` harness "captured
raw_hid_send" — but that was a throwaway /tmp shim for S3's own validation, NOT
a committed change. This task (S1) is the COMMITTED version of that capture
capability, living in qmk_stubs.c where all host tests can use it. Zero file
overlap → safe to parallelize.

## EMPIRICAL VALIDATION (prototype in /tmp, exact contract change)
Applied all 4 edits to a /tmp copy of qmk_stubs.c:
1. **Standalone compile** `gcc -Wall -Wextra -std=c99 -c qmk_stubs.c` →
   clean, exit 0, 0 warnings.
2. **Driver** (linked the proto stubs.o) exercising: a typed 0x51 response
   (asserted [0]=0x51,[1]=0x01,[2]=2,[3..5]=payload,[6]=0 pad); a legacy ack
   (asserted overwrite [0]=0x01); and a short `length=4` send (asserted clamp,
   no over-read, tail zero) → **ALL ASSERTIONS PASSED**.
3. **Baseline gate** `./run_notifier_stub_tests.sh` (current repo) → 31/31 PASS,
   0 FAIL, both binaries exit 0, `✓ notifier stub-compile gate PASSED`. The new
   symbol is purely additive → cannot break existing linking.

## Risk assessment (from findings F6)
- **Risk: none.** `stub_get_active_layer()` (plan 002) established the
  test-harness-observable precedent; `stub_get_last_response()` follows it
  exactly. Pure additive test infrastructure; no production behavior change
  (qmk_stubs.c is NEVER compiled into firmware — only the host harness links it).

## Mode-A documentation (rides with the work — DOCS §5)
The contract requires the capture be commented:
"Test-harness observable (not production); mirrors stub_get_active_layer
precedent (F6)." This goes on BOTH the `g_last_response` declaration and the
`stub_get_last_response` accessor (matching how `stub_get_active_layer` carries
its own explanatory block comment). No README change in this task — README sync
is P1.M3.T3.S1.