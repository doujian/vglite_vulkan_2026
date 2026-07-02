/*
 * test_blit_mixed — stress test interleaving allocate / blit / draw / draw_pattern.
 *
 * No vg_lite_clear used. Single vg_lite_finish at the end.
 * Buffers initialized via CPU memset.
 *
 * Sequence:
 *   allocate source1 (red, CPU) → blit target <- source1          (native RP opens)
 *   allocate source2 (green)     → blit target <- source2          (interrupts deferred RP)
 *   vg_lite_draw(target)                                          (flush blits -> MSAA RP)
 *   allocate source3 (blue)      → blit target <- source3          (flush MSAA -> no-MSAA RP)
 *   allocate source4 (yellow)    → blit target <- source4          (same target RP merge)
 *   vg_lite_draw_pattern(target)                                  (flush blits -> MSAA RP)
 *   allocate source5 (magenta)   → blit target <- source5          (flush pattern RP -> no-MSAA RP)
 *   allocate source6 (cyan)      → blit target <- source6          (same target RP merge)
 *   allocate target2             → blit target2 <- target          (target becomes source)
 *   finish
 *   verify target2 == target pixel-for-pixel, target == cyan
 */

#include <stdio.h>
#include <string.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

#define W 128
#define H 128

static vg_lite_buffer_t target;
static vg_lite_buffer_t target2;
static vg_lite_buffer_t src[6];

/* path data: simple triangle (S8: 2=move_to, 4=line_to, 0=close) */
static const int8_t tri_data[] = {
    2, -20, -20,
    4,  20, -20,
    4,   0,  20,
    0,
};

/* pattern path: rectangle */
static const int8_t rect_data[] = {
    2, -30, -30,
    4,  30, -30,
    4,  30,  30,
    4, -30,  30,
    0,
};

/* BGRA8888 colors as uint32 (little-endian: [B,G,R,A]) */
#define RED     0xFF0000FF
#define GREEN   0xFF00FF00
#define BLUE    0xFFFF0000
#define YELLOW  0xFF00FFFF
#define MAGENTA 0xFFFF00FF
#define CYAN    0xFFFFFF00

static void cpu_fill(vg_lite_buffer_t *buf, uint32_t color)
{
    uint32_t *p = (uint32_t*)buf->memory;
    for (int i = 0; i < buf->width * buf->height; i++)
        p[i] = color;
}

