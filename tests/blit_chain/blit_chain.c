/*
 * test_blit_chain — verify blit chain: same-target RP merge + target switch with
 * previous target as source.
 *
 * Scenario:
 *   1. buf_a = 128x128 BGRA8888, clear to red (0xFFFF0000)
 *   2. buf_b = 128x128 BGRA8888, clear to green (0xFF00FF00)
 *   3. buf_c = 128x128 BGRA8888, clear to blue (0xFF0000FF)
 *   4. blit buf_a -> buf_b  (BLEND_NONE, native, same target merge)
 *   5. blit buf_a -> buf_b  (again, tests RP reuse on same target)
 *   6. blit buf_b -> buf_c  (buf_b is source = previous target, tests barrier)
 *   7. finish, verify buf_c == red everywhere
 */

#include <stdio.h>
#include <string.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

static vg_lite_buffer_t buf_a;
static vg_lite_buffer_t buf_b;
static vg_lite_buffer_t buf_c;

void cleanup(void)
{
    if (buf_a.handle) vg_lite_free(&buf_a);
    if (buf_b.handle) vg_lite_free(&buf_b);
    if (buf_c.handle) vg_lite_free(&buf_c);
    vg_lite_close();
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int fail = 0;

    CHECK_ERROR(vg_lite_init(640, 480));

    vg_lite_matrix_t matrix;
    vg_lite_identity(&matrix);

    /* Allocate source buffer, clear to red */
    buf_a.width = 128; buf_a.height = 128; buf_a.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&buf_a));
    CHECK_ERROR(vg_lite_clear(&buf_a, NULL, 0xFFFF0000));  /* red */
    CHECK_ERROR(vg_lite_finish());

    /* Blit 1: buf_a -> buf_b — allocate buf_b just before blit, clear green */
    buf_b.width = 128; buf_b.height = 128; buf_b.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&buf_b));
    CHECK_ERROR(vg_lite_clear(&buf_b, NULL, 0xFF00FF00));  /* green */
    CHECK_ERROR(vg_lite_finish());
    CHECK_ERROR(vg_lite_blit(&buf_b, &buf_a, &matrix,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* Blit 2: buf_a -> buf_b again (same target, RP should merge) */
    CHECK_ERROR(vg_lite_blit(&buf_b, &buf_a, &matrix,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* Blit 3: buf_b -> buf_c — allocate buf_c just before blit, clear blue */
    buf_c.width = 128; buf_c.height = 128; buf_c.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&buf_c));
    CHECK_ERROR(vg_lite_clear(&buf_c, NULL, 0xFF0000FF));  /* blue */
    CHECK_ERROR(vg_lite_finish());
    CHECK_ERROR(vg_lite_blit(&buf_c, &buf_b, &matrix,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    CHECK_ERROR(vg_lite_finish());

    /* Save debug images */
    vg_lite_save_png("blit_chain_a.png", &buf_a);
    vg_lite_save_png("blit_chain_b.png", &buf_b);
    vg_lite_save_png("blit_chain_c.png", &buf_c);

    /* Verify: buf_c should be entirely red (buf_a's color) */
    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(
            buf_c.width, buf_c.height, buf_c.format);
        vg_lite_expected_clear(eb, NULL, 0xFF0000FF);  /* start blue */
        /* Simulate: buf_b = red (from buf_a), then buf_c = buf_b = red */
        vg_lite_expected_clear(eb, NULL, 0xFFFF0000);
        fail += vg_lite_expected_verify(eb, &buf_c, 0);
        vg_lite_expected_destroy(eb);
    }

    /* Also verify buf_b is red */
    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(
            buf_b.width, buf_b.height, buf_b.format);
        vg_lite_expected_clear(eb, NULL, 0xFFFF0000);
        fail += vg_lite_expected_verify(eb, &buf_b, 0);
        vg_lite_expected_destroy(eb);
    }

    if (fail == 0)
        printf("blit_chain OK (3 blits: A->B, A->B, B->C; C==red)\n");
    else
        printf("blit_chain FAILED (%d mismatches)\n", fail);

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
