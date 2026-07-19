// notifier.c
#include QMK_KEYBOARD_H

/*
 * Cap the Thompson NFA's processed-pattern budget for the MCU before pulling in
 * pattern_match.c. The host/test default (NFA_MAX_PATTERN 2048) is sized for the
 * multi-KB stress patterns but costs ~160 KB of stack per pattern_match() call —
 * a guaranteed stack smash on a few-KB MCU stack. PRD §7.9/§16 targets
 * NFA_MAX_PATTERN = 128 (~12 KB stack), which is fine on desktop and RP2040 and
 * leaves headroom for QMK's own stack frames. This MUST precede the #include so
 * pattern_match.c's `#ifndef NFA_MAX_PATTERN` guard picks it up silently.
 * (Fixes validation ISSUE-1 — the ~128 KB/call stack blowup on hardware.)
 */
#define NFA_MAX_PATTERN 128

#include "pattern_match.c"
#include "notifier.h"
#include "raw_hid.h"
#include <string.h>
#ifdef CONSOLE_ENABLE
#include "print.h"
#endif

/* Compile-time guard: if the override above was removed or shadowed, the host
 * default (2048) would silently take over and smash the MCU stack. This trips a
 * build error in that case so the regression cannot ship. (Validates ISSUE-1.) */
typedef char notifier_nfa_pattern_too_large_for_mcu[
    (NFA_MAX_PATTERN <= 128) ? 1 : -1];

/*
 * Logical size of a QMK Raw HID report exchanged with the host.
 *
 * This is 32 bytes on every QMK USB protocol — and is NOT the same as
 * RAW_EPSIZE (the USB interrupt *packet* size):
 *   - ChibiOS (STM32/RP2040/ATSAM) and LUFA (ATmega32U4): endpoint = 32,
 *     and send_raw_hid() guards on length == 32.
 *   - V-USB (low-speed AVR): endpoint = 8, but the driver reassembles a
 *     32-byte logical report and guards on length == 32, fragmenting it
 *     into 8-byte packets internally — passing 8 here would be rejected.
 * 32 is therefore the single value raw_hid_send() accepts on any board.
 */
#define RAW_REPORT_SIZE 32

// Function to sanitize strings by removing non-ASCII characters
// This prevents Unicode decode errors when the data is processed by the Python CLI
// Function to sanitize strings by removing non-ASCII characters
// This prevents Unicode decode errors when the data is processed by the Python CLI.
// Iterates by explicit length (PRD F2.3) so an embedded NUL (0x00) — which is not
// in the allowlist — is STRIPPED rather than truncating the scan at the first NUL.
static void sanitize_string(char *str, size_t len) {
    if (!str) return;

    char *write_ptr = str;

    // Length-bounded scan: read exactly `len` bytes. A NUL (0x00) is < 32 and is
    // not 9/10/13/GS/ETX, so it fails the allowlist and is skipped (stripped),
    // letting subsequent valid bytes through. (Previously `while (*read_ptr)`
    // stopped at the first NUL, truncating instead of stripping — bug §Issue 2.)
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        // Only allow printable ASCII (32-126) + essential control chars (9,10,13)
        // + our delimiter (GS) and terminator (ETX).
        if ((c >= 32 && c <= 126) ||
            c == 9 ||   // tab
            c == 10 ||  // newline
            c == 13 ||  // carriage return
            c == GS_DELIMITER[0] ||  // group separator (our delimiter)
            c == ETX_TERMINATOR[0]) { // end of text (our terminator)
            *write_ptr++ = c;
        }
        // Skip every other byte (incl. NUL 0x00, >127, other control chars).
    }

    // Null terminate the sanitized string at the write pointer (<= str + len).
    *write_ptr = '\0';
}

// Maximum size for the assembled command.
#define MSG_BUFFER_SIZE 256
// Buffer to accumulate incoming data.
static char msg_buffer[MSG_BUFFER_SIZE];
// Current write index into the buffer.
static uint16_t msg_index = 0;
// Overflow flag: once set, payload bytes are dropped until the next ETX clears a
// message boundary. PRD F2.2 requires an oversized message to be dropped in its
// entirety ("reset the index and drop the message"), not merely truncated and
// re-buffered. Without this, a >255-byte message splices a bogus suffix into a
// dispatched message and prefixes leftover bytes onto the next real message.
// (Fixes validation ISSUE-2 — spurious dispatch + cross-message state bleed.)
static bool dropping = false;

