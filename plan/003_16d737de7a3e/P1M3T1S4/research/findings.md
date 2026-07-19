# Research Findings — P1.M3.T1.S4

**Scope:** Add coexistence/backward-compat + multi-report typed framing test
cases to `test_notifier_host.c`. These APPEND three test blocks to `main()`
(after S2's QUERY blocks + S3's SET_OS/AHC blocks, before the summary):

1. **coexist-i** — legacy string is NOT routed to the typed path (response[0] is
   the match-bool 0/1, NOT 0x51); + full side-effect proof via a matching string.
2. **coexist-ii** — a non-magic report (data[0]!=0x81) is silently discarded
   (response buffer unchanged).
3. **multi-report** — a typed command split across two 32-byte reports
   reassembles + dispatches correctly.

All three are **empirically verified PASSING** against the real `notifier.c` via
the probe `/tmp/probe_s4.c` (see "Probe evidence" below).

---

## F4-1 — Coexistence: legacy string with printable data[2] never hits the typed branch

**Code path (notifier.c `hid_notify`):**
```c
if (msg_index == 0 && length >= 3 && data[2] == NOTIFY_CMD_DISCRIMINATOR) {
    typed_mode = true;     // 0xF0 -> typed
}
...
for each byte: if (c == ETX) { if (typed_mode) handle_typed_command else sanitize+process_full_message }
```

A legacy string has `data[2]` printable (0x20–0x7E). The sanitizer allows only
0x20–0x7E (+ 9/10/13/GS/ETX), so `0xF0` **can never begin a real matched string**
(PRD §4.6). Therefore `typed_mode` stays `false`, the byte loop hits ETX, and the
**legacy** path runs: `sanitize_string` → `process_full_message` → legacy ack
`response[0] = match` (0 or 1), distinct from the typed marker `0x51`.

**Probe evidence:**
```
(i)(a) 'firefox': r[0]=0  -> !=0x51(1) ==0(1)        # no-match legacy ack
(i)(b) 'neovide': r[0]=1 board_cmd_en=1 active=5      # match legacy ack + side effects
        -> !=0x51(1) ==1(1) cmd_en==1(1) layer==5(1)
```

**Why both a no-match AND a match string:** (a) proves the discriminator check is
transparent to a printable data[2] AND that process_full_message ran (no-match → 0);
(b) proves the FULL legacy dispatch path (disable→scan→deactivate→activate→enable)
runs intact ALONGSIDE the typed path — `board_cmd_en==1`, `active==5`. This is
"test_notifier_dispatch-style behavior is intact alongside the typed path" (item
spec). findings_and_risks F5 ("existing tests never send 0xF0") is the invariant
these tests encode.

## F4-2 — Non-magic report is silently discarded (no response side effects)

**Code path:** `hid_notify`'s FIRST statement:
```c
if (length < 2 || data[0] != 0x81 || data[1] != 0x9F) {
    return;   // Discard — BEFORE touching msg_buffer/typed_mode, BEFORE raw_hid_send
}
```
A report with `data[0] != 0x81` returns immediately. `raw_hid_send` is **never
called**, so the stub's `g_last_response` is **unchanged** from the prior send.

**Assertion strategy:** send a typed QUERY_INFO first (sets `g_last_response =
[0x51, 0x01, …]`), capture `r0[0]/r0[1]`; then send the non-magic report; then
assert `stub_get_last_response()[0..1]` is **identical** to the captured bytes —
proving no response was emitted for the discard.

**Probe evidence:**
```
setup QUERY_INFO: r[0]=81 r[1]=1
non-magic report:  r[0]=81 r[1]=1 -> unchanged(1)
```
State cleanliness: the early `return` happens before `data += 2` and before the
byte loop, so `msg_buffer`/`msg_index`/`typed_mode` are untouched. No leakage.

## F4-3 — Multi-report typed framing WORKS (with the 0x03 avoidance caveat)

**Reassembly model (notifier.c `hid_notify` + findings F7):**
- The magic header `[0x81][0x9F]` is checked + stripped on **every** report
  (`data += 2; length -= 2;`). So **BOTH** reports must carry the magic header.
- `typed_mode` is set ONLY when `msg_index == 0 && data[2] == 0xF0` — i.e. the
  FIRST report's `data[2]`. Continuation reports never re-check the discriminator
  (their `data[2]` is payload, may coincidentally be 0xF0 — F7).
- Bytes are appended to `msg_buffer` until ETX (0x03). ETX dispatches.
- `typed_mode` persists across hid_notify calls (set on report 1, read on ETX in
  report 2). It is reset at every ETX/overflow boundary (RISK-1).

