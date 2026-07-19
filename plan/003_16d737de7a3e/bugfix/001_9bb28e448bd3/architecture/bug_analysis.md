# Bug Analysis — Typed Reassembly Desync (Issue 1, Major)

## Root Cause

The length-aware typed reassembly (introduced to fix ETX/0x03 collision in SET_OS
and AHC payloads) trusts `APPLY_HOST_CONTEXT.count` unconditionally. When the
declared count exceeds the actual id bytes present, `typed_literal_remaining`
over-consumes: it swallows the intended ETX terminator and subsequent message
bytes as fake literal ids.

### Exact Mechanism

In `notifier.c` `hid_notify()` byte loop (lines ~858-936):

1. **AHC count extension (line ~924-926):**
   ```c
   uint8_t ahc_count = (uint8_t)msg_buffer[4];
   uint16_t room = (uint16_t)((MSG_BUFFER_SIZE - 1) - msg_index);
   typed_literal_remaining += (ahc_count > room) ? room : ahc_count;
   ```
   `ahc_count` is the host-supplied count byte. It is clamped only to buffer
   *room* (up to ~250), NOT to the bytes actually present in the stream.

2. **ETX suppression (line ~858-862):**
   ```c
   bool typed_literal = (typed_mode && typed_literal_remaining > 0);
   if (c == ETX_TERMINATOR[0] && !typed_literal) { ... dispatch ... }
   ```
   While `typed_literal_remaining > 0`, every 0x03 byte is consumed as a literal
   payload byte, NOT honored as ETX.

3. **No recovery:** `typed_mode` is only cleared at:
   - ETX boundary (line ~889) — requires `typed_literal_remaining == 0`
   - Buffer overflow (line ~934)
   
   If neither fires (count is small enough to fit in the buffer but larger than
   actual ids), `typed_mode` stays `true` **permanently** across reports.

### Byte-by-Byte Trace: Malformed AHC (count=5, only 1 id)

Report: `[0x81][0x9F][0xF0][0x05][E0][00][05][41][03][00]...[00]`

After `data += 2` strip, 30 bytes processed:

| i | byte | role | tlr before→after | msg_idx | note |
|---|------|------|-------------------|---------|------|
| 0 | 0xF0 | disc | 2→1 | 1 | literal consume |
| 1 | 0x05 | cmd | 1→0→**3** | 2 | msg_idx==2 → +3 fixed args |
| 2 | 0xE0 | layer | 3→2 | 3 | |
| 3 | 0x00 | flags | 2→1 | 4 | |
| 4 | 0x05 | count | 1→0→**5** | 5 | msg_idx==5 → +5 ids |
| 5 | 0x41 | id[0] | 5→4 | 6 | only real id |
| 6 | **0x03** | **ETX** | 4→3 | 7 | **SWALLOWED** — tlr=4>0 |
| 7 | 0x00 | filler | 3→2 | 8 | fake id[2] |
| 8 | 0x00 | filler | 2→1 | 9 | fake id[3] |
| 9 | 0x00 | filler | 1→0 | 10 | fake id[4] — counter hits 0 |
| 10-29 | 0x00 | filler | 0 | 11→30 | appended as stray bytes (typed_mode still true) |

**End of report:** `typed_mode=true`, `tlr=0`, `msg_index=30`.

Next legacy report (`"neovide\x03"`): bytes appended at msg_index=30. When 0x03
arrives, `typed_literal=false` (tlr=0), so ETX IS honored — but `typed_mode=true`
routes it to `handle_typed_command()` instead of `process_full_message()`.
Result: `[0x51]` ack, no board side effects. Legacy routing permanently broken.

### Legitimate Multi-Report AHC (count=28, 2 reports) — Works Correctly

Report 1: 25 ids consumed, `tlr=28→3` at end.
Report 2: 3 ids consumed (`tlr 3→0`), then 0x03 ETX → dispatch. ✓

The legitimate case works because the counter reaches 0 on the last real id
and the immediately following byte IS the real ETX.

## Fix Approach: `typed_awaiting_terminator` Watchdog

### Mechanism
Add a `static bool typed_awaiting_terminator` flag:
- **Set** when `typed_literal_remaining` transitions to 0 *after* the extension
  block (post line ~926), meaning all declared literal bytes have been consumed.
- **Checked** at the top of the byte loop (before the ETX gate): if the flag is
  true and the current byte is NOT 0x03, the typed message is malformed — reset
  `typed_mode`, `typed_literal_remaining`, `msg_index`, and the flag itself.
- **Cleared** at all existing reset points: ETX boundary (~889), overflow (~934),
  typed-mode entry (~836).

### Critical Implementation Constraint
The flag must be set **AFTER** the extension block, not before. The counter
transiently hits 0 at `msg_index==2` (cmd_id consumed, before fixed args added)
and `msg_index==5` (count consumed, before ids added). Setting the flag before
the extension would cause false resets on well-formed commands.

### Correctness Verification

**Malformed case (count=5, 1 id):**
- i=9: tlr 1→0, no extension → set `typed_awaiting_terminator=true`
- i=10: byte=0x00 ≠ ETX → **reset**. Buffer cleared, typed_mode=false. ✓

**Legitimate multi-report (count=28):**
- Report 2 i=2: id[27] consumed, tlr 1→0 → set flag
- Report 2 i=3: byte=0x03 = ETX → normal dispatch, flag cleared. ✓

**Cross-report boundary (last id at end of report N, ETX at start of N+1):**
- Flag set at end of report N (static, persists)
- Report N+1 byte 0: if ETX → dispatch ✓; if not → reset ✓

### Residual (Acceptable)
The fix does NOT prevent the initial over-consumption of the intended ETX (at
i=6 in the trace). It bounds the damage to one frame rather than letting it pin
`typed_mode` indefinitely. This satisfies the PRD's "at minimum" requirement:
the malformed message is dropped silently and the next well-formed legacy string
resumes normal routing.

### Reset Points (must mirror typed_mode/typed_literal_remaining)
1. `notifier.c:~836` — typed-mode entry: `typed_awaiting_terminator = false;`
2. `notifier.c:~889-890` — ETX boundary: `typed_awaiting_terminator = false;`
3. `notifier.c:~934-935` — overflow/drop: `typed_awaiting_terminator = false;`
4. New reset site (non-ETX while awaiting): clears all typed state + msg_index