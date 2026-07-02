/*
 * test_blit_chain — verify blit chain: same-target RP merge + target switch with
 * previous target as source.
 *
 * No vg_lite_clear used. Single vg_lite_finish at the end.
 * Buffers initialized via CPU memset.
 *
 * Scenario:
 *   1. allocate buf_a=red, buf_b=green, buf_c=blue (CPU memset)
 *   2. blit buf_a -> buf_b  (BLEND_NONE, native, same target merge)
 *   3. blit buf_a -> buf_b  (again, tests RP reuse on same target)
 *   4. blit buf_b -> buf_c  (buf_b is source = previous target, tests barrier)
 *   5. finish
 *   6. verify buf_b == red, buf_c == red
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

    /* Allocate buf_a (source) and CPU-init to red */
    buf_a.width = 128; buf_a.height = 128; buf_a.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&buf_a));
    {
        uint32_t *p = (uint32_t*)buf_a.memory;
        for (int i = 0; i < 128 * 128; i++) p[i] = 0xFF0000FF; /* red */
    }

    /* Allocate buf_b just before first blit, CPU-init to green */
    buf_b.width = 128; buf_b.height = 128; buf_b.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&buf_b));
    {
        uint32_t *p = (uint32_t*)buf_b.memory;
        for (int i = 0; i < 128 * 128; i++) p[i] = 0xFF00FF00; /* green */
    }

    /* Blit 1: buf_a -> buf_b (native, opens no-MSAA RP on buf_b) */
    CHECK_ERROR(vg_lite_blit(&buf_b, &buf_a, &matrix,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* Blit 2: buf_a -> buf_b again (same target, RP should merge) */
    CHECK_ERROR(vg_lite_blit(&buf_b, &buf_a, &matrix,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* Allocate buf_c just before third blit, CPU-init to blue */
    buf_c.width = 128; buf_c.height = 128; buf_c.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&buf_c));
    {
        uint32_t *p = (uint32_t*)buf_c.memory;
        for (int i = 0; i < 128 * 128; i++) p[i] = 0xFFFF0000; /* blue */
    }

    /* Blit 3: buf_b -> buf_c (buf_b was target, now source — tests barrier) */
    CHECK_ERROR(vg_lite_blit(&buf_c, &buf_b, &matrix,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* Single finish at the end */
    CHECK_ERROR(vg_lite_finish());

    /* Save debug images */
    vg_lite_save_png("blit_chain_a.png", &buf_a);
    vg_lite_save_png("blit_chain_b.png", &buf_b);
    vg_lite_save_png("blit_chain_c.png", &buf_c);

    /* Verify: buf_b and buf_c should both be red (buf_a's color) */
    {
        uint32_t expect = 0xFF0000FF; /* red in BGRA8888 LE */
        int mismatch_b = 0, mismatch_c = 0;
        uint32_t *b = (uint32_t*)buf_b.memory;
        uint32_t *c = (uint32_t*)buf_c.memory;
        for (int i = 0; i < 128 * 128; i++) {
            if (b[i] != expect) mismatch_b++;
            if (c[i] != expect) mismatch_c++;
        }
        printf("buf_b: %d mismatches\n", mismatch_b);
        printf("buf_c: %d mismatches\n", mismatch_c);
        fail = (mismatch_b > 0 || mismatch_c > 0) ? 1 : 0;
        if (fail == 0)
            printf("blit_chain OK (B==red, C==red)\n");
        else
            printf("blit_chain FAILED\n");
    }

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
