# Research Notes — P1.M2.T1.S2 (APPLY_HOST_CONTEXT payload)

## HEADLINE FINDING: Codebase diverged from the item/S1-PRP premise

The item description and the S1 PRP both assume a function named
`build_command_data` exists in `src/core.rs` with a `todo!()` arm for
`ApplyHostContext`, and that S2's job is to **replace the `todo!()`**.

**Reality (current `src/core.rs`, baseline verified 2025):**

- The function is named **`build_typed_payload`** (NOT `build_command_data`).
- Its scope is **typed commands only**: `SendMessage` and `ListDevices` return an
  empty `Vec` (the string ETX is built inline in `run()` in lib.rs). The S1 PRP
  envisioned `build_command_data` handling ALL variants including the string ETX;
  the implemented design split that responsibility differently.
- **`ApplyHostContext` is ALREADY FULLY IMPLEMENTED** — there is no `todo!()`.
  The arm pushes `[discriminator, cmd, layer, flags, count, ids…]` then the
  function appends a trailing `0x03` ETX after the `match`.

So S2 as literally described ("replace the todo!()") has **no todo!() to
replace**. The PRP must reconcile the item's *contract* (the byte layout, the
defensive clamp, the test matrix) with the real codebase.

### Naming-divergence provenance

`plan/001_b92a9b2b603f/architecture/transport_evolution.md` STILL references
`build_command_data` as the target (lines 27–31 data-flow, line 45/52/166). The
implemented function `build_typed_payload` is the realized form of that plan
under a more accurate name (it builds the TYPED payload; non-typed commands are
routed elsewhere by `run()`). **S2 does NOT rename it** — renaming would
conflict with S1's actual delivered output and is out of S2's scope.

## THE ONE REAL BUG: count-byte truncation vs defensive clamp

Current code (`build_typed_payload`, ApplyHostContext arm):

```rust
payload.push(callbacks.len() as u8);   // <-- TRUNCATES
payload.extend_from_slice(callbacks);
```

The item contract mandates:

```rust
data.push(callbacks.len().min(255) as u8);  // defensive CLAMP
```

Empirical proof (throwaway `rustc` program `/tmp/trunc_test.rs`):

```
len= 255  truncate_as_u8=255  min(255)as_u8=255
len= 256  truncate_as_u8=  0  min(255)as_u8=255   <-- CATASTROPHIC
len= 257  truncate_as_u8=  1  min(255)as_u8=255
len= 300  truncate_as_u8= 44  min(255)as_u8=255
len= 511  truncate_as_u8=255  min(255)as_u8=255
```

**Why catastrophic:** `callbacks.len() == 256` → count byte `0` tells the
firmware "zero callbacks follow", but 256 id bytes + ETX actually follow ⇒ the
firmware's APPLY_HOST_CONTEXT parser reads count=0, then mis-parses the trailing
id bytes (parse drift). `.min(255)` guarantees count ≥ the real intent (255 is
u8::MAX) so it never lies *below* the truth.

**Reachability:** in practice `callbacks.len() ≤ HOST_CALLBACK_MAX = 32` (the
firmware callback registry is u8-bounded), so this is unreachable on the happy
path. But PRD §10.1 says the transport is **uncapped**, and the item contract
explicitly requires the clamp + an edge-case test. The clamp is cheap, correct,
and contract-mandated.

### Known limitation of the item's clamp semantics (documented, NOT fixed)

The item reference clamps `count` to 255 but still copies ALL callback bytes
(`extend_from_slice(callbacks)`). For >255 callbacks this makes count (255)
inconsistent with the actual byte count. This is faithful to the item contract
(count never wraps to a dangerously small value), and since >255 is unreachable
in practice, the inconsistency is theoretical. S2 follows the item's exact
semantics (clamp count, copy all) and documents this; it does NOT truncate the
copied bytes (that would deviate from the contract).

## TEST COVERAGE MATRIX (item-required vs current)

| Item-required test                                    | Current coverage                         | Action |
|-------------------------------------------------------|------------------------------------------|--------|
| layer=None ⇒ byte 0xFF                                | `…_apply_host_context_clear_layer`       | exists |
| layer=Some(224) ⇒ byte 224                            | `…_apply_host_context_set_layer`         | exists |
| clear_board=false ⇒ flags 0x00                        | `…_apply_host_context_clear_layer`       | exists |
| clear_board=true ⇒ flags 0x01                         | `…_apply_host_context_set_layer`         | exists |
| callbacks=[1,5,10] ⇒ count=3, then [1,5,10]           | (set_layer uses [10,20,30], structural)  | **ADD** |
| callbacks>255 ⇒ count clamped to 255 (edge)           | NONE — and currently BROKEN (truncates)  | **ADD** |
| Full sequence [0xF0,0x05,layer,flags,count,ids…,0x03] | set_layer + clear_layer exact-`eq`       | exists |

**Net: 2 new tests + 1 one-line code fix.** Existing tests (set_layer,
clear_layer, multi_report_chunking) remain valid and unaffected by the clamp
(len 3 / 0 / 40 all < 255).

## BASELINE / VALIDATION FACTS

- `cargo test --lib` → **37 passed; 0 failed** (verified). After S2: 37 + 2 = **39**.
- No `rustfmt.toml` / `clippy.toml` → default configs.
- `callbacks.len().min(255) as u8` is clippy-clean under default lints
  (`as_conversions` is pedantic-only; the codebase already uses `*os as u8`).
- File touched: **`src/core.rs` ONLY** (the `ApplyHostContext` arm of
  `build_typed_payload` + 2 tests appended to `mod tests`). lib.rs / error.rs /
  main.rs / Cargo.toml untouched.

## SCOPE BOUNDARIES (what S2 does NOT do)

- Does NOT create a `build_command_data` function (would duplicate
  `build_typed_payload`; conflicts with S1's actual output).
- Does NOT rename `build_typed_payload` → `build_command_data` (out of scope;
  affects all `build_typed_payload_*` tests + callers).
- Does NOT touch the `SendMessage`/`ListDevices` arms (they correctly return
  empty `Vec`; string ETX is built inline in `run()` — that's S1/P1.M3.T3 territory).
- Does NOT introduce `ETX_TERMINATOR_BYTE` constant (S1 PRP wanted it; real code
  uses literal `0x03`; out of S2's scope).
- Does NOT wire `build_typed_payload` into `run()` (that's P1.M3.T3.S1).