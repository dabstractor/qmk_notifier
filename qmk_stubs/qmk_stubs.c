#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Observable QMK symbol implementations for the host stub harness.
 * In production these are provided by QMK (action_layer.c, raw_hid.c). */
static uint8_t g_active_layer = 255;

/* Test-harness observable (NOT production); mirrors stub_get_active_layer
 * precedent (F6). Captures the full 32-byte raw_hid_send response so host
 * tests can assert the typed-response marker 0x51 ([0]), the cmd echo ([1]),
 * and payload bytes ([2..]) for QUERY_INFO/QUERY_CALLBACK/SET_OS/
 * APPLY_HOST_CONTEXT responses. */
static uint8_t g_last_response[32];

void layer_on(uint8_t layer) {
    g_active_layer = layer;
    fprintf(stderr, "[stub] layer_on(%u) -> active=%u\n", layer, g_active_layer);
}
void layer_off(uint8_t layer) {
    (void)layer;
    g_active_layer = 255;
    fprintf(stderr, "[stub] layer_off -> active=255\n");
}
void raw_hid_send(uint8_t *data, uint8_t length) {
    memcpy(g_last_response, data, (length < 32) ? length : 32);
    fprintf(stderr, "[stub] raw_hid_send response[0]=%u\n", data[0]);
}

/* Test-harness observable (NOT production code): exposes the file-static
 * g_active_layer so host tests (test_notifier_os.c) can assert WHICH layer
 * won. In a real QMK build layer_state provides this; the stub tracks it
 * locally. Added for the multi-OS test harness (findings F6). */
uint8_t stub_get_active_layer(void) { return g_active_layer; }

/* Test-harness observable (NOT production); mirrors stub_get_active_layer
 * precedent (F6). Returns a pointer to the last 32-byte raw_hid_send
 * response captured by raw_hid_send. Host tests read [0]=marker,
 * [1]=cmd echo, [2..]=payload. */
const uint8_t *stub_get_last_response(void) { return g_last_response; }
