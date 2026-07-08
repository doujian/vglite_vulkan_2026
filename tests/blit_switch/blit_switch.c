/*
 * test_blit_switch: 9 consecutive BLEND_NONE blits to target A (full
 * overwrite, each source is a different solid color), then a 10th blit
 * that copies A → B.
 *
 * Tests:
 * 1. RP reuse across 9 consecutive blits (same target, no flush)
 * 2. Target switch on 10th blit (A→B) triggers resolve + RP end/new
 * 3. B must contain A's accumulated content (= 9th blit's color)
 */
#include <stdio.h>
#include <string.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

#define TW 256
#define TH 256

static vg_lite_buffer_t target_a;
static vg_lite_buffer_t target_b;
static vg_lite_buffer_t src;

void cleanup(void) {
    if (src.handle) vg_lite_free(&src);
    if (target_a.handle) vg_lite_free(&target_a);
    if (target_b.handle) vg_lite_free(&target_b);
    vg_lite_close();
}

static void make_src(vg_lite_buffer_t *buf, uint32_t color) {
    memset(buf, 0, sizeof(*buf));
    buf->width = TW; buf->height = TH; buf->format = VG_LITE_BGRA8888;
    if (vg_lite_allocate(buf) != VG_LITE_SUCCESS) { memset(buf, 0, sizeof(*buf)); return; }
    uint32_t *p = (uint32_t*)buf->memory;
    for (int i = 0; i < TW*TH; i++) p[i] = color;
}

/* 9 distinct colors (BGRA LE). Each blit fully overwrites target A. */
static const uint32_t colors[9] = {
    0xFF0000FF, /* red    */
    0xFF00FF00, /* green  */
    0xFFFF0000, /* blue   */
    0xFFFFFF00, /* cyan   */
    0xFFFF00FF, /* magenta*/
    0xFF00FFFF, /* yellow */
    0xFF808080, /* gray   */
    0xFF40A0C0, /* teal   */
    0xFFC0A040, /* gold   */
};

int main() {
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int fail = 0;

    CHECK_ERROR(vg_lite_init(TW, TH));
    memset(&target_a, 0, sizeof(target_a));
    target_a.width = TW; target_a.height = TH; target_a.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&target_a));

    memset(&target_b, 0, sizeof(target_b));
    target_b.width = TW; target_b.height = TH; target_b.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&target_b));

    /* 9 consecutive BLEND_NONE blits to target_a.
     * Each overwrites fully; final color = colors[8] (gold 0xFFC0A040). */
    for (int i = 0; i < 9; i++) {
        make_src(&src, colors[i]);
        if (!src.handle) { error = VG_LITE_OUT_OF_MEMORY; goto ErrorHandler; }
        vg_lite_matrix_t m; vg_lite_identity(&m);
        CHECK_ERROR(vg_lite_blit(&target_a, &src, &m, VG_LITE_BLEND_NONE,
                                  0xFFFFFFFF, VG_LITE_FILTER_POINT));
        vg_lite_free(&src);
    }

    /* 10th blit: copy target_a → target_b (switch target). */
    vg_lite_matrix_t m2; vg_lite_identity(&m2);
    CHECK_ERROR(vg_lite_blit(&target_b, &target_a, &m2, VG_LITE_BLEND_NONE,
                              0xFFFFFFFF, VG_LITE_FILTER_POINT));

    CHECK_ERROR(vg_lite_finish());

    /* Verify: target_a should be gold (last overwrite) */
    {
        uint32_t expected = colors[8]; /* 0xFFC0A040 */
        uint32_t *p = (uint32_t*)target_a.memory;
        int mismatch = 0;
        for (int i = 0; i < TW*TH; i++) {
            if (p[i] != expected) mismatch++;
        }
        if (mismatch == 0)
            printf("target_a: PASS (gold 0x%08X after 9 blits)\n", expected);
        else
            printf("target_a: FAIL (%d/%d wrong, got 0x%08X exp 0x%08X)\n",
                   mismatch, TW*TH, p[0], expected);
        fail |= (mismatch > 0);
    }

    /* Verify: target_b should also be gold (copied from target_a) */
    {
        uint32_t expected = colors[8];
        uint32_t *p = (uint32_t*)target_b.memory;
        int mismatch = 0;
        for (int i = 0; i < TW*TH; i++) {
            if (p[i] != expected) mismatch++;
        }
        if (mismatch == 0)
            printf("target_b: PASS (gold 0x%08X after blit A→B)\n", expected);
        else
            printf("target_b: FAIL (%d/%d wrong, got 0x%08X exp 0x%08X)\n",
                   mismatch, TW*TH, p[0], expected);
        fail |= (mismatch > 0);
    }

    if (!fail)
        printf("blit_switch OK (9 blits to A, A→B copy correct)\n");

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
