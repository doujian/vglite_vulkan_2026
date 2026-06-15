/*
 * Migrated from VGLite_Tests/VSI_CTS/samples/scissor/scissor.c
 * Resolution: 320 x 480, Format: VG_LITE_RGBA8888
 * Tests vg_lite_set_scissor / vg_lite_enable_scissor / vg_lite_disable_scissor
 *
 * Verification: Clear blue → set scissor to left 160px → draw octagon centered.
 * Expected: octagon only visible in left half, right half stays blue.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"

#define IS_ERROR(status)         (status > 0)
#define CHECK_ERROR(Function) \
    error = Function; \
    if (IS_ERROR(error)) \
    { \
        printf("error %d at line %d\n", (int)error, __LINE__); \
        goto ErrorHandler; \
    }

static int fb_width = 320, fb_height = 480;

void cleanup(vg_lite_buffer_t *buf)
{
    if (buf->handle != NULL)
        vg_lite_free(buf);
    vg_lite_close();
}

int main(int argc, const char *argv[])
{
    vg_lite_matrix_t matrix;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_buffer_t buffer;

    static char path_data[] = {
        2, -5, -10,
        4, 5, -10,
        4, 10, -5,
        4, 0, 0,
        4, 10, 5,
        4, 5, 10,
        4, -5, 10,
        4, -10, 5,
        4, -10, -5,
        0,
    };

    CHECK_ERROR(vg_lite_init(fb_width, fb_height));

    memset(&buffer, 0, sizeof(buffer));
    buffer.width = fb_width;
    buffer.height = fb_height;
    buffer.format = VG_LITE_RGBA8888;
    CHECK_ERROR(vg_lite_allocate(&buffer));

    /* Step 1: Clear entire buffer to blue */
    CHECK_ERROR(vg_lite_clear(&buffer, NULL, 0xFFFF0000));
    CHECK_ERROR(vg_lite_finish());

    /* Step 2: Set scissor to left 160px only */
    vg_lite_set_scissor(0, 0, 160, fb_height);
    vg_lite_enable_scissor();

    /* Step 3: Draw octagon centered at (160, 240), scaled 5x.
     * Octagon spans roughly (110,190)-(210,290).
     * With scissor (left 160px): right half of octagon clipped.
     * Right side of buffer stays blue. */
    vg_lite_path_t path;
    memset(&path, 0, sizeof(path));
    vg_lite_init_path(&path, VG_LITE_S8, VG_LITE_HIGH, sizeof(path_data), path_data,
                      -10, -10, 10, 10);

    vg_lite_identity(&matrix);
    vg_lite_translate(160.0f, 240.0f, &matrix);
    vg_lite_scale(5, 5, &matrix);
    CHECK_ERROR(vg_lite_draw(&buffer, &path, VG_LITE_FILL_EVEN_ODD, &matrix,
                             VG_LITE_BLEND_SRC_OVER, 0xFF6600FF));

    CHECK_ERROR(vg_lite_finish());
    vg_lite_disable_scissor();

    vg_lite_save_png("scissors.png", &buffer);

    {
        uint8_t *mem = (uint8_t*)buffer.memory;
        printf("Left edge  (0,0):     [%02x %02x %02x %02x] (expect blue)\n",
               mem[0], mem[1], mem[2], mem[3]);
        printf("Right edge (319,0):   [%02x %02x %02x %02x] (expect blue)\n",
               mem[319*4], mem[319*4+1], mem[319*4+2], mem[319*4+3]);
        printf("Draw area (140,240):  [%02x %02x %02x %02x] (expect draw color)\n",
               mem[240*buffer.stride + 140*4],
               mem[240*buffer.stride + 140*4+1],
               mem[240*buffer.stride + 140*4+2],
               mem[240*buffer.stride + 140*4+3]);
        printf("Clipped   (180,240):  [%02x %02x %02x %02x] (expect blue if scissor)\n",
               mem[240*buffer.stride + 180*4],
               mem[240*buffer.stride + 180*4+1],
               mem[240*buffer.stride + 180*4+2],
               mem[240*buffer.stride + 180*4+3]);
    }

    printf("Saved: scissors.png (%dx%d)\n", fb_width, fb_height);

ErrorHandler:
    cleanup(&buffer);
    return 0;
}
