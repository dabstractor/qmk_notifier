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
static void cb_mute_on(void)    { cb_mute_en++; }
static void cb_mute_off(void)   { cb_mute_dis++; }
static void cb_layout_on(void)  { cb_layout_en++; }
static void cb_layout_off(void) { cb_layout_dis++; }
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

    printf("\nTotal tests run: %d / passed: %d / failed: %d\n", g_pass + g_fail, g_pass, g_fail);
    return g_fail ? 1 : 0;
}