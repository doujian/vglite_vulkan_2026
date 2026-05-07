/*
 * Test: 2x2 RGBA8888 blit without 16-pixel alignment
 * Writes 4 known-color pixels into src, blits to dst, verifies output.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

int main(int argc, const char *argv[])
{
    vg_lite_filter_t filter;
    vg_lite_matrix_t matrix;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_buffer_t srcbuf = {0};
    vg_lite_buffer_t dstbuf = {0};
    int fb_width = 2, fb_height = 2;
    int pass = 1;

    const unsigned int color_data[4] = { 0xff000050, 0xff005000, 0xff500000, 0xff505050 };

    CHECK_ERROR(vg_lite_init(fb_width, fb_height));

    vg_lite_identity(&matrix);
    filter = VG_LITE_FILTER_POINT;

    srcbuf.width = fb_width;
    srcbuf.height = fb_height;
    srcbuf.format = VG_LITE_RGBA8888;
    CHECK_ERROR(vg_lite_allocate(&srcbuf));

    memset(srcbuf.memory, 0xffffffff, srcbuf.stride * srcbuf.height);
    memcpy(srcbuf.memory, &color_data[0], 8);
    memcpy((char *)srcbuf.memory + srcbuf.stride, &color_data[2], 8);

    dstbuf.width = fb_width;
    dstbuf.height = fb_height;
    dstbuf.format = VG_LITE_RGBA8888;
    CHECK_ERROR(vg_lite_allocate(&dstbuf));

    CHECK_ERROR(vg_lite_blit(&dstbuf, &srcbuf, &matrix, VG_LITE_BLEND_NONE, 0, filter));
    CHECK_ERROR(vg_lite_finish());

    vg_lite_save_png("16pixels_align.png", &dstbuf);

    {
        int x, y, idx;
        for (y = 0; y < 2; y++) {
            for (x = 0; x < 2; x++) {
                idx = y * 2 + x;
                uint32_t got = vg_lite_read_pixel(&dstbuf, x, y);
                int gr = got & 0xFF, gg = (got >> 8) & 0xFF, gb = (got >> 16) & 0xFF, ga = (got >> 24) & 0xFF;
                int er = color_data[idx] & 0xFF, eg = (color_data[idx] >> 8) & 0xFF;
                int eb = (color_data[idx] >> 16) & 0xFF, ea = (color_data[idx] >> 24) & 0xFF;
                if (gr != er || gg != eg || gb != eb || ga != ea) {
                    printf("FAIL: pixel(%d,%d) expected R=%d,G=%d,B=%d,A=%d got R=%d,G=%d,B=%d,A=%d\n",
                           x, y, er, eg, eb, ea, gr, gg, gb, ga);
                    pass = 0;
                }
            }
        }
    }

    if (pass) printf("16pixels_align OK\n");
    else      printf("16pixels_align FAILED\n");

ErrorHandler:
    vg_lite_free(&srcbuf);
    vg_lite_free(&dstbuf);
    vg_lite_close();
    return (error == VG_LITE_SUCCESS && pass) ? 0 : -1;
}
