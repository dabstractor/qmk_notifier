# Research — Test-status honesty for the README "Running Tests" update

## The runner's CURRENT state (verified by reading run_notifier_stub_tests.sh)

`run_notifier_stub_tests.sh` is ALREADY extended (the P1.M3.T2.S1 deliverable is in
place on disk): it compiles `notifier.c` → ONE shared `notifier_stub.o`, then LINKS
and RUNS **three** drivers — `test_notifier_dispatch`, `test_notifier_os`, and
`test_notifier_host` — prints `notifier <name> fails=N (exit=M)` for each, and ends
`✓ notifier stub-compile gate PASSED` IFF all three have 0 `^FAIL:` and exit 0.
Header comment already names the third binary. So the README "Running Tests" update
just needs to DESCRIBE this third binary's coverage.

## test_notifier_host.c coverage (verified by reading the test file, 64 assertions)

Blocks (all in `test_notifier_host.c`):
- **(i)** QUERY_INFO response layout — §4.6 capability handshake.
- **(ii)** `has_been_queried` — board state SURVIVES QUERY_INFO — §4.6 handshake timing.
- **(iii)** QUERY_CALLBACK valid index — §4.6 name discovery.
- **(iv)** QUERY_CALLBACK out-of-range — §4.6 name absent.
- (side-effect-free) QUERY_* fired NO host callback (read-only).
- **(i–iv) SET_OS** blocks — response layout, OS-map selection after SET_OS, F9
  clear-on-change, idempotence.
- **(v–viii) APPLY_HOST_CONTEXT** — STACK (clear_board=0), REPLACE (clear_board=1),
  callback-diff ordering (disable-before-enable), layer=0xFF clears host layer.
- **(coexist-i)** legacy string coexists with typed path.
- **(coexist-ii)** non-magic report silently discarded.
- **(multi-rep)** two-report APPLY_HOST_CONTEXT reassembly.

## ⚠️ KNOWN UPSTREAM STATE — SET_OS framing flaw (NOT this task's bug)

The P1.M3.T2.S1 PRP documents a **VERIFIED blocker** owned by `notifier.c` (P1.M2):
`test_notifier_host` currently emits **7 FAILs, ALL in the four SET_OS blocks
(i–iv)**. Root cause: `hid_notify`'s reassembly loop does
`if (c == ETX_TERMINATOR[0]) { dispatch; break; }` for EVERY byte, and SET_OS's
cmd_id is `0x03` (== ETX `0x03`); `OS_MACOS==3` so the os_byte arg ALSO collides.
The SET_OS handler (`case NOTIFY_CMD_SET_OS:` at notifier.c:693) is unreachable via
`hid_notify`. This is a wire-protocol framing flaw (ETX-termination is safe for
text `0x20–0x7E` but incompatible with binary typed payloads) — **owned by the
firmware (P1.M2), NOT this documentation task**, and NOT in scope to fix.

The OTHER 57 assertions (QUERY_INFO/QUERY_CALLBACK/AHC v–viii/coexistence/
multi-report) PASS. `test_notifier_dispatch` (14) and `test_notifier_os` (31) are
GREEN and byte-for-byte unchanged.

## Implication for the README "Current Test Status" subsection

The README's existing `### Current Test Status` currently claims "all suites green
... 2023/2023 ... 14/14 + 31/31". This task adds `test_notifier_host` to that block.
**The implementer MUST report the ACTUAL numbers observed by running the gates —
NEVER fabricate a green status.**

### Decision rule for the implementer (run both gates, then choose wording):

1. `cd <repo> && ./run_all_tests.sh` → read the 9-suite aggregate.
2. `./run_notifier_stub_tests.sh` → read the three `fails=N (exit=M)` lines.
3. IF `test_notifier_host` prints `fails=0 (exit=0)` (i.e. the SET_OS framing flaw
   was fixed upstream by the time this runs):
   - State `test_notifier_host` 64/64 (or the observed count) green, alongside
     dispatch 14/14 + os 31/31, and the 2023/2023 pattern corpus.
4. ELSE (test_notifier_host still shows the 7 SET_OS FAILs — the verified current
   state): document HONESTLY. Two honest options (pick the second for safety):
   - Option A: list `test_notifier_host` coverage (QUERY_INFO/QUERY_CALLBACK/
     AHC stack+replace+diff/coexistence/multi-report = the 57 passing) and note
     the four SET_OS blocks are gated on the notifier.c ETX-collision framing fix
     (tracked upstream), WITHOUT claiming green.
   - Option B (preferred, most robust): describe what each of the THREE binaries
     COVERS (factual, stable), and for the status line state ONLY the suites that
     are observably green (`./run_all_tests.sh` + dispatch + os), adding a single
     honest sentence that `test_notifier_host`'s SET_OS blocks are pending the
     upstream framing fix. Do NOT print a fabricated "64/64 green".

**FORBIDDEN** (would make the README lie / mask the flaw):
- ❌ Claiming `test_notifier_host` is green when `run_notifier_stub_tests.sh` ends
  `✗ ... FAILED` + exit 1.
- ❌ "Rounding up" or omitting the SET_OS caveat.
- ❌ Editing `notifier.c` / `notifier.h` / the test / the runner to make numbers
  pass — that is out of scope for THIS docs task (owned by P1.M2 / P1.M3.T2.S1).

The honest-status path keeps the README truthful AND ships the full feature
documentation (the primary deliverable, which is 100% unaffected by the framing flaw).