void cleanup(void)
{
    for (int i = 0; i < 6; i++)
        if (src[i].handle) vg_lite_free(&src[i]);
    if (target.handle) vg_lite_free(&target);
    if (target2.handle) vg_lite_free(&target2);
    vg_lite_close();
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int fail = 0;

    CHECK_ERROR(vg_lite_init(W, H));

    memset(src, 0, sizeof(src));
    memset(&target, 0, sizeof(target));
    memset(&target2, 0, sizeof(target2));

    /* Setup target: CPU fill dark gray */
    target.width = W; target.height = H; target.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&target));
    cpu_fill(&target, 0xFF202020);

    vg_lite_matrix_t id_mat;
    vg_lite_identity(&id_mat);

    /* --- allocate source1 (red) --- */
    src[0].width = W; src[0].height = H; src[0].format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&src[0]));
    cpu_fill(&src[0], RED);

    /* blit target <- source1 (native, opens no-MSAA RP on target) */
    CHECK_ERROR(vg_lite_blit(&target, &src[0], &id_mat,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* --- allocate source2 (green) --- */
    src[1].width = W; src[1].height = H; src[1].format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&src[1]));
    cpu_fill(&src[1], GREEN);

    /* blit target <- source2 (re-open no-MSAA RP) */
    CHECK_ERROR(vg_lite_blit(&target, &src[1], &id_mat,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* vg_lite_draw(target) — flush blits, switch to MSAA RP */
    {
        vg_lite_path_t path;
        memset(&path, 0, sizeof(path));
        vg_lite_init_path(&path, VG_LITE_S8, VG_LITE_HIGH,
                          sizeof(tri_data), (void*)tri_data,
                          -20, -20, 20, 20);
        vg_lite_matrix_t m;
        vg_lite_identity(&m);
        vg_lite_translate(W / 2.0f, H / 2.0f, &m);
        CHECK_ERROR(vg_lite_draw(&target, &path, VG_LITE_FILL_EVEN_ODD,
                                 &m, VG_LITE_BLEND_SRC_OVER, 0xFFFF00FF));
        vg_lite_clear_path(&path);
    }

    /* --- allocate source3 (blue) --- */
    src[2].width = W; src[2].height = H; src[2].format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&src[2]));
    cpu_fill(&src[2], BLUE);

    /* blit target <- source3 (flush MSAA -> no-MSAA RP) */
    CHECK_ERROR(vg_lite_blit(&target, &src[2], &id_mat,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* --- allocate source4 (yellow) --- */
    src[3].width = W; src[3].height = H; src[3].format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&src[3]));
    cpu_fill(&src[3], YELLOW);

    /* blit target <- source4 */
    CHECK_ERROR(vg_lite_blit(&target, &src[3], &id_mat,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* vg_lite_draw_pattern(target) — flush blits, switch to MSAA RP */
    {
        vg_lite_path_t ppath;
        memset(&ppath, 0, sizeof(ppath));
        vg_lite_init_path(&ppath, VG_LITE_S8, VG_LITE_HIGH,
                          sizeof(rect_data), (void*)rect_data,
                          -30, -30, 30, 30);

        vg_lite_matrix_t mat_path, mat_pat;
        vg_lite_identity(&mat_path);
        vg_lite_translate(W / 2.0f, H / 2.0f, &mat_path);
        vg_lite_identity(&mat_pat);
        vg_lite_scale(1.0f, 1.0f, &mat_pat);

        CHECK_ERROR(vg_lite_draw_pattern(&target, &ppath,
                     VG_LITE_FILL_EVEN_ODD, &mat_path,
                     &src[2], &mat_pat,
                     VG_LITE_BLEND_NONE,
                     VG_LITE_PATTERN_COLOR, 0xFFCCCCCC, 0,
                     VG_LITE_FILTER_POINT));
        vg_lite_clear_path(&ppath);
    }

    /* --- allocate source5 (magenta) --- */
    src[4].width = W; src[4].height = H; src[4].format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&src[4]));
    cpu_fill(&src[4], MAGENTA);

    /* blit target <- source5 (flush pattern RP -> no-MSAA RP) */
    CHECK_ERROR(vg_lite_blit(&target, &src[4], &id_mat,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* --- allocate source6 (cyan) --- */
    src[5].width = W; src[5].height = H; src[5].format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&src[5]));
    cpu_fill(&src[5], CYAN);

    /* blit target <- source6 */
    CHECK_ERROR(vg_lite_blit(&target, &src[5], &id_mat,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* --- allocate target2 --- */
    target2.width = W; target2.height = H; target2.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&target2));

    /* blit target2 <- target (target was render target, now is source) */
    CHECK_ERROR(vg_lite_blit(&target2, &target, &id_mat,
                             VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT));

    /* Single finish at the end */
    CHECK_ERROR(vg_lite_finish());

    /* Save debug images */
    vg_lite_save_png("blit_mixed_target.png", &target);
    vg_lite_save_png("blit_mixed_target2.png", &target2);

    /* Verify: target2 should match target pixel-for-pixel */
    {
        uint32_t *t  = (uint32_t*)target.memory;
        uint32_t *t2 = (uint32_t*)target2.memory;
        int mismatches = 0;
        for (int i = 0; i < W * H; i++) {
            if (t[i] != t2[i]) mismatches++;
        }
        if (mismatches == 0)
            printf("blit_mixed OK (target2 == target, all pixels match)\n");
        else
            printf("blit_mixed FAILED (%d/%d pixel mismatches)\n", mismatches, W * H);
        fail = (mismatches > 0) ? 1 : 0;
    }

    /* Also sanity-check: target should be cyan (source6) */
    {
        int mismatch = 0;
        uint32_t *t = (uint32_t*)target.memory;
        for (int i = 0; i < W * H; i++) {
            if (t[i] != CYAN) mismatch++;
        }
        if (mismatch == 0)
            printf("blit_mixed target == cyan (source6), OK\n");
        else
            printf("blit_mixed target has %d non-cyan pixels (expected 0)\n", mismatch);
    }

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
