/*
 * Resolution: 256 x 256
 * Format: VG_LITE_BGRA8888
 * Transformation: Translate
 * Alpha Blending: None
 * Related APIs: vg_lite_clear/vg_lite_translate/vg_lite_get_path_length/
 *               vg_lite_init_path/vg_lite_append_path/vg_lite_set_stroke/
 *               vg_lite_update_stroke/vg_lite_set_path_type/vg_lite_draw
 * Description: Render a "petal"-shaped path (4 arcs + 4 lines) in three modes:
 *              1) stroke-only: 3x3 matrix of cap_style x join_style with a
 *                 dashed stroke.
 *              2) fill + stroke: same path drawn twice, first as solid fill,
 *                 then overlaid as stroke, for each join style.
 *              3) fill+stroke combined: single vg_lite_draw with
 *                 VG_LITE_DRAW_FILL_STROKE_PATH.
 *              Each iteration saves a PNG for visual inspection.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"

#define DEFAULT_SIZE   256.0f

static int   fb_width = 256, fb_height = 256;
static float fb_scale = 1.0f;

static vg_lite_buffer_t buffer;
static vg_lite_buffer_t *fb;

/* ---- Stroke attribute tables ---- */
static vg_lite_path_type_t draw_type[] = {
    VG_LITE_DRAW_FILL_PATH,
    VG_LITE_DRAW_STROKE_PATH,
    VG_LITE_DRAW_FILL_STROKE_PATH
};

static vg_lite_cap_style_t cap_style[] = {
    VG_LITE_CAP_BUTT,
    VG_LITE_CAP_ROUND,
    VG_LITE_CAP_SQUARE
};

static vg_lite_join_style_t join_style[] = {
    VG_LITE_JOIN_MITER,
    VG_LITE_JOIN_ROUND,
    VG_LITE_JOIN_BEVEL
};

/* ---- Path: 4 small clockwise arcs joined by 4 straight lines ---- */
static uint8_t sides_cmd[] = {
    VLC_OP_MOVE,
    VLC_OP_SCWARC,
    VLC_OP_LINE,
    VLC_OP_SCWARC,
    VLC_OP_LINE,
    VLC_OP_SCWARC,
    VLC_OP_LINE,
    VLC_OP_SCWARC,

    VLC_OP_END
};

static float sides_data_left[] = {
    50, 0,
    50, 50, 0, 0, 50,
    0, 100,
    50, 50, 0, 50, 150,
    200, 150,
    50, 50, 0, 250, 100,
    250, 50,
    50, 50, 0, 200, 0
};

/* Bounding box of sides_data_left: x in [0, 250], y in [0, 150]. */
#define PATH_MIN_X   0.0f
#define PATH_MIN_Y   0.0f
#define PATH_MAX_X   250.0f
#define PATH_MAX_Y   150.0f

static vg_lite_path_t path;