/* typed_mode — set on the first report of a message whose data[2] ==
 * NOTIFY_CMD_DISCRIMINATOR (0xF0), read at ETX to route to
 * handle_typed_command, and reset at every ETX/overflow boundary (RISK-1).
 * Persists across hid_notify calls for multi-report messages. */
static bool typed_mode = false;

// Default empty map if user doesn't define them
static command_map_t empty_command_map[1] = {0};
static layer_map_t empty_layer_map[1] = {0};

// Default implementations that will be used if the user doesn't call DEFINE_SERIAL_COMMANDS/LAYERS
__attribute__((weak)) command_map_t* get_command_map(void) {
    return empty_command_map;
}

__attribute__((weak)) size_t get_command_map_size(void) {
    return 0;
}

__attribute__((weak)) layer_map_t* get_layer_map(void) {
    return empty_layer_map;
}

__attribute__((weak)) size_t get_layer_map_size(void) {
    return 0;
}

// Weak-default host-callback registry accessors (§14). Overridden by
// DEFINE_HOST_CALLBACKS in the keymap. {NULL,0} ⇒ no registry ⇒ feature_flags
// bit 0x02 (NOTIFY_FEATURE_CALLBACK_REGISTRY) clear and callback_count = 0, so a
// keymap that omits DEFINE_HOST_CALLBACKS links and behaves identically to today.
__attribute__((weak)) host_callback_t* get_host_callbacks(void) { return NULL; }
__attribute__((weak)) size_t           get_host_callbacks_size(void) { return 0; }

#define LAYER_UNSET 255
uint8_t activated_layer = LAYER_UNSET;
// reference to currently active command:
command_map_t *current_command = {0};

/* The host OS used for multi-OS map selection (§2 F8). Pushed in by the keymap
 * via notifier_set_os(); never read from detected_host_os() directly (no link
 * dependency on the OS-detection subsystem). OS_UNSURE ⇒ default maps only
 * (invariant 17, §2 F8.2/F8.6). */
os_variant_t current_os = OS_UNSURE;

/* --- HOST STATE (a plane separate from board state; architecture invariant 21) --
 * Board state above (activated_layer / current_command / current_os) is driven by
 * the legacy string path (process_full_message). The host state below is driven by
 * typed commands (handle_typed_command, §14/§4.6) and is intentionally orthogonal:
 * process_full_message never touches it; handle_typed_command touches board state
 * only via clear_board (§14). */
static uint8_t host_layer = LAYER_UNSET;                       /* independent of board activated_layer (§14) */
static bool    host_cb_enabled[HOST_CALLBACK_MAX] = {false};   /* zero-init; diff target for apply_host_callbacks (§14) */
static bool    has_been_queried = false;                       /* set on first QUERY_INFO service (§4.6 handshake-timing rule) */

/* --- Per-OS weak accessors + selector (multi-OS overlay, §2 F8 / §8.3) --------
 * Each accessor returns {NULL, 0} ("no OS-specific map") UNLESS overridden by a
 * DEFINE_SERIAL_COMMANDS_OS / DEFINE_SERIAL_LAYERS_OS macro in the keymap. The
 * symbol names MUST match the ##os token-paste in notifier.h EXACTLY — e.g.
 * DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…) generates _notifier_get_command_map_OS_MACOS
 * and _notifier_get_command_map_OS_MACOS_size; a typo here = link failure.
 * select_*_map_os() dispatch by current_os; OS_UNSURE and any unexpected value
 * resolve to {NULL, 0} so the default map is used (§8.3). A size of 0 makes the
 * caller's scan loop run 0 iterations and fall through to the default map — this
 * IS the backward-compat guarantee (invariant 19), so no #ifdef is needed. */

