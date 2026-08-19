#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

/* A4 alpha-mask blit test — mirrors tests/imgA8.
 *
 * A4 is packed 4bpp (2 pixels per byte, high nibble = even x). The CPU
 * uploads VGLite packed rows; the implementation expands to a GPU R8
 * image ((n<<4)|n bit replication) and samples it like an A8 mask under
 * MULTIPLY image mode. Alpha data is a program-generated horizontal
 * 16-level gradient (nibble n = x & 0xF) so every nibble value 0..15
 * (expanded 0x00,0x11,...,0xFF) is exercised. POINT filter avoids
 * bilinear rounding; 2x scale makes each source pixel a 2x2 block. */

#define ALIGMENT(value,base)   ((value + base - 1) & ~(base-1))

static int fb_width = 256, fb_height = 256;
static int src_width = 128, src_height = 128;

static vg_lite_buffer_t buffer;
static vg_lite_matrix_t matrix;
static vg_lite_buffer_t offscreenBuf;

void cleanup(void)
{
    if (offscreenBuf.handle != NULL) {
        vg_lite_free(&offscreenBuf);
    }
    if (buffer.handle != NULL) {
        vg_lite_free(&buffer);
    }
    vg_lite_close();
}

static vg_lite_error_t init_offscreenBuf(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_buffer_t *vg_buffer = &offscreenBuf;
    vg_buffer->width  = src_width;
    vg_buffer->height = src_height;
    vg_buffer->format = VG_LITE_A4;
    vg_buffer->image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;
    vg_buffer->transparency_mode = VG_LITE_IMAGE_TRANSPARENT;
    CHECK_ERROR(vg_lite_allocate(vg_buffer));
    /* stride (packed bytes per row, 64-aligned) computed by vg_lite_allocate */

ErrorHandler:
    return error;
}

static vg_lite_error_t init_vg_lite(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    CHECK_ERROR(vg_lite_init(128, 128));
    CHECK_ERROR(init_offscreenBuf());

ErrorHandler:
    return error;
}

static vg_lite_error_t render(void)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_blend_t blend = VG_LITE_BLEND_SRC_OVER;

    buffer.format = VG_LITE_RGBA8888;
    buffer.width = ALIGMENT(fb_width, 64);
    buffer.height = fb_height;
    buffer.tiled = VGLITE_TARGET_TILING;

    /* Build the packed A4 surface: alpha nibble = x & 0xF (bit-replicated
     * to 0x00..0xFF on the GPU side). Data written as packed rows through
     * vg_lite_buffer_write, exercising the shadow + expand path. */
    {
        uint8_t *tmp = (uint8_t *)calloc(1, offscreenBuf.stride * offscreenBuf.height);
        for (int y = 0; y < src_height; y++) {
            uint8_t *row = tmp + (size_t)y * offscreenBuf.stride;
            for (int x = 0; x < src_width; x += 2) {
                uint8_t hi = (uint8_t)(x & 0xF);
                uint8_t lo = (uint8_t)((x + 1) & 0xF);
                row[x >> 1] = (uint8_t)((hi << 4) | lo);
            }
        }
        CHECK_ERROR(vg_lite_buffer_write(&offscreenBuf, tmp));
        free(tmp);
    }
    CHECK_ERROR(vg_lite_finish());
    CHECK_ERROR(vg_lite_allocate(&buffer));
    /* Clear surface with red */
    CHECK_ERROR(vg_lite_clear(&buffer, NULL, 0xFF0000FF));
    CHECK_ERROR(vg_lite_finish());
    vg_lite_identity(&matrix);
    vg_lite_scale(2, 2, &matrix);
    vg_lite_translate(0, 0, &matrix);

    offscreenBuf.image_mode = VG_LITE_MULTIPLY_IMAGE_MODE;

    CHECK_ERROR(vg_lite_blit(&buffer, &offscreenBuf, &matrix, blend,
                             0xFF00FF00, VG_LITE_FILTER_POINT));
    CHECK_ERROR(vg_lite_finish());

ErrorHandler:
    return error;
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error;
    int fail = 0;

    CHECK_ERROR(init_vg_lite());
    CHECK_ERROR(render());

    vg_lite_save_png("imgA4.png", &buffer);

    {
        vg_lite_expected_buffer_t *eb = vg_lite_expected_create(buffer.width, buffer.height, buffer.format);
        vg_lite_expected_clear(eb, NULL, 0xFF0000FF);
        vg_lite_expected_blit(eb, &offscreenBuf, &matrix, VG_LITE_BLEND_SRC_OVER, VG_LITE_FILTER_POINT,
                              VG_LITE_MULTIPLY_IMAGE_MODE, 8, 0xFF00FF00, NULL);
        fail += vg_lite_expected_verify(eb, &buffer, 12);
        vg_lite_expected_destroy(eb);
    }

    if (fail == 0) printf("imgA4 test PASSED\n");
    else           printf("imgA4 test FAILED (%d mismatches)\n", fail);

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS && fail == 0) ? 0 : -1;
}
