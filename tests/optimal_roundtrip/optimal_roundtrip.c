/*
 * test_optimal_roundtrip - texture upload/download round-trip integrity.
 *
 * Loads tests/data/landscape.raw (16-byte VGLite raw header: width, height,
 * raw_stride, format code; then tightly packed pixel rows), uploads it via
 * vg_lite_buffer_write() (OPTIMAL configs go through the staging
 * vkCmdCopyBufferToImage path, LINEAR configs through mapped memory), then
 * reads the image back with vg_lite_buffer_download() and compares every
 * pixel byte against the original file content.
 *
 * Pass criteria: 0 mismatched bytes out of width*height*4.
 */
#include "vg_lite.h"
#include "vg_lite_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_long(FILE *fp)
{
    unsigned char buf[4];
    if (fread(buf, 1, 4, fp) != 4) return 0;
    return (int)((unsigned)buf[0] |
                 ((unsigned)buf[1] << 8) |
                 ((unsigned)buf[2] << 16) |
                 ((unsigned)buf[3] << 24));
}

static FILE *open_landscape(void)
{
    static const char *paths[] = {
        "landscape.raw",
        "data/landscape.raw",
        "../tests/data/landscape.raw",
        "../../tests/data/landscape.raw",
    };
    for (int i = 0; i < 4; i++) {
        FILE *fp = fopen(paths[i], "rb");
        if (fp) return fp;
    }
    return NULL;
}

int main(void)
{
    FILE *fp = open_landscape();
    if (!fp) {
        printf("Cannot open landscape.raw\n");
        return -1;
    }

    int w        = read_long(fp);
    int h        = read_long(fp);
    int raw_str  = read_long(fp);
    int fmt_code = read_long(fp);

    vg_lite_buffer_format_t fmt;
    switch (fmt_code) {
    case 0:
    case 1024: fmt = VG_LITE_RGBA8888; break;
    case 1:
    case 1025: fmt = VG_LITE_BGRA8888; break;
    case 4:
    case 1028: fmt = VG_LITE_RGB565;   break;
    case 5:
    case 1029: fmt = VG_LITE_BGR565;   break;
    default:   fmt = VG_LITE_RGBA8888; break;
    }
    if (raw_str == 0) raw_str = w * (fmt == VG_LITE_RGB565 || fmt == VG_LITE_BGR565 ? 2 : 4);

    printf("landscape.raw: %dx%d fmt=%d raw_stride=%d\n", w, h, fmt_code, raw_str);

    if (vg_lite_init(32, 32) != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed\n");
        fclose(fp);
        return -1;
    }

    vg_lite_buffer_t buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.width   = w;
    buffer.height  = h;
    buffer.format  = fmt;
    buffer.tiled   = VG_LITE_TILED;   /* force OPTIMAL image allocation */
    vg_lite_dump_enable_host_optimal(1); /* make OPTIMAL memory mappable for raw dump */

    if (vg_lite_allocate(&buffer) != VG_LITE_SUCCESS) {
        printf("vg_lite_allocate failed\n");
        fclose(fp);
        vg_lite_close();
        return -1;
    }
    printf("allocated: stride=%d tiled=%d\n", (int)buffer.stride, (int)buffer.tiled);

    /* Keep a private copy of the original file content (stride layout). */
    unsigned char *orig = (unsigned char *)malloc(buffer.stride * h);
    unsigned char *dl   = (unsigned char *)malloc(buffer.stride * h);
    if (!orig || !dl) {
        printf("out of memory\n");
        goto fail;
    }
    memset(orig, 0, buffer.stride * h);
    {
        unsigned char *row = orig;
        for (int y = 0; y < h; y++) {
            if (fread(row, raw_str, 1, fp) != 1) {
                printf("short read at row %d\n", y);
                goto fail;
            }
            row += buffer.stride;
        }
    }
    fclose(fp);

    /* Upload (OPTIMAL: staging + vkCmdCopyBufferToImage). */
    if (vg_lite_buffer_write(&buffer, orig) != VG_LITE_SUCCESS) {
        printf("vg_lite_buffer_write failed\n");
        goto fail;
    }
    if (vg_lite_finish() != VG_LITE_SUCCESS) {
        printf("vg_lite_finish failed\n");
        goto fail;
    }

    /* Download back (OPTIMAL: vkCmdCopyImageToBuffer + pack). */
    memset(dl, 0xCC, buffer.stride * h);
    if (vg_lite_buffer_download(&buffer, dl) != VG_LITE_SUCCESS) {
        printf("vg_lite_buffer_download failed\n");
        goto fail;
    }

    /* Compare pixel area of every row (stride padding is not guaranteed). */
    {
        long mismatches = 0;
        long total = (long)w * h * 4;
        for (int y = 0; y < h; y++) {
            const unsigned char *a = orig + (long)y * buffer.stride;
            const unsigned char *b = dl   + (long)y * buffer.stride;
            for (int x = 0; x < w * 4; x++) {
                if (a[x] != b[x]) {
                    if (mismatches < 10)
                        printf("mismatch [%d,%d byte %d]: got %02X exp %02X\n",
                               x / 4, y, x % 4, b[x], a[x]);
                    mismatches++;
                }
            }
        }
        printf("round-trip: %ld/%ld bytes match (%ld mismatches)\n",
               total - mismatches, total, mismatches);

        /* Dump the downloaded image for visual inspection. */
        vg_lite_save_png("optimal_roundtrip.png", &buffer);

        /* Debug: dump raw GPU-side memory (OPTIMAL physical bytes,
         * driver-private layout — for inspection only, not compared). */
        vg_lite_error_t derr = vg_lite_dump_raw("landscape", &buffer);
        printf("raw dump: %s\n", derr == VG_LITE_SUCCESS ? "written" : "unavailable");

        vg_lite_free(&buffer);
        vg_lite_close();
        free(orig);
        free(dl);
        return mismatches == 0 ? 0 : -1;
    }

fail:
    if (fp) fclose(fp);
    vg_lite_free(&buffer);
    vg_lite_close();
    free(orig);
    free(dl);
    return -1;
}