**The ETX-collision constraint (interaction with S3's blocker):** the byte loop
treats **ANY** `0x03` byte as ETX — including the SET_OS cmd_id (`0x03`) and any
binary arg byte == 3 (S3's research/findings.md blocker). Therefore the
multi-report test MUST choose a command whose **cmd_id ≠ 0x03** AND whose **args
contain no 0x03 byte**.

**Choice: APPLY_HOST_CONTEXT (0x05).** cmd_id `0x05` ≠ ETX. Args layout for AHC:
`data[2]=layer, data[3]=flags, data[4]=count, data[5..]=ids`. Choosing
`layer=224(0xE0)`, `flags=0`, `count=28(0x1C)`, all `ids=0` ⇒ **no byte is 0x03**.
This is exactly why the item spec offers "a large APPLY_HOST_CONTEXT id list" as
the multi-report vehicle (and why QUERY_CALLBACK — 1-byte request — can't span).

**Split math (count=28 ⇒ spans two reports):**
- Report 1 (32 B, NO ETX): `[0x81][0x9F][0xF0][0x05][224][0][28][id0..id24]`
  = 2 magic + 0xF0 + 0x05 + layer + flags + count + **25 ids** = 32. ✓
- Report 2 (with ETX): `[0x81][0x9F][id25][id26][id27][0x03][0…]`
  = 2 magic + **3 ids** + ETX. 25 + 3 = 28 = count. ✓

**Reassembly trace:** after report 1, `msg_buffer=[0xF0,0x05,224,0,28,0,…,0]`
(30 bytes), `msg_index=30`, `typed_mode=true` (no ETX ⇒ no reset). After report 2,
the 3 continuation ids append (`msg_index=33`), then `0x03` ⇒ dispatch.
`handle_typed_command`: `data[1]=0x05` ⇒ AHC; `layer=224`, `flags=0`, `count=28`,
`ids=&data[5]` (28 zeros). `set_host_layer(224)` ⇒ `active=224`.
`apply_host_callbacks(28 zeros, 28)`: Phase1 disable (none enabled), Phase2 enable
id 0 once (`cb_mute_en=1`; dups skipped via `host_cb_enabled[id]` guard). Response
`[0x51][0x05][0x01]`.

**Probe evidence:**
```
multi-report AHC: r[0]=81 r[1]=5 r[2]=1 active=224 cb_mute_en=1
  -> marker==0x51(1) cmd_echo==0x05(1) ack==1(1) layer==224(1) cb_mute_en==1(1)
```
**The `r[1]==0x05` cmd-echo is the load-bearing assertion:** it proves report 1's
discriminator+cmd_id were correctly persisted and reassembled with report 2's
payload. If reassembly failed (e.g. `msg_index` reset between reports, or report 2
not appended), `r[1]` would be a garbage/stale byte, not `0x05`.

**Overflow check:** reassembled length = 33 B (`0xF0+0x05+layer+flags+count+28
ids`), well under `MSG_BUFFER_SIZE=256` ⇒ no `dropping` mode. Clean.

## F4-4 — Negative probe: confirms the SET_OS cmd_id (0x03) == ETX collision

The probe's final block sends `[0x81][0x9F][0xF0][0x03][0x00][0x03]`. The byte
loop appends `0xF0`, then sees `0x03` (the **cmd_id**) and dispatches. Result:
`r[1]` echoes a STALE `msg_buffer[1]` value (the cmd byte was never appended),
NOT `0x03`. This independently re-confirms S3's blocker and justifies using AHC
(0x05) for the multi-report test. SET_OS multi/single-report is OUT OF SCOPE for
S4 (it is S3's documented blocker; P1.M2 owns the framing fix).

## F4-5 — Robustness to S3's state mutations (no current_os reset needed)

S4 blocks run AFTER S3's blocks (S3 mutates `current_os`, `host_layer`,
`host_cb_enabled`). Analysis shows S4's assertions are robust regardless:
- **coexist-i "neovide":** matches the DEFAULT map via fallback even if
  `current_os==OS_MACOS` (the OS_MACOS "iTerm" map doesn't match "neovide" ⇒
  default "neovide" wins). `board_cmd_en==1`, `layer==5` hold for any `current_os`.
  `process_full_message("neovide")` deactivates then activates layer 5 ⇒
  `active==5` regardless of prior host/board layer state.
- **coexist-ii:** the magic-header check is state-independent ⇒ a non-magic report
  is always discarded.
- **multi-report:** AHC sets `host_layer`/callbacks; assertions (`r[1]==0x05`,
  `active==224`) don't depend on `current_os`.

Each block **resets the flags it asserts** (`board_cmd_en/dis`, `cb_mute_en`) at
its start, so blocks are independent. **No `current_os` reset is required**, but
implementers MAY add `notifier_set_os(OS_UNSURE)` at the top of the S4 section for
extra determinism (declared in `notifier.h`; optional, not required).

## F4-6 — Parallel-merge with S3 (file edit strategy)

Both S3 and S4 EDIT `test_notifier_host.c` and APPEND blocks before the summary
`printf`. The orchestrator serializes them. The **stable anchor** for S4's
insertion is the summary line:
```c
    printf("\nTotal tests run: %d / passed: %d / failed: %d\n", g_pass + g_fail, g_pass, g_fail);
```
S4 inserts its 3 blocks immediately BEFORE this line (S3 inserts its 8 blocks
there too; both compose because neither alters the line). S4 needs **NO new
file-scope declarations** — it reuses S2's `board_cmd_*`/`cb_*` flags, S3's
sequence stamps (unused by S4), and the `send_typed` helper. The multi-report
block inlines its two reports (no new helper) to keep the diff purely in `main()`
(minimal merge surface).

The S2 "side-effect-free" `CK(cb_*_en==0…)` runs BEFORE S3/S4 blocks (it sits
after S2's blocks, before the summary), so S3/S4's callback-firing blocks don't
break it.

## F4-7 — Verified assertion values (the contract, traced from notifier.c)

| Block | Assertion | Verified value |
|-------|-----------|----------------|
| coexist-i(a) | `r[0] != 0x51 && r[0] == 0` | 0 (no-match legacy ack) |
| coexist-i(b) | `r[0] != 0x51 && r[0] == 1 && board_cmd_en==1 && active==5` | all true |
| coexist-ii | response `[0],[1]` unchanged after non-magic report | true |
| multi-report | `r[0]==0x51 && r[1]==0x05 && r[2]==1 && active==224 && cb_mute_en==1` | all true |

All four-block-category coverage is achieved: the full suite (after S3+S4 land)
covers QUERY_INFO, QUERY_CALLBACK, SET_OS, APPLY_HOST_CONTEXT stack/replace,
coexistence/backward-compat, and multi-report framing (the item's OUTPUT criterion).