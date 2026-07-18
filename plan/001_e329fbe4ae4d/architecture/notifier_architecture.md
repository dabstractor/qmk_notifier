# Notifier Architecture — notifier.c / notifier.h / rules.mk

## Overview

The QMK-side file. Receives Raw HID reports, reassembles messages, sanitizes
them, pattern-matches against user-defined rules, and dispatches to layer
switches and/or callbacks.

## Include Order (preserved exactly)

```c
#include QMK_KEYBOARD_H
#include "pattern_match.c"   // the .c directly, NOT pattern_match.h
#include "notifier.h"
#include "raw_hid.h"
#include <string.h>
#ifdef CONSOLE_ENABLE
#include "print.h"
#endif
```

> **Quirk:** `notifier.c` does `#include "pattern_match.c"` (the .c), not the .h.
> Host tests instead compile `pattern_match.c` as a separate translation unit.
> Both work because the matcher has no file-scope mutable state except `nfa_gen`.

## Constants & Globals

```c
#define RAW_REPORT_SIZE 32          // logical HID report size, ALL QMK protocols
#define MSG_BUFFER_SIZE 256         // reassembly buffer
#define LAYER_UNSET     255

static char     msg_buffer[MSG_BUFFER_SIZE];
static uint16_t msg_index = 0;       // persists across hid_notify calls
uint8_t         activated_layer = LAYER_UNSET;
command_map_t  *current_command = {0};  // effectively NULL
```

> **RAW_REPORT_SIZE = 32** is load-bearing. On every QMK USB protocol (LUFA,
> ChibiOS, V-USB), the logical report is 32 bytes. V-USB has an 8-byte endpoint
> but reassembles into 32. The host's `raw_hid_send` guard requires length == 32.
> **32 is the single value that works on every board.**

## Public API (notifier.h)

### Type Aliases & Structs

```c
typedef void (*callback_t)(void);

typedef struct {
    const char   *pattern;
    callback_t    on_enable;
    callback_t    on_disable;      // may be NULL
    const bool    case_sensitive;
} command_map_t;

typedef struct {
    const char   *pattern;
    const int     layer;
    const bool    case_sensitive;
} layer_map_t;
```

### Map Accessors (user overrides via macros; weak defaults in notifier.c)

```c
command_map_t* get_command_map(void);
size_t         get_command_map_size(void);
layer_map_t*   get_layer_map(void);
size_t         get_layer_map_size(void);
```

### Constants & Helper Macros

```c
#define GS_DELIMITER      "\x1D"   // ASCII 29 — Group Separator
#define ETX_TERMINATOR    "\x03"   // ASCII 3  — End of Text
#define WINDOW_TITLE(classname, title)  classname GS_DELIMITER title
#define WT(...) WINDOW_TITLE(__VA_ARGS__)
```

### Map-Definition Macros (override weak defaults at link time)

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

> C aggregate initialization zero-fills omitted trailing fields, so
> `case_sensitive` may be omitted (defaults to `false`).

## Weak Default Maps

```c
static command_map_t empty_command_map[1] = {0};
static layer_map_t   empty_layer_map[1]   = {0};

__attribute__((weak)) command_map_t* get_command_map(void)     { return empty_command_map; }
__attribute__((weak)) size_t         get_command_map_size(void){ return 0; }
__attribute__((weak)) layer_map_t*   get_layer_map(void)       { return empty_layer_map; }
__attribute__((weak)) size_t         get_layer_map_size(void)  { return 0; }
```

If keymap defines `DEFINE_SERIAL_*`, non-weak overrides replace these at link time.

## sanitize_string(char *str) — In-Place ASCII Filter

Single pass, read/write pointers. Keep byte `b` iff:
```
(b >= 32 && b <= 126) || b == 9 || b == 10 || b == 13 || b == 0x1D || b == 0x03
```
NUL-terminate at write pointer. NULL input → immediate return.

## hid_notify(uint8_t *data, uint8_t length) — Entry Point

```
1. if (length < 2 || data[0] != 0x81 || data[1] != 0x9F) return;   ← coexistence guard
2. data += 2; length -= 2;                                           ← strip magic header
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
         else: msg_index = 0;             ← overflow: drop message, reset
5. uint8_t response[32] = {0}; response[0] = match ? 1 : 0;
   raw_hid_send(response, RAW_REPORT_SIZE);
```

A single report may contain a partial message (no ETX) → bytes accumulate across
successive calls until ETX arrives. Response sent once per report; only the
report containing the ETX has `match == true`.

## match_pattern(pattern, message, case_sensitive) — Delimiter-Aware Wrapper

**INTERNAL** function (not the public `pattern_match`). Implements two-part semantics:

1. Find first GS (0x1D) in `pattern`
2. NULL `pattern` or `message` → `false`
3. **No delimiter in pattern:**
   - Message has delimiter → match pattern against class half only
   - Else → `pattern_match(pattern, message, ...)` directly
4. **Delimiter in pattern:**
   - Message has no delimiter → match pattern's class half against whole message
   - Both have delimiter → split both, match left vs left AND right vs right
5. Buffer/length guards (≥ 256) → `false`

Helpers: `find_first_delimiter(str)` → pointer to first GS or NULL.
`split_by_delimiter(source, delim_pos, left, left_size, right, right_size)` → copies
both halves, NUL-terminates; returns false on truncation.

### F4 Delimiter Matching Matrix (verified by scout stub-compile)

| Case | Pattern | Message | Behavior | Verified |
|---|---|---|---|---|
| F4.4 | no delim | no delim | direct pattern_match | ✅ |
| F4.2 | no delim | has delim | match pattern vs class half only | ✅ |
| F4.3 | has delim | no delim | match pattern's class half vs whole message | ✅ |
| F4.1 | has delim | has delim | split both, AND-match left+right | ✅ |

## process_full_message(char *data) — The Dispatcher

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

### Ordering Invariants (CRITICAL)

- **disable_command()** runs BEFORE the command-map scan → previous command's
  `on_disable` fires before any new matching
- **deactivate_layer()** runs BEFORE `activate_layer()` → exactly one notifier
  layer active at a time
- **First-match-wins** in each map (scan order = definition order)
- **Unmatched message clears state** — no enable/activate occurs, and
  disable+deactivate already ran

## Layer & Command State Machines

```c
void activate_layer(uint8_t layer);   // layer_on(layer); activated_layer = layer;
void deactivate_layer(void);          // if != LAYER_UNSET: layer_off; activated_layer = LAYER_UNSET
void enable_command(command_map_t *c);// current_command = c; c->on_enable();
void disable_command(void);           // if current_command && on_disable: call it; current_command = NULL
```

## rules.mk

```make
RAW_ENABLE = yes
SRC += qmk-notifier/notifier.c
```

`RAW_ENABLE` turns on QMK's Raw HID (usage page 0xFF60 / usage 0x61, 32-byte reports).
`SRC +=` compiles `notifier.c` (which itself `#include`s `pattern_match.c`).

User integrates via:
```make
include keyboards/<...>/<keyboard>/qmk-notifier/rules.mk
```
