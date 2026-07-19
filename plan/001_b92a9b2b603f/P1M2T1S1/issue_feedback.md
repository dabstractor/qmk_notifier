# Implementation Issue Feedback (Attempt 1/3)

The previous implementation attempt encountered an issue that requires re-planning.

## Issue Details

HALTED: fundamental conflict between the PRP's premise and the actual working tree. The PRP (P1.M2.T1.S1) assumes src/core.rs is in the committed HEAD state — seven wire constants each carrying #[allow(dead_code)], and NO payload-building function yet existing — and instructs me to add a brand-new function `build_command_data` (with a SendMessage arm producing [bytes,0x03], an ApplyHostContext todo!() stub, an ETX_TERMINATOR_BYTE const, and 5 named tests). However, the UNCOMMITTED working tree already contains a DIFFERENT, fully-implemented function named `build_typed_payload` (line ~343) that: (1) has a different name; (2) is in a different location (after batches_for, not between parse_hex_or_decimal and list_hid_devices); (3) handles SendMessage oppositely (returns empty Vec instead of [bytes,0x03]); (4) FULLY implements ApplyHostContext, which the PRP explicitly defers to subtask S2 as a todo!() stub (~15 times); (5) omits the ETX_TERMINATOR_BYTE const (uses literal 0x03); (6) has 7 differently-named tests. Verified via `git show HEAD:src/core.rs` (matches PRP premise exactly) vs `git diff` (the divergence is all uncommitted). Current `cargo test --lib` = 37 passed, not the 30 baseline / 35 target the PRP expects. Blindly applying the PRP would create TWO conflicting payload-builder functions with contradictory SendMessage and ApplyHostContext behavior, and would revert already-working ApplyHostContext code back to a todo!(). plan/.../tasks.json is also modified (uncommitted) — orchestrator-owned, I did not touch it. To resolve, a human/orchestrator must choose: (A) the PRP is stale and build_typed_payload is the real design → skip/rewrite this PRP; (B) the PRP is correct and the working-tree changes are stray → run `git checkout src/core.rs` (and tasks.json) to restore HEAD, then I execute cleanly; (C) reconcile the two → requires a redesigned PRP (not a one-pass mechanical edit) and a decision on whether ApplyHostContext stays implemented or reverts. I made NO modifications (my one attempted import edit was rejected by the edit tool against the diverged file) and did not touch any orchestrator-owned files.

## Full Agent Output

The picture is now fully verified. I must halt and report this as a fundamental conflict with the plan.

---

## HALTING — Fundamental Conflict with the Plan

I have discovered a fundamental conflict between the PRP's stated assumptions and the actual state of the working tree. Per my Failure Protocol ("If a fundamental issue with the plan is found, halt and produce a thorough explanation of the problem at a 10th grade level"), I am stopping before making any changes that would corrupt the codebase.

### The Problem, Plainly

The PRP tells me to add a brand-new function called `build_command_data` to `src/core.rs`. It promises that the file is currently in a clean "starting state" left behind by a previous task (P1.M1.T2.S1): seven wire constants each wearing a `#[allow(dead_code)]` tag, and **no payload-building function existing yet**. The PRP is so confident about this that it gives me the *exact* code to paste in, the *exact* place to put it, and five *exact* tests — all written around the assumption that nothing like this exists yet.

**But that assumption is false against the working tree.** When I opened `src/core.rs`, I found that someone (a prior run, or a parallel agent) has **already** added a different function called `build_typed_payload` that does almost the same job — but with important differences. It is **uncommitted** (it shows up in `git diff`, not in `git log`).

Here's the side-by-side of what the PRP wants versus what's already sitting in the file:

