# SPECIFICATION — qmk-notifier (QMK Firmware Module)

**Master Product Requirements & Engineering Specification**
Target: a single document complete enough for a developer agent to one-shot this
entire codebase from scratch. Read it top to bottom before writing any code.

> **Scope of "this codebase."** This document specifies **qmk-notifier** (hyphen) —
> the **C firmware module** that runs on a QMK keyboard. It is *not* the desktop
> app (that is `QMKonnect`, repo `dabstractor/qmkonnect`) and *not* the Rust
> transport crate (`qmk_notifier`, underscore, repo `dabstractor/qmk_notifier`).
> Those are the other two nodes of the ecosystem (see §1.2). A developer
> rebuilding **this** repo produces only the four source files in §3.

---

## Table of Contents

1. [Product Overview](#1-product-overview)
2. [Functional Requirements](#2-functional-requirements)
3. [Repository Layout & Deliverables](#3-repository-layout--deliverables)
4. [The Wire Protocol Contract (most important section)](#4-the-wire-protocol-contract-most-important-section)
5. [File Specification: `notifier.h`](#5-file-specification-notifierh)
6. [File Specification: `pattern_match.h`](#6-file-specification-pattern_matchh)
7. [File Specification: `pattern_match.c` — the matcher](#7-file-specification-pattern_matchc--the-matcher)
8. [File Specification: `notifier.c` — the receiver/dispatcher](#8-file-specification-notifierc--the-receiverdispatcher)
9. [File Specification: `rules.mk`](#9-file-specification-rulesmk)
10. [The User-Facing API & Reference Keymap](#10-the-user-facing-api--reference-keymap)
11. [Build & Test (the acceptance gate)](#11-build--test-the-acceptance-gate)
12. [Non-Functional Requirements](#12-non-functional-requirements)
13. [Key Invariants a Dev Must Preserve](#13-key-invariants-a-dev-must-preserve)
14. [Planned Future: Host-Side Rules (v0.3.0, NOT implemented now)](#14-planned-future-host-side-rules-v030-not-implemented-now)
15. [Appendix A — Pattern-Semantics Reference Table](#15-appendix-a--pattern-semantics-reference-table)
16. [Appendix B — Constants Reference](#16-appendix-b--constants-reference)
17. [Appendix C — File Sizes & Live Source of Truth](#17-appendix-c--file-sizes--live-source-of-truth)

---

## 1. Product Overview

### 1.1 What qmk-notifier is

**qmk-notifier** is a QMK **module** (consumed as a git submodule inside a QMK
keymap) that turns a mechanical keyboard into a *context-aware* keyboard. It
listens on QMK's Raw HID interface for short messages sent by a companion
desktop app, **pattern-matches** each message against user-defined rules, and on
a match either **switches the active keymap layer** and/or **invokes a user
callback** (e.g. toggle a vim-mode module).

Concretely, the desktop app (QMKonnect) sends the *foreground window's* identity
on every focus change as the string

```
<application_class>\x1D<window_title>
```

qmk-notifier reassembles that string from 32-byte HID reports, sanitizes it, and
runs it through two user-supplied lookup tables — `command_map` (pattern →
callback) and `layer_map` (pattern → QMK layer). The user writes those tables
with two macros, `DEFINE_SERIAL_COMMANDS({ ... })` and
`DEFINE_SERIAL_LAYERS({ ... })`, directly in their `keymap.c`. Changing which
apps trigger which behavior currently requires editing the keymap and
**reflashing** (the planned v0.3.0 host-rules feature, §14, removes that
requirement but is out of scope here).

### 1.2 The broader ecosystem (a dev must understand all three)

| Project | Repo | Language | Role |
|---|---|---|---|
| **qmk-notifier** ← *this repo* | `dabstractor/qmk-notifier` | C | On-keyboard **receiver + matcher + actor**. Consumed as a submodule by the user's keymap. |
| **QMKonnect** | `dabstractor/qmkonnect` | Rust | Cross-platform **desktop daemon**. Detects the foreground window and *sends* the `class\x1Dtitle` string. Never decides behavior. |
| **qmk_notifier** (underscore) | `dabstractor/qmk_notifier` | Rust | **Transport crate** QMKonnect links. Owns the wire framing (magic header, 32-byte chunking, ETX terminator, device cache). |
| **qmk_firmware** | `qmk/qmk_firmware` | C | Upstream QMK. The host firmware; qmk-notifier plugs into it via `RAW_ENABLE`. |

> **Naming hazard (read once):** `qmk-notifier` (hyphen) = the firmware C module
> (this repo). `qmk_notifier` (underscore) = the Rust transport crate. The two
> halves communicate over a tiny fixed wire protocol (§4). Any byte wrong and the
> halves will not talk.

### 1.3 Design philosophy

- **Coexistence-safe.** The module inspects only messages beginning with the
  magic bytes `0x81 0x9F` and ignores everything else, so other Raw HID modules
  (e.g. `qmk-field-kit`, VIA) can share the same interface.
- **Minimal resource use.** A single static 256-byte reassembly buffer, a tiny
  stack-allocated NFA, no dynamic allocation on the receive hot path (one
  `malloc` per `pattern_match` call, freed before return).
- **Backward-compatible by construction.** The pattern matcher is a strict
  superset of the original glob matcher; existing rules keep matching identically.
- **Robust to garbage.** Non-ASCII bytes are stripped; oversized messages are
  dropped silently; NULL pointers return `false`; no input can crash it.

---

## 2. Functional Requirements

The module MUST satisfy all of the following. Each is verified by a test in §11
or by an invariant in §13.

### F1 — Raw HID reception & coexistence
- F1.1 Receive data via `hid_notify(uint8_t *data, uint8_t length)` called from
  the keymap's `raw_hid_receive`.
- F1.2 **Discard** any report whose first two bytes are not `0x81` then `0x9F`,
  or whose `length < 2`. This is the coexistence guard.
- F1.3 After discarding the 2-byte header, reassemble the remaining bytes into a
  static 256-byte buffer across multiple reports until an **ETX** (`0x03`).

### F2 — Message reassembly & sanitization
- F2.1 Buffer exactly `MSG_BUFFER_SIZE = 256` bytes; NUL-terminate on ETX.
- F2.2 On buffer overflow (`msg_index >= 255`), reset the index and drop the
  message (do **not** write past the buffer).
- F2.3 `sanitize_string`: keep only bytes `0x20`–`0x7E` (printable ASCII) plus
  `0x09` (tab), `0x0A` (LF), `0x0D` (CR), `0x1D` (GS delimiter), `0x03` (ETX).
  Strip every other byte. (ETX is processed before sanitize runs, so its presence
  in the allow-list is belt-and-suspenders.)

### F3 — Pattern matching (`pattern_match`)
A public function `bool pattern_match(const char *pattern, const char *str, bool case_sensitive)`
supporting the constructs in §7 and §15. Must be **non-crashing on any input**
(NULL → `false`) and **linear-time** (Thompson NFA, no catastrophic backtracking).

### F4 — Delimiter-aware matching (`match_pattern`, internal to `notifier.c`)
- F4.1 A pattern containing the GS delimiter (`0x1D`) is a **two-part** pattern:
  both the class half and the title half must match the corresponding halves of
  the message.
- F4.2 A pattern **without** a delimiter matches the **class part only** when the
  message has a delimiter (firmware parity with the desktop's class-only rules).
- F4.3 A pattern with a delimiter matched against a message **without** one
  matches the pattern's class half against the whole message.
- F4.4 Neither has a delimiter → direct `pattern_match`.

### F5 — Rule dispatch (`process_full_message`)
- F5.1 On **every** fully-reassembled message: first run `disable_command()`
  (fires the previous command's `on_disable` if non-NULL), then scan
  `command_map`, then scan `layer_map`.
- F5.2 **First-match-wins** in each map (scan order = definition order).
- F5.3 Always `deactivate_layer()` (the currently-active notifier layer) before
  activating a new one → **exactly one notifier layer is active at a time**.
- F5.4 An unmatched/empty message deactivates the layer and disables the command
  (neutral keymap). This is how an empty workspace clears state.
- F5.5 Commands and layers are **independent** — one window can match both a
  command (toggle vim) and a layer (switch keymap) simultaneously.

### F6 — Acknowledgement
- F6.1 After processing, always call `raw_hid_send(response, 32)` with
  `response[0] = 1` if any command or layer matched, else `0`. (QMK silently
  drops this today due to a separate `length == RAW_EPSIZE` guard in the host's
  receive path — see §4.4 — but the firmware must still send it.)

### F7 — Weak-symbol defaults
- F7.1 If the keymap defines neither `DEFINE_SERIAL_COMMANDS` nor
  `DEFINE_SERIAL_LAYERS`, the module compiles and runs against **empty maps**
  (weak default accessors returning size 0). The module must never fail to link
  because a user omitted the macros.

---

## 3. Repository Layout & Deliverables

The repo root contains exactly these source files plus tests, docs, and the test
runner. A from-scratch rebuild must produce:

```
qmk-notifier/
├── notifier.h                 # public API: structs, macros, decls        (~42 lines)
├── notifier.c                 # receiver, reassembler, dispatcher         (~352 lines)
├── pattern_match.h            # pattern_match() public decl + doc comment (~53 lines)
├── pattern_match.c            # the matcher (anchors/escapes/classes/NFA) (~514 lines)
├── rules.mk                   # single-line QMK integration                (2 lines)
├── README.md                  # user-facing README                         (provided)
├── run_all_tests.sh           # builds + runs all 9 suites                 (~181 lines)
└── test_*.c                   # 9 host-side test programs (gcc, not QMK)   (the spec)
```

> **Two compilation contexts.** `notifier.c` and `pattern_match.c` are meant to
> be compiled **by the QMK build system** (they `#include QMK_KEYBOARD_H` etc.).
> The 9 `test_*.c` programs are compiled **with plain gcc on a host** to test
> `pattern_match.c` in isolation. The test binaries link `pattern_match.c` only —
> they do **not** compile `notifier.c` (which depends on QMK symbols).
> `test_comprehensive_integration.c` is built with `-DNOTIFIER_STUB`; this macro
> is currently vestigial (not referenced in source) and may be passed harmlessly.

---

## 4. The Wire Protocol Contract (most important section)

> Get any byte here wrong and the desktop app and the firmware will not talk.
> This is the exact contract with `qmk_notifier` (the Rust crate) and `QMKonnect`.

### 4.1 The logical message

```
<application_class>\x1D<window_title>
```

- `application_class` — the stable app identifier (e.g. `firefox`, `code`,
  `neovide`, `Chrome_WidgetWin_1`). What the user matches in firmware.
- `\x1D` — ASCII **Group Separator** (decimal 29). The delimiter. Macro name:
  `GS_DELIMITER`. Comment in the header calls it "Unit Separator" — it is in fact
  GS (0x1F is US); the byte value `0x1D` (29 = GS) is authoritative.
- `window_title` — the window's title, may be empty.

Examples produced by the desktop:
- VS Code → `code\x1Dmain.rs - qmkonnect`
- Firefox → `firefox\x1DGitHub - Mozilla Firefox`
- Empty Hyprland workspace → `\x1D` (both empty)
- macOS without Screen Recording → `Safari\x1D` (app name, empty title)

> The desktop builds the message **without** a terminator. The transport crate
> (`qmk_notifier`) appends the **ETX** (`0x03`) terminator *before* framing.
> The firmware never sees a terminator in the user payload until the crate adds it.

### 4.2 Report framing on the wire

QMK Raw HID (`RAW_ENABLE = yes`) exposes a vendor-defined interface with **usage
page `0xFF60`**, **usage `0x61`** (QMK defaults `RAW_USAGE_PAGE` / `RAW_USAGE_ID`).
Each logical HID report is **32 bytes**.

The transport crate sends each 32-byte report to the device as a **33-byte**
buffer to `hidapi::HidDevice::write` (hidapi demands a leading report-ID byte;
the interface has no report ID so it's `0x00`):

```
byte[0]      = 0x00     (report-ID leading byte for hidapi write)
byte[1]      = 0x81     (magic header byte 1)
byte[2]      = 0x9F     (magic header byte 2)
byte[3..33]  = up to 30 payload bytes (zero-filled on the final report)
```

**30 payload bytes per report** (`PAYLOAD_PER_REPORT = 32 - 2`). A payload longer
than 30 bytes is split into `ceil(len/30)` back-to-back reports. The end of the
logical message is signaled by an **ETX terminator** (`0x03`) appended to the
payload **before** framing. Burst-write (all reports back-to-back, no per-report
ACK) is safe because QMK's raw-HID OUT endpoint buffers up to 4 reports and NAKs
when full — reports are never dropped.

### 4.3 What `raw_hid_receive` on the firmware sees

QMK delivers **32 bytes** per report to `raw_hid_receive(data, length)` where
`length == 32`. (On V-USB low-speed AVR boards the USB packet is 8 bytes, but QMK
reassembles it into the 32-byte logical report before calling the hook — so the
firmware always sees `length == 32`. Do not handle `length == 8`.)

```
data[0]  = 0x81      ← qmk-notifier magic byte 1
data[1]  = 0x9F      ← qmk-notifier magic byte 2
data[2..]= <payload bytes for this report>
```

### 4.4 The acknowledgement (sent but currently dropped)

After each report the firmware sends a 32-byte response with `response[0] =
(matched ? 1 : 0)`. **QMK's host-side `raw_hid_send` consumer currently requires
`length == RAW_EPSIZE` and silently drops this ack** — so the desktop does not
rely on it. The firmware MUST still send it (a future fix to the host guard will
make it meaningful). See `notifier.c` §8.

### 4.5 Why the magic header exists

Multiple Raw HID modules may share the one interface (e.g. `qmk-notifier` +
`qmk-field-kit` + VIA). The reference keymap wires:

```c
void raw_hid_receive(uint8_t *data, uint8_t length) {
    field_kit_process_message(data, length);   // other module
    hid_notify(data, length);                  // this module
}
```

`hid_notify` returns immediately if `data[0..1] != 0x81 0x9F`, so each module
only consumes its own messages. `0x81 0x9F` is arbitrary but fixed forever.

### 4.6 Planned typed-command namespace (v0.3.0, forward-looking — NOT now)

The planned host-rules feature (§14) reserves a typed-command discriminator at
`data[2] == 0xF0` (request) / response marker `0x51`. Because the sanitizer only
allows bytes `0x20–0x7E`, `0xF0` can never begin a real string, so a future
firmware can distinguish `[0x81][0x9F][0xF0][...]` (typed command) from
`[0x81][0x9F][<printable>...]` (legacy string) unambiguously. **Do not implement
this now.** It is documented only so the v0.3.0 upgrade path is non-breaking.

---

## 5. File Specification: `notifier.h`

The **public** header consumed by the user's `keymap.c`. It defines two structs,
two macros, two constants, four accessor declarations, and the entry-point
declaration.

### 5.1 Type aliases & structs

```c
#pragma once
#include <stdbool.h>

/* A user callback is a nullary C function. */
typedef void (*callback_t)(void);

/* One entry in the command map: pattern → on_enable/on_disable callbacks. */
typedef struct {
    const char   *pattern;
    callback_t    on_enable;
    callback_t    on_disable;      /* may be NULL */
    const bool    case_sensitive;
} command_map_t;

/* One entry in the layer map: pattern → QMK layer index. */
typedef struct {
    const char   *pattern;
    const int     layer;
    const bool    case_sensitive;
} layer_map_t;
```

### 5.2 Map accessors (user overrides via macros; module provides weak defaults)

```c
command_map_t* get_command_map(void);
size_t         get_command_map_size(void);
layer_map_t*   get_layer_map(void);
size_t         get_layer_map_size(void);
```

### 5.3 Constants & helper macros

```c
#define GS_DELIMITER      "\x1D"   /* ASCII 29 — Group Separator, class|title delim */
#define ETX_TERMINATOR    "\x03"   /* ASCII 3  — End of Text, message terminator    */

/* Build a two-part pattern: classname + GS + title.
   Expands to the literal `classname "\x1D" title`. */
#define WINDOW_TITLE(classname, title)  classname GS_DELIMITER title
#define WT(...) WINDOW_TITLE(__VA_ARGS__)
```

`WT` is the short alias and is what keymaps actually use (e.g.
`WT("firefox", "*youtube*")`). It accepts variadic args purely so callers may
write `WT(class, title)`; it always expands to the 2-argument `WINDOW_TITLE`.

### 5.4 Map-definition macros

These macros, used at file scope in the keymap, define the array **and** the
matching accessor pair — overriding the weak defaults in `notifier.c`.

```c
#define DEFINE_SERIAL_COMMANDS(...) \
    command_map_t user_command_map[] = __VA_ARGS__; \
    const size_t user_command_map_size = sizeof(user_command_map) / sizeof(user_command_map[0]); \
    command_map_t* get_command_map(void) { return user_command_map; } \
    size_t get_command_map_size(void) { return user_command_map_size; }

#define DEFINE_SERIAL_LAYERS(...) \
    layer_map_t user_layer_map[] = __VA_ARGS__; \
    const size_t user_layer_map_size = sizeof(user_layer_map) / sizeof(user_layer_map[0]); \
    layer_map_t* get_layer_map(void) { return user_layer_map; } \
    size_t get_layer_map_size(void) { return user_layer_map_size; }
```

> Because C aggregate initialization zero-fills omitted trailing struct fields,
  a `DEFINE_SERIAL_COMMANDS` row may omit the `case_sensitive` 4th field (it
  becomes `false`) and a `DEFINE_SERIAL_LAYERS` row may omit its 3rd field. The
  reference keymap relies on this.

### 5.5 Entry points (implemented by QMK / this module)

```c
/* Provided by the user's keymap (QMK's hook). */
void raw_hid_receive(uint8_t* data, uint8_t length);
/* Provided by this module. The keymap's raw_hid_receive delegates to it. */
void hid_notify(uint8_t* data, uint8_t length);
```

---

## 6. File Specification: `pattern_match.h`

Public header for the matcher. One function, exhaustively documented in the
doc comment (reproduce the doc comment verbatim in a rebuild).

```c
#pragma once
#include <stdbool.h>

bool pattern_match(const char *pattern, const char *str, bool case_sensitive);
```

**Contract (from the doc comment):**
- Wildcard `*` matches any sequence (including empty).
- `^` at pattern start → anchor to beginning. `$` at pattern end → anchor to end.
  `^...$` together → exact full-string match.
- Escapes `\^ \$ \* \\` match the literal character.
- `@note` Returns `false` if either argument is `NULL`.
- Thread-safe (no global state). Memory managed internally; caller frees nothing.
- **No anchors ⇒ substring match** (this is the backward-compatible default).

---

## 7. File Specification: `pattern_match.c` — the matcher

This is the most algorithmically intricate file. It is a **Thompson-NFA** regex
engine over a small, fixed token set. The public pipeline is:

```
pattern_match(pattern, str, case_sensitive)        [public API]
 ├─ parse_pattern(pattern)                          [strip ^/$ anchors, record flags]
 │     └─ process_escapes(core)  → "processed pattern" byte string
 ├─ match_with_anchors(&parsed, str, case)         [choose strategy, loop offsets]
 │     ├─ match_string_with_start(...)      == nfa_match(..., full_match=false)
 │     └─ match_reaches_end_with_start(...) == nfa_match(..., full_match=true)
 └─ free_parsed_pattern(...)
```

`match_with_anchors` loops over input start offsets for the **unanchored
(substring)** and **end-anchored** cases, calling the NFA at each offset.
`string_start` is always the **original** string base (so `\b`/`\B` can compute
absolute positions). The NFA replaces only the leaf matcher; everything above
stays.

### 7.1 The processed-pattern byte contract (what the NFA consumes)

`process_escapes()` emits a NUL-terminated byte string. **Every byte is one of:**

| Byte(s) | Source | Meaning | Width |
|---|---|---|---|
| `0x2A` `*` | bare `*` | **glob wildcard** — any sequence incl. `\n`/`\r` | variable |
| `0x0E` | `+` after a consuming element | **`+` quantifier marker** (`X+` = ≥1 of X) | marker |
| `0x01`–`0x04` | `\^` `\$` `\*` `\\` | escaped literals | 1 consuming |
| `0x05`–`0x0A` | `\d` `\D` `\w` `\W` `\s` `\S` | character classes | 1 consuming |
| `0x0B` `0x0C` | `\b` `\B` | word-boundary / non-boundary assertions | 0 (zero-width) |
| `0x0D` | bare `.` | dot metacharacter — any char **except `\n`/`\r`** | 1 consuming |
| `0x2E` `.` | `\.` | literal dot | 1 consuming |
| `0x2B` `+` | `\+`, or a bare `+` not following a consumable element | literal plus | 1 consuming |
| any other | ordinary char | literal byte | 1 consuming |
| `0x00` | end | NUL terminator | — |

`get_escaped_char(placeholder)` reverses `0x01`–`0x0D` back to the readable char
(for debugging; the matcher uses `pattern_char_matches`, not this, for matching).

### 7.2 Escape & metacharacter processing rules (`process_escapes`)

Maintains a `bool last_consumable` flag while walking the source pattern:

- `\X` (backslash + next char):
  - `\^ \$ \* \\` → placeholder `0x01`–`0x04`; `last_consumable = true`.
  - `\. \+` → literal `.`/`+` (ordinary bytes); `last_consumable = true`.
  - `\d \D \w \W \s \S` → placeholder `0x05`–`0x0A`; `last_consumable = true`.
  - `\b \B` → placeholder `0x0B`/`0x0C`; **`last_consumable = false`** (zero-width).
  - **Unrecognized** (`\x`, `\z`, …) → emit `\\` + the char **literally** (so
    `\x` matches the two-byte string `\x`); `last_consumable = true`.
- A trailing lone `\` (backslash then NUL) → emit `\` literally.
- Bare `*` → emit `0x2A`; `last_consumable = false` (wildcards don't consume
  in the "previous element" sense).
- Bare `+` → if `last_consumable` is true, emit `0x0E` (quantifier) and set
  `last_consumable = false`; **else** emit literal `+` and set `last_consumable = true`.
- Bare `.` → emit `0x0D`; `last_consumable = true`.
- Anything else → emit literally; `last_consumable = true`.

### 7.3 Anchor detection (`parse_pattern`)

- If the pattern starts with `^` → `start_anchored = true`; skip it.
- If the pattern ends with `$` **and the `$` is not escaped** (an even number —
  including zero — of backslashes precede it) → `end_anchored = true`; drop it.
- The substring between the anchors is the **core pattern**, fed to
  `process_escapes`.

### 7.4 Matching strategy (`match_with_anchors`)

| Anchors | Strategy |
|---|---|
| `^` + `$` (both) | **Exact**: `nfa_match(core, str, full=true)` — accept only if MATCH reachable after consuming the whole string. |
| `^` only | **Prefix**: `nfa_match(core, str, str, full=false)` — MATCH reachable at any point from position 0. |
| `$` only | **Suffix**: try `nfa_match(core, str+i, str, full=true)` for `i = 0..len`; succeed on first hit. |
| neither | **Substring** (backward-compat default). Special case: an empty core matches **only** the empty string. Otherwise try `nfa_match(core, str+i, str, full=false)` for `i = 0..len`. |

### 7.5 The NFA engine

Compile the processed pattern once into a stack-allocated pool of `State`s
(Thompson construction), then simulate with two state-lists and a generation
tag. **No backtracking → guaranteed O(states × strlen).**

```c
#define NFA_MAX_PATTERN 128
#define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)

enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };

struct State {
    int    op;
    char   arg;       /* OP_CHAR: pattern byte; OP_ASSERT: 0x0B or 0x0C */
    State *out;
    State *out1;
    int    lastlist;  /* generation tag, set during simulation */
};
```

**Compile (`nfa_compile`)** walks the processed pattern, threading a `tail`
pointer that each unit writes its start into:

- `0x2A` (glob `*`) → `OP_ANY` looping back through an `OP_SPLIT`
  (matches `.*` semantics: any sequence, including empty, including newline).
- `0x0B`/`0x0C` (`\b`/`\B`) → `OP_ASSERT` (zero-width).
- A consuming element `X`:
  - If followed by `0x0E` (`X+`) → `OP_CHAR(X)` then `OP_SPLIT` (loop back to X
    / exit) → compiles `a+` as linear (no catastrophic blow-up for `a+a+a+…`).
  - Else → `OP_CHAR(X)`.
- A stray `0x0E` (should not occur) → skipped defensively.
- End → append an `OP_MATCH` accepting state.
- Zero `lastlist` on every allocated state (the pool is fresh each call).

**Simulate (`nfa_match`, signature identical to the old backtracking core):**

```c
static bool nfa_match(const char *pattern, const char *str,
                      const char *string_start, bool case_sensitive, bool full_match);
```

- `abspos = str - string_start` (absolute offset into the **original** string —
  needed for `\b`/`\B`).
- Seed the current list (`clist`) with the epsilon-closure of the start state at
  `abspos`. If `!full_match` and MATCH is already reachable → return true
  (matches empty prefix).
- For each input char `c` at position `pos`: build the next list (`nlist`) by
  advancing every `OP_ANY` state (consume any non-NUL char, **including**
  newline) and every `OP_CHAR` state whose `pattern_char_matches(arg, c, …)`
  holds; bump `nfa_gen` so closure de-dup works. Swap lists. If `!full_match`
  and MATCH reachable → return true. If the list is empty → break (dead).
- Return `nfa_has_match(clist)` (for `full=true`, accept only at end).

**Epsilon-closure (`nfa_addstate`)** — guarded by `lastlist == nfa_gen`:
- `OP_MATCH` → added to list.
- `OP_SPLIT` → recurse into both `out` and `out1` (same `abspos`).
- `OP_ASSERT` → recurse into `out` **only if** `is_word_boundary(string_start,
  abspos) == want_boundary` (zero-width; `want_boundary` is true for `0x0B`).
  *Empty original string*: neither boundary nor non-boundary (legacy semantics).
- `OP_CHAR`/`OP_ANY` → added to list, waiting to consume a char.

### 7.6 Character classification helpers (static)

```c
static bool is_digit_char(char c);        /* '0'..'9' */
static bool is_word_char(char c);         /* [A-Za-z0-9_] */
static bool is_whitespace_char(char c);   /* ' ' \t \n \r \f \v */
static bool is_word_boundary(const char *str, size_t pos);  /* see below */
```

`is_word_boundary(str, pos)`:
- `pos == 0` → true iff `str[0]` is a word char.
- `pos == strlen` → true iff `str[len-1]` is a word char.
- `pos > strlen` → false.
- interior → true iff `is_word_char(str[pos-1]) != is_word_char(str[pos])`.
- `str == NULL` → false.

### 7.7 The single-char predicate (`pattern_char_matches`)

```c
static bool pattern_char_matches(char pc, char sc, bool case_sensitive);
```

- `pc` in `0x01`–`0x04` → literal (decoded via `get_escaped_char`), case-folded
  via `tolower` unless `case_sensitive`.
- `0x05` `0x06` → `is_digit_char(sc)` / its negation (`\d` / `\D`).
- `0x07` `0x08` → `is_word_char(sc)` / its negation (`\w` / `\W`).
- `0x09` `0x0A` → `is_whitespace_char(sc)` / its negation (`\s` / `\S`).
- `0x0D` → `sc != '\n' && sc != '\r'` (the dot).
- default → ordinary literal, case-folded unless `case_sensitive`.

### 7.8 Why an NFA (not backtracking)

The previous engine backtracked and went **exponential** on patterns like
`a+a+a+a+a+a+a+a+a+a+b` against a long run of `a`. A `MATCH_STEP_BUDGET` cap
prevented hangs but returned wrong "no match" results. The Thompson NFA
compiles-once and simulates in **O(states × input_len), always**. Reference:
Russ Cox, *"Regular Expression Matching Can Be Simple And Fast"*
(https://swtch.com/~rsc/regexp/regexp1.html). The acceptance test (§11.2B)
requires that pathological case to finish in **< 50 ms**.

### 7.9 Sizing note (MCU RAM)

`State pool[NFA_MAX_STATES]` + two pointer lists live on the stack. With
`NFA_MAX_PATTERN = 128` that is ~256 `State`s (~6–8 KB) + ~2 KB of lists per
call. Fine on desktop and RP2040. **For low-RAM AVR, lower `NFA_MAX_PATTERN`
(e.g. 48).** Patterns are short (user keymap rules), so this is ample. The arrays
must stay on the stack (not `static`) if reentrancy is ever needed; in QMK the
matcher is single-threaded.

---

## 8. File Specification: `notifier.c` — the receiver/dispatcher

This is the QMK-side file. It `#include`s (in order) `QMK_KEYBOARD_H`,
`pattern_match.c` (note: the **.c**, directly — so the matcher compiles into the
same translation unit), `notifier.h`, `raw_hid.h`, `<string.h>`, and (if
`CONSOLE_ENABLE`) `print.h`.

> **Include quirk to preserve:** `notifier.c` does `#include "pattern_match.c"`,
> not `pattern_match.h`. The host test programs instead compile `pattern_match.c`
> as a separate translation unit. Both work because the matcher has no
> file-scope mutable global state (only a monotonic `nfa_gen` int).

### 8.1 Constants & globals

```c
#define RAW_REPORT_SIZE 32          /* logical HID report size, ALL QMK protocols */
#define MSG_BUFFER_SIZE 256         /* reassembly buffer */
#define LAYER_UNSET     255

static char    msg_buffer[MSG_BUFFER_SIZE];
static uint16_t msg_index = 0;       /* persists across hid_notify calls */

uint8_t activated_layer = LAYER_UNSET;
command_map_t *current_command = {0};   /* effectively NULL */
```

> The comment on `RAW_REPORT_SIZE` is load-bearing: 32 is the **logical** report
> on every QMK USB protocol and is **not** `RAW_EPSIZE` (the USB packet size).
> V-USB (low-speed AVR) has an 8-byte endpoint but reassembles a 32-byte logical
> report; the host's `raw_hid_send` guard requires `length == 32`. **32 is the
> single value that works on every board.**

### 8.2 `sanitize_string(char *str)` — in-place ASCII filter

Single pass, read/write pointers. Keep byte `b` iff:
`(b >= 32 && b <= 126) || b == 9 || b == 10 || b == 13 || b == 0x1D || b == 0x03`.
NUL-terminate at the write pointer. NULL input → immediate return.

### 8.3 Weak default maps

```c
static command_map_t empty_command_map[1] = {0};
static layer_map_t   empty_layer_map[1]   = {0};

__attribute__((weak)) command_map_t* get_command_map(void)    { return empty_command_map; }
__attribute__((weak)) size_t         get_command_map_size(void){ return 0; }
__attribute__((weak)) layer_map_t*   get_layer_map(void)      { return empty_layer_map; }
__attribute__((weak)) size_t         get_layer_map_size(void)  { return 0; }
```

If the keymap defines `DEFINE_SERIAL_COMMANDS`/`DEFINE_SERIAL_LAYERS`, those
non-weak definitions override these at link time. If not, the module still links
and matches nothing.

### 8.4 Layer & command state machines

```c
void activate_layer(uint8_t layer);   /* layer_on(layer); activated_layer = layer; */
void deactivate_layer(void);          /* if != LAYER_UNSET: layer_off; activated_layer = LAYER_UNSET */
void enable_command(command_map_t *c);/* current_command = c; c->on_enable(); */
void disable_command(void);           /* if current_command && on_disable: call it; current_command = NULL */
```

`activate`/`deactivate` and `enable`/`disable` wrap QMK's `layer_on`/`layer_off`
and the user's callbacks. Debug `uprintf`s are emitted only under `CONSOLE_ENABLE`.

### 8.5 `match_pattern(pattern, message, case_sensitive)` — delimiter-aware wrapper

This is the **internal** function `process_full_message` calls (it is **not**
the public `pattern_match`). It applies the firmware's two-part semantics:

1. Find the first GS (`0x1D`) in `pattern`.
2. NULL `pattern` or `message` → `false`.
3. **No delimiter in pattern:**
   - If `message` has a delimiter → copy the message's class half (before GS)
     into a 256-byte buffer and `pattern_match(pattern, class_half, …)`.
   - Else → `pattern_match(pattern, message, …)` directly.
4. **Delimiter in pattern:**
   - If `message` has **no** delimiter → copy the pattern's class half and
     `pattern_match(pattern_left, message, …)`.
   - If both have a delimiter → split both (helper `split_by_delimiter`), then
     `pattern_match(pattern_left, msg_left) && pattern_match(pattern_right, msg_right)`.
5. Buffer/length guards (`>= 256`) → `false`.

Helper `find_first_delimiter(str)` returns a pointer to the first GS or NULL.
Helper `split_by_delimiter(source, delimiter_pos, left, left_size, right,
right_size)` copies the left part (length = `delimiter_pos - source`) and the
right part (everything after the delimiter), NUL-terminating both; returns
`false` on truncation.

### 8.6 `process_full_message(char *data)` — the dispatcher

```
1. length = strlen(data); if length >= 256 return false.
2. memcpy into local received_command[256]; NUL-terminate.
3. disable_command();                     ← ALWAYS (runs prev on_disable)
4. scan command_map: first match_pattern() hit → remember index + pointer; break.
5. scan layer_map:   first match_pattern() hit → remember index + layer; break.
6. deactivate_layer();                    ← ALWAYS (clears prev notifier layer)
7. if command found: enable_command(it).  ← fires on_enable
8. if layer found:   activate_layer(it).  ← layer_on
9. (CONSOLE_ENABLE) print match/miss; GS shown as '|'.
10. return (command_found || layer_found).
```

> **Ordering matters:** disable-before-scan, deactivate-before-activate. This
> guarantees clean transitions and exactly-one-active-layer.

### 8.7 `hid_notify(uint8_t *data, uint8_t length)` — the entry point

```
1. if (length < 2 || data[0] != 0x81 || data[1] != 0x9F) return;   ← coexistence guard
2. data += 2; length -= 2;                                            ← strip magic header
3. match = false;
4. for each byte c in data[0..length):
     if c == ETX (0x03):
         msg_buffer[msg_index] = '\0';
         sanitize_string(msg_buffer);
         msg_index = 0;
         match = process_full_message(msg_buffer);
         break;
     else:
         if msg_index < MSG_BUFFER_SIZE-1: msg_buffer[msg_index++] = c;
         else: msg_index = 0;            ← overflow: drop message, reset
5. uint8_t response[32] = {0}; response[0] = match ? 1 : 0;
   raw_hid_send(response, RAW_REPORT_SIZE);
```

Note: a single report may contain a partial message (no ETX) → bytes accumulate
in `msg_buffer` across successive `hid_notify` calls until ETX arrives. The
response is sent **once per report** (not once per message); only the report
that contained the ETX will have `match == true`.

---

## 9. File Specification: `rules.mk`

Exactly two lines:

```make
RAW_ENABLE = yes
SRC += qmk-notifier/notifier.c
```

`RAW_ENABLE` turns on QMK's Raw HID feature (usage page `0xFF60` / usage `0x61`,
32-byte reports). `SRC +=` compiles `notifier.c` (which itself `#include`s
`pattern_match.c`). The user's keymap pulls this in with:

```make
include keyboards/<...>/<keyboard>/qmk-notifier/rules.mk
```

Do **not** hand-write `SRC += lib/...` or point at a non-existent
`qmk_notifier.c` — that fails to link.

---

## 10. The User-Facing API & Reference Keymap

### 10.1 Integration (the end user's steps)

1. **Add the submodule** under the keyboard dir:
   ```bash
   cd <qmk_firmware>/keyboards/<your_keyboard>
   git submodule add https://github.com/dabstractor/qmk-notifier.git qmk-notifier
   ```
2. **In the keymap's `rules.mk`:**
   ```make
   include keyboards/<...>/<keyboard>/qmk-notifier/rules.mk
   ```
3. **In `keymap.c`:**
   ```c
   #include QMK_KEYBOARD_H
   #include "./qmk-notifier/notifier.h"

   void raw_hid_receive(uint8_t *data, uint8_t length) {
       hid_notify(data, length);
       /* other Raw HID modules can be called here too */
   }
   ```
4. **Define rules** with the two macros (see reference keymap below).
5. `qmk compile -kb <kb> -km <km>` then `qmk flash`.

### 10.2 Reference keymap (validated against this spec)

The maintainer's Dactyl-Manuform 5×7 (RP2040, split, `SERIAL_DRIVER = vendor`)
exercises every construct. Rebuilding the matcher must keep all of these
matching exactly:

```c
DEFINE_SERIAL_COMMANDS({
    { "neovide", &disable_vim },
    { WT("*tty$", "^terminal$"), &disable_vim },
    { WT("*tty$", "*tty"), &disable_vim },
    { "*iterm*", &disable_vim },
    { WT("^Claude$", "^Claude$"), &vim_lazy_insert, &disable_vim },   // exact class+title
    { WT("*chrome*", "*claude*"), &vim_lazy_insert, &disable_vim },
    { WT("*chrome*", "*chatgpt*"), &vim_lazy_insert, &disable_vim },
    { WT("^brave-browser$", "*Claude - Brave$"), &vim_lazy_insert, &disable_vim },
    { "Mulletware Wiki", &vim_lazy_insert, &disable_vim },
    { WT("*", "*orderlands*"), &disable_vim },
    { WT("steam_app*", "*"), &disable_vim },
    { WT("cs2", "Counter-Strike 2"), &disable_vim },                  // case-insensitive default
});

DEFINE_SERIAL_LAYERS({
    { "*calculator", _NUMPAD },
    { WT("*chrome*", "*jitsi*"), _JITSI },
    { WT("tty$", "^terminal$"), _TERMINAL },
    { "*iterm*", _TERMINAL },
    { WT("*alacritty*", "*matterhorn*"), _MATTERHORN },
    { "*clickup*", _CLICKUP },
    { "*neovide*", _NEOVIM },
    { "chrome*", _BROWSER },
    { WT("brave-browser", "*"), _BROWSER },
    { WT("firefox", "*"), _BROWSER },
    { "*inkscape*", _INKSCAPE },
    { "blender", _BLENDER },
    { "borderlands*", _GAMING },
    { WT("steam_app*", "*orderlands*"), _GAMING },
    { "steam_app*", _GAMING },
    { WT("cs2", "Counter-Strike 2"), _GAMING },
});
```

**What this demonstrates (and therefore what the desktop must produce):**
- Bare patterns match the **class** alone: `"chrome*"`, `"blender"`, `"steam_app*"`.
- `WT(class, title)` matches both halves: `WT("brave-browser", "*Claude - Brave$")`.
- Anchors for precision: `WT("^Claude$", "^Claude$")` (so a browser tab titled
  "Claude" doesn't trip the desktop-app rule).
- `case_sensitive` defaults to `false` (`"Counter-Strike 2"` matches case-insensitively).
- Commands and layers are independent — one window can fire both.

---

## 11. Build & Test (the acceptance gate)

The tests are the spec. A from-scratch rebuild is **done** only when every test
suite reports `0 failures` and the pathological-NFA stress completes in < 50 ms.

### 11.1 Build all 9 suites (exact flags — copy/paste)

```bash
gcc -o test_pattern_match             test_pattern_match.c             pattern_match.c
gcc -o test_char_classification       test_char_classification.c       pattern_match.c
gcc -o test_word_boundary_basic       test_word_boundary_basic.c       pattern_match.c
gcc -o test_word_boundary_integration test_word_boundary_integration.c pattern_match.c
gcc -o test_metachar_verification     test_metachar_verification.c     pattern_match.c
gcc -o test_comprehensive_integration test_comprehensive_integration.c pattern_match.c -std=c99 -DNOTIFIER_STUB
gcc -o test_error_handling            test_error_handling.c            pattern_match.c -I.
gcc -o test_memory_stress             test_memory_stress.c             pattern_match.c -I.
gcc -o test_invalid_patterns          test_invalid_patterns.c          pattern_match.c -I.
```

Or simply `./run_all_tests.sh` (rebuilds the nine, runs them, prints a summary,
then builds & runs a performance micro-benchmark).

### 11.2 Acceptance gate — all must be true

**(A) Every suite has 0 failures:**
```bash
for t in test_pattern_match test_char_classification test_word_boundary_basic \
         test_word_boundary_integration test_metachar_verification \
         test_comprehensive_integration test_error_handling test_memory_stress \
         test_invalid_patterns; do
  printf "%-36s fails=%s\n" "$t" "$(./$t 2>&1 | grep -c '^FAIL:')"
done
# expect fails=0 for every line
```

**(B) The pathological case that used to hang now finishes in milliseconds:**
```bash
cat > /tmp/nfa_stress.c <<'EOF'
#include <stdio.h>
#include <time.h>
#include "pattern_match.h"
int main(void){
  char s[200]; for(int i=0;i<199;i++) s[i]='a'; s[199]='\0';
  const char* p="a+a+a+a+a+a+a+a+a+a+b";
  clock_t t=clock(); int r=pattern_match(p,s,1);
  printf("result=%d  %.1f us\n", r, 1e6*(double)(clock()-t)/CLOCKS_PER_SEC);
  return 0;
}
EOF
gcc -O2 -w /tmp/nfa_stress.c pattern_match.c -I. -o /tmp/nfa_stress
timeout 5 /tmp/nfa_stress   # must print result=0 in < 50 ms
```

**(C) Realistic patterns still match (all six must print `1`):**
```bash
cat > /tmp/nfa_real.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  printf("%d\n", pattern_match("\\w+","hello",1));                       /* 1 */
  printf("%d\n", pattern_match("\\b\\w+\\b\\s+\\b\\w+\\b","hello world",1)); /* 1 */
  printf("%d\n", pattern_match("^\\w+@\\w+$","user_host",1));            /* 1 */
  printf("%d\n", pattern_match("v\\.code","v.code",1));                  /* 1 */
  printf("%d\n", pattern_match("a+b","aaab",1));                         /* 1 */
  printf("%d\n", pattern_match("*slack*","Slack - general",0));          /* 1 */
  return 0;
}
EOF
gcc -w /tmp/nfa_real.c pattern_match.c -I. -o /tmp/nfa_real && /tmp/nfa_real
```

### 11.3 Test inventory (what each suite covers)

| Suite | Compiles with | Counts / Style | Covers |
|---|---|---|---|
| `test_pattern_match` | `pattern_match.c` | 376 numbered cases | anchors, escapes, wildcards, case sensitivity, parsing, edge cases, metachars w/ anchors/wildcards, word-boundary escape processing |
| `test_char_classification` | `pattern_match.c` | 179 cases | digit/word/whitespace classification (indirect via metachars) |
| `test_word_boundary_basic` | `pattern_match.c` | 74 cases | `\b`/`\B` boundary semantics |
| `test_word_boundary_integration` | `pattern_match.c` | 189 cases | `\b`/`\B` integrated with anchors/wildcards/classes |
| `test_metachar_verification` | `pattern_match.c` | boolean PASS/FAIL printout | `\d \D \w \W \s \S` smoke test + combos |
| `test_comprehensive_integration` | `pattern_match.c -std=c99 -DNOTIFIER_STUB` | 10 categories | multi-feature combos, performance, memory management |
| `test_error_handling` | `pattern_match.c -I.` | survival/case counts | NULL/garbage inputs, malformed escapes |
| `test_memory_stress` | `pattern_match.c -I.` | survival | long strings, repeated alloc/free (no leaks/crashes) |
| `test_invalid_patterns` | `pattern_match.c -I.` | 1008 cases (920 combo + …) | 46 pathological patterns × many inputs |

**Total assertion-level checks across all suites: ~1 826, all must pass.**
(The README's older "1 992 / 2 048" figures and per-suite counts like "383/263"
are stale; the live counts above are authoritative — `./run_all_tests.sh` prints
the real totals.)

### 11.4 The test framework

Each counting suite uses the same pattern: a `test_case_t { pattern, input,
case_sensitive, expected, description }` table; a `run_test(t)` helper that
calls `pattern_match`, compares to `expected`, prints `PASS:`/`FAIL:`; and a
final summary (`Total tests run: N / Tests passed: P / Tests failed: F /
Success rate: …%`). `run_all_tests.sh` greps these summary lines to aggregate.
The exit code is non-zero iff any suite failed.

---

## 12. Non-Functional Requirements

- **Performance.** Pattern matching is sub-microsecond per call for realistic
  patterns (~0.1 µs measured). The NFA is linear in `states × strlen` for **all**
  inputs, with no catastrophic backtracking. The receive hot path does no dynamic
  allocation; `process_escapes` does exactly one `malloc`+`free` per
  `pattern_match` call (pre-existing; out of scope to remove).
- **Robustness.** NULL inputs → `false`. Non-ASCII bytes stripped. Oversized
  messages dropped. Malformed/unknown escapes kept literal. No input crashes the
  matcher (verified by `test_error_handling`, `test_memory_stress`,
  `test_invalid_patterns` — 920 pathological pattern/input combinations).
- **MCU footprint.** 256 B static reassembly buffer; ~6–8 KB stack for the NFA
  pool at `NFA_MAX_PATTERN=128` (lower for AVR). No `malloc` on the receive path.
- **Backward compatibility.** The matcher is a strict superset of the original
  glob matcher; unanchored patterns without escapes match identically to before.
- **Coexistence.** The `0x81 0x9F` guard means the module never reacts to other
  modules' messages and never breaks them.

---

## 13. Key Invariants a Dev Must Preserve

1. **Magic header is exactly `0x81 0x9F`.** The coexistence guard checks
   `data[0]==0x81 && data[1]==0x9F`. Never change without coordinating both halves.
2. **GS is `0x1D`; ETX is `0x03`.** The payload is `class\x1Dtitle`; ETX
   terminates the reassembled message. Never change unilaterally.
3. **`RAW_REPORT_SIZE` is 32.** This is the logical report on every QMK protocol;
   it is **not** `RAW_EPSIZE`. `raw_hid_send` must be called with `32`.
4. **Disable-before-scan, deactivate-before-activate.** `process_full_message`
   always calls `disable_command()` then scans, then `deactivate_layer()` then
   activates. This yields clean transitions and exactly-one-active-layer.
5. **First-match-wins** in each map (scan order = definition order).
6. **Exactly one notifier layer active at a time.** `LAYER_UNSET = 255`.
7. **An unmatched message clears state** (deactivates layer, disables command).
8. **Glob `*` matches any char including `\n`/`\r`;** dot `.` matches any char
   **except** `\n`/`\r`. Do not conflate them (§7.5, §7.7).
9. **`+` is a postfix quantifier (`X+`), `*` is a standalone wildcard token.**
   They compile differently in the NFA (§7.5) — do not conflate.
10. **Absolute position for `\b`/`\B`** must be computed from `string_start`
    (the original string base), not from the per-offset `str` (§7.5).
11. **The `lastlist` generation tag is mandatory** in the NFA — without it,
    `OP_SPLIT` and `\b\b` recurse infinitely.
12. **No observable result may change.** The test suites encode the exact
    intended semantics. If a test flips red, fix the matcher, not the test.
13. **Weak defaults must remain** so a keymap without `DEFINE_*` still links.
14. **`pattern_match.c` must compile both ways:** `#include`d by `notifier.c`
    (QMK build) and as a standalone translation unit (host tests).

---

## 14. Planned Future: Host-Side Rules (v0.3.0, NOT implemented now)

This section is **forward-looking only** — it documents the roadmap so the
current design stays upgrade-compatible. **Do not implement any of it in a
rebuild of the current codebase.** Full design lives in the QMKonnect companion
spec `HOST_RULES.md`.

The planned v0.3.0 feature moves app→layer and app→callback matching onto the
desktop host (editable `rules.toml`, no reflash), stacking on top of the board's
`DEFINE_*` rules. It spans all three repos and would add to this firmware:

- A **named callback registry** (`DEFINE_HOST_CALLBACKS({ … })`) + accessor pair,
  mirroring the existing `command_map` weak-default pattern.
- A **second layer tracker** `host_layer` (independent of `activated_layer`) and
  a host-callback enable set `host_cb_enabled[]`.
- **Typed-command dispatch** at the top of `hid_notify()`: if `data[2] == 0xF0`,
  parse `data[3]` as a typed command id (`QUERY_INFO=0x01`, `QUERY_CALLBACK=0x02`,
  `APPLY_HOST_CONTEXT=0x05`) and reply with a `0x51`-marked response; else fall
  through to the legacy string path unchanged.
- Handlers `set_host_layer` (on/off/clear; orthogonal to the board layer) and
  `apply_host_callbacks` (disable-before-enable diff against the desired set).

The current design is already compatible with this upgrade:
- `0xF0` can never begin a real matched string (the sanitizer allows only
  `0x20–0x7E`), so legacy firmware safely ignores typed commands.
- Typed responses use marker `0x51` (≥2), distinguishable from the legacy `0`/`1`
  match-bool response.
- Board rules keep running unchanged; host rules stack on top (board layer first,
  host layer above; board callbacks first, host callbacks after).

---

## 15. Appendix A — Pattern-Semantics Reference Table

Verified against the live test suite (`/tmp/probe` harness, §11.2C style).

| Pattern | Input | cs | Result | Why |
|---|---|---|---|---|
| `*` | `a\nb` | 1 | **true** | glob matches newline |
| `a.b` | `a\nb` | 1 | **false** | dot excludes `\n`/`\r` |
| `a.b` | `axb` | 1 | true | dot matches any other char |
| `a+b` | `aaab` | 1 | true | `+` = one-or-more |
| `a+b` | `b` | 0 | false | `+` needs ≥1 |
| `\^` | `^` | 1 | true | escaped caret literal |
| `a\+b` | `a+b` | 1 | true | escaped plus literal |
| `v\.code` | `v.code` | 1 | true | escaped dot literal |
| `\x` | `\x` | 1 | true | unknown escape kept literal (2 bytes) |
| `\bword\b` | `a word here` | 0 | true | word-boundary anchored (case-insensitive) |
| `^$` | `` | 1 | true | empty exact |
| `neovide` | `code\x1Dmain.rs` | 0 | false | bare pattern matches **class only** |
| `^hello` | `hello world` | 0 | true | start anchor |
| `world$` | `hello world` | 0 | true | end anchor |
| `^hello$` | `hello` | 1 | true | full anchor (exact) |
| `^hello$` | `hello world` | 1 | false | full anchor rejects non-exact |
| `sear*term` | `presearchtermpost` | 1 | true | unanchored wildcard substring |
| `abc` | `ABC` | 0 | true | case-insensitive substring |
| `abc` | `ABC` | 1 | false | case-sensitive no-match |

**Construct reference:**

| Construct | Meaning |
|---|---|
| `*` | Wildcard — any sequence (incl. empty, incl. `\n`/`\r`) |
| `^` at start | Anchor to beginning |
| `$` at end | Anchor to end |
| `^…$` | Exact full-string match |
| `\^ \$ \* \\` | Literal escaped char |
| `\. \+` | Literal dot / plus |
| `\d \D` | Digit / non-digit |
| `\w \W` | Word char `[A-Za-z0-9_]` / non-word |
| `\s \S` | Whitespace `[ \t\n\r\f\v]` / non-whitespace |
| `\b \B` | Word boundary / non-boundary (zero-width) |
| `.` | Any char except `\n`/`\r` |
| `X+` | One or more of element X (linear-time) |
| (no anchors) | Substring match (backward-compatible default) |

> **v0.3.0 host-rules "stable subset":** only `*`, `^`, `$`, and two-part `WT`
> are required for host-side parity. The regex classes (`\d \w \s \b . +`) are
> firmware-extras not needed on the host.

---

## 16. Appendix B — Constants Reference

| Constant | Value | Where | Meaning |
|---|---|---|---|
| Magic header | `0x81 0x9F` | first 2 payload bytes | qmk-notifier coexistence guard |
| Group Separator (GS) | `0x1D` (29) | `GS_DELIMITER` | class\|title delimiter in payload |
| End of Text (ETX) | `0x03` (3) | `ETX_TERMINATOR` | message terminator (appended by the transport crate) |
| `RAW_REPORT_SIZE` | `32` | notifier.c | logical HID report size (all QMK protocols) |
| `MSG_BUFFER_SIZE` | `256` | notifier.c | reassembly buffer |
| `LAYER_UNSET` | `255` | notifier.c | "no notifier layer active" sentinel |
| Payload per report | `30` | transport crate | `RAW_REPORT_SIZE - 2` (after magic) |
| Usage page | `0xFF60` | QMK `RAW_USAGE_PAGE` | auto-discovery primary id |
| Usage | `0x61` | QMK `RAW_USAGE_ID` | auto-discovery primary id |
| `NFA_MAX_PATTERN` | `128` | pattern_match.c | max processed-pattern length |
| `NFA_MAX_STATES` | `2*128+2=258` | pattern_match.c | NFA state-pool cap |
| Request discriminator (planned) | `0xF0` | v0.3.0 | typed-command marker (after `0x81 0x9F`) |
| Response marker (planned) | `0x51` | v0.3.0 | typed-response marker (vs legacy `0`/`1`) |

Placeholder bytes (processed pattern, §7.1): `0x01–0x04` escaped literals
(`^ $ * \`), `0x05–0x0A` classes (`\d \D \w \W \s \S`), `0x0B/0x0C`
(`\b`/`\B`), `0x0D` (dot), `0x0E` (`+` quantifier marker).

---

## 17. Appendix C — File Sizes & Live Source of Truth

Approximate line counts (a rebuild should be in this ballpark; exact text need
not match byte-for-byte as long as all tests pass and all invariants hold):

| File | Lines | Role |
|---|---|---|
| `notifier.h` | ~42 | public API |
| `notifier.c` | ~352 | receiver + dispatcher |
| `pattern_match.h` | ~53 | matcher decl + doc |
| `pattern_match.c` | ~514 | Thompson NFA matcher |
| `rules.mk` | 2 | QMK integration |
| `run_all_tests.sh` | ~181 | test runner + perf bench |
| `test_pattern_match.c` | 843 | 376-case main suite |
| `test_char_classification.c` | 299 | 179 cases |
| `test_word_boundary_basic.c` | 206 | 74 cases |
| `test_word_boundary_integration.c` | 382 | 189 cases |
| `test_metachar_verification.c` | 51 | smoke test |
| `test_comprehensive_integration.c` | 449 | 10 categories |
| `test_error_handling.c` | 566 | survival |
| `test_memory_stress.c` | 238 | survival |
| `test_invalid_patterns.c` | 292 | 1008 cases |

> **Living source of truth:** the production codebase itself
> (`notifier.c`, `notifier.h`, `pattern_match.c`, `pattern_match.h`, `rules.mk`)
> and the test corpus (`test_*.c`, `run_all_tests.sh`). Where this spec and the
> code disagree, **the code + the passing tests win**; report the drift. This
> spec captures the intended design at the current `HEAD`.

---

## Definition of Done (one-shot rebuild)

A developer agent has correctly one-shot this codebase when **all** of the
following hold:

- [ ] The five source files (`notifier.{c,h}`, `pattern_match.{c,h}`, `rules.mk`)
      exist and reproduce the public API in §5–§9.
- [ ] `./run_all_tests.sh` reports **0 failures** across all 9 suites (~1 826
      assertions pass) and the performance micro-benchmark is sub-second.
- [ ] The pathological stress `/tmp/nfa_stress` (`a+a+a+a+a+a+a+a+a+a+b` vs
      199×`a`) prints `result=0` in **< 50 ms** (§11.2B).
- [ ] `/tmp/nfa_real` prints six `1`s (§11.2C).
- [ ] Every invariant in §13 holds; the wire-protocol bytes in §4 match exactly.
- [ ] No new compiler warnings beyond pre-existing ones.
- [ ] A keymap that `#include`s `notifier.h`, wires `raw_hid_receive → hid_notify`,
      `include`s `rules.mk`, and uses `DEFINE_SERIAL_*` + `WT(...)` compiles
      cleanly under QMK and reacts to desktop-sent focus changes by switching
      layers / firing callbacks.

---

*End of specification. This module is the keyboard half of a two-part system;
see the QMKonnect `PRD.md` and its companion specs (`PROTOCOL.md`,
`FIRMWARE.md`, `HOST_RULES.md`) for the desktop half and the planned v0.3.0
host-rules evolution.*
