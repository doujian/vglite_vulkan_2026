/* test_msaaSwitch.c - runtime MSAA 2x/4x switching smoke test.
 *
 * Exercises the full switch path (vg_lite_set_msaa_samples): pipeline /
 * render-pass invalidation, per-buffer MSAA attachment teardown, lazy
 * rebuild, and continued rendering across switches. Verifies by checking
 * that drawn content actually lands in the target after each switch.
 */

#include "vg_lite.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static vg_lite_buffer_t target;
static vg_lite_buffer_t src;

static int draw_frame(void)
{
    vg_lite_matrix_t m;
    vg_lite_identity(&m);
    if (vg_lite_clear(&target, NULL, 0xFF3366AA) != VG_LITE_SUCCESS) return 0;
    if (vg_lite_blit(&target, &src, &m,
                     VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT)
        != VG_LITE_SUCCESS) return 0;
    return vg_lite_finish() == VG_LITE_SUCCESS;
}

static int content_present(void)
{
    /* blit covers (0,0); clear color must be replaced by the source
     * color 0xFF44CC88 there */
    for (int y = 0; y < 64; y += 8)
        for (int x = 0; x < 64; x += 8)
            if (vg_lite_read_pixel(&target, x, y) == 0xFF3366AA) return 0;
    return 1;
}

int main(void)
{
    if (vg_lite_init(320, 480) != VG_LITE_SUCCESS) {
        printf("msaaSwitch test FAILED (init)\n");
        return 1;
    }
    target.width  = 320;
    target.height = 480;
    target.format = VG_LITE_BGRA8888;
    if (vg_lite_allocate(&target) != VG_LITE_SUCCESS) {
        printf("msaaSwitch test FAILED (alloc)\n");
        vg_lite_close();
        return 1;
    }

    /* 64x64 solid-color source (mapped LINEAR, fill directly) */
    memset(&src, 0, sizeof(src));
    src.width = 64; src.height = 64;
    src.format = VG_LITE_BGRA8888;
    if (vg_lite_allocate(&src) != VG_LITE_SUCCESS || !src.memory) {
        printf("msaaSwitch test FAILED (src alloc)\n");
        vg_lite_free(&target);
        vg_lite_close();
        return 1;
    }
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            *(uint32_t *)((uint8_t *)src.memory + (size_t)y * src.stride + x * 4)
                = 0xFF44CC88;

    /* 1. draw at default (4x) */
    if (!draw_frame()) { printf("msaaSwitch test FAILED (4x draw)\n"); goto fail; }
    if (!content_present()) { printf("msaaSwitch test FAILED (4x content)\n"); goto fail; }

    /* 2. switch to 2x at runtime and draw again */
    vg_lite_error_t e = vg_lite_set_msaa_samples(2);
    if (e != VG_LITE_SUCCESS) { printf("msaaSwitch test FAILED (set 2x: %d)\n", e); goto fail; }
    if (!draw_frame()) { printf("msaaSwitch test FAILED (2x draw)\n"); goto fail; }
    if (!content_present()) { printf("msaaSwitch test FAILED (2x content)\n"); goto fail; }

    /* 3. switch back to 4x */
    e = vg_lite_set_msaa_samples(4);
    if (e != VG_LITE_SUCCESS) { printf("msaaSwitch test FAILED (set 4x: %d)\n", e); goto fail; }
    if (!draw_frame()) { printf("msaaSwitch test FAILED (4x redraw)\n"); goto fail; }

    /* 4. invalid args rejected */
    if (vg_lite_set_msaa_samples(3) != VG_LITE_INVALID_ARGUMENT) {
        printf("msaaSwitch test FAILED (3x accepted)\n"); goto fail;
    }

    vg_lite_save_png("msaaSwitch_output.png", &target);
    printf("msaaSwitch test PASSED\n");
    vg_lite_free(&src);
    vg_lite_free(&target);
    vg_lite_close();
    return 0;

fail:
    vg_lite_free(&src);
    vg_lite_free(&target);
    vg_lite_close();
    return 1;
}
