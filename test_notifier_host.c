/* test_notifier_host.c — Host-Side Rules & Typed Commands (§4.6) host test.
 *
 * Stub-compiles notifier.c (P1.M2 implementation) and drives typed command
 * reports through the PUBLIC hid_notify entry, asserting the [0x51][cmd_id]
 * [payload…] responses (§4.6) via stub_get_last_response() (P1.M3.T1.S1).
 * Follows the EXACT pattern of test_notifier_dispatch.c / test_notifier_os.c
 * (file-scope DEFINE_*, CK helper, PASS:/FAIL:, summary, return g_fail?1:0);
 * the runner greps `grep -c '^FAIL:'`.
 *
 * This slice (P1.M3.T1.S2) gates the two READ-ONLY query handlers:
 *   (i)   QUERY_INFO (0x01) response layout — §4.6 capability handshake:
 *         [proto_ver][feature_flags][callback_count][board_rules_present].
 *   (ii)  has_been_queried — §4.6 handshake timing: QUERY_INFO bypasses
 *         process_full_message, so board state set by a prior legacy dispatch
 *         survives QUERY_INFO (even a second one). has_been_queried is a
 *         file-static with no accessor; assert its observable consequence.
 *   (iii) QUERY_CALLBACK (0x02) valid index — §4.6 name discovery:
 *         [index][name bytes, NUL-padded].
 *   (iv)  QUERY_CALLBACK out-of-range — §4.6 name absent: [index][0x00].
 * Siblings P1.M3.T1.S3 (SET_OS + APPLY_HOST_CONTEXT) and S3.T1.S4 (coexistence +
 * multi-report) APPEND blocks to this file; this task seeds the scaffolding.
 *
 * Build (PRD §11.1):
 *   gcc -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
 *       notifier.c qmk_stubs/qmk_stubs.c test_notifier_host.c -std=c99
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"

/* Non-static entry points implemented in notifier.c (which #includes pattern_match.c). */
void hid_notify(uint8_t *data, uint8_t length);
bool process_full_message(char *data);

/* Test-harness observables in qmk_stubs.c — MANUAL EXTERN (F6 convention; NOT a
 * header). stub_get_last_response ships with the parallel P1.M3.T1.S1. */
uint8_t        stub_get_active_layer(void);
const uint8_t *stub_get_last_response(void);

/* --- HOST callback registry (F6 distinguishable callbacks; S3-ready) ---
 * Two named entries so QUERY_INFO reports callback_count=2 and QUERY_CALLBACK
 * can discover both names. on_enable/on_disable set flags S3 (APPLY_HOST_CONTEXT)
 * will assert; here the flags also prove QUERY_* fired NO host callback. */
static int cb_mute_en = 0, cb_mute_dis = 0, cb_layout_en = 0, cb_layout_dis = 0;
/* Sequence stamps (P1.M3.T1.S3 callback-diff ordering test vii): plain counters
 * can't show disable-before-enable ORDER, so each callback records a monotonic
 * g_seq at call time. Backward-compatible — the _en/_dis counters STILL
 * increment, so S2's `cb_*_en==0` assertions after QUERY_* still hold (the _seq
 * vars stay 0 there). */
static int g_seq = 0;
static int cb_mute_on_seq = 0, cb_mute_off_seq = 0, cb_layout_on_seq = 0, cb_layout_off_seq = 0;
static void cb_mute_on(void)    { cb_mute_en++;    cb_mute_on_seq    = ++g_seq; }
static void cb_mute_off(void)   { cb_mute_dis++;   cb_mute_off_seq   = ++g_seq; }
static void cb_layout_on(void)  { cb_layout_en++;  cb_layout_on_seq  = ++g_seq; }
static void cb_layout_off(void) { cb_layout_dis++; cb_layout_off_seq = ++g_seq; }
DEFINE_HOST_CALLBACKS({
    { "mute",   cb_mute_on,  cb_mute_off  },
    { "layout", cb_layout_on, cb_layout_off },
});

/* --- BOARD maps (make board_rules_present==1; seed board state for (ii)) --- */
static int board_cmd_en = 0, board_cmd_dis = 0;
static void board_cmd_on(void)  { board_cmd_en++; }
static void board_cmd_off(void) { board_cmd_dis++; }
DEFINE_SERIAL_COMMANDS({
    { "neovide", board_cmd_on, board_cmd_off, false },
});
DEFINE_SERIAL_LAYERS({
    { "neovide", 5, false },
});

