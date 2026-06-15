//-----------------------------------------------------------------------------
// Migrated from VGLite_Tests/VSI_CTS/samples/unit_test1/src/Cases/Gradient.c
// Tests vg_lite_draw_grad with multiple blend modes, path shapes, and color counts.
//-----------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "vg_lite.h"
#include "vg_lite_util.h"

#define WINDSIZEX 320.0f
#define WINDSIZEY 240.0f

#define COLOR_COUNT     16
#define BLEND_COUNT     5

#define __func__ __FUNCTION__
#define IS_ERROR(status)         (status > 0)
#define CHECK_ERROR(Function) \
    error = Function; \
    if (IS_ERROR(error)) \
    { \
        printf("[%s: %d] error type is %d\n", __func__, __LINE__, (int)error); \
        goto ErrorHandler; \
    }

/* ---- Blend modes (Vulkan-native hardware blend only) ---- */
static vg_lite_blend_t blend_mode[] = {
    VG_LITE_BLEND_NONE,
    VG_LITE_BLEND_SRC_OVER,
    VG_LITE_BLEND_DST_OVER,
    VG_LITE_BLEND_ADDITIVE,
    VG_LITE_BLEND_SUBTRACT,
};

/* ---- Path data (from Common.c) ---- */
static int32_t path_data1[] = {
    2,  35, 50,
    4,  75, 15,
    4, 110, 35,
    4, 100, 50,
    4, 110, 65,
    4,  75, 85,
    0,
};
static int32_t path_data2[] = {
    2, 155, 155,
    5,  80,   0,
    5,   0,  80,
    5, -80,   0,
    5,   0, -80,
    2, 165, 165,
    5,  60,   0,
    5,   0,  60,
    5, -60,   0,
    5,   0, -60,
    0,
};
static int32_t path_data3[] = {
    5, 170,  0,
    5, -85, 85,
    0
};
static int32_t path_data4[] = {
    5, 100,   0,
    5, -50, 100,
    5, -50, 100,
    5, 100,   0,
    0
};
static int32_t path_data5[] = {
    5, 100,   0,
    5,   0, 100,
    5,-100,   0,
    0
};
static int32_t path_data6[] = {
    2, -5, -10,
    4,  5, -10,
    4, 10,  -5,
    4,  0,   0,
    4, 10,   5,
    4,  5,  10,
    4, -5,  10,
    4,-10,   5,
    4,-10,  -5,
    0,
};
static int32_t path_data7[] = {
    4,  80,  40,
    4,  40,  80,
    4,   0,   0,
    4, 200,   0,
    4, 160,  80,
    4, 120,  40,
    4, 200,   0,
    4, 200, 200,
    4, 120, 160,
    4, 160, 120,
    4, 200, 200,
    4,   0, 200,
    4,  40, 120,
    4,  80, 160,
    4,   0, 200,
    0
};
static int32_t path_data8[] = {
    7,   85, -60, 170,-40,
    7,  -50,  50, -70,210,
    7,  -90, -30,-100,-170,
    0
};
static int32_t path_data9[] = {
    VLC_OP_QUAD,     8,     22,     45,     0,
    VLC_OP_QUAD,    37,     26,     45,     53,
    VLC_OP_QUAD,    30,     45,     15,     53,
    VLC_OP_LINE,    15,     18,
    VLC_OP_LINE,    35,     18,
    VLC_OP_LINE,    35,     40,
    VLC_OP_LINE,     0,     40,
    VLC_OP_QUAD,     7,     20,      0,      0,
    VLC_OP_END
};

static int32_t *test_path[9] = {
    path_data1, path_data2, path_data3, path_data4,
    path_data5, path_data6, path_data7, path_data8,
    path_data9,
};
static int32_t path_length[] = {
    sizeof(path_data1), sizeof(path_data2), sizeof(path_data3),
    sizeof(path_data4), sizeof(path_data5), sizeof(path_data6),
    sizeof(path_data7), sizeof(path_data8), sizeof(path_data9),
};

/* ---- Simple LCG random (matches source util.c) ---- */
static unsigned long int s_random_value = 32557;
#define RANDOM_MAX 32767

static int random_rand(void)
{
    s_random_value = s_random_value * 1103515245 + 12345;
    return (unsigned int)(s_random_value / 65536) % 32768;
}

static unsigned long int Random_i(unsigned long int lo, unsigned long int hi)
{
    unsigned long int x, y;
    x = random_rand();
    if ((y = (hi - lo)) > RANDOM_MAX)
        y = lo + y / (RANDOM_MAX) * x;
    else
        y = lo + (y * x + (RANDOM_MAX) / y) / (RANDOM_MAX);
    return y;
}

/* ---- Helpers ---- */
static vg_lite_error_t Allocate_Buffer(vg_lite_buffer_t *buffer,
                                       vg_lite_buffer_format_t format,
                                       int32_t width, int32_t height)
{
    vg_lite_error_t error;
    buffer->width  = width;
    buffer->height = height;
    buffer->format = format;
    buffer->stride = 0;
    buffer->handle = NULL;
    buffer->memory = NULL;
    buffer->address = 0;
    buffer->tiled   = (vg_lite_buffer_layout_t)0;
    error = vg_lite_allocate(buffer);
    return error;
}

static void Free_Buffer(vg_lite_buffer_t *buffer)
{
    vg_lite_free(buffer);
}

