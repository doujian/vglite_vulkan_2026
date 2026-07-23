/*
 * test_blit_accum: consecutive native-blend blits to same target.
 * Each blit uses BLEND_NONE (full overwrite). Without proper resolve
 * between blits, earlier blit results are lost.
 *
 * After 3 overwrites, target should be entirely blue (last blit).
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
static vg_lite_buffer_t src_red, src_green, src_blue;

void cleanup(void) {
    if (src_red.handle) vg_lite_free(&src_red);
    if (src_green.handle) vg_lite_free(&src_green);
    if (src_blue.handle) vg_lite_free(&src_blue);
    if (target.handle) vg_lite_free(&target);
    vg_lite_close();
}

static void make_src(vg_lite_buffer_t *buf, uint32_t color) {
    memset(buf, 0, sizeof(*buf));
    buf->width = TW; buf->height = TH; buf->format = VG_LITE_BGRA8888;
    if (vg_lite_allocate(buf) != VG_LITE_SUCCESS) { memset(buf, 0, sizeof(*buf)); return; }
    uint32_t *p = (uint32_t*)buf->memory;
    for (int i = 0; i < TW*TH; i++) p[i] = color;
}

int main() {
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int fail = 0;

    CHECK_ERROR(vg_lite_init(TW, TH));
    memset(&target, 0, sizeof(target));
    target.width = TW; target.height = TH; target.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&target));

    /* Premultiplied alpha sources (engine uses premultiplied blend).
     * 50% opacity: A=128, color channel = full_color * 128/255 */
    make_src(&src_red,   0x80000080); /* A=128, R=128 (premult of R=255) */
    make_src(&src_green, 0x80008000); /* A=128, G=128 */
    make_src(&src_blue,  0x80800000); /* A=128, B=128 */
    if (!src_red.handle || !src_green.handle || !src_blue.handle) {
        error = VG_LITE_OUT_OF_MEMORY;
        goto ErrorHandler;
    }

    /* Clear target to black first */
    CHECK_ERROR(vg_lite_clear(&target, NULL, 0xFF000000));
    CHECK_ERROR(vg_lite_finish());

    /* 3 consecutive SRC_OVER blits with 50% alpha.
     * Hardware blend reads dst, so seed_msaa MUST bring forward the
     * previous blit's accumulated result into the MSAA surface.
     *
     * Expected (SRC_OVER on black, A=0.5):
     *   After red:   R=128
     *   After green: R=64,  G=128
     *   After blue:  R=32,  G=64,  B=128
     *
     * If seed_msaa reads stale/empty MSAA instead of accumulated target,
     * each blit blends with black instead of previous result. */
    /* 3 consecutive SRC_OVER blits with premultiplied 50% alpha.
     * Hardware blend reads dst, so seed_msaa MUST bring forward the
     * previous blit's accumulated result into the MSAA surface.
     *
     * Expected (premultiplied SRC_OVER, alpha=128/255≈0.502):
     *   After red:   R=128
     *   After green: R=63, G=128
     *   After blue:  R=31, G=63, B=128
     */
    {
        vg_lite_matrix_t m; vg_lite_identity(&m);
        CHECK_ERROR(vg_lite_blit(&target, &src_red, &m, VG_LITE_BLEND_SRC_OVER, 0x80000080, VG_LITE_FILTER_POINT));
    }
    {
        vg_lite_matrix_t m; vg_lite_identity(&m);
        CHECK_ERROR(vg_lite_blit(&target, &src_green, &m, VG_LITE_BLEND_SRC_OVER, 0x80008000, VG_LITE_FILTER_POINT));
    }
    {
        vg_lite_matrix_t m; vg_lite_identity(&m);
        CHECK_ERROR(vg_lite_blit(&target, &src_blue, &m, VG_LITE_BLEND_SRC_OVER, 0x80800000, VG_LITE_FILTER_POINT));
    }

    CHECK_ERROR(vg_lite_finish());

    /* Expected: B=128, G=63, R=31 (BGRA8888 LE: 0xFF803F1F) */
    {
        uint32_t *p = (uint32_t*)target.memory;
        uint32_t expected = 0xFF804020; /* A=255, B=128, G=64, R=32 */
        printf("  pixel(0,0) = 0x%08X (expected 0x%08X)\n", p[0], expected);
        int mismatch = 0;
        for (int i = 0; i < TW*TH; i++) {
            if (p[i] != expected) {
                mismatch++;
                if (mismatch == 1)         printf("  first wrong: got 0x%08X exp 0x%08X at (%d,%d)\n", p[i], expected, i%TW, i/TW);
            }
        }
        /* Sample grid for debugging */
        printf("  Sample grid (32px step):\n");
        for (int y = 0; y < TH; y += 32) {
            printf("  y=%3d: ", y);
            for (int x = 0; x < TW; x += 32)
                printf("%08X ", p[y*TW+x]);
            printf("\n");
        }
        /* Count unique values */
        uint32_t vals[256]; int counts[256]; int nunique = 0;
        for (int i = 0; i < TW*TH; i++) {
            int found = -1;
            for (int j = 0; j < nunique; j++) if (vals[j] == p[i]) { found = j; break; }
            if (found >= 0) counts[found]++;
            else if (nunique < 256) { vals[nunique] = p[i]; counts[nunique] = 1; nunique++; }
        }
        printf("  Unique values: %d\n", nunique);
        for (int j = 0; j < nunique; j++)
            printf("    0x%08X: %d pixels\n", vals[j], counts[j]);
        if (mismatch == 0)
            printf("blit_accum OK (single blend test passed)\n");
        else
            printf("blit_accum FAILED (%d/%d pixels wrong)\n", mismatch, TW*TH);
        fail = (mismatch > 0) ? 1 : 0;
    }

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