/* --- OS_MACOS maps (P1.M3.T1.S3 SET_OS OS-map-selection test ii) ---
 * "iTerm" exists ONLY in the OS_MACOS maps (not in the default "neovide" maps
 * above), so it matches the OS-specific map iff current_os==OS_MACOS. These maps
 * are inert until SET_OS flips current_os — exactly what test (ii) asserts.
 * Mirror S2's rows including the trailing `false` (case_sensitive) to avoid
 * -Wmissing-field-initializers under -Wextra. */
static int mac_cmd_en = 0, mac_cmd_dis = 0;
static void mac_cmd_on(void)  { mac_cmd_en++; }
static void mac_cmd_off(void) { mac_cmd_dis++; }
DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {
    { "iTerm", mac_cmd_on, mac_cmd_off, false },
});
DEFINE_SERIAL_LAYERS_OS(OS_MACOS, {
    { "iTerm", 44, false },
});

static int g_pass = 0, g_fail = 0;
#define CK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS: %s\n", name); } \
    else      { g_fail++; printf("FAIL: %s\n", name); } \
} while (0)

/* send a single-report typed command [0x81][0x9F][0xF0][cmd_id][args…][0x03],
 * drive it through hid_notify, and return the captured 32-byte response.
 * §4.6 ETX framing; single-report (all QUERY_* args fit in 30 payload bytes). */
static const uint8_t *send_typed(uint8_t cmd_id, const uint8_t *args, uint8_t nargs) {
    uint8_t rep[32];
    memset(rep, 0, sizeof(rep));
    rep[0] = 0x81; rep[1] = 0x9F; rep[2] = NOTIFY_CMD_DISCRIMINATOR; /* 0xF0 */
    rep[3] = cmd_id;
    for (uint8_t i = 0; i < nargs; i++) rep[4 + i] = args[i];
    rep[4 + nargs] = ETX_TERMINATOR[0];                              /* 0x03 */
    hid_notify(rep, 32);
    return stub_get_last_response();
}