void cleanup(void)
{
    if (buffer.handle != NULL) {
        vg_lite_free(&buffer);
    }

    /* vg_lite_clear_path frees the internally allocated path/stroke data. */
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
    uint32_t         data_size;
    vg_lite_float_t  dash[2] = { 32.0f, 32.0f };
    int              i, j;
    char             filename[64];

    (void)argc;
    (void)argv;

    /* Initialize vg_lite engine. */
    CHECK_ERROR(vg_lite_init(fb_width, fb_height));

    fb_scale = (float)fb_width / DEFAULT_SIZE;
    printf("Framebuffer size: %d x %d\n", fb_width, fb_height);

    /* Allocate the off-screen buffer (BGRA8888 is the Vulkan-native format). */
    buffer.width  = fb_width;
    buffer.height = fb_height;
    buffer.format = VG_LITE_BGRA8888;
    CHECK_ERROR(vg_lite_allocate(&buffer));
    fb = &buffer;

    /* Build a translate matrix that offsets the path slightly. */
    vg_lite_identity(&matrix);
    vg_lite_translate(5.0f, 5.0f, &matrix);

    /* Pre-compute the encoded path data size for the FP32 format. */
    data_size = vg_lite_get_path_length(sides_cmd, sizeof(sides_cmd), VG_LITE_FP32);

    /* ---- Test 1: stroke-only, iterate cap x join with a dashed stroke. ---- */
    for (i = 0; i < (int)(sizeof(cap_style) / sizeof(cap_style[0])); i++) {
        for (j = 0; j < (int)(sizeof(join_style) / sizeof(join_style[0])); j++) {
            CHECK_ERROR(vg_lite_clear(fb, NULL, 0xFFFF0000));
            snprintf(filename, sizeof(filename), "stroke%d_%d.png", i, j);

            memset(&path, 0, sizeof(vg_lite_path_t));
            vg_lite_init_path(&path, VG_LITE_FP32, VG_LITE_LOW, data_size, NULL,
                              PATH_MIN_X, PATH_MIN_Y, PATH_MAX_X, PATH_MAX_Y);
            path.path = malloc(data_size);
            if (path.path == NULL) {
                printf("malloc failed for stroke path (cap=%d, join=%d)\n", i, j);
                error = VG_LITE_OUT_OF_MEMORY;
                goto ErrorHandler;
            }

            CHECK_ERROR(vg_lite_append_path(&path, sides_cmd, sides_data_left,
                                            sizeof(sides_cmd)));

            CHECK_ERROR(vg_lite_set_stroke(&path, cap_style[i], join_style[j],
                                           4.0f, 8.0f, dash, 2, 8.0f, 0));
            CHECK_ERROR(vg_lite_update_stroke(&path));
            CHECK_ERROR(vg_lite_set_path_type(&path, VG_LITE_DRAW_STROKE_PATH));
            CHECK_ERROR(vg_lite_draw(fb, &path, VG_LITE_FILL_EVEN_ODD, &matrix,
                                     VG_LITE_BLEND_NONE, 0xFF0000FF));

            CHECK_ERROR(vg_lite_finish());
            vg_lite_save_png(filename, fb);

            /* Release path data before the next iteration reuses `path`. */
            vg_lite_clear_path(&path);
            memset(&path, 0, sizeof(vg_lite_path_t));
        }
    }

    /* ---- Test 2: fill + stroke (two passes), iterate join style. ---- */
    for (j = 0; j < (int)(sizeof(join_style) / sizeof(join_style[0])); j++) {
        CHECK_ERROR(vg_lite_clear(fb, NULL, 0xFFFF0000));
        snprintf(filename, sizeof(filename), "stroke%d.png", j + 3);

        memset(&path, 0, sizeof(vg_lite_path_t));
        vg_lite_init_path(&path, VG_LITE_FP32, VG_LITE_HIGH, data_size, NULL,
                          PATH_MIN_X, PATH_MIN_Y, PATH_MAX_X, PATH_MAX_Y);
        path.path = malloc(data_size);
        if (path.path == NULL) {
            printf("malloc failed for fill+stroke path (join=%d)\n", j);
            error = VG_LITE_OUT_OF_MEMORY;
            goto ErrorHandler;
        }

        CHECK_ERROR(vg_lite_append_path(&path, sides_cmd, sides_data_left,
                                        sizeof(sides_cmd)));

        CHECK_ERROR(vg_lite_set_stroke(&path, VG_LITE_CAP_ROUND, join_style[j],
                                       4.0f, 8.0f, NULL, 0, 8.0f, 0));
        CHECK_ERROR(vg_lite_update_stroke(&path));

        /* First pass: solid fill (blue). */
        CHECK_ERROR(vg_lite_set_path_type(&path, draw_type[0]));
        CHECK_ERROR(vg_lite_draw(fb, &path, VG_LITE_FILL_EVEN_ODD, &matrix,
                                 VG_LITE_BLEND_NONE, 0xFF0000FF));
        /* Second pass: stroke overlay (yellow). */
        CHECK_ERROR(vg_lite_set_path_type(&path, draw_type[1]));
        CHECK_ERROR(vg_lite_draw(fb, &path, VG_LITE_FILL_EVEN_ODD, &matrix,
                                 VG_LITE_BLEND_NONE, 0xFF00FFFF));

        CHECK_ERROR(vg_lite_finish());
        vg_lite_save_png(filename, fb);

        vg_lite_clear_path(&path);
        memset(&path, 0, sizeof(vg_lite_path_t));
    }

    /* ---- Test 3: combined fill+stroke in a single draw call. ---- */
    CHECK_ERROR(vg_lite_clear(fb, NULL, 0xFFFF0000));
    snprintf(filename, sizeof(filename), "fill_stroke.png");

    memset(&path, 0, sizeof(vg_lite_path_t));
    vg_lite_init_path(&path, VG_LITE_FP32, VG_LITE_HIGH, data_size, NULL,
                      PATH_MIN_X, PATH_MIN_Y, PATH_MAX_X, PATH_MAX_Y);
    path.path = malloc(data_size);
    if (path.path == NULL) {
        printf("malloc failed for fill_stroke path\n");
        error = VG_LITE_OUT_OF_MEMORY;
        goto ErrorHandler;
    }

    CHECK_ERROR(vg_lite_append_path(&path, sides_cmd, sides_data_left,
                                    sizeof(sides_cmd)));

    CHECK_ERROR(vg_lite_set_stroke(&path, VG_LITE_CAP_ROUND, VG_LITE_JOIN_ROUND,
                                   4.0f, 8.0f, NULL, 0, 8.0f, 0));
    CHECK_ERROR(vg_lite_update_stroke(&path));
    CHECK_ERROR(vg_lite_set_path_type(&path, draw_type[2]));
    CHECK_ERROR(vg_lite_draw(fb, &path, VG_LITE_FILL_EVEN_ODD, &matrix,
                             VG_LITE_BLEND_NONE, 0xFF0000FF));

    CHECK_ERROR(vg_lite_finish());
    vg_lite_save_png(filename, fb);

    vg_lite_clear_path(&path);
    memset(&path, 0, sizeof(vg_lite_path_t));

    printf("All stroke tests completed.\n");

ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS) ? 0 : -1;
}
