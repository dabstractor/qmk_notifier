# Notifier Issue Analysis — notifier.c

## Area 1: sanitize_string (Issue 2)

### Location & signature
- Definition: notifier.c:46-69
- Signature: `static void sanitize_string(char *str)` — static, file-local, in-place two-pointer compaction

### The bug
Line 52: `while (*read_ptr)` — terminates at first NUL byte. NUL (0x00) is not
in the allowlist but the loop never reaches it, causing truncation instead of stripping.

### Allowlist (lines 55-61)
```c
if ((*read_ptr >= 32 && *read_ptr <= 126) ||
    *read_ptr == 9 ||   // tab
    *read_ptr == 10 ||  // newline
    *read_ptr == 13 ||  // carriage return
    *read_ptr == GS_DELIMITER[0] ||  // 0x1D
    *read_ptr == ETX_TERMINATOR[0])  // 0x03
```

### Call site (notifier.c:495, inside hid_notify)
```c
if (!dropping) {
    msg_buffer[msg_index] = '\0';
    sanitize_string(msg_buffer);
    match = process_full_message(msg_buffer);
}
```
`msg_index` is the count of payload bytes written. Available at the call site.

### Data flow
```
hid_notify(data)                     notifier.c:481+
  append each payload byte            (~510, NO byte filtering)
  on ETX (0x03):
    msg_buffer[msg_index] = '\0'     notifier.c:492
    sanitize_string(msg_buffer)      notifier.c:495   <-- BUG: NUL truncates
    process_full_message(msg_buffer) notifier.c:497
```

## Area 2: CONSOLE_ENABLE block (Issue 3)

### Location: notifier.c:417-434
```c
#ifdef CONSOLE_ENABLE
for (size_t i = 0; i < strlen(received_command); i++) {
    if (received_command[i] == GS_DELIMITER[0]) {
        received_command[i] = '|';
    }
}
if (command_found != NULL) {
    uprintf("Matched message %s on command: %s\n", received_command, command_found->pattern);
} else {
    uprintf("Did not match message %s on any command\n", received_command);
}
#endif
```

### Missing: layer-track print
Only COMMAND track is printed. `layer_found` (uint8_t, LAYER_UNSET=255 on miss)
is available in scope but not printed. PRD §8.6 step 9 says "per-track."

### Variables in scope (process_full_message, notifier.c:334+)
- `received_command[256]` — local buffer, GS→'|' substituted
- `command_found` — command_map_t* (NULL if miss)
- `layer_found` — uint8_t (LAYER_UNSET=255 if miss)