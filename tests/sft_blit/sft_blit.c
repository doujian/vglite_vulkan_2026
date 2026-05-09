//-----------------------------------------------------------------------------
// Port of SFT_Blit test cases from VSI_CTS.
// Tests blitting of different sizes/formats/transformations/blend modes.
//-----------------------------------------------------------------------------
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define IS_ERROR(status)    (status > 0)
#define CHECK_ERROR(Function) { error = Function; if (IS_ERROR(error)) { printf("[%s: %d] error=%d\n", __func__, __LINE__, error); goto ErrorHandler; } }
#define CHECK_GEN(fn) { int _rc = (fn); if (_rc != 0) { printf("[%s: %d] gen_buffer failed\n", __func__, __LINE__); error = VG_LITE_OUT_OF_MEMORY; goto ErrorHandler; } }

#define ALIGN(value, base)  ((value + base - 1) & ~(base - 1))

#define NUM_SRC_FORMATS 3
#define NUM_DST_FORMATS 3
#define NUM_BLEND_MODES 9

static vg_lite_buffer_format_t formats[] = {
    VG_LITE_BGRA8888,
    VG_LITE_RGB565,
    VG_LITE_L8
};

static vg_lite_blend_t blend_mode[] = {
    VG_LITE_BLEND_NONE,
    VG_LITE_BLEND_SRC_OVER,
    VG_LITE_BLEND_DST_OVER,
    VG_LITE_BLEND_SRC_IN,
    VG_LITE_BLEND_DST_IN,
    VG_LITE_BLEND_SCREEN,
    VG_LITE_BLEND_MULTIPLY,
    VG_LITE_BLEND_ADDITIVE,
    VG_LITE_BLEND_SUBTRACT
};

static vg_lite_filter_t filter = VG_LITE_FILTER_POINT;
static vg_lite_matrix_t identity_matrix;

static int get_tolerance(vg_lite_buffer_format_t format, vg_lite_blend_t blend)
{
    int tol = 0;
    uint32_t bpp = get_bpp(format);
    if (bpp >= 32) tol = 0;
    else if (bpp >= 16) tol = 12;
    else tol = 8;

    switch (blend) {
    case VG_LITE_BLEND_SRC_OVER:
    case VG_LITE_BLEND_DST_OVER:
    case VG_LITE_BLEND_ADDITIVE:
    case VG_LITE_BLEND_SUBTRACT:
        tol += 1;
        break;
    default:
        break;
    }
    return tol;
}

static int can_verify_blend(vg_lite_blend_t blend)
{
    switch (blend) {
    case VG_LITE_BLEND_NONE:
    case VG_LITE_BLEND_SRC_OVER:
    case VG_LITE_BLEND_DST_OVER:
    case VG_LITE_BLEND_SRC_IN:
    case VG_LITE_BLEND_DST_IN:
    case VG_LITE_BLEND_MULTIPLY:
    case VG_LITE_BLEND_SCREEN:
    case VG_LITE_BLEND_ADDITIVE:
    case VG_LITE_BLEND_SUBTRACT:
        return 1;
    default:
        return 0;
    }
}

static vg_lite_error_t Allocate_Buffer(vg_lite_buffer_t *buffer,
                                        vg_lite_buffer_format_t format,
                                        int32_t width, int32_t height)
{
    vg_lite_error_t error;
    memset(buffer, 0, sizeof(vg_lite_buffer_t));
    buffer->width  = ALIGN(width, 128);
    buffer->height = height;
    buffer->format = format;
    buffer->stride = 0;
    CHECK_ERROR(vg_lite_allocate(buffer));
ErrorHandler:
    return error;
}

static void Free_Buffer(vg_lite_buffer_t *buffer)
{
    vg_lite_free(buffer);
}

static vg_lite_error_t get_transform_matrix_impl(
    vg_lite_float_point4_t src, vg_lite_float_point4_t dst, vg_lite_matrix_t *mat)
{
    int i;
    float sx[4], sy[4], dx[4], dy[4];
    float a[8][9];

    for (i = 0; i < 4; i++) {
        sx[i] = src[i].x; sy[i] = src[i].y;
        dx[i] = dst[i].x; dy[i] = dst[i].y;
    }

    for (i = 0; i < 8; i++)
        for (int j = 0; j < 9; j++)
            a[i][j] = 0.0f;

    for (i = 0; i < 4; i++) {
        a[i*2][0] = sx[i]; a[i*2][1] = sy[i]; a[i*2][2] = 1.0f;
        a[i*2][6] = -dx[i]*sx[i]; a[i*2][7] = -dx[i]*sy[i]; a[i*2][8] = dx[i];
        a[i*2+1][3] = sx[i]; a[i*2+1][4] = sy[i]; a[i*2+1][5] = 1.0f;
        a[i*2+1][6] = -dy[i]*sx[i]; a[i*2+1][7] = -dy[i]*sy[i]; a[i*2+1][8] = dy[i];
    }

    for (i = 0; i < 8; i++) {
        int pivot = i;
        float maxv = 0.0f;
        for (int k = i; k < 8; k++) {
            float v = a[k][i] < 0 ? -a[k][i] : a[k][i];
            if (v > maxv) { maxv = v; pivot = k; }
        }
        if (pivot != i) {
            float tmp[9];
            memcpy(tmp, a[i], sizeof(tmp));
            memcpy(a[i], a[pivot], sizeof(tmp));
            memcpy(a[pivot], tmp, sizeof(tmp));
        }
        float div = a[i][i];
        if (fabsf(div) < 1e-10f) return VG_LITE_INVALID_ARGUMENT;
        for (int j = i; j < 9; j++) a[i][j] /= div;
        for (int k = 0; k < 8; k++) {
            if (k == i) continue;
            float f = a[k][i];
            for (int j = i; j < 9; j++) a[k][j] -= f * a[i][j];
        }
    }

    mat->m[0][0] = a[0][8]; mat->m[0][1] = a[1][8]; mat->m[0][2] = a[2][8];
    mat->m[1][0] = a[3][8]; mat->m[1][1] = a[4][8]; mat->m[1][2] = a[5][8];
    mat->m[2][0] = a[6][8]; mat->m[2][1] = a[7][8]; mat->m[2][2] = 1.0f;
    mat->scaleX = 1.0f; mat->scaleY = 1.0f; mat->angle = 0.0f;
    return VG_LITE_SUCCESS;
}

