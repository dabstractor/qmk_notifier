# Findings & Risks

## Research Summary

### Issue 1 — Typed Reassembly Desync (Major)
**Status**: Confirmed and fully analyzed. The bug is real and reproducible.

**Root cause**: `typed_literal_remaining` is extended by the host-supplied
`APPLY_HOST_CONTEXT.count` (clamped only to buffer room ~250, not to stream
bytes). If count > actual ids, the counter over-consumes the intended ETX and
subsequent bytes, leaving `typed_mode=true` permanently.

**Fix approach validated**: `typed_awaiting_terminator` watchdog flag (see
`bug_analysis.md`). Correctly handles both malformed and legitimate multi-report
cases. Bounded residual: the initial ETX over-consumption within the malformed
frame cannot be prevented, but the damage is limited to one frame instead of
permanent desync.

**Risk**: LOW. The fix adds one static bool and ~10 lines of logic in the
existing byte loop. No wire protocol change. No impact on well-formed commands.

### Issue 2 — Stale README (Minor)
**Status**: Confirmed. README lines ~540-545 claim SET_OS tests are "pending an
upstream ETX-collision framing fix" but `run_notifier_stub_tests.sh` reports
64/64 passing. The fix (length-aware typed reassembly) already ships.

**Fix**: Update the README block to state all 64 cases pass. Straightforward
text edit.

**Risk**: NONE. Documentation-only.

### Issue 3 — NFA_MAX_PATTERN Test Fidelity Gap (Minor)
**Status**: Confirmed. The 9 pattern suites in `run_all_tests.sh` compile
`pattern_match.c` directly at NFA_MAX_PATTERN=2048. Only the notifier stub
suites (Harness B) run at the firmware budget of 128.

**Fix**: Add a 10th suite `test_fidelity_nfa128.c` compiled with
`-DNFA_MAX_PATTERN=128` to `run_all_tests.sh`. Must test boundary patterns
(exactly 128 bytes, 129 bytes) to verify clamping behavior.

**Risk**: LOW. New test file + one compile line + one run_test call. No
existing tests affected (must NOT add -D to existing suites — test_memory_stress
needs 2048).

### Issue 4 — has_been_queried Dead State (Minor)
**Status**: Confirmed. `has_been_queried` is set to `true` at notifier.c:706
(QUERY_INFO handler) but never read by any code path. The PRD §4.6 only requires
the firmware to *set* it; the host enforces "at most once per board boot."

**Fix**: Document as intentionally write-only (reserved for future host
observability). Exposing via QUERY_INFO would change the 4-byte response payload
to 5 bytes, risking host compatibility — NOT recommended for a Minor issue.

**Risk**: NONE. Comment/documentation-only change.

---

## Architectural Constraints

### notifier.c Byte Loop (lines ~858-936)
The byte loop in `hid_notify()` is the single most critical code path. All
modifications must preserve:
1. **Multi-report reassembly**: `typed_mode`, `typed_literal_remaining`,
   `msg_index` are `static` and persist across `hid_notify()` calls
2. **ETX framing**: 0x03 terminates messages on the legacy path AND on the typed
   path when `typed_literal_remaining == 0`
3. **Overflow handling**: messages exceeding MSG_BUFFER_SIZE (256) are dropped
   entirely (PRD F2.2)
4. **Board/host state separation**: typed commands bypass
   `process_full_message` (no board disable/deactivate except via clear_board)

### Test Result Contracts (must match harness)
- **Harness A** (`run_all_tests.sh`): `Total tests run:` / `Tests passed:` /
  `Tests failed:`
- **Harness B** (`run_notifier_stub_tests.sh`): `CK` macro emits `PASS:`/`FAIL:`,
  runner greps `^FAIL:`

### Documentation Sync
- **Mode A (doc-with-work)**: Each subtask that changes user-facing behavior,
  config, or test status updates the docs it directly touches
- **Mode B (changeset-level)**: A final task syncs README.md overview/features
  to reflect the entire changeset

---

## Dependency Graph

```
P1.M1.T1.S1 (watchdog impl) ──────┐
                                   ├──► P1.M1.T2.S1 (adversarial tests)
                                   │
P1.M2.T1.S1 (README fix)     ──┐  │
P1.M2.T2.S1 (NFA-128 test)   ──┼──┼──► P1.M2.T4.S1 (changeset docs sync)
P1.M2.T3.S1 (dead state doc) ──┘  │
                                   │
                      ────────────┘
```

The watchdog implementation (S1) must complete before adversarial tests (T2.S1)
can validate it. All implementing subtasks must complete before the changeset-
level docs sync (T4.S1).