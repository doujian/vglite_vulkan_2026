/*
 * test_blit_perf: A/B performance comparison between original fullscreen
 * blit triangle and AABB-optimized blit triangle.
 *
 * Phase 1: Correctness — blit same source to two targets with mode=0 and
 *          mode=1, verify pixel-identical output.
 * Phase 2: Performance — for multiple source sizes, measure GPU time
 *          for N blits in each mode using GPU timestamps.
 * Phase 3: Report [PERF] lines with ns measurements.
 *
 * Vulkan pipeline stage for timestamp: BOTTOM_OF_PIPE_BIT.
 */
#include <stdio.h>
#include <string.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

/* BOTTOM_OF_PIPE_BIT = 0x00002000 per Vulkan spec */
#define BOTTOM_OF_PIPE_BIT 0x00002000

#define TW 512
#define TH 512

static vg_lite_buffer_t target;
static vg_lite_buffer_t target_ref;

void cleanup(void) {
    if (target.handle) vg_lite_free(&target);
    if (target_ref.handle) vg_lite_free(&target_ref);
    vg_lite_close();
}

static void make_src(vg_lite_buffer_t *buf, int w, int h, uint32_t color) {
    memset(buf, 0, sizeof(*buf));
    buf->width = w; buf->height = h; buf->format = VG_LITE_BGRA8888;
    if (vg_lite_allocate(buf) != VG_LITE_SUCCESS) { memset(buf, 0, sizeof(*buf)); return; }
    uint32_t *p = (uint32_t*)buf->memory;
    for (int i = 0; i < w * h; i++) p[i] = color;
}

/* Test sizes: small to large source relative to 512x512 target */
static const int test_sizes[][2] = {
    { 32,  32 },   /* tiny:  0.4% of target */
    { 64,  64 },   /* small: 1.6% */
    {128, 128},    /* med:   6.3% */
    {256, 256},    /* large: 25% */
    {512, 512},    /* full:  100% */
};
#define NUM_SIZES (sizeof(test_sizes) / sizeof(test_sizes[0]))
#define BLIT_ITERATIONS 50

int main() {
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int fail = 0;

    CHECK_ERROR(vg_lite_init(TW, TH));

    /* Allocate targets */
    memset(&target, 0, sizeof(target));
    target.width = TW; target.height = TH; target.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&target));

    memset(&target_ref, 0, sizeof(target_ref));
    target_ref.width = TW; target_ref.height = TH; target_ref.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&target_ref));

    /* ===== Phase 1: Correctness Check ===== */
    printf("=== Phase 1: Correctness ===\n");
    {
        vg_lite_buffer_t src;
        make_src(&src, 256, 256, 0xFF4080C0);
        if (!src.handle) { error = VG_LITE_OUT_OF_MEMORY; goto ErrorHandler; }

        /* Mode 0: original fullscreen triangle */
        vg_lite_set_blit_aabb_mode(0);
        vg_lite_clear(&target_ref, NULL, 0xFF000000);
        {
            vg_lite_matrix_t m; vg_lite_identity(&m);
            vg_lite_blit(&target_ref, &src, &m, VG_LITE_BLEND_NONE,
                         0xFFFFFFFF, VG_LITE_FILTER_POINT);
        }
        vg_lite_finish();

        /* Mode 1: AABB triangle */
        vg_lite_set_blit_aabb_mode(1);
        vg_lite_clear(&target, NULL, 0xFF000000);
        {
            vg_lite_matrix_t m; vg_lite_identity(&m);
            vg_lite_blit(&target, &src, &m, VG_LITE_BLEND_NONE,
                         0xFFFFFFFF, VG_LITE_FILTER_POINT);
        }
        vg_lite_finish();

        /* Compare */
        uint32_t *p_ref = (uint32_t*)target_ref.memory;
        uint32_t *p_aabb = (uint32_t*)target.memory;
        int mismatch = 0;
        for (int i = 0; i < TW * TH; i++) {
            if (p_ref[i] != p_aabb[i]) mismatch++;
        }
        if (mismatch == 0)
            printf("Correctness: PASS (mode 0 and mode 1 pixel-identical)\n");
        else {
            printf("Correctness: FAIL (%d/%d pixels differ)\n", mismatch, TW * TH);
            printf("  ref pixel(0,0)   = 0x%08X\n", p_ref[0]);
            printf("  aabb pixel(0,0)  = 0x%08X\n", p_aabb[0]);
            fail = 1;
        }
        vg_lite_free(&src);
    }

    /* ===== Phase 2: Performance Measurement ===== */
    printf("\n=== Phase 2: Performance ===\n");
    printf("[PERF] format=BGRA8888 target=%dx%d iterations=%d\n", TW, TH, BLIT_ITERATIONS);

    for (int s = 0; s < (int)NUM_SIZES; s++) {
        int sw = test_sizes[s][0], sh = test_sizes[s][1];
        vg_lite_buffer_t src;
        make_src(&src, sw, sh, 0xFF4080C0);
        if (!src.handle) { error = VG_LITE_OUT_OF_MEMORY; goto ErrorHandler; }

        /* Measure mode 0 (original fullscreen) */
        vg_lite_set_blit_aabb_mode(0);
        vg_lite_clear(&target, NULL, 0xFF000000);
        {
            uint32_t start_slot = vg_lite_write_timestamp(BOTTOM_OF_PIPE_BIT);
            for (int i = 0; i < BLIT_ITERATIONS; i++) {
                vg_lite_matrix_t m; vg_lite_identity(&m);
                vg_lite_blit(&target, &src, &m, VG_LITE_BLEND_NONE,
                             0xFFFFFFFF, VG_LITE_FILTER_POINT);
            }
            uint32_t end_slot = vg_lite_write_timestamp(BOTTOM_OF_PIPE_BIT);
            vg_lite_finish();
            double elapsed = vg_lite_get_elapsed_ns(start_slot, end_slot);
            double per_blit = elapsed / BLIT_ITERATIONS;
            printf("[PERF] src=%dx%d mode=0 fullscreen  total=%.0f ns  per_blit=%.0f ns\n",
                   sw, sh, elapsed, per_blit);
        }

        /* Measure mode 1 (AABB optimized) */
        vg_lite_set_blit_aabb_mode(1);
        vg_lite_clear(&target, NULL, 0xFF000000);
        {
            uint32_t start_slot = vg_lite_write_timestamp(BOTTOM_OF_PIPE_BIT);
            for (int i = 0; i < BLIT_ITERATIONS; i++) {
                vg_lite_matrix_t m; vg_lite_identity(&m);
                vg_lite_blit(&target, &src, &m, VG_LITE_BLEND_NONE,
                             0xFFFFFFFF, VG_LITE_FILTER_POINT);
            }
            uint32_t end_slot = vg_lite_write_timestamp(BOTTOM_OF_PIPE_BIT);
            vg_lite_finish();
            double elapsed = vg_lite_get_elapsed_ns(start_slot, end_slot);
            double per_blit = elapsed / BLIT_ITERATIONS;
            printf("[PERF] src=%dx%d mode=1 aabb       total=%.0f ns  per_blit=%.0f ns\n",
                   sw, sh, elapsed, per_blit);
        }

        vg_lite_free(&src);
    }

    /* ===== Phase 3: Summary ===== */
    printf("\n=== Phase 3: Summary ===\n");
    printf("Correctness: %s\n", fail ? "FAIL" : "PASS");
    printf("Performance data emitted in [PERF] lines above.\n");
    printf("blit_perf %s\n", fail ? "FAILED" : "OK");

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
