/****************************************************************************
 * test_uploadBatch — verify vg_lite_upload_buffers (batch upload).
 *
 * Builds a mixed batch of buffers covering every internal path:
 *   - LINEAR BGRA8888 / RGB565 / L8   (direct mapped-memory writes)
 *   - TILED  BGRA8888 / RGB565 / L8   (batched compute dispatches or
 *                                      copy regions, decided per buffer)
 * All with odd user strides. After ONE vg_lite_upload_buffers call each
 * buffer is downloaded and compared row-by-row against the source data.
 ****************************************************************************/
#include "vg_lite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBUF 6

static const struct {
    vg_lite_buffer_format_t fmt;
    const char *name;
    uint32_t bpp;
    int tiled;
    uint32_t w, h;
} cases[NBUF] = {
    {VG_LITE_BGRA8888, "lin_bgra8888", 32, 0, 96, 64},
    {VG_LITE_RGB565,   "lin_rgb565",   16, 0, 95, 63},
    {VG_LITE_L8,       "lin_l8",        8, 0, 95, 63},
    {VG_LITE_BGRA8888, "til_bgra8888", 32, 1, 96, 64},
    {VG_LITE_RGB565,   "til_rgb565",   16, 1, 96, 64},
    {VG_LITE_L8,       "til_l8",        8, 1, 95, 63},
};

int main(void)
{
    if (vg_lite_init(320, 480) != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed\n");
        return 1;
    }

    vg_lite_buffer_t bufs[NBUF];
    vg_lite_uint8_t *datas[NBUF];
    vg_lite_uint32_t strides[NBUF];
    uint32_t row_bytes[NBUF], user_stride[NBUF];
    int ok = 1;

    for (int i = 0; i < NBUF; i++) {
        memset(&bufs[i], 0, sizeof(bufs[i]));
        bufs[i].format = cases[i].fmt;
        bufs[i].width = (int32_t)cases[i].w;
        bufs[i].height = (int32_t)cases[i].h;
        bufs[i].tiled = cases[i].tiled ? VG_LITE_TILED : VG_LITE_LINEAR;
        if (vg_lite_allocate(&bufs[i]) != VG_LITE_SUCCESS) {
            printf("[%s] allocate failed\n", cases[i].name);
            return 1;
        }
        row_bytes[i] = cases[i].w * cases[i].bpp / 8;
        user_stride[i] = row_bytes[i] + 7; /* odd stride on purpose */
        size_t total = (size_t)user_stride[i] * cases[i].h;
        datas[i] = (vg_lite_uint8_t *)malloc(total);
        if (!datas[i]) { printf("malloc failed\n"); return 1; }
        for (size_t k = 0; k < total; k++)
            datas[i][k] = (uint8_t)(k * 11 + i * 31 + (k / user_stride[i]) * 3 + 0x3C);
        strides[i] = user_stride[i];
    }

    /* THE batch call: one staging, one submit for all six buffers. */
    vg_lite_buffer_t *buf_ptrs[NBUF];
    for (int i = 0; i < NBUF; i++) buf_ptrs[i] = &bufs[i];
    vg_lite_error_t err = vg_lite_upload_buffers(buf_ptrs, datas, strides, NBUF);
    if (err != VG_LITE_SUCCESS) {
        printf("vg_lite_upload_buffers failed: %d\n", err);
        ok = 0;
    }

    /* Verify every buffer against its source rows. */
    for (int i = 0; ok && i < NBUF; i++) {
        size_t dl_size = (size_t)bufs[i].stride * cases[i].h;
        uint8_t *dl = (uint8_t *)malloc(dl_size);
        if (!dl) { printf("malloc failed\n"); ok = 0; break; }
        if (vg_lite_buffer_download(&bufs[i], dl) != VG_LITE_SUCCESS) {
            printf("[%s] download failed\n", cases[i].name);
            free(dl); ok = 0; break;
        }
        uint32_t mism = 0;
        for (uint32_t y = 0; y < cases[i].h; y++) {
            const uint8_t *exp = datas[i] + (size_t)y * user_stride[i];
            const uint8_t *got = dl + (size_t)y * bufs[i].stride;
            for (uint32_t x = 0; x < row_bytes[i]; x++)
                if (exp[x] != got[x]) mism++;
        }
        if (mism) {
            printf("[%s] FAILED: %u bytes differ\n", cases[i].name, mism);
            ok = 0;
        } else {
            printf("[%s] OK (%ux%u, stride %u->%u)\n", cases[i].name,
                   cases[i].w, cases[i].h, user_stride[i], bufs[i].stride);
        }
        free(dl);
    }

    for (int i = 0; i < NBUF; i++) {
        vg_lite_free(&bufs[i]);
        free(datas[i]);
    }
    vg_lite_close();
    printf("uploadBatch test %s\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