int main(void) {
    /* ===== (i) QUERY_INFO response layout — §4.6 capability handshake ===== */
    {
        const uint8_t *r = send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);
        CK(r[0] == NOTIFY_RESPONSE_MARKER,                                              "(i) QUERY_INFO r[0]=0x51 marker [§4.6]");
        CK(r[1] == NOTIFY_CMD_QUERY_INFO,                                               "(i) QUERY_INFO r[1]=0x01 cmd echo [§4.6]");
        CK(r[2] == NOTIFY_PROTO_VER,                                                    "(i) QUERY_INFO r[2]=proto_ver=2 [§4.6]");
        CK((r[3] & NOTIFY_FEATURE_APPLY_HOST_CONTEXT) && (r[3] & NOTIFY_FEATURE_CALLBACK_REGISTRY), "(i) QUERY_INFO r[3]=feature_flags bits 0,1 set [§4.6]");
        CK(r[4] == 2,                                                                   "(i) QUERY_INFO r[4]=callback_count=2 [§4.6]");
        CK(r[5] == 1,                                                                   "(i) QUERY_INFO r[5]=board_rules_present=1 [§4.6]");
    }

    /* ===== (ii) has_been_queried — board state survives QUERY_INFO — §4.6 handshake timing =====
     * has_been_queried is a file-static set in the QUERY_INFO handler but never
     * read by any gate (no accessor). Its observable consequence: the typed path
     * bypasses process_full_message, so board state is untouched by QUERY_INFO. */
    {
        board_cmd_en = board_cmd_dis = 0;
        char m[] = "neovide";
        process_full_message(m);                              /* board layer 5 + board cmd */
        CK(stub_get_active_layer() == 5,                      "(ii) setup: legacy dispatch set board layer 5");
        CK(board_cmd_en == 1,                                 "(ii) setup: board command enabled");

        board_cmd_dis = 0;
        (void)send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);     /* 1st QUERY_INFO */
        CK(stub_get_active_layer() == 5,                      "(ii) 1st QUERY_INFO: board layer NOT cleared (typed path side-effect-free) [§4.6]");
        CK(board_cmd_dis == 0,                                "(ii) 1st QUERY_INFO: board command NOT disabled [§4.6]");

        (void)send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);     /* 2nd QUERY_INFO */
        CK(stub_get_active_layer() == 5,                      "(ii) 2nd QUERY_INFO: board layer still NOT cleared [§4.6 handshake timing]");
    }

    /* ===== (iii) QUERY_CALLBACK valid index — §4.6 name discovery ===== */
    {
        uint8_t idx0 = 0;
        const uint8_t *r0 = send_typed(NOTIFY_CMD_QUERY_CALLBACK, &idx0, 1);
        CK(r0[0] == NOTIFY_RESPONSE_MARKER,                  "(iii) QUERY_CALLBACK(0) r[0]=0x51 marker [§4.6]");
        CK(r0[1] == NOTIFY_CMD_QUERY_CALLBACK,               "(iii) QUERY_CALLBACK(0) r[1]=0x02 cmd echo [§4.6]");
        CK(r0[2] == 0,                                       "(iii) QUERY_CALLBACK(0) r[2]=index echo 0 [§4.6]");
        CK(r0[3]=='m' && r0[4]=='u' && r0[5]=='t' && r0[6]=='e', "(iii) QUERY_CALLBACK(0) r[3..]='mute' [§4.6]");
        CK(r0[7] == 0,                                       "(iii) QUERY_CALLBACK(0) name NUL-padded after 'mute' [§4.6]");

        uint8_t idx1 = 1;
        const uint8_t *r1 = send_typed(NOTIFY_CMD_QUERY_CALLBACK, &idx1, 1);
        CK(r1[0] == NOTIFY_RESPONSE_MARKER,                  "(iii) QUERY_CALLBACK(1) r[0]=0x51 marker [§4.6]");
        CK(r1[1] == NOTIFY_CMD_QUERY_CALLBACK,               "(iii) QUERY_CALLBACK(1) r[1]=0x02 cmd echo [§4.6]");
        CK(r1[2] == 1,                                       "(iii) QUERY_CALLBACK(1) r[2]=index echo 1 [§4.6]");
        CK(r1[3]=='l' && r1[4]=='a' && r1[5]=='y' && r1[6]=='o' && r1[7]=='u' && r1[8]=='t', "(iii) QUERY_CALLBACK(1) r[3..]='layout' [§4.6]");
        CK(r1[9] == 0,                                       "(iii) QUERY_CALLBACK(1) name NUL-padded after 'layout' [§4.6]");
    }

    /* ===== (iv) QUERY_CALLBACK out-of-range — §4.6 name absent =====
     * callback_count==2 ⇒ index 2 is out of range ⇒ [index][0x00]. */
    {
        uint8_t idx2 = 2;
        const uint8_t *r2 = send_typed(NOTIFY_CMD_QUERY_CALLBACK, &idx2, 1);
        CK(r2[0] == NOTIFY_RESPONSE_MARKER,                  "(iv) QUERY_CALLBACK(OOB) r[0]=0x51 marker [§4.6]");
        CK(r2[1] == NOTIFY_CMD_QUERY_CALLBACK,               "(iv) QUERY_CALLBACK(OOB) r[1]=0x02 cmd echo [§4.6]");
        CK(r2[2] == 2,                                       "(iv) QUERY_CALLBACK(OOB) r[2]=index echo 2 [§4.6]");
        CK(r2[3] == 0x00,                                    "(iv) QUERY_CALLBACK(OOB) r[3]=0x00 name absent [§4.6]");
    }

    /* ===== side-effect-free: QUERY_* fired NO host callback (read-only) — §4.6 =====
     * Only APPLY_HOST_CONTEXT (P1.M3.T1.S3) calls apply_host_callbacks. Reading
     * these flags also guarantees no unused-symbol warnings under -Wextra. */
    CK(cb_mute_en == 0 && cb_mute_dis == 0 && cb_layout_en == 0 && cb_layout_dis == 0,
                                                              "QUERY_INFO/QUERY_CALLBACK fired no host callback (read-only queries) [§4.6]");

    /* ================================================================ */
    /* ===== P1.M3.T1.S3 — SET_OS (0x03) contract blocks (i-iv) ======== */
    /* ================================================================ */
    /* RESOLVED (was BUG-1): SET_OS cmd_id 0x03 == ETX 0x03 and OS_MACOS==3 make the
     * os_byte ALSO collide. notifier.c now does length-aware typed reassembly
     * (typed_literal_remaining) so binary payload bytes equal to 0x03 are consumed
     * literally instead of terminating reassembly. SET_OS dispatches and changes
     * current_os; these four blocks now PASS per the §4.6/§4.7 contract. */

    /* ===== (ii-pre) OS_UNSURE baseline — run BEFORE any SET_OS — §4.7 / F8.6 =====
     * Now that the BUG-1 framing flaw is fixed, (i)'s SET_OS(OS_MACOS) legitimately
     * changes current_os. test_notifier_os.c establishes the pattern ("do the
     * OS_UNSURE cases FIRST, before any notifier_set_os call"), so the pre-SET_OS
     * baseline is checked here, before (i), to match its documented intent
     * ("Before SET_OS, current_os==OS_UNSURE"). The assertion text is unchanged. */
    {
        mac_cmd_en = mac_cmd_dis = 0;
        { char m[] = "iTerm"; process_full_message(m); }
        CK(mac_cmd_en == 0,                               "(ii) pre-SET_OS: OS_MACOS-only pattern does not match at OS_UNSURE [§4.7]");
    }

    /* ===== (i) SET_OS response layout — §4.6 ===== */
    {
        uint8_t os = OS_MACOS;
        const uint8_t *r = send_typed(NOTIFY_CMD_SET_OS, &os, 1);
        CK(r[0] == NOTIFY_RESPONSE_MARKER,                  "(i) SET_OS r[0]=0x51 marker [§4.6]");
        CK(r[1] == NOTIFY_CMD_SET_OS,                       "(i) SET_OS r[1]=0x03 cmd echo [§4.6]");
        CK(r[2] == 1,                                       "(i) SET_OS r[2]=ack=1 [§4.6]");
    }

    /* ===== (ii) SET_OS changes current_os (OS_MACOS map now selected) — §4.7 =====
     * "iTerm" matches ONLY the OS_MACOS map. After SET_OS(OS_MACOS) it fires
     * mac_cmd and selects OS_MACOS layer 44 — proving current_os changed and the
     * OS map is selected. (The OS_UNSURE baseline is checked in (ii-pre) above.) */
    {
        /* SET_OS(OS_MACOS) -> current_os=OS_MACOS */
        uint8_t os = OS_MACOS; (void)send_typed(NOTIFY_CMD_SET_OS, &os, 1);

        mac_cmd_en = mac_cmd_dis = 0;
        { char m[] = "iTerm"; process_full_message(m); }
        CK(mac_cmd_en == 1,                                 "(ii) post-SET_OS(OS_MACOS): OS_MACOS command fired (current_os changed) [§4.7]");
        CK(stub_get_active_layer() == 44,                   "(ii) post-SET_OS(OS_MACOS): OS_MACOS layer 44 selected [§4.7]");
    }

    /* ===== (iii) SET_OS change fires F9 clear — §4.7 / F9 =====
     * Mirror test_notifier_os.c (v): establish board state, SET_OS to a DIFFERENT
     * OS => prev command on_disable + layer deactivated + no re-dispatch. After
     * (ii) current_os==OS_MACOS; SET_OS(OS_LINUX) is a CHANGED os so the F9 clear
     * fires (disable_command + deactivate_layer) with no re-dispatch. */
    {
        board_cmd_en = board_cmd_dis = 0;
        { char m[] = "neovide"; process_full_message(m); }  /* board layer 5 + board cmd */
        CK(stub_get_active_layer() == 5,                    "(iii) setup: board layer 5 active");
        CK(board_cmd_en == 1,                               "(iii) setup: board command enabled");

        board_cmd_dis = 0;
        uint8_t os = OS_LINUX; (void)send_typed(NOTIFY_CMD_SET_OS, &os, 1);  /* CHANGED os => clear */
        CK(board_cmd_dis == 1,                              "(iii) SET_OS change: prev command on_disable fired [§4.7/F9.1]");
        CK(stub_get_active_layer() == 255,                  "(iii) SET_OS change: board layer deactivated (cleared) [§4.7/F9.1]");
        CK(board_cmd_en == 0 || board_cmd_en == 1,          "(iii) SET_OS change: no re-dispatch (on_enable not re-fired) [§4.7/F9.2]");
    }

    /* ===== (iv) SET_OS idempotent — §4.7 / F9.3 =====
     * Mirror test_notifier_os.c (iv): SET_OS to the SAME os => no spurious
     * on_disable, no layer change. */
    {
        board_cmd_dis = 0;
        uint8_t os = OS_LINUX; (void)send_typed(NOTIFY_CMD_SET_OS, &os, 1);  /* SAME os */
        CK(board_cmd_dis == 0,                              "(iv) SET_OS idempotent: no spurious on_disable on same-OS [§4.7/F9.3]");
        CK(stub_get_active_layer() == 255,                  "(iv) SET_OS idempotent: no layer change on same-OS [§4.7/F9.3]");
    }

    /* ================================================================ */
    /* ===== P1.M3.T1.S3 — APPLY_HOST_CONTEXT (0x05) blocks (v-viii) === */
    /* ================================================================ */
    /* The length-aware typed reassembly consumes AHC args (224/0/1/0xFF, ids 0/1)
     * literally, so all four blocks PASS. Gotcha: the stub models layers as a
     * SINGLE g_active_layer, so stub_get_active_layer()==224 in BOTH stack and
     * replace — the DISTINGUISHER is board_cmd_dis (0 vs 1). */

    /* ===== (v) STACK (clear_board=0): board preserved + host layer active — §14 ===== */
    {
        board_cmd_en = board_cmd_dis = 0;
        { char m[] = "neovide"; process_full_message(m); }  /* board layer 5 + board cmd */
        CK(stub_get_active_layer() == 5,                    "(v) setup: board layer 5 active");

        board_cmd_dis = 0;
        uint8_t a[] = { 224, 0x00, 0 };                     /* layer=224, flags=0 (clear_board=0), count=0 */
        (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 3);
        CK(stub_get_active_layer() == 224,                  "(v) stack: host layer 224 active (highest-layer-wins) [§14]");
        CK(board_cmd_dis == 0,                              "(v) stack: board command NOT torn down (clear_board=0) [§14]");
    }

    /* ===== (vi) REPLACE (clear_board=1): board torn down + host layer active — §14 ===== */
    {
        board_cmd_en = board_cmd_dis = 0;
        { char m[] = "neovide"; process_full_message(m); }  /* board layer 5 + board cmd */
        CK(stub_get_active_layer() == 5,                    "(vi) setup: board layer 5 active");

        board_cmd_dis = 0;
        uint8_t a[] = { 224, 0x01, 0 };                     /* layer=224, flags=0x01 (clear_board=1), count=0 */
        (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 3);
        CK(board_cmd_dis == 1,                              "(vi) replace: board command torn down (clear_board=1) [§14]");
        CK(stub_get_active_layer() == 224,                  "(vi) replace: host layer 224 active [§14]");
    }

    /* ===== (vii) callback diff ordering (disable-before-enable) — §14 =====
     * Enable id 0 only, then switch to id 1 only. Phase 1 disables id 0
     * (on_disable) BEFORE Phase 2 enables id 1 (on_enable) — observable via the
     * sequence stamps: cb_mute_off_seq==1, cb_layout_on_seq==2. */
    {
        /* enable id 0 only */
        g_seq = 0; cb_mute_en = cb_mute_dis = cb_layout_en = cb_layout_dis = 0;
        cb_mute_on_seq = cb_mute_off_seq = cb_layout_on_seq = cb_layout_off_seq = 0;
        { uint8_t a[] = { 224, 0x00, 1, 0 }; (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 4); }
        CK(cb_mute_en == 1,                                 "(vii) AHC{[0]}: on_enable fired for id 0 [§14]");

        /* switch to id 1 only -> id 0 disabled, id 1 enabled */
        g_seq = 0; cb_mute_on_seq = cb_mute_off_seq = cb_layout_on_seq = cb_layout_off_seq = 0;
        { uint8_t a[] = { 224, 0x00, 1, 1 }; (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 4); }
        CK(cb_mute_dis == 1,                                "(vii) AHC{[1]}: on_disable fired for id 0 [§14]");
        CK(cb_layout_en == 1,                               "(vii) AHC{[1]}: on_enable fired for id 1 [§14]");
        CK(cb_mute_off_seq == 1 && cb_layout_on_seq == 2,
                                                            "(vii) AHC{[1]}: on_disable(id0) BEFORE on_enable(id1) [§14 disable-before-enable]");
    }

    /* ===== (viii) APPLY_HOST_CONTEXT{layer=0xFF} clears host layer — §14 ===== */
    {
        /* establish a host layer with NO competing board layer (fresh: board is LAYER_UNSET) */
        { uint8_t a[] = { 224, 0x00, 0 }; (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 3); }
        CK(stub_get_active_layer() == 224,                  "(viii) setup: host layer 224 active");

        { uint8_t a[] = { 0xFF, 0x00, 0 }; (void)send_typed(NOTIFY_CMD_APPLY_HOST_CONTEXT, a, 3); }
        CK(stub_get_active_layer() == 255,                  "(viii) AHC{layer=0xFF}: host layer cleared (LAYER_UNSET) [§14]");
    }

    /* ================================================================ */
    /* ===== P1.M3.T1.S4 — COEXISTENCE / backward-compat (coexist-i/ii) = */
    /* ================================================================ */
    /* §4.6: "0xF0 can never begin a real matched string (sanitizer allows
     * only 0x20–0x7E), so a host that sends only legacy strings coexists
     * unchanged." §13 invariant 1: magic header is exactly 0x81 0x9F; the
     * coexistence guard checks data[0]==0x81 && data[1]==0x9F. */

    /* ===== (coexist-i) Legacy string coexists with typed path — §4.6 / §13 inv.1-2 / F5 =====
     * A legacy string (printable data[2], !=0xF0) is NOT routed to the typed
     * path: its ack is the match-bool 0/1, NOT 0x51. Proven both with a no-match
     * ("firefox"->0) and a match ("neovide"->1 + board side effects) so the
     * FULL legacy dispatch path is shown intact ALONGSIDE the typed path. */
    {
        /* (coexist-i)(a) "firefox" (no-match legacy string) */
        uint8_t rep[32]; memset(rep, 0, sizeof(rep));
        rep[0] = 0x81; rep[1] = 0x9F;                         /* §13 inv.1 magic header */
        rep[2] = 'f'; rep[3] = 'i'; rep[4] = 'r'; rep[5] = 'e'; /* data[2]='f'=0x66 (!=0xF0) */
        rep[6] = 'f'; rep[7] = 'o'; rep[8] = 'x'; rep[9] = ETX_TERMINATOR[0];
        hid_notify(rep, 32);
        const uint8_t *r = stub_get_last_response();
        CK(r[0] != NOTIFY_RESPONSE_MARKER, "(coexist-i)(a) legacy 'firefox' NOT routed to typed (r[0]!=0x51) [§4.6/F5]");
        CK(r[0] == 0,                      "(coexist-i)(a) legacy 'firefox' no-match ack=0 (process_full_message ran) [§13]");

        /* (coexist-i)(b) "neovide" (match legacy string — full side-effect proof) */
        board_cmd_en = board_cmd_dis = 0;
        memset(rep, 0, sizeof(rep));
        rep[0] = 0x81; rep[1] = 0x9F;
        rep[2] = 'n'; rep[3] = 'e'; rep[4] = 'o'; rep[5] = 'v'; rep[6] = 'i'; rep[7] = 'd'; rep[8] = 'e';
        rep[9] = ETX_TERMINATOR[0];
        hid_notify(rep, 32);
        const uint8_t *r2 = stub_get_last_response();
        CK(r2[0] != NOTIFY_RESPONSE_MARKER, "(coexist-i)(b) legacy 'neovide' NOT routed to typed (r[0]!=0x51) [§4.6/F5]");
        CK(r2[0] == 1,                      "(coexist-i)(b) legacy 'neovide' match ack=1 [§13]");
        CK(board_cmd_en == 1,               "(coexist-i)(b) legacy dispatch fired board on_enable (process_full_message intact) [§13]");
        CK(stub_get_active_layer() == 5,    "(coexist-i)(b) legacy dispatch activated board layer 5 [§13]");
    }

    /* ===== (coexist-ii) Non-magic report silently discarded — §13 invariant 1 =====
     * A non-magic report (data[0]!=0x81) is silently discarded: no raw_hid_send,
     * so stub_get_last_response() is UNCHANGED. First send a typed QUERY_INFO to
     * set a known response, capture it, then send the non-magic report and assert
     * the buffer is UNCHANGED (last-write-wins guard). */
    {
        const uint8_t *r0 = send_typed(NOTIFY_CMD_QUERY_INFO, NULL, 0);  /* known response */
        CK(r0[0] == NOTIFY_RESPONSE_MARKER, "(coexist-ii) setup: QUERY_INFO set a known typed response [§4.6]");
        uint8_t marker0 = r0[0], echo0 = r0[1];
        uint8_t bad[32]; memset(bad, 0x55, sizeof(bad));    /* data[0]=0x55 != 0x81 */
        hid_notify(bad, 32);                                 /* discarded BEFORE raw_hid_send */
        const uint8_t *r1 = stub_get_last_response();
        CK(r1[0] == marker0 && r1[1] == echo0,
                                       "(coexist-ii) non-magic report discarded: response UNCHANGED (no raw_hid_send) [§13 inv.1]");
    }

    /* ================================================================ */
    /* ===== P1.M3.T1.S4 — MULTI-REPORT typed framing (multi-rep) ====== */
    /* ================================================================ */
    /* §4.6: "Typed commands are ETX-framed and may span multiple 32-byte
     * reports." A typed APPLY_HOST_CONTEXT (0x05) split across two reports
     * reassembles + dispatches. The length-aware typed reassembly state
     * (typed_literal_remaining) is `static`, so it survives across hid_notify
     * calls exactly like msg_index — proving multi-report typed framing works
     * for the variable-length ids tail (count=28 spans two reports). */

    /* ===== (multi-rep) Two-report APPLY_HOST_CONTEXT reassembly — §4.6 =====
     * count=28 ids spans two reports (report1 holds 25 ids, report2 holds 3).
     * r[1]==0x05 is the load-bearing reassembly proof (report1's cmd_id
     * persisted into the reassembled buffer). */
    {
        cb_mute_en = cb_mute_dis = 0;
        /* Report 1 (32 B, NO ETX): [0x81][0x9F][0xF0][0x05][224][0][28][id0..id24] */
        uint8_t rep1[32]; memset(rep1, 0, sizeof(rep1));
        rep1[0] = 0x81; rep1[1] = 0x9F; rep1[2] = NOTIFY_CMD_DISCRIMINATOR;
        rep1[3] = NOTIFY_CMD_APPLY_HOST_CONTEXT;   /* 0x05 (chosen: != ETX 0x03) */
        rep1[4] = 224;                              /* layer (HOST_LAYER_BASE; 0xE0, !=0x03) */
        rep1[5] = 0x00;                             /* flags (clear_board=0) */
        rep1[6] = 28;                               /* count (0x1C, !=0x03) -> forces 2 reports */
        /* rep1[7..31] already 0 == id0..id24 (25 ids, all 0, none == 0x03) */
        hid_notify(rep1, 32);
        /* Report 2 (with ETX): [0x81][0x9F][id25][id26][id27][0x03] */
        uint8_t rep2[32]; memset(rep2, 0, sizeof(rep2));
        rep2[0] = 0x81; rep2[1] = 0x9F;
        /* rep2[2..4] already 0 == id25,id26,id27 */
        rep2[5] = ETX_TERMINATOR[0];               /* 0x03 terminates + dispatches */
        hid_notify(rep2, 32);
        const uint8_t *r = stub_get_last_response();
        CK(r[0] == NOTIFY_RESPONSE_MARKER,              "(multi-rep) two-report AHC r[0]=0x51 marker [§4.6]");
        CK(r[1] == NOTIFY_CMD_APPLY_HOST_CONTEXT,        "(multi-rep) two-report AHC r[1]=0x05 cmd echo (reassembly OK) [§4.6]");
        CK(r[2] == 1,                                    "(multi-rep) two-report AHC r[2]=ack=1 [§4.6]");
        CK(stub_get_active_layer() == 224,               "(multi-rep) two-report AHC host layer 224 active (set_host_layer ran) [§4.6]");
        CK(cb_mute_en == 1,                              "(multi-rep) two-report AHC callback diff ran (id 0 enabled once) [§4.6]");
    }

    printf("\nTotal tests run: %d / passed: %d / failed: %d\n", g_pass + g_fail, g_pass, g_fail);
    return g_fail ? 1 : 0;
}