/****************************************************************************
 * test_uploadTiled — verify vg_lite_upload_buffer into a TILED destination.
 *
 * For each format {BGRA8888, RGBA8888, L8}:
 *   1. Fill a source byte array (odd user stride to exercise strided copy).
 *   2. Upload into a LINEAR buffer (existing path) and blit into target A.
 *   3. Upload the same data into a TILED buffer (texelFetch/imageStore
 *      compute path) and blit into target B (same clear, matrix, filter).
 *   4. Compare A and B readbacks pixel-exactly: tiled upload must produce
 *      the identical GPU-visible content as the linear upload.
 ****************************************************************************/
#include "vg_lite.h"
#include "vg_lite_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 96
#define H 64

static int run_case(vg_lite_buffer_format_t fmt, const char *name)
{
    uint32_t bpp = (fmt == VG_LITE_L8) ? 8 : (fmt == VG_LITE_RGB565) ? 16 : 32;
    uint32_t row_bytes = W * bpp / 8;
    uint32_t user_stride = row_bytes + 9; /* odd stride on purpose */
    uint32_t total = user_stride * H;

    uint8_t *src = malloc(total);
    if (!src) { printf("[%s] malloc failed\n", name); return 0; }
    for (uint32_t i = 0; i < total; i++)
        src[i] = (uint8_t)(i * 7 + (i / user_stride) * 13 + 0x5A);

    /* --- Linear reference --- */
    vg_lite_buffer_t lin = {0};
    lin.format = fmt;
    lin.width = W;
    lin.height = H;
    lin.tiled = VG_LITE_LINEAR;
    if (vg_lite_allocate(&lin) != VG_LITE_SUCCESS) {
        printf("[%s] linear allocate failed\n", name); free(src); return 0;
    }
    uint8_t *data[3] = {src, NULL, NULL};
    uint32_t stride[3] = {user_stride, 0, 0};
    if (vg_lite_upload_buffer(&lin, data, stride) != VG_LITE_SUCCESS) {
        printf("[%s] linear upload failed\n", name);
        vg_lite_free(&lin); free(src); return 0;
    }

    /* --- Tiled upload target --- */
    vg_lite_buffer_t tiled = {0};
    tiled.format = fmt;
    tiled.width = W;
    tiled.height = H;
    tiled.tiled = VG_LITE_TILED;
    if (vg_lite_allocate(&tiled) != VG_LITE_SUCCESS) {
        printf("[%s] tiled allocate failed\n", name);
        vg_lite_free(&lin); free(src); return 0;
    }
    if (tiled.memory != NULL) {
        printf("[%s] tiled buffer unexpectedly host-mapped\n", name);
        vg_lite_free(&tiled); vg_lite_free(&lin); free(src); return 0;
    }
    if (vg_lite_upload_buffer(&tiled, data, stride) != VG_LITE_SUCCESS) {
        printf("[%s] tiled upload failed\n", name);
        vg_lite_free(&tiled); vg_lite_free(&lin); free(src); return 0;
    }

    /* --- Blit both into linear BGRA targets and compare --- */
    vg_lite_buffer_t tgtA = {0}, tgtB = {0};
    int ok = 1;
    vg_lite_buffer_t *tgts[2] = {&tgtA, &tgtB};
    vg_lite_buffer_t *srcs[2] = {&lin, &tiled};
    for (int i = 0; i < 2; i++) {
        tgts[i]->format = VG_LITE_BGRA8888;
        tgts[i]->width = W;
        tgts[i]->height = H;
        if (vg_lite_allocate(tgts[i]) != VG_LITE_SUCCESS) {
            printf("[%s] target allocate failed\n", name);
            ok = 0; break;
        }
        if (vg_lite_clear(tgts[i], NULL, 0xFF000000) != VG_LITE_SUCCESS) { ok = 0; break; }
        vg_lite_matrix_t m;
        vg_lite_identity(&m);
        if (vg_lite_blit(tgts[i], srcs[i], &m, VG_LITE_BLEND_NONE, 0,
                VG_LITE_FILTER_LINEAR) != VG_LITE_SUCCESS) { ok = 0; break; }
        if (vg_lite_finish() != VG_LITE_SUCCESS) ok = 0;
    }
    if (ok && vg_lite_finish() != VG_LITE_SUCCESS) ok = 0;

    if (ok) {
        uint32_t mism = 0;
        /* Compare only actual pixel rows (stride padding may differ) */
        uint32_t px_bytes = W * 4;
        const uint8_t *a = tgtA.memory, *b = tgtB.memory;
        for (uint32_t y = 0; y < H; y++)
            for (uint32_t x = 0; x < px_bytes; x++)
                if (a[(size_t)y * tgtA.stride + x] != b[(size_t)y * tgtB.stride + x]) mism++;
        char pngA[128], pngB[128];
        snprintf(pngA, sizeof(pngA), "uploadTiled_%s_lin.png", name);
        snprintf(pngB, sizeof(pngB), "uploadTiled_%s_tiled.png", name);
        vg_lite_save_png(pngA, &tgtA);
        vg_lite_save_png(pngB, &tgtB);
        if (mism) {
            printf("[%s] FAILED: %u bytes differ between linear and tiled uploads (dumped %s / %s)\n",
                   name, mism, pngA, pngB);
            ok = 0;
        } else {
            printf("[%s] OK (tiled == linear, %ux%u)\n", name, W, H);
        }
    }

    vg_lite_free(&tgtA); vg_lite_free(&tgtB);
    vg_lite_free(&tiled); vg_lite_free(&lin);
    free(src);
    return ok;
}

int main(void)
{
    if (vg_lite_init(320, 480) != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed\n");
        return 1;
    }
    int pass = 0, total = 0;
    struct { vg_lite_buffer_format_t fmt; const char *name; } cases[] = {
        {VG_LITE_BGRA8888, "bgra8888"},
        {VG_LITE_RGBA8888, "rgba8888"},
        {VG_LITE_RGB565,   "rgb565"},
        {VG_LITE_L8,       "l8"},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        total++;
        pass += run_case(cases[i].fmt, cases[i].name);
    }
    vg_lite_close();
    printf("uploadTiled test %s (%d/%d)\n", pass == total ? "PASSED" : "FAILED",
           pass, total);
    return pass == total ? 0 : 1;
}
