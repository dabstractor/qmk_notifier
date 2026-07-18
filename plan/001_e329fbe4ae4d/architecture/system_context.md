# System Context — qmk-notifier

## What This Repo Is

**qmk-notifier** (hyphen) is a **QMK firmware module** written in C. It runs on a
mechanical keyboard, listens on QMK's Raw HID interface for messages from a
companion desktop app, pattern-matches each message against user-defined rules,
and on a match switches the active keymap layer and/or invokes a user callback.

The repo produces **five source files** consumed as a git submodule inside a
QMK keymap:

| File | Lines | Role |
|---|---|---|
| `notifier.h` | ~42 | Public API: structs, macros, declarations |
| `notifier.c` | ~352 | Receiver, reassembler, dispatcher (QMK-compiled) |
| `pattern_match.h` | ~53 | pattern_match() public declaration + doc comment |
| `pattern_match.c` | ~514 | Thompson NFA matcher (anchors/escapes/classes/NFA) |
| `rules.mk` | 2 | QMK integration (RAW_ENABLE + SRC +=) |

Plus **9 host-side test programs** (~1826 assertions total) and a test runner
(`run_all_tests.sh`).

## Two Compilation Contexts

1. **QMK build** — `notifier.c` and `pattern_match.c` are compiled by the QMK
   build system. `notifier.c` does `#include "pattern_match.c"` (the .c directly,
   not the .h), so the matcher compiles into the same translation unit. The
   `#include QMK_KEYBOARD_H` directive pulls in QMK headers.

2. **Host test build** — The 9 `test_*.c` programs are compiled with plain gcc.
   Each links `pattern_match.c` as a separate translation unit (they do NOT
   compile `notifier.c`, which depends on QMK symbols). Both contexts work
   because the matcher has no file-scope mutable global state except a monotonic
   `nfa_gen` int.

## The Three-Node Ecosystem

| Project | Repo | Language | Role |
|---|---|---|---|
| **qmk-notifier** ← THIS REPO | `dabstractor/qmk-notifier` | C | On-keyboard receiver + matcher + actor |
| **QMKonnect** | `dabstractor/qmkonnect` | Rust | Desktop daemon — detects foreground window, sends `class\x1Dtitle` |
| **qmk_notifier** (underscore) | `dabstractor/qmk_notifier` | Rust | Transport crate — owns wire framing (magic header, 32-byte chunking, ETX) |

> **Naming hazard:** `qmk-notifier` (hyphen) = firmware C module. `qmk_notifier`
> (underscore) = Rust transport crate. They communicate over a fixed wire protocol.

## Wire Protocol Summary

```
Desktop (QMKonnect)                    Keyboard (qmk-notifier)
  │                                        │
  │  Builds: class\x1Dtitle                 │
  │  Transport adds: ETX (0x03)             │
  │  Frames: [0x81][0x9F][≤30 payload]...   │
  │  ─────── 32-byte HID reports ──────►   │
  │                                        │  hid_notify() strips 0x81 0x9F
  │                                        │  Reassembles until ETX
  │                                        │  sanitize → match → dispatch
  │  ◄──── 32-byte response [0|1, 0...]──  │  raw_hid_send(response, 32)
```

**Magic header:** `0x81 0x9F` (coexistence guard — other modules ignored)
**GS delimiter:** `0x1D` (decimal 29, ASCII Group Separator) — separates class|title
**ETX terminator:** `0x03` (message end — appended by transport crate)
**Report size:** 32 bytes (logical, on ALL QMK protocols)
**Payload per report:** 30 bytes (32 − 2 magic header bytes)

## Key Design Principles

1. **Coexistence-safe** — inspects only messages beginning with `0x81 0x9F`
2. **Minimal resource use** — 256-byte static buffer, stack-allocated NFA, one malloc per pattern_match
3. **Backward-compatible** — matcher is strict superset of original glob matcher
4. **Robust to garbage** — non-ASCII stripped, oversized dropped, NULL → false, no crashes

## Current Codebase State

- **All 1826 tests pass** (100% success rate)
- Performance: ~0.1 µs per pattern_match call
- Pathological NFA stress (`a+a+a+a+a+a+a+a+a+a+b` vs 199×`a`): ~1.6ms (< 50ms gate)
- Git HEAD: `513f000` — "add comprehensive project-wide specification doc"
- Source files match PRD Appendix C line counts exactly
