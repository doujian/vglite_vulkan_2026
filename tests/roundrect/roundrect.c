/*
 * Resolution: 256 x 256
 * Format: VG_LITE_BGRA8888
 * Transformation: identity / rotate / scale-translate
 * Alpha Blending: NONE / SRC_OVER
 * Related APIs: vg_lite_clear / vg_lite_get_path_length / vg_lite_init_path /
 *               vg_lite_append_path / vg_lite_draw / vg_lite_finish /
 *               vg_lite_save_png
 * Description: Render a rounded rectangle (200x120, corner radius 40) built
 *              from 4 LINE + 4 QUAD segments (control point at the corner,
 *              giving an approximate 90-degree arc). Three frames:
 *                0) identity matrix, BLEND_NONE, solid fill
 *                1) rotated ~30 deg + scaled 0.8, BLEND_NONE
 *                2) identity translate, BLEND_SRC_OVER semi-transparent fill
 *                   on top of frame-1 content
 *              Each frame saves a PNG for visual inspection.
 *
 * Original test for vglite_vulkan_2026 (pattern follows tests/stroke/stroke.c).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

static int fb_width = 256, fb_height = 256;

static vg_lite_buffer_t buffer;
static vg_lite_buffer_t *fb;

/* Rounded rect: x in [28,228], y in [68,188], corner radius 40. */
static uint8_t rect_cmd[] = {
    VLC_OP_MOVE,                 /* start top-left after corner */
    VLC_OP_LINE,                 /* top edge */
    VLC_OP_QUAD,                 /* top-right corner */
    VLC_OP_LINE,                 /* right edge */
    VLC_OP_QUAD,                 /* bottom-right corner */
    VLC_OP_LINE,                 /* bottom edge */
    VLC_OP_QUAD,                 /* bottom-left corner */
    VLC_OP_LINE,                 /* left edge */
    VLC_OP_QUAD,                 /* top-left corner */
    VLC_OP_END
};

/* QUAD data order: control point (cx,cy) then end point (x,y). */
static float rect_data[] = {
    68.0f, 68.0f,                              /* MOVE  */
    188.0f, 68.0f,                             /* LINE  */
    228.0f, 68.0f,   228.0f, 108.0f,           /* QUAD  */
    228.0f, 148.0f,                            /* LINE  */
    228.0f, 188.0f,  188.0f, 188.0f,           /* QUAD  */
    68.0f, 188.0f,                             /* LINE  */
    28.0f, 188.0f,   28.0f, 148.0f,            /* QUAD  */
    28.0f, 108.0f,                             /* LINE  */
    28.0f, 68.0f,    68.0f, 68.0f              /* QUAD  */
};

static vg_lite_path_t path;

void cleanup(void)
{
    if (buffer.handle != NULL) {
        vg_lite_free(&buffer);
    }

    if (path.path != NULL) {
        vg_lite_clear_path(&path);
        memset(&path, 0, sizeof(vg_lite_path_t));
    }

    vg_lite_close();
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_matrix_t matrix;
    uint32_t        data_size;
    char            filename[64];

    (void)argc;
    (void)argv;

    CHECK_ERROR(vg_lite_init(fb_width, fb_height));

    buffer.width  = fb_width;
    buffer.height = fb_height;
    buffer.format = VG_LITE_BGRA8888;
    buffer.tiled  = VGLITE_TARGET_TILING;
    CHECK_ERROR(vg_lite_allocate(&buffer));
    fb = &buffer;

    data_size = vg_lite_get_path_length(rect_cmd, sizeof(rect_cmd), VG_LITE_FP32);

    /* Frame 0: identity, BLEND_NONE, solid green fill on blue background. */
    CHECK_ERROR(vg_lite_clear(fb, NULL, 0xFFFF0000));
    snprintf(filename, sizeof(filename), "roundrect0.png");

    vg_lite_identity(&matrix);
    memset(&path, 0, sizeof(vg_lite_path_t));
    vg_lite_init_path(&path, VG_LITE_FP32, VG_LITE_HIGH, data_size, NULL,
                      0.0f, 0.0f, 0.0f, 0.0f);
    path.path = malloc(data_size);
    CHECK_ERROR(vg_lite_append_path(&path, rect_cmd, rect_data,
                                    sizeof(rect_cmd)));
    CHECK_ERROR(vg_lite_draw(fb, &path, VG_LITE_FILL_EVEN_ODD, &matrix,
                              VG_LITE_BLEND_NONE, 0xFF00FF00));
    CHECK_ERROR(vg_lite_finish());
    vg_lite_save_png(filename, fb);
    printf("Saved %s\n", filename);

    /* Frame 1: rotate ~30 deg around the rect center + scale 0.8, yellow. */
    CHECK_ERROR(vg_lite_clear(fb, NULL, 0xFFFF0000));
    snprintf(filename, sizeof(filename), "roundrect1.png");

    vg_lite_identity(&matrix);
    vg_lite_translate(128.0f, 128.0f, &matrix);
    vg_lite_rotate(30.0f, &matrix);
    vg_lite_scale(0.8f, 0.8f, &matrix);
    vg_lite_translate(-128.0f, -128.0f, &matrix);

    memset(&path, 0, sizeof(vg_lite_path_t));
    vg_lite_init_path(&path, VG_LITE_FP32, VG_LITE_HIGH, data_size, NULL,
                      0.0f, 0.0f, 0.0f, 0.0f);
    path.path = malloc(data_size);
    CHECK_ERROR(vg_lite_append_path(&path, rect_cmd, rect_data,
                                    sizeof(rect_cmd)));
    CHECK_ERROR(vg_lite_draw(fb, &path, VG_LITE_FILL_EVEN_ODD, &matrix,
                              VG_LITE_BLEND_NONE, 0xFF00FFFF));
    CHECK_ERROR(vg_lite_finish());
    vg_lite_save_png(filename, fb);
    printf("Saved %s\n", filename);

    /* Frame 2: keep frame-1 content, overlay semi-transparent white rect,
     * BLEND_SRC_OVER (alpha 0x80 -> blended against background). */
    snprintf(filename, sizeof(filename), "roundrect2.png");

    vg_lite_identity(&matrix);
    memset(&path, 0, sizeof(vg_lite_path_t));
    vg_lite_init_path(&path, VG_LITE_FP32, VG_LITE_HIGH, data_size, NULL,
                      0.0f, 0.0f, 0.0f, 0.0f);
    path.path = malloc(data_size);
    CHECK_ERROR(vg_lite_append_path(&path, rect_cmd, rect_data,
                                    sizeof(rect_cmd)));
    CHECK_ERROR(vg_lite_draw(fb, &path, VG_LITE_FILL_EVEN_ODD, &matrix,
                              VG_LITE_BLEND_SRC_OVER, 0x80FFFFFF));
    CHECK_ERROR(vg_lite_finish());
    vg_lite_save_png(filename, fb);
    printf("Saved %s\n", filename);

    printf("All roundrect tests completed.\n");

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS) ? 0 : -1;
}