/* Test 1: Different src/dst format blit, identity matrix, no blending. */
static vg_lite_error_t SFT_Blit_001(void)
{
    vg_lite_buffer_t src_buf, dst_buf;
    int i, j;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc = 0xffffffff;
    int32_t width, height;
    int total_fail = 0;
    int img_idx = 0;

    memset(&src_buf, 0, sizeof(src_buf));
    memset(&dst_buf, 0, sizeof(dst_buf));

    for (i = 0; i < NUM_SRC_FORMATS; i++) {
        width = (int32_t)Random_r(1.0f, WINDSIZEX);
        height = (int32_t)Random_r(1.0f, WINDSIZEY);
        printf("  SFT_Blit_001: src format %d, %dx%d\n", i, width, height);
        fflush(stdout);
        CHECK_GEN(gen_buffer(i % 2, &src_buf, formats[i], width, height));
        printf("  SFT_Blit_001: src gen_buffer done\n");
        fflush(stdout);

        for (j = 0; j < NUM_DST_FORMATS; j++) {
            width = (int32_t)Random_r(1.0f, WINDSIZEX);
            height = (int32_t)Random_r(1.0f, WINDSIZEY);
            printf("  SFT_Blit_001: dst format %d, %dx%d\n", j, width, height);
            fflush(stdout);
            CHECK_GEN(gen_buffer(0, &dst_buf, formats[j], width, height));
            printf("  SFT_Blit_001: dst gen_buffer done, clearing\n");
            fflush(stdout);

            CHECK_ERROR(vg_lite_clear(&dst_buf, NULL, cc));
            printf("  SFT_Blit_001: cleared, blitting\n");
            fflush(stdout);
            CHECK_ERROR(vg_lite_blit(&dst_buf, &src_buf, &identity_matrix, VG_LITE_BLEND_NONE, 0, filter));
            printf("  SFT_Blit_001: blit done, finishing\n");
            fflush(stdout);

            {
                int tol = get_tolerance(dst_buf.format, VG_LITE_BLEND_NONE);
                vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dst_buf.width, dst_buf.height, dst_buf.format);
                vg_lite_expected_clear(eb, NULL, cc);
                vg_lite_expected_blit(eb, &src_buf, &identity_matrix, 0, filter);
                total_fail += vg_lite_expected_verify(eb, &dst_buf, tol);
                vg_lite_expected_destroy(eb);
            }

            char fname[128];
            snprintf(fname, sizeof(fname), "SFT_Blit_001_%d.png", img_idx++);
            vg_lite_save_png(fname, &dst_buf);

            Free_Buffer(&dst_buf);
            memset(&dst_buf, 0, sizeof(dst_buf));
        }
        Free_Buffer(&src_buf);
        memset(&src_buf, 0, sizeof(src_buf));
    }

    printf("  SFT_Blit_001: %d pixel failures\n", total_fail);
    return (total_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;
ErrorHandler:
    if (dst_buf.handle != NULL) vg_lite_free(&dst_buf);
    if (src_buf.handle != NULL) vg_lite_free(&src_buf);
    return error;
}

/* Test 2: Scale matrix, different formats, no blending. */
static vg_lite_error_t SFT_Blit_002(void)
{
    vg_lite_buffer_t src_buf, dst_buf;
    int i, j;
    float x, y;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc = 0xffffffff;
    vg_lite_matrix_t matrix;
    vg_lite_float_t xScl, yScl;
    int32_t width, height;
    int total_fail = 0;
    int img_idx = 0;

    memset(&src_buf, 0, sizeof(src_buf));
    memset(&dst_buf, 0, sizeof(dst_buf));

    for (i = 0; i < NUM_SRC_FORMATS; i++) {
        width = (int32_t)Random_r(1.0f, WINDSIZEX);
        height = (int32_t)Random_r(1.0f, WINDSIZEY);
        CHECK_GEN(gen_buffer(0, &src_buf, formats[i], width, height));

        for (j = 0; j < NUM_DST_FORMATS; j++) {
            width = (int32_t)Random_r(1.0f, WINDSIZEX);
            height = (int32_t)Random_r(1.0f, WINDSIZEY);
            CHECK_GEN(gen_buffer(0, &dst_buf, formats[j], width, height));

            xScl = (vg_lite_float_t)Random_r(-2.0f, 2.0f);
            yScl = (vg_lite_float_t)Random_r(-2.0f, 2.0f);
            x = dst_buf.width / 2.0f;
            y = dst_buf.height / 2.0f;

            vg_lite_identity(&matrix);
            vg_lite_translate(x, y, &matrix);
            vg_lite_scale(xScl, yScl, &matrix);
            printf("  blit with scale: xScl = %f, yScl = %f\n", xScl, yScl);

            CHECK_ERROR(vg_lite_clear(&dst_buf, NULL, cc));
            CHECK_ERROR(vg_lite_blit(&dst_buf, &src_buf, &matrix, VG_LITE_BLEND_NONE, 0, filter));
            CHECK_ERROR(vg_lite_finish());

            {
                int tol = get_tolerance(dst_buf.format, VG_LITE_BLEND_NONE) + 4;
                vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dst_buf.width, dst_buf.height, dst_buf.format);
                vg_lite_expected_clear(eb, NULL, cc);
                vg_lite_expected_blit(eb, &src_buf, &matrix, 0, filter);
                total_fail += vg_lite_expected_verify(eb, &dst_buf, tol);
                vg_lite_expected_destroy(eb);
            }

            char fname[128];
            snprintf(fname, sizeof(fname), "SFT_Blit_002_%d.png", img_idx++);
            vg_lite_save_png(fname, &dst_buf);

            Free_Buffer(&dst_buf);
            memset(&dst_buf, 0, sizeof(dst_buf));
        }
        Free_Buffer(&src_buf);
        memset(&src_buf, 0, sizeof(src_buf));
    }

    printf("  SFT_Blit_002: %d pixel failures\n", total_fail);
    return (total_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;
ErrorHandler:
    if (dst_buf.handle != NULL) vg_lite_free(&dst_buf);
    if (src_buf.handle != NULL) vg_lite_free(&src_buf);
    return error;
}

