/*
 * test_blit_draw: verify blit→blit→draw sequence correctness.
 *
 * Scenario:
 *   1. Clear target to black (0xFF000000)
 *   2. Blit a red source at (0,0) with BLEND_NONE
 *   3. Blit a blue source at (64,0) with BLEND_NONE
 *   4. Draw a filled rectangle path at (128,0) with solid green fill
 *   5. finish + save PNG
 *   6. Verify:
 *      - Left region (0-63):   red
 *      - Mid region  (64-127): blue
 *      - Draw region (128-191): green
 *      - Outside     (192+):   black
 *
 * This exercises the seed_msaa transition from blit RP to draw RP.
 */
#include <stdio.h>
#include <string.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

#define TW 256
#define TH 64

void cleanup(vg_lite_buffer_t *t, vg_lite_buffer_t *s1, vg_lite_buffer_t *s2) {
    if (s1->handle) vg_lite_free(s1);
    if (s2->handle) vg_lite_free(s2);
    if (t->handle)  vg_lite_free(t);
    vg_lite_close();
}

static void make_src(vg_lite_buffer_t *buf, int w, int h, uint32_t color) {
    memset(buf, 0, sizeof(*buf));
    buf->width = w; buf->height = h; buf->format = VG_LITE_BGRA8888;
    if (vg_lite_allocate(buf) != VG_LITE_SUCCESS) { memset(buf, 0, sizeof(*buf)); return; }
    uint32_t *p = (uint32_t*)buf->memory;
    for (int i = 0; i < w*h; i++) p[i] = color;
}

int main() {
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int fail = 0;

    CHECK_ERROR(vg_lite_init(TW, TH));

    vg_lite_buffer_t target = {0};
    target.width = TW; target.height = TH; target.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&target));

    vg_lite_buffer_t src_red = {0}, src_blue = {0};
    make_src(&src_red,  64, TH, 0xFF0000FF); /* BGRA: B=00 G=00 R=FF A=FF → red */
    make_src(&src_blue, 64, TH, 0xFFFF0000); /* BGRA: B=FF G=00 R=00 A=FF → blue */
    if (!src_red.handle || !src_blue.handle) {
        error = VG_LITE_OUT_OF_MEMORY;
        goto ErrorHandler;
    }

    /* Step 1: Clear target to black */
    CHECK_ERROR(vg_lite_clear(&target, NULL, 0xFF000000));
    CHECK_ERROR(vg_lite_finish());

    /* Step 2: Blit red source at (0,0) — full overwrite */
    {
        vg_lite_matrix_t m; vg_lite_identity(&m);
        vg_lite_translate(0.0f, 0.0f, &m);
        CHECK_ERROR(vg_lite_blit(&target, &src_red, &m, VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));
    }

    /* Step 3: Blit blue source at (64,0) — full overwrite */
    {
        vg_lite_matrix_t m; vg_lite_identity(&m);
        vg_lite_translate(64.0f, 0.0f, &m);
        CHECK_ERROR(vg_lite_blit(&target, &src_blue, &m, VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));
    }

    /* Step 4: Draw a filled rectangle (via path) at (128,0), size 64x64, green fill.
     * Use a simple rectangle path: move(0,0) line(64,0) line(64,64) line(0,64) close */
    {
        static char rect_path[] = {
            2, 0, 0,        /* move to (0,0) */
            4, 64, 0,       /* line to (64,0) */
            4, 64, 64,      /* line to (64,64) */
            4, 0, 64,       /* line to (0,64) */
            0,              /* close */
        };
        vg_lite_path_t path;
        memset(&path, 0, sizeof(path));
        vg_lite_init_path(&path, VG_LITE_S8, VG_LITE_HIGH, sizeof(rect_path), rect_path,
                          0, 0, 64, 64);

        vg_lite_matrix_t m; vg_lite_identity(&m);
        vg_lite_translate(128.0f, 0.0f, &m);
        CHECK_ERROR(vg_lite_draw(&target, &path, VG_LITE_FILL_EVEN_ODD, &m,
                                 VG_LITE_BLEND_SRC_OVER, 0xFF00FF00)); /* BGRA: B=00 G=FF R=00 A=FF → green */
    }

    CHECK_ERROR(vg_lite_finish());
    vg_lite_save_png("blit_draw.png", &target);

    /* Verification */
    {
        uint32_t *p = (uint32_t*)target.memory;
        int mismatch = 0;

        /* Expected colors (BGRA8888 LE) */
        uint32_t black = 0xFF000000;
        uint32_t red   = 0xFF0000FF;
        uint32_t blue  = 0xFFFF0000;
        uint32_t green = 0xFF00FF00;

        /* Check regions at y=0 (top row) and y=TH/2 (middle) */
        int check_rows[] = {0, TH/2};
        for (int ri = 0; ri < 2; ri++) {
            int y = check_rows[ri];
            for (int x = 0; x < TW; x++) {
                uint32_t got = p[y * (target.stride / 4) + x];
                uint32_t exp_val;
                if      (x < 64)   exp_val = red;
                else if (x < 128)  exp_val = blue;
                else if (x < 192)  exp_val = green;
                else               exp_val = black;

                if (got != exp_val) {
                    mismatch++;
                    if (mismatch <= 5)
                        printf("  MISMATCH at (%d,%d): got 0x%08X exp 0x%08X\n", x, y, got, exp_val);
                }
            }
        }

        if (mismatch == 0) {
            printf("blit_draw OK\n");
        } else {
            printf("blit_draw FAILED (%d pixels wrong)\n", mismatch);
            fail = 1;
        }
    }

    printf("Saved: blit_draw.png (%dx%d)\n", TW, TH);

ErrorHandler:
    cleanup(&target, &src_red, &src_blue);
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
