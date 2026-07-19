# Scout: Typed-Command Reassembly Bug in `notifier.c`

## TL;DR
The length-aware typed-reassembly state machine has **no bound on how long it will keep
consuming literal bytes once a malformed/short AHC frame under-delivers ids**. Because the
ETX gate is suppressed whenever `typed_literal_remaining > 0`, a frame that *claims*
`count=N` but ships fewer than `N` id bytes swallows the intended terminator (and trailing
zeros) as fake payload bytes, then leaves `typed_mode == true` and `msg_index > 0` pinned
across report boundaries. Subsequent reports concatenate into the contaminated buffer until
a future ETX (or buffer overflow) finally fires — at which point AHC dispatches on a
**corrupted ids[] array** (BUG-2/BUG-3-class silent state mutation). The proposed
`typed_awaiting_terminator` flag correctly bounds both the malformed case and the
legitimate multi-report case.

Severity: **high** — host-driven malformed/truncated AHC frame can permanently desync the
firmware's typed-mode state and apply wrong host callbacks / layers with `ack=1`.

---

## Files Retrieved
1. `notifier.c:96` — `static bool typed_mode` declaration (persists across `hid_notify` calls).
2. `notifier.c:115` — `static uint16_t typed_literal_remaining` declaration (the byte counter at the heart of this bug).
3. `notifier.c:118-132` — `typed_fixed_arg_bytes(cmd_id)` per-command fixed-arg map.
4. `notifier.c:699-784` — `handle_typed_command()`, incl. AHC handler (`NOTIFY_CMD_APPLY_HOST_CONTEXT`, ~line 769) that reads `count = data[4]` and `ids = &data[5]`.
5. `notifier.c:833-840` — typed-mode entry: sets `typed_mode=true; typed_literal_remaining=2`.
6. `notifier.c:858-893` — the byte loop: ETX gate (862), honored-ETX reset block (889-890).
7. `notifier.c:898-936` — append branch: literal decrement + extension (919-926), overflow/drop reset (934-935).
8. `notifier.c:42` — `RAW_REPORT_SIZE = 32` (so 30 payload bytes/report after the `0x81 0x9F` strip).
9. `notifier.c:79` — `MSG_BUFFER_SIZE = 256`.

## Key Code

### ETX gate (line 862) — the suppression
```c
bool typed_literal = (typed_mode && typed_literal_remaining > 0);
if (c == ETX_TERMINATOR[0] && !typed_literal) {   /* line 862 */
    ... dispatch / reset ...
}
```
While `typed_literal_remaining > 0`, **every** byte — including a legitimate `0x03` — is
forced into the `else` append branch. This is by design for in-payload `0x03`s (BUG-1 fix),
but it has no escape valve for a frame that *under-counts* its own ids.

### Count extension (lines 919-926)
```c
if (typed_literal) {
    typed_literal_remaining--;
    if (msg_index == 2) {                       /* cmd_id just written  */
        uint16_t fixed = typed_fixed_arg_bytes((uint8_t)msg_buffer[1]);
        if (fixed != 0xFFFF) typed_literal_remaining += fixed;
    } else if (msg_index == 5 &&
               (uint8_t)msg_buffer[1] == NOTIFY_CMD_APPLY_HOST_CONTEXT) {
        uint8_t ahc_count = (uint8_t)msg_buffer[4];
        uint16_t room = (uint16_t)((MSG_BUFFER_SIZE - 1) - msg_index);
        typed_literal_remaining += (ahc_count > room) ? room : ahc_count;  /* line 926 */
    }
}
```
For AHC the counter is seeded by the **host-supplied** `count` byte. If the host then
sends fewer id bytes than `count`, the counter never reaches 0 within the real payload and
keeps eating subsequent bytes (the intended ETX, then filler).

### Where `typed_mode` is cleared (only two sites)
- `notifier.c:889` — inside the honored-ETX branch (requires `!typed_literal`, i.e. counter==0).
- `notifier.c:934` — inside the buffer-overflow/drop branch.

**There is no end-of-report cleanup.** If neither fires during a `hid_notify` call,
`typed_mode`, `typed_literal_remaining`, and `msg_index` all persist (they are `static`)
into the next report.

---

## Architecture / Data Flow