/* Test 3: Rotate matrix, different formats, no blending. */
static vg_lite_error_t SFT_Blit_003(void)
{
    vg_lite_buffer_t src_buf, dst_buf;
    int i, j;
    float x, y;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc = 0xffffffff;
    vg_lite_matrix_t matrix;
    vg_lite_float_t degrees;
    int32_t width, height;
    int total_fail = 0;
    int img_idx = 0;

    memset(&src_buf, 0, sizeof(src_buf));
    memset(&dst_buf, 0, sizeof(dst_buf));

    for (i = 0; i < NUM_SRC_FORMATS; i++) {
        width = (int32_t)Random_r(1.0f, WINDSIZEX);
        height = (int32_t)Random_r(1.0f, WINDSIZEY);
        CHECK_GEN(gen_buffer(0, &src_buf, formats[i], width, height));

        for (j = 0; j < NUM_DST_FORMATS; j++) {
            width = (int32_t)Random_r(1.0f, WINDSIZEX);
            height = (int32_t)Random_r(1.0f, WINDSIZEY);
            CHECK_GEN(gen_buffer(0, &dst_buf, formats[j], width, height));

            degrees = (vg_lite_float_t)Random_r(-360.0f, 360.0f);
            x = dst_buf.width / 2.0f;
            y = dst_buf.height / 2.0f;

            vg_lite_identity(&matrix);
            vg_lite_translate(x, y, &matrix);
            vg_lite_rotate(degrees, &matrix);
            printf("  blit with rotation: %f degrees.\n", degrees);

            CHECK_ERROR(vg_lite_clear(&dst_buf, NULL, cc));
            CHECK_ERROR(vg_lite_blit(&dst_buf, &src_buf, &matrix, VG_LITE_BLEND_NONE, 0, filter));
            CHECK_ERROR(vg_lite_finish());

            {
                int tol = get_tolerance(dst_buf.format, VG_LITE_BLEND_NONE) + 4;
                vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dst_buf.width, dst_buf.height, dst_buf.format);
                vg_lite_expected_clear(eb, NULL, cc);
                vg_lite_expected_blit(eb, &src_buf, &matrix, 0, filter);
                total_fail += vg_lite_expected_verify(eb, &dst_buf, tol);
                vg_lite_expected_destroy(eb);
            }

            char fname[128];
            snprintf(fname, sizeof(fname), "SFT_Blit_003_%d.png", img_idx++);
            vg_lite_save_png(fname, &dst_buf);

            Free_Buffer(&dst_buf);
            memset(&dst_buf, 0, sizeof(dst_buf));
        }
        Free_Buffer(&src_buf);
        memset(&src_buf, 0, sizeof(src_buf));
    }

    printf("  SFT_Blit_003: %d pixel failures\n", total_fail);
    return (total_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;
ErrorHandler:
    if (dst_buf.handle != NULL) vg_lite_free(&dst_buf);
    if (src_buf.handle != NULL) vg_lite_free(&src_buf);
    return error;
}

