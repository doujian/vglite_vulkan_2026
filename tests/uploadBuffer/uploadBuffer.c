/* uploadBuffer test — verifies vg_lite_upload_buffer (Vulkan compute shader).
 *
 * For each format, source rows are packed with a deliberately odd user
 * stride (row_bytes + 13) so the GPU strided-scatter path is exercised.
 * After upload, the mapped image memory is compared row by row against
 * the source data (row_bytes only; pitch padding is ignored).
 */
#include "vg_lite.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    vg_lite_buffer_format_t format;
    uint32_t bpp;
    uint32_t width, height;
} testcase_t;

static void fill_source(uint8_t *row, uint32_t row_bytes, uint32_t y)
{
    for (uint32_t i = 0; i < row_bytes; i++)
        row[i] = (uint8_t)(i * 7 + y * 13 + 0x5A);
}

int main(void)
{
    testcase_t cases[] = {
        {"BGRA8888", VG_LITE_BGRA8888, 32, 128, 96},
        {"RGB565",   VG_LITE_RGB565,   16, 127, 95},  /* odd width: row_bytes % 4 != 0 */
        {"L8",       VG_LITE_L8,        8, 127, 95},  /* odd width: row_bytes % 4 != 0 */
    };
    int failed = 0;

    vg_lite_error_t err = vg_lite_init(320, 480);
    if (err != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed: %d\n", err);
        return 1;
    }

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        testcase_t *tc = &cases[c];
        printf("case %zu: %s %ux%u bpp=%u\n", c, tc->name, tc->width, tc->height, tc->bpp);

        vg_lite_buffer_t buf;
        memset(&buf, 0, sizeof(buf));
        buf.format = tc->format;
        buf.width = tc->width;
        buf.height = tc->height;
        err = vg_lite_allocate(&buf);
        if (err != VG_LITE_SUCCESS) {
            printf("  vg_lite_allocate(%s) failed: %d\n", tc->name, err);
            failed++;
            continue;
        }

        uint32_t row_bytes = (tc->width * tc->bpp + 7) / 8;
        uint32_t user_stride = row_bytes + 13; /* odd stride to force strided copy */
        uint8_t *src = malloc((size_t)user_stride * tc->height);
        for (uint32_t y = 0; y < tc->height; y++)
            fill_source(src + (size_t)y * user_stride, row_bytes, y);

        vg_lite_uint8_t *data[3] = {src, NULL, NULL};
        vg_lite_uint32_t strides[3] = {user_stride, 0, 0};
        err = vg_lite_upload_buffer(&buf, data, strides);
        if (err != VG_LITE_SUCCESS) {
            printf("  vg_lite_upload_buffer(%s) failed: %d\n", tc->name, err);
            free(src);
            vg_lite_free(&buf);
            failed++;
            continue;
        }

        /* buf.memory is host-mapped and upload waited on the fence */
        uint32_t bad = 0;
        for (uint32_t y = 0; y < tc->height && bad < 4; y++) {
            const uint8_t *dst = (const uint8_t *)buf.memory + (size_t)y * buf.stride;
            if (memcmp(dst, src + (size_t)y * user_stride, row_bytes) != 0) {
                printf("  row %u mismatch (stride=%u)\n", y, buf.stride);
                for (uint32_t b = 0; b < row_bytes && bad < 4; b++) {
                    if (dst[b] != src[(size_t)y * user_stride + b]) {
                        printf("    byte %u: got 0x%02X want 0x%02X\n", b, dst[b],
                               src[(size_t)y * user_stride + b]);
                        bad++;
                    }
                }
                if (!bad) bad = 1;
            }
        }

        if (bad == 0)
            printf("  %s upload PASSED (row_bytes=%u user_stride=%u dst_stride=%u)\n",
                   tc->name, row_bytes, user_stride, buf.stride);
        else
            failed++;

        /* also blit the uploaded buffer to sanity-check GPU-side visibility */
        {
            vg_lite_buffer_t target;
            memset(&target, 0, sizeof(target));
            target.format = VG_LITE_BGRA8888;
            target.width = 320;
            target.height = 480;
            if (vg_lite_allocate(&target) == VG_LITE_SUCCESS) {
                vg_lite_matrix_t m;
                vg_lite_identity(&m);
                if (vg_lite_clear(&target, NULL, 0xFF336699) == VG_LITE_SUCCESS &&
                    vg_lite_blit(&target, &buf, &m, VG_LITE_BLEND_NONE, 0,
                                 VG_LITE_FILTER_POINT) == VG_LITE_SUCCESS &&
                    vg_lite_finish() == VG_LITE_SUCCESS) {
                    printf("  blit after upload OK\n");
                }
                char fn[128];
                snprintf(fn, sizeof(fn), "uploadBuffer_%s_output.png", tc->name);
                vg_lite_save_png(fn, &target);
                vg_lite_free(&target);
            }
        }

        free(src);
        vg_lite_free(&buf);
    }

    vg_lite_close();
    if (failed) {
        printf("uploadBuffer test FAILED (%d case(s))\n", failed);
        return 1;
    }
    printf("uploadBuffer test PASSED\n");
    return 0;
}
