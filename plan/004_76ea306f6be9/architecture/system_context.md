# System Context — §18 Community Module Distribution Delta

## Current Codebase State (verified at HEAD this session)

The qmk_notifier module is **fully implemented** — all functional features (pattern
matcher, receiver/dispatcher, multi-OS selection, typed commands, host-side rules)
are present and tested. This delta adds **only** the §18 Community Module
Distribution migration (R1-R6). No runtime code changes.

### Baseline Test Gates (re-verified)
- `run_all_tests.sh`: **2029/2029 assertions pass**, perf 0.11 µs/call ✓
- `run_notifier_stub_tests.sh`: dispatch=0, os=0, host=0 fails, `✓ gate PASSED` ✓

### §18 Artifact Status (verified absent — all to be created)
| Artifact | Status |
|----------|--------|
| `qmk_module.json` | ❌ ABSENT — to create (R1) |
| `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION` in notifier.c | ❌ ABSENT — to add (R3) |
| `rules.mk` module-context form | ❌ Currently submodule-context (`SRC += qmk_notifier/notifier.c`) — to rewrite (R2) |
| README module-flow install | ❌ Currently submodule flow (lines 49-98) — to rewrite (R5) |
| `LICENSE` file | ❌ ABSENT — license TBD field in manifest |

### Rename Verification (§A — confirmed complete)
- cwd: `/home/dustin/projects/qmk_notifier`; remote: `dabstractor/qmk_notifier` ✓
- `rules.mk` already has `SRC += qmk_notifier/notifier.c` ✓
- `grep -rn "qmk-notifier"` in source/tests → only correct references to the Rust crate ✓
- The underscore repo name is what makes §18 implementable (valid C identifier)

### Key Files and Their Current State

| File | Lines | Current Content | §18 Change |
|------|-------|-----------------|------------|
| `notifier.c` | 1022 | Line 2: `#include QMK_KEYBOARD_H` | Add `#ifdef` guard after line 2 (R3) |
| `notifier.h` | 134 | Public API (complete) | **No change** (R6) |
| `pattern_match.c` | 628 | Thompson NFA (complete) | **No change** (R6) |
| `pattern_match.h` | 53 | Matcher decl (complete) | **No change** (R6) |
| `rules.mk` | 2 | `RAW_ENABLE = yes` + `SRC += qmk_notifier/notifier.c` | Rewrite SRC line to `SRC += notifier.c` (R2) |
| `README.md` | ~560 | Submodule Setup flow (lines 49-98) | Rewrite to module flow (R5) |
| `qmk_module.json` | — | Does not exist | Create (R1) |

### QMK Build Environment (verified)
- `qmk` CLI at `/usr/bin/qmk`, version 1.2.0
- `qmk_firmware` checkout at `~/projects/qmk_firmware` (full source available)
- `qmk config`: `user.keyboard=handwired/dactyl_manuform/5x7_1`, `user.keymap=default`
- No `~/qmk_userspace` — full module-build validation may need a userspace setup;
  fallback is `qmk lint` + host-test gates