/* Test 4: Translate matrix, different formats, no blending. */
static vg_lite_error_t SFT_Blit_004(void)
{
    vg_lite_buffer_t src_buf, dst_buf;
    int i, j;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc = 0xffffffff;
    vg_lite_matrix_t matrix;
    vg_lite_float_t xOffs, yOffs;
    int32_t width, height;
    int total_fail = 0;
    int img_idx = 0;

    memset(&src_buf, 0, sizeof(src_buf));
    memset(&dst_buf, 0, sizeof(dst_buf));

    for (i = 0; i < NUM_SRC_FORMATS; i++) {
        width = (int32_t)Random_r(1.0f, WINDSIZEX);
        height = (int32_t)Random_r(1.0f, WINDSIZEY);
        CHECK_GEN(gen_buffer(0, &src_buf, formats[i], width, height));

        for (j = 0; j < NUM_DST_FORMATS; j++) {
            width = (int32_t)Random_r(1.0f, WINDSIZEX);
            height = (int32_t)Random_r(1.0f, WINDSIZEY);
            CHECK_GEN(gen_buffer(0, &dst_buf, formats[j], width, height));

            xOffs = (vg_lite_float_t)Random_r((float)-src_buf.width, (float)dst_buf.width);
            yOffs = (vg_lite_float_t)Random_r((float)-src_buf.height, (float)dst_buf.height);

            vg_lite_identity(&matrix);
            vg_lite_translate(xOffs, yOffs, &matrix);
            printf("  blit with translation: %f, %f\n", xOffs, yOffs);

            CHECK_ERROR(vg_lite_clear(&dst_buf, NULL, cc));
            CHECK_ERROR(vg_lite_blit(&dst_buf, &src_buf, &matrix, VG_LITE_BLEND_NONE, 0, filter));
            CHECK_ERROR(vg_lite_finish());

            {
                int tol = get_tolerance(dst_buf.format, VG_LITE_BLEND_NONE);
                vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dst_buf.width, dst_buf.height, dst_buf.format);
                vg_lite_expected_clear(eb, NULL, cc);
                vg_lite_expected_blit(eb, &src_buf, &matrix, 0, filter);
                total_fail += vg_lite_expected_verify(eb, &dst_buf, tol);
                vg_lite_expected_destroy(eb);
            }

            char fname[128];
            snprintf(fname, sizeof(fname), "SFT_Blit_004_%d.png", img_idx++);
            vg_lite_save_png(fname, &dst_buf);

            Free_Buffer(&dst_buf);
            memset(&dst_buf, 0, sizeof(dst_buf));
        }
        Free_Buffer(&src_buf);
        memset(&src_buf, 0, sizeof(src_buf));
    }

    printf("  SFT_Blit_004: %d pixel failures\n", total_fail);
    return (total_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;
ErrorHandler:
    if (dst_buf.handle != NULL) vg_lite_free(&dst_buf);
    if (src_buf.handle != NULL) vg_lite_free(&src_buf);
    return error;
}

/* Test 5: Perspective matrix, different formats, no blending. */
static vg_lite_error_t SFT_Blit_005(void)
{
    vg_lite_buffer_t src_buf, dst_buf;
    int i, j;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc = 0xffffffff;
    vg_lite_matrix_t matrix;
    vg_lite_float_t w0, w1;
    int32_t width, height;
    vg_lite_float_point4_t src, dst;
    int total_fail = 0;
    int img_idx = 0;

    memset(&src_buf, 0, sizeof(src_buf));
    memset(&dst_buf, 0, sizeof(dst_buf));

    for (i = 0; i < NUM_SRC_FORMATS; i++) {
        width = (int32_t)Random_r(1.0f, WINDSIZEX);
        height = (int32_t)Random_r(1.0f, WINDSIZEY);
        CHECK_GEN(gen_buffer(0, &src_buf, formats[i], width, height));

        for (j = 0; j < NUM_DST_FORMATS; j++) {
            width = (int32_t)Random_r(1.0f, WINDSIZEX);
            height = (int32_t)Random_r(1.0f, WINDSIZEY);
            CHECK_GEN(gen_buffer(0, &dst_buf, formats[j], width, height));

            w0 = (vg_lite_float_t)Random_r(0.0001f, 0.01f);
            w1 = (vg_lite_float_t)Random_r(0.0001f, 0.01f);

            src[0].x = 0.0f; src[0].y = 0.0f;
            src[1].x = (float)src_buf.width; src[1].y = 0.0f;
            src[2].x = (float)src_buf.width; src[2].y = (float)src_buf.height;
            src[3].x = 0.0f; src[3].y = (float)src_buf.height;

            dst[0].x = 0.0f; dst[0].y = 0.0f;
            dst[1].x = (float)src_buf.width / (w0 * (float)src_buf.width + 1) + 0.5f; dst[1].y = 0.0f;
            dst[2].x = (float)src_buf.width / (w0 * (float)src_buf.width + w1 * (float)src_buf.height + 1) + 0.5f;
            dst[2].y = (float)src_buf.height / (w0 * (float)src_buf.width + w1 * (float)src_buf.height + 1) + 0.5f;
            dst[3].x = 0.0f;
            dst[3].y = (float)src_buf.height / (w1 * (float)src_buf.height + 1) + 0.5f;

            vg_lite_identity(&matrix);
            get_transform_matrix_impl(src, dst, &matrix);
            printf("  blit with perspective factor: %f, %f\n", w0, w1);

            CHECK_ERROR(vg_lite_clear(&dst_buf, NULL, cc));
            CHECK_ERROR(vg_lite_blit(&dst_buf, &src_buf, &matrix, VG_LITE_BLEND_NONE, 0, filter));
            CHECK_ERROR(vg_lite_finish());

            {
                int tol = get_tolerance(dst_buf.format, VG_LITE_BLEND_NONE) + 2;
                vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dst_buf.width, dst_buf.height, dst_buf.format);
                vg_lite_expected_clear(eb, NULL, cc);
                vg_lite_expected_blit(eb, &src_buf, &matrix, 0, filter);
                total_fail += vg_lite_expected_verify(eb, &dst_buf, tol);
                vg_lite_expected_destroy(eb);
            }

            char fname[128];
            snprintf(fname, sizeof(fname), "SFT_Blit_005_%d.png", img_idx++);
            vg_lite_save_png(fname, &dst_buf);

            Free_Buffer(&dst_buf);
            memset(&dst_buf, 0, sizeof(dst_buf));
        }
        Free_Buffer(&src_buf);
        memset(&src_buf, 0, sizeof(src_buf));
    }

    printf("  SFT_Blit_005: %d pixel failures\n", total_fail);
    return (total_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;
ErrorHandler:
    if (dst_buf.handle != NULL) vg_lite_free(&dst_buf);
    if (src_buf.handle != NULL) vg_lite_free(&src_buf);
    return error;
}