Report framing: each 32-byte raw HID report is `[0x81][0x9F][30 payload bytes]`.
`hid_notify` strips the 2-byte header, then loops the remaining 30 bytes. Buffer layout for
a typed message:

| msg_buffer idx | meaning |
|---|---|
| 0 | discriminator 0xF0 |
| 1 | cmd_id |
| 2.. | args (AHC: `[layer][flags][count][id...][id]`) |

`typed_literal_remaining` is the "how many more bytes must I swallow verbatim before I may
honor ETX" counter. It is correct for well-formed frames and for legitimate multi-report
AHC, but it is **trust-based on the host's `count`**: a too-large `count` with too few
following ids makes the counter over-consume.

---

## Byte-by-byte trace: malformed AHC (count=5, only 1 real id)

Single 32-byte report payload (after `0x81 0x9F` strip, `length=30`, loop `i=0..29`):

| i | byte | role | `typed_literal_remaining` before→after | msg_index | note |
|---|------|------|------|------|------|
| 0 | 0xF0 | disc | 2→1 | 1 | literal; no ext |
| 1 | 0x05 | cmd AHC | 1→0→**3** | 2 | `msg_index==2` → +3 fixed args |
| 2 | 0x01 | layer | 3→2 | 3 | |
| 3 | 0x00 | flags | 2→1 | 4 | |
| 4 | 0x05 | count | 1→0→**5** | 5 | `msg_index==5` && AHC → +5 (room=250≥5) |
| 5 | 0x41 | id[0] (only real id) | 5→4 | 6 | |
| 6 | **0x03** | **intended ETX** | 4→3 | 7 | **ETX GATE SUPPRESSED** (tlr=4>0) → swallowed as fake id[1] |
| 7 | 0x00 | filler | 3→2 | 8 | fake id[2] |
| 8 | 0x00 | filler | 2→1 | 9 | fake id[3] |
| 9 | 0x00 | filler | 1→0 | 10 | fake id[4] — counter finally hits 0 |
| 10 | 0x00 | filler | 0 (stays) | 11 | tlr==0 ⇒ `typed_literal=false`; not ETX ⇒ appended as stray legacy byte |
| 11..29 | 0x00 ×19 | filler | 0 | 12→30 | 19 more stray zeros appended (typed_mode still true) |

**State at end of report:** `typed_mode = true`, `typed_literal_remaining = 0`,
`msg_index = 30`, buffer =
`[F0 05 01 00 05 41 03 00 00 00 00 00…00]`.

### Why `typed_mode` stays true
- The intended ETX (byte at i=6) was consumed as a literal because `tlr` was 4 — the
  honored-ETX reset at line 889 **never executed**.
- `msg_index` peaked at 30, far below `MSG_BUFFER_SIZE-1` (255) — the overflow reset at
  line 934 **never executed**.
- No end-of-report reset exists.

### Consequence
`typed_mode` is now **pinned true across report boundaries**. The next report's bytes
append starting at `msg_index=30`. Whichever report next carries a `0x03` triggers AHC
dispatch with `count=5` and `ids = [0x41, 0x03, 0x00, 0x00, 0x00]` — `apply_host_callbacks`
applies host contexts for ids 3/0/0/0 (wrong), `set_host_layer(1)`, and if `flags&1` also
deactivates the board layer + disables the board command. The firmware replies `ack=1`
as if it succeeded. If no future ETX arrives, the buffer slowly fills to 255 then drops —
but only after possibly dozens of contaminated reports. This is a persistent
stuck-in-typed-mode / silent-corruption bug.

---

## Byte-by-byte trace: legitimate multi-report AHC (count=28, 2 reports)

Total payload = 2(disc+cmd)+3(header)+28(ids)+1(ETX) = 34 bytes ⇒ 2 reports (30 + 4).
`room` at extension = 250 ≥ 28 ⇒ no clamp.

**Report 1** (30 bytes: `F0 05 layer flags 1C id[0..24]`):
- i=0: 0xF0, tlr 2→1
- i=1: 0x05, tlr 1→0→3
- i=2..3: layer,flags, tlr 3→2→1
- i=4: count=0x1C(28), tlr 1→0→28
- i=5..29: id[0..24] (25 ids), tlr 28→**3**