/* command map, per OS — weak; overridden by DEFINE_SERIAL_COMMANDS_OS */
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_LINUX(void)   { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_LINUX_size(void)   { return 0; }
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_WINDOWS(void) { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_WINDOWS_size(void) { return 0; }
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_MACOS(void)   { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_MACOS_size(void)   { return 0; }
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_IOS(void)     { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_IOS_size(void)     { return 0; }

/* layer map, per OS — weak; overridden by DEFINE_SERIAL_LAYERS_OS */
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_LINUX(void)   { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_LINUX_size(void)   { return 0; }
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_WINDOWS(void) { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_WINDOWS_size(void) { return 0; }
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_MACOS(void)   { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_MACOS_size(void)   { return 0; }
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_IOS(void)     { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_IOS_size(void)     { return 0; }

/* Resolve the OS-specific command/layer map for `os`, or {NULL,0} if none.
 * Dispatch by current_os; OS_UNSURE / unexpected => {NULL,0} => default-map fallback (§8.3). */
static void select_command_map_os(os_variant_t os, command_map_t **map, size_t *size) {
    switch (os) {
        case OS_LINUX:   *map = _notifier_get_command_map_OS_LINUX();   *size = _notifier_get_command_map_OS_LINUX_size();   return;
        case OS_WINDOWS: *map = _notifier_get_command_map_OS_WINDOWS(); *size = _notifier_get_command_map_OS_WINDOWS_size(); return;
        case OS_MACOS:   *map = _notifier_get_command_map_OS_MACOS();   *size = _notifier_get_command_map_OS_MACOS_size();   return;
        case OS_IOS:     *map = _notifier_get_command_map_OS_IOS();     *size = _notifier_get_command_map_OS_IOS_size();     return;
        default:         *map = NULL; *size = 0; return;   /* OS_UNSURE / unexpected */
    }
}
static void select_layer_map_os(os_variant_t os, layer_map_t **map, size_t *size) {
    switch (os) {
        case OS_LINUX:   *map = _notifier_get_layer_map_OS_LINUX();   *size = _notifier_get_layer_map_OS_LINUX_size();   return;
        case OS_WINDOWS: *map = _notifier_get_layer_map_OS_WINDOWS(); *size = _notifier_get_layer_map_OS_WINDOWS_size(); return;
        case OS_MACOS:   *map = _notifier_get_layer_map_OS_MACOS();   *size = _notifier_get_layer_map_OS_MACOS_size();   return;
        case OS_IOS:     *map = _notifier_get_layer_map_OS_IOS();     *size = _notifier_get_layer_map_OS_IOS_size();     return;
        default:         *map = NULL; *size = 0; return;
    }
}

/* board_rules_present (§4.6 QUERY_INFO.board_rules_present) — true iff ANY board
 * map is non-empty. Checks the default command/layer maps AND every per-OS map.
 * The host only needs a single bit ("does this keymap use the notifier matcher at
 * all?"); per-OS granularity is deliberately NOT exposed (§4.6: "a single bit
 * suffices"). (findings_and_risks.md F8: must check ALL maps.) */
static bool board_rules_present(void) {
    if (get_command_map_size() > 0) return true;
    if (get_layer_map_size() > 0)   return true;
    if (_notifier_get_command_map_OS_LINUX_size() > 0)   return true;
    if (_notifier_get_layer_map_OS_LINUX_size() > 0)     return true;
    if (_notifier_get_command_map_OS_WINDOWS_size() > 0) return true;
    if (_notifier_get_layer_map_OS_WINDOWS_size() > 0)   return true;
    if (_notifier_get_command_map_OS_MACOS_size() > 0)   return true;
    if (_notifier_get_layer_map_OS_MACOS_size() > 0)     return true;
    if (_notifier_get_command_map_OS_IOS_size() > 0)     return true;
    if (_notifier_get_layer_map_OS_IOS_size() > 0)       return true;
    return false;
}

void activate_layer(uint8_t layer) {
    #ifdef CONSOLE_ENABLE
    uprintf("Activating layer %d\n", layer);
    #endif
    layer_on(layer);
    activated_layer = layer;
}

void deactivate_layer(void) {
    if (activated_layer == LAYER_UNSET) {
        return;
    }
    #ifdef CONSOLE_ENABLE
    uprintf("Deactivating layer %d\n", activated_layer);
    #endif
    layer_off(activated_layer);
    activated_layer = LAYER_UNSET;
}

/* set_host_layer (§14) — Host layer tracker ONLY. Operates on host_layer,
 * independent of the board activated_layer (architecture "two independent state
 * planes" / invariant 21). It wraps QMK layer_on/layer_off exactly as the board
 * activate_layer/deactivate_layer do, but for the HOST tracker:
 *   - layer == LAYER_UNSET (0xFF): clear the host layer (turn it off if one was
 *     set, then mark host_layer unset).
 *   - otherwise (a real host layer, reserved >= HOST_LAYER_BASE 224 per §14/§4.6
 *     so it resolves above board layers under QMK's highest-layer-wins rule):
 *     turn off the previous host layer first, then layer_on the new one.
 * This function touches ONLY host_layer — it NEVER reads or writes the board
 * activated_layer, and it does NOT check or clear board state (that is clear_board's
 * job in the APPLY_HOST_CONTEXT handler, P1.M2.T2.S3, which calls this). Per RISK-2
 * there is NO layer-range validation here: the >= 224 reservation is a host-side
 * convention the firmware trusts (no collision because host_layer and
 * activated_layer are distinct variables tracking distinct layer ranges). */
static void set_host_layer(uint8_t layer) {
    if (layer == LAYER_UNSET) {                 /* (a) clear the host layer */
        if (host_layer != LAYER_UNSET) {
            layer_off(host_layer);
        }
        host_layer = LAYER_UNSET;
    } else {                                    /* (b) set a real host layer (>= 224) */
        if (host_layer != LAYER_UNSET) {
            layer_off(host_layer);              /* turn off the old host layer first */
        }
        layer_on(layer);
        host_layer = layer;
    }
}

/* apply_host_callbacks (§14) — Host callback diff against host_cb_enabled[].
 * Given the host's FULL desired enabled id set (ids[0..count)), this mirrors the
 * board disable-before-enable ordering (§13 invariant 4): Phase 1 disables newly-
 * out-of-set ids (fires on_disable), THEN Phase 2 enables newly-in-set ids (fires
 * on_enable). Doing disables first prevents a callback being briefly in both
 * states during a transition (an id moving out of set A and into set B fires its
 * on_disable before any on_enable). Unchanged ids fire neither callback.
 *
 * Defensive id range check (RISK-3, findings_and_risks.md): malformed host data
 * must not crash the firmware, so both phases range-check ids against
 * get_host_callbacks_size() before dereferencing the registry, and Phase 2 also
 * checks id < HOST_CALLBACK_MAX (bounds for host_cb_enabled[]). Phase 1 still
 * clears host_cb_enabled[id] for an out-of-set id even if it is >= the registry
 * size (the array, not the dereference, is what we clear). NULL on_enable/
 * on_disable are guarded (same pattern as enable_command/disable_command). The
 * sole caller is the APPLY_HOST_CONTEXT handler (P1.M2.T2.S3). */
static void apply_host_callbacks(const uint8_t *ids, uint8_t count) {
    host_callback_t *cbs     = get_host_callbacks();     /* NULL when no DEFINE_HOST_CALLBACKS (weak default) */
    size_t           cb_size = get_host_callbacks_size(); /* 0 when no registry -> no derefs below (RISK-3) */

    /* PHASE 1 — DISABLE (newly-out-of-set): for each currently-enabled id NOT in
     * the desired set, fire on_disable then clear. Done BEFORE enables so a
     * callback is never briefly in both states (board disable-before-enable,
     * §13 invariant 4). */
    for (uint8_t id = 0; id < HOST_CALLBACK_MAX; id++) {
        if (!host_cb_enabled[id]) continue;
        bool still_desired = false;
        for (uint8_t i = 0; i < count; i++) {
            if (ids[i] == id) { still_desired = true; break; }
        }
        if (still_desired) continue;
        if (id < cb_size && cbs[id].on_disable != NULL) {   /* RISK-3 guard before deref */
            cbs[id].on_disable();
        }
        host_cb_enabled[id] = false;
    }

    /* PHASE 2 — ENABLE (newly-in-set): for each desired id not already enabled,
     * fire on_enable then set. Skips out-of-range ids (RISK-3) and ids already
     * enabled (the diff fires on_enable only for newly-in-set ids). */
    for (uint8_t i = 0; i < count; i++) {
        uint8_t id = ids[i];
        if (id >= HOST_CALLBACK_MAX) continue;             /* RISK-3: host_cb_enabled[] bounds */
        if (id >= cb_size) continue;                       /* RISK-3: registry bounds */
        if (host_cb_enabled[id]) continue;                 /* already enabled (diff) */
        if (cbs[id].on_enable != NULL) {                   /* NULL guard (enable_command pattern) */
            cbs[id].on_enable();
        }
        host_cb_enabled[id] = true;
    }
}

void enable_command(command_map_t *command) {
    current_command = command;
    // Guard against a NULL on_enable callback, mirroring the symmetric guard on
    // on_disable in disable_command(). A keymap entry with a NULL on_enable is a
    // user misconfiguration; crashing the keyboard (calling NULL()) is a poor
    // failure mode. on_enable is expected to be present (PRD §10.2 always sets
    // it), so we simply record the active command and skip the callback.
    // (Fixes validation ISSUE-3 — SIGSEGV on NULL on_enable.)
    if (command->on_enable != NULL) {
        command->on_enable();
    }
}

void disable_command(void) {
    if (current_command != NULL && current_command->on_disable != NULL) {
        current_command->on_disable();
    }
    current_command = NULL;
}

// Helper function to find the group separator (GS, 0x1D) in a string
const char* find_first_delimiter(const char *str) {
    for (const char *p = str; *p != '\0'; p++) {
        if (*p == GS_DELIMITER[0]) {  // Add any other delimiters here
            return p;
        }
    }
    return NULL;
}

// Helper function to split string by delimiter
bool split_by_delimiter(const char *source, const char *delimiter_pos,
                         char *left, size_t left_size,
                         char *right, size_t right_size) {
    if (!delimiter_pos) {
        return false;
    }

    // Split source
    size_t left_len = delimiter_pos - source;
    if (left_len >= left_size) {
        return false;
    }

    strncpy(left, source, left_len);
    left[left_len] = '\0';

    // Copy right part
    if (strlen(delimiter_pos + 1) >= right_size) {
        return false;
    }

    strcpy(right, delimiter_pos + 1);
    return true;
}

// Generic function for pattern matching with delimiter support
bool match_pattern(const char *pattern, const char *message, bool case_sensitive) {
    // NULL guard FIRST (PRD §8.5 step 2) — find_first_delimiter(pattern) below
    // would dereference a NULL pattern (it loops `for(p=str; *p; p++)`), so this
    // must precede any use of `pattern`. Fixes BUG-1 (former SIGSEGV on NULL).
    if (message == NULL || pattern == NULL) {
        return false;
    }

    const char *pattern_delimiter = find_first_delimiter(pattern);

    if (pattern_delimiter == NULL) {
        // No delimiter in pattern
        // But check if message has a delimiter
        const char *msg_delimiter_pos = find_first_delimiter(message);

        if (msg_delimiter_pos != NULL) {
            // Message has a delimiter but pattern doesn't
            // Match only against first part of message
            char msg_left[256] = {0};
            size_t left_len = msg_delimiter_pos - message;

            if (left_len >= sizeof(msg_left)) {
                return false;
            }

            strncpy(msg_left, message, left_len);
            msg_left[left_len] = '\0';

            // Match pattern against only first part of message
            bool result = pattern_match(pattern, msg_left, case_sensitive);
            return result;
        }

        // No delimiter in either string, use direct pattern matching
        bool result = pattern_match(pattern, message, case_sensitive);
        return result;
    }

    // Pattern contains a delimiter, check if message has the same delimiter
    char delimiter = *pattern_delimiter;
    char *msg_delimiter_pos = NULL;

    for (char *p = (char*)message; *p != '\0'; p++) {
        if (*p == delimiter) {
            msg_delimiter_pos = p;
            break;
        }
    }

    if (msg_delimiter_pos == NULL) {
        // Message doesn't have the delimiter
        // But we should still try to match the part before the delimiter
        char pattern_left[256] = {0};
        size_t left_len = pattern_delimiter - pattern;

        if (left_len >= sizeof(pattern_left)) {
            return false;
        }

        strncpy(pattern_left, pattern, left_len);
        pattern_left[left_len] = '\0';

        // Only match the first part of the pattern against the entire message
        return pattern_match(pattern_left, message, case_sensitive);
    }

    // Split both pattern and message
    char pattern_left[256] = {0};
    char pattern_right[256] = {0};
    char msg_left[256] = {0};
    char msg_right[256] = {0};

    if (!split_by_delimiter(pattern, pattern_delimiter,
                           pattern_left, sizeof(pattern_left),
                           pattern_right, sizeof(pattern_right))) {
        return false;
    }

    if (!split_by_delimiter(message, msg_delimiter_pos,
                          msg_left, sizeof(msg_left),
                          msg_right, sizeof(msg_right))) {
        return false;
    }

    // Match both sides
    return pattern_match(pattern_left, msg_left, case_sensitive) &&
           pattern_match(pattern_right, msg_right, case_sensitive);
}

bool process_full_message(char *data) {
    char received_command[256] = {0};
    int length = strlen(data);
    command_map_t *command_found = NULL;
    uint8_t layer_found = LAYER_UNSET;

    if ((size_t)length >= sizeof(received_command)) {
        return false;
    }

    memcpy(received_command, data, length);
    received_command[length] = '\0';

    // Always disable current command first (step 3 — disable-before-scan).
    disable_command();

    /* Resolve the maps for current_os (§8.6 step 2):
     *   - OS-specific maps come from the per-OS accessors (select_*_map_os);
     *     they return {NULL, 0} when current_os has no per-OS map (the boot
     *     state OS_UNSURE, or no DEFINE_*_OS macro for this OS). A NULL map /
     *     size 0 makes the OS scan below run 0 iterations.
     *   - Default maps come from the user's DEFINE_SERIAL_* (or the weak
     *     {empty_*_map, 0} defaults).
     * When no per-OS map exists, the OS scan is a 0-iteration no-op and the
     * default map is scanned — this IS the backward-compat guarantee
     * (invariant 19), so no #ifdef is needed. */
    command_map_t *os_cmd_map;   size_t os_cmd_size;
    layer_map_t   *os_layer_map; size_t os_layer_size;
    select_command_map_os(current_os, &os_cmd_map,   &os_cmd_size);
    select_layer_map_os  (current_os, &os_layer_map, &os_layer_size);
    command_map_t *def_cmd_map   = get_command_map();   size_t def_cmd_size   = get_command_map_size();
    layer_map_t   *def_layer_map = get_layer_map();     size_t def_layer_size = get_layer_map_size();

    /* COMMAND TRACK — OS-first, default-fallback, first-match-wins (§8.6 step 4).
     * OS-specific map scanned FIRST; a match wins and the default map for this
     * track is NOT scanned. No OS map (or no match in it) => scan the default
     * map (§2 F8.4). The two tracks decide independently (§2 F8.5). */
    for (size_t i = 0; i < os_cmd_size; i++) {
        if (match_pattern(os_cmd_map[i].pattern, received_command, os_cmd_map[i].case_sensitive)) {
            command_found = &os_cmd_map[i];
            break;
        }
    }
    if (command_found == NULL) {
        for (size_t i = 0; i < def_cmd_size; i++) {
            if (match_pattern(def_cmd_map[i].pattern, received_command, def_cmd_map[i].case_sensitive)) {
                command_found = &def_cmd_map[i];
                break;
            }
        }
    }

    /* LAYER TRACK — same rule, INDEPENDENT of the command track (§8.6 step 5 /
     * §2 F8.5): a layer may resolve from the OS map while a command resolves
     * from the default map, or vice versa. */
    for (size_t i = 0; i < os_layer_size; i++) {
        if (match_pattern(os_layer_map[i].pattern, received_command, os_layer_map[i].case_sensitive)) {
            layer_found = os_layer_map[i].layer;
            break;
        }
    }
    if (layer_found == LAYER_UNSET) {
        for (size_t i = 0; i < def_layer_size; i++) {
            if (match_pattern(def_layer_map[i].pattern, received_command, def_layer_map[i].case_sensitive)) {
                layer_found = def_layer_map[i].layer;
                break;
            }
        }
    }

    // Always deactivate the current layer first (step 6 — deactivate-before-activate).
    deactivate_layer();

    // Enable new command if found (step 7 — fires on_enable).
    if (command_found != NULL) {
        enable_command(command_found);
    }

    // Activate new layer if found (step 8 — layer_on).
    if (layer_found != LAYER_UNSET) {
        activate_layer(layer_found);
    }

    #ifdef CONSOLE_ENABLE
    // replace all group separators (GS) with '|' for console readability
    for (size_t i = 0; i < strlen(received_command); i++) {
        if (received_command[i] == GS_DELIMITER[0]) {
            received_command[i] = '|';
        }
    }

    /* DEBUG (step 9): print per-track match/miss. Use the pointer
     * command_found->pattern (already set to whichever entry — OS or default —
     * matched) rather than re-indexing by a single-map variable name; after the
     * split the matched map could be either (findings F2). GS is shown as '|'. */
    if (command_found != NULL) {
        uprintf("Matched message %s on command: %s\n", received_command, command_found->pattern);
    } else {
        uprintf("Did not match message %s on any command\n", received_command);
    }

    /* LAYER track (step 9 - mirrors the command track above): print the
     * matched layer index, or a miss. The index is sufficient for debugging;
     * printing the matched pattern would require capturing a layer_map_t*
     * pointer in the scan loops above, a dispatch change not warranted for a
     * Minor debug issue. GS was already substituted to '|' by the loop above. */
    if (layer_found != LAYER_UNSET) {
        uprintf("Matched message %s on layer: %d\n", received_command, layer_found);
    } else {
        uprintf("Did not match message %s on any layer\n", received_command);
    }
    #endif

    return command_found != NULL || layer_found != LAYER_UNSET;
}

/* apply_os_change — shared seam for OS changes from both the keymap
 * (notifier_set_os) and the host (SET_OS typed cmd). Sole mutation point for
 * current_os (invariant 17). Idempotent on an unchanged value (F9.3); clears
 * state on change (F9.1). Does NOT re-dispatch the last message (F9.2).
 *
 * Contract (§2 F9):
 *   - IDEMPOTENT on an unchanged value (no-op; F9.3): repeated stable-detection
 *     callbacks (e.g. macOS-on-ARM's delayed stability) do not flap state.
 *   - On a CHANGED value it CLEARS all notifier state before recording the new
 *     OS: disable_command() fires the previous command's on_disable if active,
 *     deactivate_layer() turns off the active notifier layer if any (F9.1). This
 *     guarantees no layer/command chosen under the previous OS's maps survives.
 *   - It does NOT re-dispatch the last message (F9.2): the next focus-change
 *     message from the host re-establishes state under the new maps. */
static void apply_os_change(os_variant_t os) {
    if (os == current_os) return;                 /* idempotent: no flap on repeat (F9.3) */
    #ifdef CONSOLE_ENABLE
    uprintf("notifier: OS %u -> %u; clearing state\n", (unsigned)current_os, (unsigned)os);
    #endif
    current_os = os;
    disable_command();      /* fires prev on_disable if a command was active (F9.1) */
    deactivate_layer();     /* turns off the active notifier layer if any (F9.1)    */
    /* Intentionally do NOT re-dispatch the last message. The next focus-change
     * message from the host re-establishes state under the new maps (F9.2). */
}

/* notifier_set_os — keymap entry point for the OS selector (§8.7). Delegates to
 * apply_os_change(), the sole mutation point for current_os (invariant 17): the
 * module never calls detected_host_os(), so there is no link dependency on the
 * OS-detection subsystem — the OS is PUSHED in by the keymap (conventionally
 * from process_detected_host_os_kb, §10.1 step 3).
 *
 * Symbol-name parity: the keymap's DEFINE_SERIAL_COMMANDS_OS(OS_MACOS,…) /
 * DEFINE_SERIAL_LAYERS_OS(OS_MACOS,…) macros (##os token-paste in notifier.h)
 * generate the strong _notifier_get_*_map_OS_MACOS[_size] symbols that override
 * the weak defaults; this function only flips current_os (via apply_os_change)
 * so the next dispatch's select_*_map_os() resolves the override. */
void notifier_set_os(os_variant_t os) {
    apply_os_change(os);
}

/* STUB — real handle_typed_command lands in P1.M2.T2 (typed response builder +
 * the four handlers). Present here so hid_notify's typed branch links. The real
 * version owns the [0x51] typed response (§4.6) and routes cmd_id to
 * QUERY_INFO/QUERY_CALLBACK/SET_OS/APPLY_HOST_CONTEXT. */
static bool handle_typed_command(char *buf) {
    (void)buf;
    return false;
}

void hid_notify(uint8_t *data, uint8_t length) {
    // Check for our identifiers to ensure no conflicts with other libraries
    if (length < 2 || data[0] != 0x81 || data[1] != 0x9F) {
        return; // Discard the message if it doesn't match
    }

    /* §4.6 typed-command discriminator: data[2] == 0xF0 routes to the typed
     * path (handle_typed_command), which bypasses process_full_message so it
     * has NO board disable/deactivate side effects. Checked ONLY on the first
     * report of a message (msg_index == 0): the discriminator is at data[2] in
     * the first report, but continuation reports carry payload there (which may
     * coincidentally be 0xF0). Legacy strings have a printable data[2]
     * (0x20-0x7E), never 0xF0, so this routing is transparent to them. */
    if (msg_index == 0 && length >= 3 && data[2] == NOTIFY_CMD_DISCRIMINATOR) {
        typed_mode = true;
    }

    // Strip off those 2 identifying characters
    data += 2;
    length -= 2;

    // Process each byte of the incoming packet.
    bool match = false;
    bool typed_dispatched = false;   /* true iff a typed msg was serviced on ETX this call */
    for (uint8_t i = 0; i < length; i++) {
        char c = (char)data[i];
        // End of text (ASCII 3) indicates the end of the message.
        if (c == ETX_TERMINATOR[0]) {
            // PRD F2.2: an oversized message (one that triggered `dropping`) is
            // dropped in its entirety — do NOT dispatch its partial buffer, and
            // do NOT let leftover bytes prefix the next message. Only dispatch
            // if we were accumulating a clean (non-overflowed) message.
            if (!dropping) {
                if (typed_mode) {
                    /* Typed path (§4.6): handle_typed_command owns the [0x51]
                     * response and bypasses process_full_message (no board
                     * disable/deactivate). NO sanitize — the typed payload is
                     * binary (cmd_id + args), not an ASCII string. */
                    match = handle_typed_command(msg_buffer);
                    typed_dispatched = true;   /* suppress the legacy 0/1 ack below */
                } else {
                    // Sanitize the buffer in place, iterating by explicit length so an
                    // embedded NUL is stripped (PRD F2.3) rather than truncating the scan.
                    // sanitize_string NUL-terminates at write_ptr (<= str + msg_index).
                    sanitize_string(msg_buffer, (size_t)msg_index);
                    match = process_full_message(msg_buffer);
                }
            }
            // Either way, the message boundary clears the overflow state so the
            // next message starts from a clean buffer.
            msg_index = 0; // Reset the buffer for the next message
            dropping = false;
            typed_mode = false;   /* RISK-1: reset at every ETX boundary */
            break;
        } else if (dropping) {
            // Mid-oversized-message: silently ignore all payload bytes until the
            // terminating ETX clears `dropping` above.
            continue;
        } else {
            // Append character if space is available.
            if (msg_index < (MSG_BUFFER_SIZE - 1)) {
                msg_buffer[msg_index++] = c;
            } else {
                // Buffer overflow – enter drop mode: reset the index and discard
                // the rest of this (oversized) message until ETX (PRD F2.2).
                msg_index = 0;
                dropping = true;
                typed_mode = false;   /* RISK-1: dropped msg clears typed classification */
            }
        }
    }
    /* Legacy acknowledgement only. Typed responses are sent INSIDE
     * handle_typed_command ([0x51]...), so hid_notify must NOT also send the
     * legacy 0/1 ack for typed commands (typed_dispatched guards it). */
    if (!typed_dispatched) {
        uint8_t response[RAW_REPORT_SIZE] = {0};
        response[0] = match;
        raw_hid_send(response, RAW_REPORT_SIZE);
    }
}