| Aspect | PRP asks for (`build_command_data`) | What's already in working tree (`build_typed_payload`) |
|---|---|---|
| **Function name** | `build_command_data` | `build_typed_payload` ❌ different name |
| **Location** | between `parse_hex_or_decimal` and `list_hid_devices` (top of file) | after `batches_for` (much later, ~line 343) ❌ different location |
| **`SendMessage` arm** | produces `[string_bytes…, 0x03]` (a real payload) | returns empty `Vec` ❌ different behavior |
| **`ListDevices` arm** | returns empty `Vec` ✓ | returns empty `Vec` ✓ (same) |
| **`ApplyHostContext` arm** | `todo!()` stub — **explicitly deferred** to subtask S2 | **fully implemented** with `[layer][flags][count][ids…]` ❌ violates the PRP's explicit scope boundary |
| **ETX constant** | introduces `ETX_TERMINATOR_BYTE = 0x03` | uses literal `0x03`, no const ❌ missing the named constant |
| **Tests** | 5 tests with specific names (`build_command_data_*`) | 7 tests with different names (`build_typed_payload_*`) ❌ different tests |
| **Test count target** | 35 (30 baseline + 5 new) | currently 37 (the pre-existing run already added 7) |

### Why this matters (the conflict is real, not cosmetic)

This isn't a trivial naming disagreement. The two designs **disagree on what the function does**:

1. **`SendMessage` handling is opposite.** The PRP says `build_command_data` should be the *single source of truth* for framing SendMessage payloads (`[msg_bytes, 0x03]`). The existing `build_typed_payload` treats SendMessage as "not my job" and returns empty. These cannot both be right.

2. **`ApplyHostContext` scope is opposite.** The PRP is *extremely explicit* (repeated ~15 times) that the `ApplyHostContext` arm MUST be a `todo!()` stub in *this* subtask, because its multi-report payload is a whole separate subtask (P1.M2.T1.S2) that is supposed to land next. The existing code has already implemented it. If I follow the PRP, I would be *reverting* already-written, working code back to a `todo!()`.

3. **The "dead_code" rationale collapses.** The PRP's entire `#[allow(dead_code)]` removal strategy (remove from 4 constants, keep on 3) is premised on exactly one consumer existing. The existing `build_typed_payload` already consumed **5** constants (it also uses `CMD_APPLY_HOST_CONTEXT`), which is why that constant's `#[allow(dead_code)]` is *already* removed in the working tree.

### If I blindly followed the PRP, here's what would break

If I pasted in the PRP's `build_command_data` verbatim, I would end up with **two** payload-building functions in the same file (`build_command_data` AND `build_typed_payload`), both referencing the same constants, with contradictory behavior on `SendMessage` and `ApplyHostContext`. Downstream task P1.M3.T3 (which wires `run()` to "the builder") would have no idea which one to call. The test suite would contain 12 overlapping tests asserting contradictory things about the same commands. The crate would still compile and pass (37 + 5 = 42 tests), but the codebase would be internally inconsistent and the S2 subtask's entire premise (implement the `ApplyHostContext` arm) would be silently already-done-and-conflicting.

### Verification that this is genuinely a working-tree divergence, not my misreading

I confirmed three things to be sure this is real and not a mistake on my end:

1. **`git show HEAD:src/core.rs`** (the committed version) **matches the PRP's premise exactly**: all 7 constants have `#[allow(dead_code)`, no `build_command_data`, no `build_typed_payload`, no `ETX_TERMINATOR_BYTE`. So the PRP was *correctly written* against the last commit.
2. **`git diff src/core.rs`** shows the divergence is entirely **uncommitted working-tree changes**: someone added `build_typed_payload`, removed 5 (not 4) `#[allow(dead_code)` tags, and added 7 tests.
3. **`cargo test --lib` currently reports 37 passed** — not the 30 baseline the PRP expects, and not the 35 target. The working tree is 7 tests ahead of the PRP's baseline.

The `plan/001_b92a9b2b603f/tasks.json` is also modified (uncommitted), which is an orchestrator-owned file I am forbidden from touching — suggesting the pipeline state itself may have advanced past the point this PRP assumes.

### What I did NOT do

