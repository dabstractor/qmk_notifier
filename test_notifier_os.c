/* test_notifier_os.c — multi-OS map-selection (F8) + OS-change clearing (F9)
 * host test. Stub-compiles notifier.c and drives process_full_message +
 * notifier_set_os, covering the SIX PRD §11.2D categories:
 *
 *   (i)   OS-specific map selected + DEFAULT SKIPPED when current_os is set and
 *         the OS map matches — per track (command AND layer). [F8.4]
 *   (ii)  DEFAULT map used as fallback when (a) the OS map is ABSENT for
 *         current_os, (b) the OS map exists but matches NOTHING, or
 *         (c) current_os == OS_UNSURE. [F8.4 fallback / F8.6]
 *   (iii) command and layer tracks fall back INDEPENDENTLY: a message may match
 *         an OS layer but only a default command, or vice versa. [F8.5]
 *   (iv)  notifier_set_os IDEMPOTENT on an unchanged value — no spurious
 *         on_disable / deactivate. [F9.3]
 *   (v)   notifier_set_os on a CHANGED value clears state (prev on_disable fires,
 *         active layer deactivated) and does NOT re-dispatch. [F9.1 / F9.2]
 *   (vi)  a no-override OS (no DEFINE_*_OS for it) behaves identically with/
 *         without notifier_set_os — default maps still consulted (backward-compat).
 *         [invariant 19; the fully-default-only keymap is test_notifier_dispatch.c]
 *
 * Observation (findings F6): distinguishable callbacks (os_cmd_* vs def_cmd_*)
 * reveal WHICH command map matched; distinct layer numbers (OS: 11/44 vs default:
 * 22/33) + stub_get_active_layer() reveal WHICH layer won. Build (PRD §11.1):
 *   gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
 *       notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -std=c99
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"

/* Non-static entry points implemented in notifier.c (#includes pattern_match.c). */
bool  match_pattern(const char *pattern, const char *message, bool case_sensitive);
bool  process_full_message(char *data);
void  hid_notify(uint8_t *data, uint8_t length);
void  notifier_set_os(os_variant_t os);
uint8_t stub_get_active_layer(void);   /* test-harness observable in qmk_stubs.c */

/* --- distinguishable command callbacks (findings F6) --- */
static int os_cmd_en = 0, os_cmd_dis = 0, def_cmd_en = 0, def_cmd_dis = 0;
static void os_cmd_on(void)  { os_cmd_en++;  }
static void os_cmd_off(void) { os_cmd_dis++; }
static void def_cmd_on(void) { def_cmd_en++; }
static void def_cmd_off(void){ def_cmd_dis++; }

/* DEFAULT maps (OS-agnostic). Distinct layer numbers from the OS maps. */
DEFINE_SERIAL_COMMANDS({
    { "neovide",          def_cmd_on, def_cmd_off, false },   /* default-only cmd          */
    { WT("blender", "*"), def_cmd_on, def_cmd_off, false },   /* collides with OS cmd (i)  */
});
DEFINE_SERIAL_LAYERS({
    { "blender",   22, false },   /* default layer; OS layer for same msg = 11 */
    { "calculator", 33, false },  /* default-only layer                        */
});

/* OS_MACOS maps (strong overrides; symbol names via ##os paste in notifier.h). */
DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {
    { WT("blender", "*"), os_cmd_on, os_cmd_off, false },     /* collides with default cmd (i) */
});
DEFINE_SERIAL_LAYERS_OS(OS_MACOS, {
    { "blender", 11, false },   /* collides with default layer 22 (i) */
    { "iTerm",   44, false },   /* OS-only layer (iii)                */
});

static int g_pass = 0, g_fail = 0;
#define CK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS: %s\n", name); } \
    else      { g_fail++; printf("FAIL: %s\n", name); } \
} while (0)

static void reset_flags(void) {
    os_cmd_en = os_cmd_dis = def_cmd_en = def_cmd_dis = 0;
}

