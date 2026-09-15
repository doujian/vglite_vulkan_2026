/* Guards the multi-buffer pending-clear bug:
 *   g_pending_clear_buffer was a single global slot. Sequence
 *   clear(A); clear(B); finish();  materialized only B's clear (the slot
 *   was overwritten), silently dropping A's clear — readback of A returned
 *   stale pixels. Same for vg_lite_buffer_read_ptr(A) (it flushed the
 *   global slot, not A).
 *
 * Cases:
 *   1. fullscreen clear on A then B -> finish -> both must materialize.
 *   2. partial clear on A then fullscreen on B -> finish -> both must
 *      materialize (exercises the MSRTSS deferred-partial-clear fields).
 *   3. fullscreen clear on A then B -> read_ptr(A) *without* finish ->
 *      A must materialize (read_ptr flush path).
 */
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

static int case_fullscreen_fullscreen(void)
{
    vg_lite_buffer_t a, b;
    int fail = 0;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.width = b.width = 64; a.height = b.height = 64;
    a.format = b.format = VG_LITE_RGBA8888;
    a.tiled = b.tiled = VGLITE_TARGET_TILING;

    if (vg_lite_allocate(&a) || vg_lite_allocate(&b)) {
        printf("  allocate failed\n");
        return 1;
    }

    vg_lite_clear(&a, NULL, 0xFFFF0000);   /* A red */
    vg_lite_clear(&b, NULL, 0xFF0000FF);   /* B blue */
    vg_lite_finish();                      /* must materialize BOTH clears */

    {
        vg_lite_expected_buffer_t *ea = vg_lite_expected_create(a.width, a.height, a.format);
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(b.width, b.height, b.format);
        vg_lite_expected_clear(ea, NULL, 0xFFFF0000);
        vg_lite_expected_clear(eb, NULL, 0xFF0000FF);
        int fa = vg_lite_expected_verify(ea, &a, 0);
        int fb = vg_lite_expected_verify(eb, &b, 0);
        printf("  fullscreen/fullscreen: A %s (%d), B %s (%d)\n",
               fa ? "FAIL" : "PASS", fa, fb ? "FAIL" : "PASS", fb);
        fail += fa + fb;
        vg_lite_expected_destroy(ea);
        vg_lite_expected_destroy(eb);
    }

    vg_lite_free(&a); vg_lite_free(&b);
    return fail;
}

static int case_partial_fullscreen(void)
{
    vg_lite_buffer_t a, b;
    int fail = 0;
    vg_lite_rectangle_t rect = {8, 8, 32, 32};

    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.width = b.width = 64; a.height = b.height = 64;
    a.format = b.format = VG_LITE_RGBA8888;
    a.tiled = b.tiled = VGLITE_TARGET_TILING;

    if (vg_lite_allocate(&a) || vg_lite_allocate(&b)) {
        printf("  allocate failed\n");
        return 1;
    }

    vg_lite_clear(&a, NULL, 0xFF00FF00);   /* A green base */
    vg_lite_clear(&a, &rect, 0xFF0000FF);  /* A partial blue (deferred under MSRTSS) */
    vg_lite_clear(&b, NULL, 0xFFFFFF00);   /* B cyan */
    vg_lite_finish();                      /* must materialize all three */

    {
        vg_lite_expected_buffer_t *ea = vg_lite_expected_create(a.width, a.height, a.format);
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(b.width, b.height, b.format);
        vg_lite_expected_clear(ea, NULL, 0xFF00FF00);
        vg_lite_expected_clear(ea, &rect, 0xFF0000FF);
        vg_lite_expected_clear(eb, NULL, 0xFFFFFF00);
        int fa = vg_lite_expected_verify(ea, &a, 0);
        int fb = vg_lite_expected_verify(eb, &b, 0);
        printf("  partial/fullscreen: A %s (%d), B %s (%d)\n",
               fa ? "FAIL" : "PASS", fa, fb ? "FAIL" : "PASS", fb);
        fail += fa + fb;
        vg_lite_expected_destroy(ea);
        vg_lite_expected_destroy(eb);
    }

    vg_lite_free(&a); vg_lite_free(&b);
    return fail;
}

static int case_read_ptr_no_finish(void)
{
    vg_lite_buffer_t a, b;
    int fail = 0;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.width = b.width = 64; a.height = b.height = 64;
    a.format = b.format = VG_LITE_RGBA8888;
    a.tiled = b.tiled = VGLITE_TARGET_TILING;

    if (vg_lite_allocate(&a) || vg_lite_allocate(&b)) {
        printf("  allocate failed\n");
        return 1;
    }

    vg_lite_clear(&a, NULL, 0xFF00FFFF);   /* A yellow */
    vg_lite_clear(&b, NULL, 0xFFFF00FF);   /* B magenta */
    /* NO finish — read_ptr must flush A's pending clear itself */
    {
        const uint8_t *mem = (const uint8_t *)vg_lite_buffer_read_ptr(&a);
        int bad = 0;
        if (!mem) { printf("  read_ptr returned NULL\n"); fail++; }
        else {
            printf("  first px bytes: %02x %02x %02x %02x\n", mem[0], mem[1], mem[2], mem[3]);
            /* Memory word == vg_lite color literal (BGRA byte order):
             * 0xFF00FFFF (A=FF R=00 G=FF B=FF) -> bytes FF FF 00 FF */
            for (int i = 0; i < a.width * a.height; i++) {
                if (mem[i*4+0] != 0xFF || mem[i*4+1] != 0xFF ||
                    mem[i*4+2] != 0x00 || mem[i*4+3] != 0xFF) { bad++; }
            }
            vg_lite_buffer_read_ptr_release(&a);
        }
        printf("  read_ptr-no-finish: A %s (%d stale px)\n", bad ? "FAIL" : "PASS", bad);
        fail += bad;
    }

    vg_lite_free(&a); vg_lite_free(&b);
    return fail;
}

int main(void)
{
    printf("Initializing vg_lite...\n");
    if (vg_lite_init(0, 0) != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed\n");
        return 1;
    }

    printf("=== clear_multi Tests ===\n");
    int fail = 0;
    printf("Case: clear_multi_001 ::::::::::::: Started\n");
    fail += case_fullscreen_fullscreen();
    printf("Case: clear_multi_002 ::::::::::::: Started\n");
    fail += case_partial_fullscreen();
    printf("Case: clear_multi_003 ::::::::::::: Started\n");
    fail += case_read_ptr_no_finish();

    if (fail == 0) printf("\n=== Results: 3 passed, 0 failed ===\n");
    else           printf("\n=== Results: FAILED (%d)\n", fail);

    vg_lite_close();
    return (fail == 0) ? 0 : -1;
}