End report 1: `msg_index=30`, `tlr=3`, `typed_mode=true`. (Correctly mid-reassembly.)

**Report 2** (`id[25] id[26] id[27] 03 00…`):
- i=0: id[25], tlr 3→2
- i=1: id[26], tlr 2→1
- i=2: id[27], tlr 1→**0**
- i=3: **0x03 ETX**. `typed_literal=(true && 0>0)=false` ⇒ ETX gate fires ⇒ dispatch AHC
  with `len=33`, `ids=&data[5]` (all 28 real ids). Reset `typed_mode=false`, `tlr=0`. ✓

The legitimate case works **precisely because** the counter reaches 0 on the last real id
byte and the immediately following byte is the real ETX.

---

## Fix assessment: `typed_awaiting_terminator` flag

**Mechanism:** a `static bool` set when `typed_literal_remaining` transitions to 0 *and
remains 0 after the extension logic* in the same iteration. On the next byte:
- if it is ETX → existing gate already honors it (tlr==0 ⇒ `typed_literal=false`); clear flag.
- if it is **not** ETX → **reset** (clear buffer, `msg_index=0`, `typed_mode=false`,
  `tlr=0`, `awaiting=false`) — the malformed frame is discarded.

### Correctness on the malformed case (count=5, 1 id)
- At i=9 (the 5th consumed id — a fake `0x00`), `tlr 1→0`, no extension ⇒ set
  `awaiting_terminator=true`.
- i=10: byte=`0x00`, **not ETX** ⇒ fix triggers reset. Buffer cleared, `typed_mode=false`.
  No stuck state, no corrupted dispatch. ✓ (The already-swallowed real ETX + 3 filler zeros
  are discarded along with the frame, which is correct — that frame *was* malformed.)

### Correctness on the legitimate case (count=28)
- Report 2 i=2: id[27] consumed, `tlr 1→0`, set `awaiting_terminator=true`.
- Report 2 i=3: byte=`0x03` ⇒ ETX honored, dispatch, clear flag. ✓ No false reset.

### Cross-report-boundary correctness
The flag is `static`, so it survives the report boundary exactly like `typed_mode`/`tlr`:
- Legitimate (last id at end of report 1, ETX first byte of report 2): flag set at end of
  report 1, report 2 byte 0 = ETX ⇒ honored. ✓
- Malformed (last id at end of report 1, report 2 starts non-ETX): flag set, report 2 byte
  0 ≠ ETX ⇒ reset. ✓

### Implementation constraints (must-haves for the fix to be correct)
1. **Set the flag AFTER the extension block** (after lines 920-926), not before. The
   counter transiently hits 0 at `msg_index==2` (cmd_id) and `msg_index==5` (AHC count)
   but is immediately re-extended in the same iteration; the flag must only latch when
   `tlr==0` *post-extension*.
2. **Declare it `static`** (same lifetime as `typed_mode` at line 96 and `tlr` at 115).
3. **Reset at every existing boundary**: honored-ETX (line 889), overflow/drop (line 934),
   and typed-mode entry (line 836) — mirroring `typed_mode`/`tlr`.

### Residual considerations (non-blocking)
- After the malformed-reset at i=10, the remaining bytes in the *current* report
  (i=11..29) append as legacy bytes (typed_mode now false) until the next ETX. This is
  benign legacy noise cleared at the next message boundary. Optional hardening: also set
  `dropping=true` on the malformed-reset to discard the rest of the current report.
- The flag does **not** prevent the initial over-consumption of the intended ETX (that
  happens at i=6 before the counter reaches 0). It bounds the *damage* to one frame rather
  than letting it pin `typed_mode` indefinitely. Fully preventing byte swallowing would
  require a different mechanism (e.g. validating id count against reassembled length per
  report), which is a larger change and out of scope for this fix.

---

## Start Here
Open `notifier.c` at **line 858** (the `for` loop in `hid_notify`) and read through line
936. The fix site is the append branch: add the `typed_awaiting_terminator` declaration
near line 96/115, set it after the extension block (~line 927), and check it at the top of
the loop (near line 860, before the ETX gate) to force a reset on a non-ETX byte while
awaiting termination. Reset it at lines 836, 889, and 934.