/* Test 6: Combined scale+rotate+perspective, different formats, no blending. */
static vg_lite_error_t SFT_Blit_006(void)
{
    vg_lite_buffer_t src_buf, dst_buf;
    int i, j;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc = 0xffffffff;
    vg_lite_matrix_t matrix;
    vg_lite_float_t sx, sy, tx, ty, degrees, w0, w1;
    int32_t width, height;
    vg_lite_float_point4_t src, dst;
    int total_fail = 0;
    int img_idx = 0;

    memset(&src_buf, 0, sizeof(src_buf));
    memset(&dst_buf, 0, sizeof(dst_buf));

    for (i = 0; i < NUM_SRC_FORMATS; i++) {
        width = (int32_t)Random_r(1.0f, WINDSIZEX);
        height = (int32_t)Random_r(1.0f, WINDSIZEY);
        CHECK_GEN(gen_buffer(0, &src_buf, formats[i], width, height));

        for (j = 0; j < NUM_DST_FORMATS; j++) {
            width = (int32_t)Random_r(1.0f, WINDSIZEX);
            height = (int32_t)Random_r(1.0f, WINDSIZEY);
            CHECK_GEN(gen_buffer(0, &dst_buf, formats[j], width, height));

            sx = (vg_lite_float_t)Random_r(-2.0f, 2.0f);
            sy = (vg_lite_float_t)Random_r(-2.0f, 2.0f);
            tx = (vg_lite_float_t)dst_buf.width / 2.0f;
            ty = (vg_lite_float_t)dst_buf.height / 2.0f;
            degrees = (vg_lite_float_t)Random_r(-360.0f, 360.0f);
            w0 = (vg_lite_float_t)Random_r(0.001f, 0.01f);
            w1 = (vg_lite_float_t)Random_r(0.001f, 0.01f);

            vg_lite_identity(&matrix);
            vg_lite_translate(tx, ty, &matrix);
            vg_lite_rotate(degrees, &matrix);
            vg_lite_scale(sx, sy, &matrix);

            src[0].x = 0.0f; src[0].y = 0.0f;
            src[1].x = (float)src_buf.width; src[1].y = 0.0f;
            src[2].x = (float)src_buf.width; src[2].y = (float)src_buf.height;
            src[3].x = 0.0f; src[3].y = (float)src_buf.height;

            dst[0].x = 0.0f; dst[0].y = 0.0f;
            dst[1].x = (float)src_buf.width / (w0 * (float)src_buf.width + 1) + 0.5f; dst[1].y = 0.0f;
            dst[2].x = (float)src_buf.width / (w0 * (float)src_buf.width + w1 * (float)src_buf.height + 1) + 0.5f;
            dst[2].y = (float)src_buf.height / (w0 * (float)src_buf.width + w1 * (float)src_buf.height + 1) + 0.5f;
            dst[3].x = 0.0f;
            dst[3].y = (float)src_buf.height / (w1 * (float)src_buf.height + 1) + 0.5f;

            get_transform_matrix_impl(src, dst, &matrix);

            printf("  blit rotate: %f, trans: %f, %f, perspective: %f, %f, scale: %f, %f\n",
                degrees, tx, ty, w0, w1, sx, sy);

            CHECK_ERROR(vg_lite_clear(&dst_buf, NULL, cc));
            CHECK_ERROR(vg_lite_blit(&dst_buf, &src_buf, &matrix, VG_LITE_BLEND_NONE, 0, filter));
            CHECK_ERROR(vg_lite_finish());

            {
                int tol = get_tolerance(dst_buf.format, VG_LITE_BLEND_NONE) + 2;
                vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dst_buf.width, dst_buf.height, dst_buf.format);
                vg_lite_expected_clear(eb, NULL, cc);
                vg_lite_expected_blit(eb, &src_buf, &matrix, 0, filter);
                total_fail += vg_lite_expected_verify(eb, &dst_buf, tol);
                vg_lite_expected_destroy(eb);
            }

            char fname[128];
            snprintf(fname, sizeof(fname), "SFT_Blit_006_%d.png", img_idx++);
            vg_lite_save_png(fname, &dst_buf);

            Free_Buffer(&dst_buf);
            memset(&dst_buf, 0, sizeof(dst_buf));
        }
        Free_Buffer(&src_buf);
        memset(&src_buf, 0, sizeof(src_buf));
    }

    printf("  SFT_Blit_006: %d pixel failures\n", total_fail);
    return (total_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;
ErrorHandler:
    if (dst_buf.handle != NULL) vg_lite_free(&dst_buf);
    if (src_buf.handle != NULL) vg_lite_free(&src_buf);
    return error;
}

