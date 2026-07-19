# Research Notes — P1.M2.T1.S1 (Fix stale README "Overall Test Results" block)

## Task scope
Replace the stale "Overall Test Results" block in `README.md` (lines ~540-545)
that falsely reports the four `SET_OS` blocks as "pending an upstream
ETX-collision framing fix" with accurate text. The fix already ships; all host
cases pass.

## ⚠️ CRITICAL FINDING — the contract's "64" is itself stale (count is now 79)
The item contract says "reflect 64/64 host cases passing." But the **parallel
task P1.M1.T2.S1 (adversarial typed-command tests) is LANDED** — verified:
- `git status` shows `M test_notifier_host.c` (its adv-A..adv-D section is present
  at lines 414+, 15 new CK assertions).
- The live gate reports `test_notifier_host` = **79/79** (was 64 before
  P1.M1.T2.S1; 64 + 15 adversarial = 79). Verified stable (ran twice).
- P1.M1.T2.S1's PRP confirms: "79/79 pass (existing 64 + 15 new assertions)" and
  it does NOT touch README.md.

So when THIS task runs, the count is **79**, not 64. Writing "64" would recreate
the exact defect this task exists to fix (stale numbers). The parallel-execution
context mandates "consume/build upon the outputs of the previous PRP" — so the
README must reflect the post-P1.M1.T2.S1 reality (79).

**Decision (documented in PRP as a justified deviation):** write the LIVE count
(79), not the contract's literal 64. The implementer is instructed to VERIFY the
live count at edit time (run `run_notifier_stub_tests.sh`, read the number, write
that — currently 79) so the README is robust to any further drift.

## Ground-truth locations (current README.md, 569 lines)
Two stale spots — both contain "(64 cases)":
1. **Line 516** (coverage description list item): `- **\`test_notifier_host\`** (64 cases) — the typed-command / host-rules ...`
   The COVERAGE PROSE (what it tests: QUERY_INFO/QUERY_CALLBACK/SET_OS/.../multi-report
   reassembly) is accurate. Only the COUNT token "(64 cases)" is stale.
2. **Lines 540-545** (the "Overall Test Results" block — THE primary fix target):
   ```
   - `test_notifier_host` (64 cases): the four `SET_OS` blocks are currently pending
     an upstream ETX-collision framing fix in `notifier.c` (the `SET_OS` `cmd_id`
     `0x03` collides with the `ETX` terminator during reassembly); the remaining 57
     assertions (...) pass.
   ```
   This is the stale SET_OS-pending text → the core of Issue 2.

## What's accurate (DO NOT touch)
- Pattern-match corpus result (line 538): `2023/2023` — correct.
- Dispatch/OS results (line 539-540): `test_notifier_dispatch 14/14` +
  `test_notifier_os 31/31` — correct.
- Performance line (~546): unchanged.
- The coverage PROSE body (lines 517-523) — accurate (the contract says don't
  touch it; only the count token changes).

## The fix that resolved SET_OS (reference for accurate README prose)
`notifier.c:115` `static uint16_t typed_literal_remaining = 0;` — the length-aware
typed-reassembly path (notifier.c:98-127, "fixes BUG-1/BUG-2/BUG-3"). It tracks
how many literal payload bytes remain so the `SET_OS` `cmd_id` `0x03` is consumed
as a literal arg byte, NOT mistaken for the `ETX` terminator. This is what makes
the four SET_OS test blocks pass. (Plus `typed_awaiting_terminator` at :116-123,
the Issue 1 watchdog — but that's a separate concern; the SET_OS fix is
`typed_literal_remaining`.)

## Cross-task awareness (no conflicts)
- **P1.M1.T2.S1** (LANDED): added adv cases to test_notifier_host.c (64→79); did
  NOT touch README. No conflict.
- **P1.M2.T4.S1** (LATER): "Sync changeset-level documentation — Update README.md
  overview/features... the complete delta: typed reassembly watchdog fix, corrected
  test results, NFA-128 fidelity suite, has_been_queried." So P1.M2.T4.S1 owns the
  OVERVIEW/FEATURES sections + the broad cross-cutting sweep. THIS task (P1.M2.T1.S1)
  owns the "Overall Test Results" block fix. Clean division — my edits are the
  results block + the count token; I leave overview/features to P1.M2.T4.S1.

## Decisions
1. **Results block (540-545)**: replace the stale SET_OS-pending text with accurate
   text: 79/79 cases pass, including the four SET_OS blocks; the SET_OS 0x03/ETX
   collision is resolved by the length-aware typed reassembly
   (`typed_literal_remaining` in notifier.c). List all passing categories
   (including the adversarial framing gate, to explain the 64→79 bump) so the
   reader understands what 79 covers.
2. **Count token at line 516**: "(64 cases)" → "(79 cases)" — a ONE-token change
   for internal consistency (coverage-list count must match results-block count).
   The coverage PROSE (517-523) is left unchanged (respects the contract's "don't
   touch the coverage description"; the adversarial mention goes in the results
   block, not the coverage prose — P1.M2.T4.S1 can expand the prose later).
3. **Implementer verifies live count**: the PRP instructs running
   `run_notifier_stub_tests.sh` and reading the actual host count, then writing
   THAT number (currently 79). Robust to further drift.

## Files touched / NOT touched
- TOUCH: `README.md` (two regions: the count token at 516, the results block 540-545).
- DO NOT TOUCH: notifier.c, notifier.h, pattern_match.*, qmk_stubs/*, test_notifier_*,
  run_*.sh, PRD.md, tasks.json, prd_snapshot.md, rules.mk, .gitignore.
- This IS the documentation fix (item §5: "no separate docs subtask").