- I did **not** modify `src/core.rs` (my one attempted edit to add the import line appears to have been rejected/not applied — the file's import block is unchanged from HEAD).
- I did **not** touch any orchestrator-owned files (`plan/`, `tasks.json`, `PRP.md`, etc.).
- I did **not** run `rm`, `git rm`, `git clean`, or `mv` on anything.
- I did **not** discard or revert the existing uncommitted `build_typed_payload` work (that would destroy someone else's in-progress changes).

### What needs to happen (requires human/orchestrator decision)

This cannot be resolved by an implementation agent acting alone, because every resolution path requires a judgment call that is above my scope:

1. **Option A — The existing `build_typed_payload` is the *real* intended design** (perhaps the plan evolved). Then *this* PRP (P1.M2.T1.S1) is stale and should be skipped or rewritten, and the `build_command_data` name/signature/`SendMessage`-handling in the PRP is wrong. The orchestrator should mark this task superseded.

2. **Option B — The PRP is correct and `build_typed_payload` is stray work** that should be discarded. Then a human/orchestrator must run `git checkout src/core.rs` (and likely `git checkout plan/001_b92a9b2b603f/tasks.json`) to restore the HEAD state the PRP expects, after which I can execute the PRP cleanly. I will not run that checkout myself — reverting uncommitted work is an irreversible, human-owned decision.

3. **Option C — The two should be reconciled** (e.g., rename `build_typed_payload` → `build_command_data`, move it, add the `SendMessage` arm, add the `ETX_TERMINATOR_BYTE` const, decide whether `ApplyHostContext` stays implemented or reverts to `todo!()`). This is a *redesign*, not the mechanical one-pass implementation the PRP describes, and it directly contradicts the PRP's explicit instruction that `ApplyHostContext` be a `todo!()` stub here. It must be authored as a revised PRP, not improvised by me.

I need a human or the orchestrator to tell me which option is correct before I can proceed safely.

```json
{
   "result": "issue",
   "message": "HALTED: fundamental conflict between the PRP's premise and the actual working tree. The PRP (P1.M2.T1.S1) assumes src/core.rs is in the committed HEAD state — seven wire constants each carrying #[allow(dead_code)], and NO payload-building function yet existing — and instructs me to add a brand-new function `build_command_data` (with a SendMessage arm producing [bytes,0x03], an ApplyHostContext todo!() stub, an ETX_TERMINATOR_BYTE const, and 5 named tests). However, the UNCOMMITTED working tree already contains a DIFFERENT, fully-implemented function named `build_typed_payload` (line ~343) that: (1) has a different name; (2) is in a different location (after batches_for, not between parse_hex_or_decimal and list_hid_devices); (3) handles SendMessage oppositely (returns empty Vec instead of [bytes,0x03]); (4) FULLY implements ApplyHostContext, which the PRP explicitly defers to subtask S2 as a todo!() stub (~15 times); (5) omits the ETX_TERMINATOR_BYTE const (uses literal 0x03); (6) has 7 differently-named tests. Verified via `git show HEAD:src/core.rs` (matches PRP premise exactly) vs `git diff` (the divergence is all uncommitted). Current `cargo test --lib` = 37 passed, not the 30 baseline / 35 target the PRP expects. Blindly applying the PRP would create TWO conflicting payload-builder functions with contradictory SendMessage and ApplyHostContext behavior, and would revert already-working ApplyHostContext code back to a todo!(). plan/.../tasks.json is also modified (uncommitted) — orchestrator-owned, I did not touch it. To resolve, a human/orchestrator must choose: (A) the PRP is stale and build_typed_payload is the real design → skip/rewrite this PRP; (B) the PRP is correct and the working-tree changes are stray → run `git checkout src/core.rs` (and tasks.json) to restore HEAD, then I execute cleanly; (C) reconcile the two → requires a redesigned PRP (not a one-pass mechanical edit) and a decision on whether ApplyHostContext stays implemented or reverts. I made NO modifications (my one attempted import edit was rejected by the edit tool against the diverged file) and did not touch any orchestrator-owned files."
}
```

## Instructions

Review this feedback and create a revised PRP that addresses the issue.
If the issue is fundamentally impossible to resolve, output `"result": "fail"` with an explanation.