/* Test 7: Same format, same size, different blend modes. */
static vg_lite_error_t SFT_Blit_007(void)
{
    int i, k;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_buffer_t srcbuffer;
    vg_lite_buffer_t dstbuffer;
    vg_lite_buffer_t tempBuffer;
    int32_t width, height;
    int total_fail = 0;
    int img_idx = 0;

    memset(&srcbuffer, 0, sizeof(srcbuffer));
    memset(&dstbuffer, 0, sizeof(dstbuffer));
    memset(&tempBuffer, 0, sizeof(tempBuffer));

    for (i = 0; i < NUM_BLEND_MODES; i++) {
        width = (int32_t)Random_r(1.0f, WINDSIZEX);
        height = (int32_t)Random_r(1.0f, WINDSIZEY);
        k = 0;
        CHECK_GEN(gen_buffer(1, &srcbuffer, formats[k], width, height));
        CHECK_GEN(gen_buffer(0, &dstbuffer, formats[k], width, height));

        /* Backup the dstbuffer before blit */
        CHECK_ERROR(Allocate_Buffer(&tempBuffer, dstbuffer.format, dstbuffer.width, dstbuffer.height));
        CHECK_ERROR(vg_lite_blit(&tempBuffer, &dstbuffer, &identity_matrix, VG_LITE_BLEND_NONE, 0, filter));
        CHECK_ERROR(vg_lite_finish());

        printf("  blit with format: %d, blend mode: %d\n", formats[k], blend_mode[i]);

        CHECK_ERROR(vg_lite_blit(&dstbuffer, &srcbuffer, &identity_matrix, blend_mode[i], 0, filter));
        CHECK_ERROR(vg_lite_finish());

        if (can_verify_blend(blend_mode[i])) {
            int tol = get_tolerance(dstbuffer.format, blend_mode[i]);
            vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dstbuffer.width, dstbuffer.height, dstbuffer.format);
            vg_lite_expected_copy(eb, &tempBuffer);
            vg_lite_expected_blit(eb, &srcbuffer, &identity_matrix, blend_mode[i], filter);
            total_fail += vg_lite_expected_verify(eb, &dstbuffer, tol);
            vg_lite_expected_destroy(eb);
        } else {
            printf("    blend mode %d: skipped verification (CPU not implemented)\n", blend_mode[i]);
        }

        char fname[128];
        snprintf(fname, sizeof(fname), "SFT_Blit_007_%d.png", img_idx++);
        vg_lite_save_png(fname, &dstbuffer);

        Free_Buffer(&srcbuffer);
        Free_Buffer(&dstbuffer);
        Free_Buffer(&tempBuffer);
        memset(&srcbuffer, 0, sizeof(srcbuffer));
        memset(&dstbuffer, 0, sizeof(dstbuffer));
        memset(&tempBuffer, 0, sizeof(tempBuffer));
    }

    printf("  SFT_Blit_007: %d pixel failures\n", total_fail);
    return (total_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;
ErrorHandler:
    if (dstbuffer.handle != NULL) vg_lite_free(&dstbuffer);
    if (srcbuffer.handle != NULL) vg_lite_free(&srcbuffer);
    if (tempBuffer.handle != NULL) vg_lite_free(&tempBuffer);
    return error;
}

/* Test 8: Different formats, same size, different blend modes. */
static vg_lite_error_t SFT_Blit_008(void)
{
    int i, j, k;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_buffer_t srcbuffer;
    vg_lite_buffer_t dstbuffer;
    vg_lite_buffer_t tempBuffer;
    int32_t width, height;
    int total_fail = 0;
    int img_idx = 0;

    memset(&srcbuffer, 0, sizeof(srcbuffer));
    memset(&dstbuffer, 0, sizeof(dstbuffer));
    memset(&tempBuffer, 0, sizeof(tempBuffer));

    width = (int32_t)Random_r(1.0f, WINDSIZEX);
    height = (int32_t)Random_r(1.0f, WINDSIZEY);

    for (i = 0; i < NUM_SRC_FORMATS; i++) {
        CHECK_GEN(gen_buffer(1, &srcbuffer, formats[i], width, height));

        for (j = 0; j < NUM_DST_FORMATS; j++) {
            if (i == j) continue;

            CHECK_GEN(gen_buffer(0, &dstbuffer, formats[j], width, height));
            CHECK_ERROR(Allocate_Buffer(&tempBuffer, dstbuffer.format, dstbuffer.width, dstbuffer.height));

            /* Backup the dstbuffer */
            CHECK_ERROR(vg_lite_blit(&tempBuffer, &dstbuffer, &identity_matrix, VG_LITE_BLEND_NONE, 0, filter));
            CHECK_ERROR(vg_lite_finish());

            for (k = 0; k < NUM_BLEND_MODES; k++) {
                CHECK_ERROR(vg_lite_blit(&dstbuffer, &srcbuffer, &identity_matrix, blend_mode[k], 0, filter));
                CHECK_ERROR(vg_lite_finish());

                if (can_verify_blend(blend_mode[k])) {
                    int tol = get_tolerance(dstbuffer.format, blend_mode[k]);
                    vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dstbuffer.width, dstbuffer.height, dstbuffer.format);
                    vg_lite_expected_copy(eb, &tempBuffer);
                    vg_lite_expected_blit(eb, &srcbuffer, &identity_matrix, blend_mode[k], filter);
                    int iter_fail = vg_lite_expected_verify(eb, &dstbuffer, tol);
                    if (iter_fail > 0) printf("    src=%d dst=%d blend=%d tol=%d: %d failures\n", formats[i], formats[j], blend_mode[k], tol, iter_fail);
                    total_fail += iter_fail;
                    vg_lite_expected_destroy(eb);
                } else {
                    printf("    blend mode %d: skipped verification\n", blend_mode[k]);
                }

                char fname[128];
                snprintf(fname, sizeof(fname), "SFT_Blit_008_%d.png", img_idx++);
                vg_lite_save_png(fname, &dstbuffer);

                /* Restore dstbuffer */
                CHECK_ERROR(vg_lite_blit(&dstbuffer, &tempBuffer, &identity_matrix, VG_LITE_BLEND_NONE, 0, filter));
                CHECK_ERROR(vg_lite_finish());
            }

            Free_Buffer(&dstbuffer);
            Free_Buffer(&tempBuffer);
            memset(&dstbuffer, 0, sizeof(dstbuffer));
            memset(&tempBuffer, 0, sizeof(tempBuffer));
        }

        Free_Buffer(&srcbuffer);
        memset(&srcbuffer, 0, sizeof(srcbuffer));
    }

    printf("  SFT_Blit_008: %d pixel failures\n", total_fail);
    return (total_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;
ErrorHandler:
    if (dstbuffer.handle != NULL) vg_lite_free(&dstbuffer);
    if (srcbuffer.handle != NULL) vg_lite_free(&srcbuffer);
    if (tempBuffer.handle != NULL) vg_lite_free(&tempBuffer);
    return error;
}

