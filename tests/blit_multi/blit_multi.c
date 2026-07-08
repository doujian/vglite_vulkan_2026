/*
 * test_blit_multi — verify 3 draws to the same target at different positions.
 *
 * Tests that deferred resolve+copy correctly preserves accumulated draw
 * content when multiple draws target the same buffer with a single finish.
 * This exercises the core optimization: N draws → 1 resolve at finish time.
 *
 * Scenario:
 *   1. clear target to gray
 *   2. draw red   triangle at top-left   (64, 64)
 *   3. draw green triangle at top-right  (192, 64)
 *   4. draw blue  triangle at center-bot (128, 192)
 *   5. finish (single resolve)
 *   6. Verify: 3 triangle centers have correct dominant color
 */

#include <stdio.h>
#include <string.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

#define TW 256
#define TH 256

static vg_lite_buffer_t target;

/* Triangle path data (S8): move_to, line_to, line_to, close */
static const int8_t tri[] = {
    2, -30, -30,
    4,  30, -30,
    4,   0,  30,
    0,
};

void cleanup(void)
{
    if (target.handle) vg_lite_free(&target);
    vg_lite_close();
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int fail = 0;

    CHECK_ERROR(vg_lite_init(TW, TH));
    memset(&target, 0, sizeof(target));

    target.width = TW; target.height = TH; target.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&target));

    /* Clear to gray (AABBGGRR: A=FF, B=20, G=20, R=20) */
    CHECK_ERROR(vg_lite_clear(&target, NULL, 0xFF202020));
    CHECK_ERROR(vg_lite_finish());

    /* Draw 1: red triangle at top-left (64, 64) */
    {
        vg_lite_path_t path;
        memset(&path, 0, sizeof(path));
        vg_lite_init_path(&path, VG_LITE_S8, VG_LITE_HIGH,
                          sizeof(tri), (void*)tri, -30, -30, 30, 30);
        vg_lite_matrix_t m;
        vg_lite_identity(&m);
        vg_lite_translate(64.0f, 64.0f, &m);
        CHECK_ERROR(vg_lite_draw(&target, &path, VG_LITE_FILL_EVEN_ODD,
                                 &m, VG_LITE_BLEND_SRC_OVER, 0xFF0000FF));
        vg_lite_clear_path(&path);
    }

    /* Draw 2: green triangle at top-right (192, 64) */
    {
        vg_lite_path_t path;
        memset(&path, 0, sizeof(path));
        vg_lite_init_path(&path, VG_LITE_S8, VG_LITE_HIGH,
                          sizeof(tri), (void*)tri, -30, -30, 30, 30);
        vg_lite_matrix_t m;
        vg_lite_identity(&m);
        vg_lite_translate(192.0f, 64.0f, &m);
        CHECK_ERROR(vg_lite_draw(&target, &path, VG_LITE_FILL_EVEN_ODD,
                                 &m, VG_LITE_BLEND_SRC_OVER, 0xFF00FF00));
        vg_lite_clear_path(&path);
    }

    /* Draw 3: blue triangle at center-bottom (128, 192) */
    {
        vg_lite_path_t path;
        memset(&path, 0, sizeof(path));
        vg_lite_init_path(&path, VG_LITE_S8, VG_LITE_HIGH,
                          sizeof(tri), (void*)tri, -30, -30, 30, 30);
        vg_lite_matrix_t m;
        vg_lite_identity(&m);
        vg_lite_translate(128.0f, 192.0f, &m);
        CHECK_ERROR(vg_lite_draw(&target, &path, VG_LITE_FILL_EVEN_ODD,
                                 &m, VG_LITE_BLEND_SRC_OVER, 0xFFFF0000));
        vg_lite_clear_path(&path);
    }

    /* Single finish — deferred resolve should handle all 3 draws */
    CHECK_ERROR(vg_lite_finish());

    /* Save debug image */
    vg_lite_save_png("blit_multi.png", &target);

    /* Verify: check center of each triangle area.
     * BGRA8888 LE: [B, G, R, A] as uint32 */
    {
        uint32_t *p = (uint32_t*)target.memory;
        int mismatch = 0;

        /* Red triangle center ~(64, 70): R > 200, G < 60, B < 60 */
        uint32_t px = p[70 * TW + 64];
        uint8_t r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
        if (!(r > 200 && g < 60 && b < 60)) {
            mismatch++;
            printf("  red check failed: R=%d G=%d B=%d\n", r, g, b);
        }

        /* Green triangle center ~(192, 70): G > 200 */
        px = p[70 * TW + 192];
        r = (px >> 16) & 0xFF; g = (px >> 8) & 0xFF; b = px & 0xFF;
        if (!(g > 200 && r < 60 && b < 60)) {
            mismatch++;
            printf("  green check failed: R=%d G=%d B=%d\n", r, g, b);
        }

        /* Blue triangle center ~(128, 198): B > 200 */
        px = p[198 * TW + 128];
        r = (px >> 16) & 0xFF; g = (px >> 8) & 0xFF; b = px & 0xFF;
        if (!(b > 200 && r < 60 && g < 60)) {
            mismatch++;
            printf("  blue check failed: R=%d G=%d B=%d\n", r, g, b);
        }

        /* Gray area (10, 200): should be gray 0x202020 */
        px = p[200 * TW + 10];
        if (px != 0xFF202020) {
            mismatch++;
            printf("  gray check failed: 0x%08X\n", px);
        }

        if (mismatch == 0)
            printf("blit_multi OK (3 triangles at different positions, colors correct)\n");
        else
            printf("blit_multi FAILED (%d checks failed)\n", mismatch);
        fail = (mismatch > 0) ? 1 : 0;
    }

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