/* ---- Core gradient draw test ---- */
static vg_lite_error_t Gradient_Draw(int32_t pathdata[], int32_t length,
                                     vg_lite_blend_t blend_mode_val,
                                     uint32_t ramps[], uint32_t stops[],
                                     uint8_t count, const char *suffix)
{
    vg_lite_buffer_t dst_buf;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t color = 0xffa0a0a0;
    vg_lite_path_t path;

    vg_lite_matrix_t matPath;
    vg_lite_matrix_t *matGrad;
    vg_lite_linear_gradient_t grad;

    memset(&grad, 0, sizeof(grad));
    memset(&dst_buf, 0, sizeof(vg_lite_buffer_t));

    if (VG_LITE_SUCCESS != vg_lite_init_grad(&grad)) {
        printf("Linear gradient is not supported.\n");
        return VG_LITE_NOT_SUPPORT;
    }

    vg_lite_set_grad(&grad, count, ramps, stops);
    vg_lite_update_grad(&grad);
    matGrad = vg_lite_get_grad_matrix(&grad);

    memset(&path, 0, sizeof(path));
    vg_lite_init_path(&path, VG_LITE_S32, VG_LITE_HIGH, length, pathdata,
                      -WINDSIZEX, -WINDSIZEY, WINDSIZEX, WINDSIZEY);

    CHECK_ERROR(Allocate_Buffer(&dst_buf, VG_LITE_BGRA8888, (int32_t)WINDSIZEX, (int32_t)WINDSIZEY));

    vg_lite_identity(matGrad);
    vg_lite_rotate(30.0f, matGrad);
    vg_lite_identity(&matPath);

    /* draw gradient */
    CHECK_ERROR(vg_lite_clear(&dst_buf, NULL, color));
    CHECK_ERROR(vg_lite_draw_grad(&dst_buf, &path, VG_LITE_FILL_EVEN_ODD,
                                   &matPath, &grad, blend_mode_val));
    CHECK_ERROR(vg_lite_finish());

    /* Save as PNG */
    {
        char filename[256];
        snprintf(filename, sizeof(filename), "Gradient_%s.png", suffix);
        vg_lite_save_png(filename, &dst_buf);
        printf("  Saved: %s\n", filename);
    }

ErrorHandler:
    if (dst_buf.handle != NULL)
        Free_Buffer(&dst_buf);
    vg_lite_clear_grad(&grad);
    return error;
}

/* ---- Test 1: Draw with all blend modes ---- */
static vg_lite_error_t Gradient_Draw_Blendmodes(void)
{
    int i;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    uint32_t ramps[] = {0xff000000, 0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff};
    uint32_t stops[] = {0, 66, 122, 200, 255};

    printf("\n=== Gradient_Draw_Blendmodes ===\n");
    for (i = 0; i < BLEND_COUNT; i++) {
        char suffix[64];
        const char *blend_names[] = {
            "NONE", "SRC_OVER", "DST_OVER", "ADDITIVE", "SUBTRACT"
        };
        snprintf(suffix, sizeof(suffix), "blend_%s", blend_names[i]);
        printf("[%d/%d] blend=%s\n", i + 1, BLEND_COUNT, blend_names[i]);
        CHECK_ERROR(Gradient_Draw(test_path[i], path_length[i], blend_mode[i],
                                  ramps, stops, 5, suffix));
    }

ErrorHandler:
    return error;
}

/* ---- Test 2: Draw with varying color counts (0 to COLOR_COUNT+1) ---- */
static vg_lite_error_t Gradient_Draw_ColorCount(void)
{
    int i, j;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    uint32_t ramps[COLOR_COUNT + 1];
    uint32_t stops[COLOR_COUNT + 1];
    int color_count[COLOR_COUNT + 2];

    printf("\n=== Gradient_Draw_ColorCount ===\n");
    for (i = 0; i <= COLOR_COUNT + 1; i++) {
        char suffix[64];
        color_count[i] = i;

        memset(ramps, 0, sizeof(ramps));
        memset(stops, 0, sizeof(stops));

        for (j = 0; j < color_count[i]; j++) {
            if (color_count[i] % 4 == 0) {
                /* out-of-order stops */
                ramps[j] = (uint32_t)Random_i(0xFF000000, 0xFFFFFFFF);
                stops[j] = (uint32_t)Random_i(0, 300);
            } else {
                /* normal ordered stops */
                ramps[j] = (uint32_t)Random_i(0xFF000000, 0xFFFFFFFF);
                if (j > 0) {
                    if (stops[j - 1] == 255)
                        stops[j] = 255;
                    else
                        stops[j] = (uint32_t)Random_i(stops[j - 1], 255);
                } else {
                    stops[j] = (uint32_t)Random_i(0, 255);
                }
            }
        }

        snprintf(suffix, sizeof(suffix), "colors_%d", color_count[i]);
        printf("[%d/%d] color_count=%d\n", i, COLOR_COUNT + 1, color_count[i]);
        CHECK_ERROR(Gradient_Draw(test_path[i % 9], path_length[i % 9],
                                  VG_LITE_BLEND_NONE, ramps, stops,
                                  (uint8_t)color_count[i], suffix));
    }

ErrorHandler:
    return error;
}

/* ---- Main ---- */
int main(int argc, const char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;

    CHECK_ERROR(vg_lite_init((int32_t)WINDSIZEX, (int32_t)WINDSIZEY));
    printf("vg_lite engine initialized (%dx%d)\n", (int)WINDSIZEX, (int)WINDSIZEY);

    CHECK_ERROR(Gradient_Draw_Blendmodes());
    CHECK_ERROR(Gradient_Draw_ColorCount());

    printf("\nAll gradient tests completed.\n");

ErrorHandler:
    vg_lite_close();
    return (int)error;
}