/* Test 9: Same format, different size, different blend modes. */
static vg_lite_error_t SFT_Blit_009(void)
{
    vg_lite_buffer_t src_buf, dst_buf;
    int i, j, nformats;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_buffer_t tempBuffer;
    int32_t width, height;
    int total_fail = 0;
    int img_idx = 0;

    memset(&src_buf, 0, sizeof(src_buf));
    memset(&dst_buf, 0, sizeof(dst_buf));
    memset(&tempBuffer, 0, sizeof(tempBuffer));

    nformats = (NUM_SRC_FORMATS > NUM_DST_FORMATS) ? NUM_DST_FORMATS : NUM_SRC_FORMATS;

    for (i = 0; i < nformats; i++) {
        width = (int32_t)Random_r(1.0f, WINDSIZEX);
        height = (int32_t)Random_r(1.0f, WINDSIZEY);

        CHECK_GEN(gen_buffer(1, &src_buf, formats[i], width, height));
        CHECK_GEN(gen_buffer(0, &dst_buf, formats[i], width, height));

        /* Backup the dst */
        CHECK_ERROR(Allocate_Buffer(&tempBuffer, dst_buf.format, dst_buf.width, dst_buf.height));
        CHECK_ERROR(vg_lite_blit(&tempBuffer, &dst_buf, &identity_matrix, VG_LITE_BLEND_NONE, 0, filter));
        CHECK_ERROR(vg_lite_finish());

        for (j = 0; j < NUM_BLEND_MODES; j++) {
            CHECK_ERROR(vg_lite_blit(&dst_buf, &src_buf, &identity_matrix, blend_mode[j], 0, filter));
            CHECK_ERROR(vg_lite_finish());

            if (can_verify_blend(blend_mode[j])) {
                int tol = get_tolerance(dst_buf.format, blend_mode[j]);
                vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dst_buf.width, dst_buf.height, dst_buf.format);
                vg_lite_expected_copy(eb, &tempBuffer);
                vg_lite_expected_blit(eb, &src_buf, &identity_matrix, blend_mode[j], filter);
                total_fail += vg_lite_expected_verify(eb, &dst_buf, tol);
                vg_lite_expected_destroy(eb);
            } else {
                printf("    blend mode %d: skipped verification\n", blend_mode[j]);
            }

            char fname[128];
            snprintf(fname, sizeof(fname), "SFT_Blit_009_%d.png", img_idx++);
            vg_lite_save_png(fname, &dst_buf);

            /* Restore the dst */
            CHECK_ERROR(vg_lite_blit(&dst_buf, &tempBuffer, &identity_matrix, VG_LITE_BLEND_NONE, 0, filter));
            CHECK_ERROR(vg_lite_finish());
        }

        Free_Buffer(&dst_buf);
        Free_Buffer(&tempBuffer);
        Free_Buffer(&src_buf);
        memset(&dst_buf, 0, sizeof(dst_buf));
        memset(&tempBuffer, 0, sizeof(tempBuffer));
        memset(&src_buf, 0, sizeof(src_buf));
    }

    printf("  SFT_Blit_009: %d pixel failures\n", total_fail);
    return (total_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;
ErrorHandler:
    if (dst_buf.handle != NULL) vg_lite_free(&dst_buf);
    if (src_buf.handle != NULL) vg_lite_free(&src_buf);
    if (tempBuffer.handle != NULL) vg_lite_free(&tempBuffer);
    return error;
}