int main(void) {
    /* current_os boots to OS_UNSURE (notifier.c §8.1). Do the OS_UNSURE cases
     * FIRST, before any notifier_set_os call. */

    /* ===== (ii)c / F8.6: OS_UNSURE => OS map inert, default maps only =====
     * "iTerm" exists ONLY in the OS_MACOS layer map; at OS_UNSURE the OS map is
     * not consulted (select_*_map_os returns {NULL,0}) so it must NOT match. */
    {
        char m[] = "iTerm";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 0, "(ii)c OS_UNSURE: OS-only pattern (iTerm) does NOT match (OS map inert) [F8.6]");
        CK(os_cmd_en == 0 && def_cmd_en == 0, "(ii)c OS_UNSURE: no command fired for OS-only pattern [F8.6]");
    }
    {
        char m[] = "calculator";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(ii)c OS_UNSURE: default layer (calculator->33) matches [F8.6]");
        CK(stub_get_active_layer() == 33, "(ii)c OS_UNSURE: default layer 33 activated [F8.6]");
    }

    /* ===== switch to OS_MACOS (this also exercises F9.1 clear-on-change) ===== */
    {
        /* Pre-condition: layer 33 active from the calculator case above. */
        notifier_set_os(OS_MACOS);
        CK(stub_get_active_layer() == 255, "(v setup) notifier_set_os(OS_MACOS) deactivated prev layer (clear-on-change) [F9.1]");
    }

    /* ===== (i) OS map wins + DEFAULT SKIPPED — command track [F8.4] =====
     * "blender" matches BOTH the OS_MACOS cmd (os_cmd_on) AND the default cmd
     * (def_cmd_on). OS map scanned first => os_cmd fires, default NOT scanned. */
    {
        char m[] = "blender";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(i) cmd: blender matches (return true) [F8.4]");
        CK(os_cmd_en == 1, "(i) cmd: OS_MACOS command callback fired (os_cmd_on) [F8.4]");
        CK(def_cmd_en == 0, "(i) cmd: default command NOT scanned (OS match prevents default) [F8.4 core]");
    }

    /* ===== (i) OS map wins + DEFAULT SKIPPED — layer track [F8.4] =====
     * "blender" layer: OS_MACOS=11 vs default=22. OS wins => active layer 11. */
    {
        char m[] = "blender";
        reset_flags();
        process_full_message(m);
        CK(stub_get_active_layer() == 11, "(i) layer: OS_MACOS layer 11 won over default 22 (OS prevents default) [F8.4]");
    }

    /* ===== (iii) tracks fall back INDEPENDENTLY [F8.5] =====
     * "iTerm": OS_MACOS layer 44 fires (no default layer for iTerm => layer track
     *          resolved from OS) while the command track resolves to NOTHING
     *          (no OS cmd, no default cmd). The two tracks decided independently. */
    {
        char m[] = "iTerm";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(iii) iTerm matches (OS layer) [F8.5]");
        CK(stub_get_active_layer() == 44, "(iii) layer resolved from OS_MACOS map (44) [F8.5]");
        CK(os_cmd_en == 0 && def_cmd_en == 0, "(iii) command track decided independently (no cmd match) [F8.5]");
    }
    /* "neovide": default cmd fires (no OS cmd for neovide => command track fell
     *            back to default) while the layer track resolves to NOTHING. */
    {
        char m[] = "neovide";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(iii) neovide matches (default cmd) [F8.5]");
        CK(def_cmd_en == 1, "(iii) command resolved from DEFAULT map (def_cmd_on) [F8.5]");
        CK(os_cmd_en == 0, "(iii) OS cmd not matched for neovide [F8.5]");
        CK(stub_get_active_layer() == 255, "(iii) layer track decided independently (no layer match) [F8.5]");
    }

    /* ===== (ii)b DEFAULT fallback: OS map exists but matches NOTHING [F8.4] =====
     * "calculator": no OS_MACOS layer entry => OS layer scan misses => fall back
     * to default layer 33. (current_os is still OS_MACOS, so the OS map IS
     * consulted, it just matches nothing.) */
    {
        char m[] = "calculator";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(ii)b fallback: calculator matches via default layer [F8.4]");
        CK(stub_get_active_layer() == 33, "(ii)b fallback: OS no-match => default layer 33 activated [F8.4]");
    }

    /* ===== (iv) notifier_set_os IDEMPOTENT on unchanged value [F9.3] ===== */
    {
        char m[] = "blender";
        reset_flags();
        process_full_message(m);                 /* os_cmd_on fired, layer 11 active */
        CK(os_cmd_en == 1, "(iv) setup: os_cmd active");
        notifier_set_os(OS_MACOS);               /* SAME os => idempotent no-op */
        CK(os_cmd_dis == 0, "(iv) idempotent: on_disable NOT fired on same-OS call [F9.3]");
        CK(def_cmd_dis == 0, "(iv) idempotent: no spurious disable [F9.3]");
        CK(stub_get_active_layer() == 11, "(iv) idempotent: layer NOT cleared on same-OS call [F9.3]");
    }

    /* ===== (v) notifier_set_os on CHANGED value clears state + no re-dispatch [F9.1/F9.2] ===== */
    {
        char m[] = "blender";
        process_full_message(m);                 /* re-establish: os_cmd active, layer 11 */
        reset_flags();
        CK(stub_get_active_layer() == 11, "(v) setup: layer 11 active");
        notifier_set_os(OS_LINUX);               /* CHANGED os => clear state */
        CK(os_cmd_dis == 1, "(v) on-change: prev command on_disable fired [F9.1]");
        CK(stub_get_active_layer() == 255, "(v) on-change: active layer deactivated (cleared) [F9.1]");
        CK(os_cmd_en == 0 && def_cmd_en == 0, "(v) no-re-dispatch: on_enable NOT re-fired after OS change [F9.2]");
        CK(stub_get_active_layer() == 255, "(v) no-re-dispatch: no layer re-activated by notifier_set_os [F9.2]");
    }

    /* ===== (ii)a / (vi) no-override OS behaves default-only (backward-compat) [invariant 19] =====
     * OS_WINDOWS has NO DEFINE_*_OS in this TU, so its selectors return {NULL,0}
     * and dispatch uses the default maps — identical to never having called
     * set_os. This is the per-OS backward-compat guarantee; the fully-default-only
     * keymap (zero DEFINE_*_OS at all) is proven by test_notifier_dispatch.c (11/11). */
    {
        notifier_set_os(OS_WINDOWS);             /* no overrides for WINDOWS */
        char m[] = "blender";
        reset_flags();
        int r = process_full_message(m);
        CK(r == 1, "(vi)/(ii)a no-override OS: blender matches default cmd [invariant 19]");
        CK(def_cmd_en == 1, "(vi)/(ii)a no-override OS: DEFAULT command fired (no OS_WINDOWS map) [invariant 19]");
        CK(os_cmd_en == 0, "(vi)/(ii)a no-override OS: OS cmd map inert for WINDOWS [invariant 19]");
        CK(stub_get_active_layer() == 22, "(vi)/(ii)a no-override OS: default layer 22 (no OS_WINDOWS layer) [invariant 19]");
    }

    printf("\nTotal tests run: %d / passed: %d / failed: %d\n", g_pass + g_fail, g_pass, g_fail);
    return g_fail ? 1 : 0;
}