/* Test 10: Different size, different formats, different blend modes. */
static vg_lite_error_t SFT_Blit_010(void)
{
    vg_lite_buffer_t src_buf, dst_buf;
    int i, j, k;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_buffer_t tempBuffer;
    int32_t width, height;
    int total_fail = 0;
    int img_idx = 0;

    memset(&src_buf, 0, sizeof(src_buf));
    memset(&dst_buf, 0, sizeof(dst_buf));
    memset(&tempBuffer, 0, sizeof(tempBuffer));

    for (i = 0; i < NUM_SRC_FORMATS; i++) {
        width = (int32_t)Random_r(1.0f, WINDSIZEX);
        height = (int32_t)Random_r(1.0f, WINDSIZEY);
        CHECK_GEN(gen_buffer(1, &src_buf, formats[i], width, height));

        for (j = 0; j < NUM_DST_FORMATS; j++) {
            width = (int32_t)Random_r(1.0f, WINDSIZEX);
            height = (int32_t)Random_r(1.0f, WINDSIZEY);
            CHECK_GEN(gen_buffer(1, &dst_buf, formats[j], width, height));

            /* Backup dst_buf */
            CHECK_ERROR(Allocate_Buffer(&tempBuffer, dst_buf.format, dst_buf.width, dst_buf.height));
            CHECK_ERROR(vg_lite_blit(&tempBuffer, &dst_buf, &identity_matrix, VG_LITE_BLEND_NONE, 0, filter));
            CHECK_ERROR(vg_lite_finish());

            for (k = 0; k < NUM_BLEND_MODES; k++) {
                CHECK_ERROR(vg_lite_blit(&dst_buf, &src_buf, &identity_matrix, blend_mode[k], 0, filter));
                CHECK_ERROR(vg_lite_finish());

                if (can_verify_blend(blend_mode[k])) {
                    int tol = get_tolerance(dst_buf.format, blend_mode[k]);
                    vg_lite_expected_buffer_t *eb = vg_lite_expected_create(dst_buf.width, dst_buf.height, dst_buf.format);
                    vg_lite_expected_copy(eb, &tempBuffer);
                    vg_lite_expected_blit(eb, &src_buf, &identity_matrix, blend_mode[k], filter);
                    total_fail += vg_lite_expected_verify(eb, &dst_buf, tol);
                    vg_lite_expected_destroy(eb);
                } else {
                    printf("    blend mode %d: skipped verification\n", blend_mode[k]);
                }

                char fname[128];
                snprintf(fname, sizeof(fname), "SFT_Blit_010_%d.png", img_idx++);
                vg_lite_save_png(fname, &dst_buf);

                /* Restore dst */
                CHECK_ERROR(vg_lite_blit(&dst_buf, &tempBuffer, &identity_matrix, VG_LITE_BLEND_NONE, 0, filter));
                CHECK_ERROR(vg_lite_finish());
            }

            Free_Buffer(&dst_buf);
            Free_Buffer(&tempBuffer);
            memset(&dst_buf, 0, sizeof(dst_buf));
            memset(&tempBuffer, 0, sizeof(tempBuffer));
        }

        Free_Buffer(&src_buf);
        memset(&src_buf, 0, sizeof(src_buf));
    }

    printf("  SFT_Blit_010: %d pixel failures\n", total_fail);
    return (total_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;
ErrorHandler:
    if (dst_buf.handle != NULL) vg_lite_free(&dst_buf);
    if (src_buf.handle != NULL) vg_lite_free(&src_buf);
    if (tempBuffer.handle != NULL) vg_lite_free(&tempBuffer);
    return error;
}

/* Main entry point */
int main(int argc, char *argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    int pass_count = 0;
    int fail_count = 0;
    int i;

    typedef vg_lite_error_t (*test_fn)(void);
    struct {
        const char *name;
        test_fn fn;
    } tests[] = {
        { "SFT_Blit_001", SFT_Blit_001 },
        { "SFT_Blit_002", SFT_Blit_002 },
        { "SFT_Blit_003", SFT_Blit_003 },
        { "SFT_Blit_004", SFT_Blit_004 },
        { "SFT_Blit_005", SFT_Blit_005 },
        { "SFT_Blit_006", SFT_Blit_006 },
        { "SFT_Blit_007", SFT_Blit_007 },
        { "SFT_Blit_008", SFT_Blit_008 },
        { "SFT_Blit_009", SFT_Blit_009 },
        { "SFT_Blit_010", SFT_Blit_010 },
    };

    int num_tests = sizeof(tests) / sizeof(tests[0]);

    printf("Initializing vg_lite...\n");
    fflush(stdout);
    CHECK_ERROR(vg_lite_init(0, 0));
    vg_lite_identity(&identity_matrix);
    printf("vg_lite initialized.\n");
    fflush(stdout);
    random_srand(32557);

    {
        vg_lite_buffer_t src, dst;
        vg_lite_filter_t f = VG_LITE_FILTER_POINT;
        memset(&src, 0, sizeof(src));
        memset(&dst, 0, sizeof(dst));
        printf("Quick test: gen_buffer src BGRA8888 64x64\n"); fflush(stdout);
        int rc = gen_buffer(0, &src, VG_LITE_BGRA8888, 64, 64);
        printf("gen_buffer src returned %d\n", rc); fflush(stdout);
        printf("Quick test: gen_buffer dst BGRA8888 64x64\n"); fflush(stdout);
        rc = gen_buffer(0, &dst, VG_LITE_BGRA8888, 64, 64);
        printf("gen_buffer dst returned %d\n", rc); fflush(stdout);
        printf("Quick test: clearing dst\n"); fflush(stdout);
        error = vg_lite_clear(&dst, NULL, 0xffffffff);
        printf("clear returned %d\n", error); fflush(stdout);
        printf("Quick test: blitting src to dst\n"); fflush(stdout);
        {
            vg_lite_matrix_t ident;
            vg_lite_identity(&ident);
            error = vg_lite_blit(&dst, &src, &ident, VG_LITE_BLEND_NONE, 0, f);
        }
        printf("blit returned %d\n", error); fflush(stdout);
        error = vg_lite_finish();
        printf("finish returned %d\n", error); fflush(stdout);
        vg_lite_free(&src);
        vg_lite_free(&dst);
        printf("Quick test: DONE\n\n"); fflush(stdout);
    }

    printf("\n=== SFT Blit Tests ===\n\n");

    for (i = 0; i < num_tests; i++) {
        vg_lite_error_t result;
        printf("Case: %s ::::::::::::: Started\n", tests[i].name);
        result = tests[i].fn();
        if (result == VG_LITE_SUCCESS) {
            printf("Case: %s ::::::::::::: PASS\n\n", tests[i].name);
            pass_count++;
        } else {
            printf("Case: %s ::::::::::::: FAIL (error=%d)\n\n", tests[i].name, result);
            fail_count++;
        }
    }

    printf("\n=== Results: %d passed, %d failed, %d total ===\n",
           pass_count, fail_count, num_tests);

    vg_lite_close();
    return (fail_count == 0) ? 0 : -1;

ErrorHandler:
    vg_lite_close();
    return -1;
}
