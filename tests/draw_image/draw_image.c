//-----------------------------------------------------------------------------
// Port of Draw_Image test cases from VSI_CTS.
// Tests blitting of different src/dst formats, image modes, filters, blend modes.
// Uses Vulkan pipeline blend (not shader blend) for NONE and SRC_OVER.
// Format matrix: src {A8, RGB565, RGBX8888, ARGB8888, ARGB1555}
//              x tgt {A8, RGB565, RGBA8888, RGBX8888, RGBA5551} (5x5).
// Blend modes restricted to: VG_LITE_BLEND_NONE, VG_LITE_BLEND_SRC_OVER
//-----------------------------------------------------------------------------
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include "Common.h"
#include "vg_lite_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_GEN(fn) { int _rc = (fn); if (_rc != 0) { printf("[%s: %d] gen_buffer failed\n", __func__, __LINE__); error = VG_LITE_OUT_OF_MEMORY; goto ErrorHandler; } }

#define ALIGN(value, base)  ((value + base - 1) & ~(base - 1))

#define NUM_SRC_FORMATS 5
#define NUM_TGT_FORMATS 5
#define NUM_BLEND_MODES 2
#define NUM_IMAGE_MODES 3
#define NUM_FILTERS     3

static vg_lite_buffer_format_t src_formats[] = {
    VG_LITE_A8,
    VG_LITE_RGB565,
    VG_LITE_RGBX8888,
    VG_LITE_ARGB8888,
    VG_LITE_ARGB1555
};

static vg_lite_buffer_format_t tgt_formats[] = {
    VG_LITE_A8,
    VG_LITE_RGB565,
    VG_LITE_RGBA8888,
    VG_LITE_RGBX8888,
    VG_LITE_RGBA5551
};

static vg_lite_blend_t blend_modes[] = {
    VG_LITE_BLEND_NONE,
    VG_LITE_BLEND_SRC_OVER
};

static vg_lite_image_mode_t image_modes[] = {
    VG_LITE_NONE_IMAGE_MODE,
    VG_LITE_NORMAL_IMAGE_MODE,
    VG_LITE_MULTIPLY_IMAGE_MODE
};

static vg_lite_filter_t filters[] = {
    VG_LITE_FILTER_POINT,
    VG_LITE_FILTER_LINEAR,
    VG_LITE_FILTER_BI_LINEAR
};

static vg_lite_matrix_t identity_matrix;

static const char *format_name(vg_lite_buffer_format_t fmt)
{
    switch (fmt) {
    case VG_LITE_BGRA8888: return "BGRA8888";
    case VG_LITE_BGR565:   return "BGR565";
    case VG_LITE_A8:       return "A8";
    case VG_LITE_RGB565:   return "RGB565";
    case VG_LITE_RGBA8888: return "RGBA8888";
    case VG_LITE_RGBX8888: return "RGBX8888";
    case VG_LITE_ARGB8888: return "ARGB8888";
    case VG_LITE_ARGB1555: return "ARGB1555";
    case VG_LITE_RGBA5551: return "RGBA5551";
    default:                return "???";
    }
}

static const char *blend_name(vg_lite_blend_t b)
{
    switch (b) {
    case VG_LITE_BLEND_NONE:          return "NONE";
    case VG_LITE_BLEND_SRC_OVER:      return "SRC_OVER";
    case VG_LITE_BLEND_DST_OVER:      return "DST_OVER";
    case VG_LITE_BLEND_SRC_IN:        return "SRC_IN";
    case VG_LITE_BLEND_DST_IN:        return "DST_IN";
    case VG_LITE_BLEND_SCREEN:        return "SCREEN";
    case VG_LITE_BLEND_ADDITIVE:      return "ADDITIVE";
    case VG_LITE_BLEND_SUBTRACT:      return "SUBTRACT";
    case VG_LITE_BLEND_NORMAL_LVGL:   return "NORMAL_LVGL";
    case VG_LITE_BLEND_ADDITIVE_LVGL: return "ADDITIVE_LVGL";
    default:                          return "???";
    }
}

static const char *image_mode_name(vg_lite_image_mode_t m)
{
    switch (m) {
    case VG_LITE_NONE_IMAGE_MODE:     return "NONE";
    case VG_LITE_NORMAL_IMAGE_MODE:   return "NORMAL";
    case VG_LITE_MULTIPLY_IMAGE_MODE: return "MULTIPLY";
    default:                            return "???";
    }
}

static const char *filter_name(vg_lite_filter_t f)
{
    switch (f) {
    case VG_LITE_FILTER_POINT:     return "POINT";
    case VG_LITE_FILTER_LINEAR:    return "LINEAR";
    case VG_LITE_FILTER_BI_LINEAR: return "BILINEAR";
    default:                         return "???";
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
    buffer->tiled = VGLITE_TARGET_TILING;
    CHECK_ERROR(vg_lite_allocate(buffer));
ErrorHandler:
    return error;
}

static void Free_Buffer(vg_lite_buffer_t *buffer)
{
    vg_lite_free(buffer);
}

static int get_tolerance(vg_lite_buffer_format_t format, vg_lite_blend_t blend)
{
    int tol = 0;
    uint32_t bpp = vg_lite_format_bpp(format);
    if (bpp >= 32) tol = 0;
    else if (bpp >= 16) tol = 12;
    else tol = 8;

    switch (blend) {
    case VG_LITE_BLEND_SRC_OVER:
        tol += 1;
        break;
    default:
        break;
    }
    return tol;
}

/*
 * Draw_Image_001: Blit with different src/dst formats, image modes, filters, blend modes.
 * Uses Vulkan pipeline blend for NONE and SRC_OVER on BGRA8888/BGR565 targets.
 * Identity matrix, fixed buffer size (256x256).
 */
static vg_lite_error_t Draw_Image_001(void)
{
    vg_lite_buffer_t src_buf, dst_buf;
    int i, j, k, m, n;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc = 0xffa0a0a0;
    vg_lite_color_t image_cc = 0xff00ffff;
    int total_fail = 0;
    int case_idx = 0;
    int case_pass = 0;
    int case_fail = 0;

    for (i = 0; i < NUM_SRC_FORMATS; i++) {
        for (j = 0; j < NUM_TGT_FORMATS; j++) {
            for (k = 0; k < NUM_IMAGE_MODES; k++) {
                for (m = 0; m < NUM_FILTERS; m++) {
                    for (n = 0; n < NUM_BLEND_MODES; n++) {
                        printf("  [%03d] src=%-8s dst=%-8s imode=%-8s filter=%-8s blend=%-8s ",
                               case_idx,
                               format_name(src_formats[i]),
                               format_name(tgt_formats[j]),
                               image_mode_name(image_modes[k]),
                               filter_name(filters[m]),
                               blend_name(blend_modes[n]));
                        fflush(stdout);

                        memset(&src_buf, 0, sizeof(src_buf));
                        memset(&dst_buf, 0, sizeof(dst_buf));

                        CHECK_GEN(gen_buffer(i % 2, &src_buf, src_formats[i], ALIGN(256, 128), 256));
                        CHECK_ERROR(Allocate_Buffer(&dst_buf, tgt_formats[j], 256, 256));
                        CHECK_ERROR(vg_lite_clear(&dst_buf, NULL, cc));

                        src_buf.image_mode = image_modes[k];
                        CHECK_ERROR(vg_lite_blit(&dst_buf, &src_buf, &identity_matrix,
                                                  blend_modes[n], image_cc, filters[m]));
                        CHECK_ERROR(vg_lite_finish());

                        {
                            int tol = get_tolerance(dst_buf.format, blend_modes[n]);
                            if (filters[m] != VG_LITE_FILTER_POINT)
                                tol += 4;
                            vg_lite_expected_buffer_t *eb = vg_lite_expected_create(
                                dst_buf.width, dst_buf.height, dst_buf.format);
                            vg_lite_expected_clear(eb, NULL, cc);
                            vg_lite_expected_blit(eb, &src_buf, &identity_matrix,
                                                  (int)blend_modes[n], (int)filters[m],
                                                  (int)image_modes[k],
                                                  (src_formats[i] == VG_LITE_A8) ? 8 : 0,
                                                  image_cc, NULL);
                            int fail = vg_lite_expected_verify(eb, &dst_buf, tol);
                            if (fail == 0) {
                                printf("PASS\n");
                                case_pass++;
                            } else {
                                printf("FAIL (%d pixels)\n", fail);
                                case_fail++;
                                total_fail += fail;
                            }
                            vg_lite_expected_destroy(eb);
                        }

                        char fname[160];
                        snprintf(fname, sizeof(fname),
                                 "Draw_Image_001_%03d_src%s_tgt%s_im%s_flt%s_bl%s.png",
                                 case_idx,
                                 format_name(src_formats[i]),
                                 format_name(tgt_formats[j]),
                                 image_mode_name(image_modes[k]),
                                 filter_name(filters[m]),
                                 blend_name(blend_modes[n]));
                        vg_lite_save_png(fname, &dst_buf);

                        Free_Buffer(&dst_buf);
                        Free_Buffer(&src_buf);
                        case_idx++;
                    }
                }
            }
        }
    }

    printf("  Draw_Image_001: %d cases, %d passed, %d failed, %d total pixel failures\n",
           case_idx, case_pass, case_fail, total_fail);
    return (case_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;

ErrorHandler:
    if (dst_buf.handle) Free_Buffer(&dst_buf);
    if (src_buf.handle) Free_Buffer(&src_buf);
    return error;
}

/*
 * Draw_Image_002: Format cross-test with fixed blend=NONE, filter=POINT.
 * 2 src formats x 2 dst formats = 4 iterations.
 */
static vg_lite_error_t Draw_Image_002(void)
{
    vg_lite_buffer_t src_buf, dst_buf;
    int i, j;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc = 0xffa0a0a0;
    vg_lite_color_t image_cc = 0xff00ffff;
    int total_fail = 0;
    int case_idx = 0;
    int case_pass = 0;
    int case_fail = 0;

    for (i = 0; i < NUM_SRC_FORMATS; i++) {
        for (j = 0; j < NUM_TGT_FORMATS; j++) {
            printf("  [%03d] src=%-8s dst=%-8s blend=NONE     filter=POINT    ",
                   case_idx,
                   format_name(src_formats[i]),
                   format_name(tgt_formats[j]));
            fflush(stdout);

            memset(&src_buf, 0, sizeof(src_buf));
            memset(&dst_buf, 0, sizeof(dst_buf));

            CHECK_GEN(gen_buffer(i % 2, &src_buf, src_formats[i], ALIGN(256, 128), 256));
            CHECK_ERROR(Allocate_Buffer(&dst_buf, tgt_formats[j], 256, 256));
            CHECK_ERROR(vg_lite_clear(&dst_buf, NULL, cc));
            CHECK_ERROR(vg_lite_blit(&dst_buf, &src_buf, &identity_matrix,
                                      VG_LITE_BLEND_NONE, image_cc, VG_LITE_FILTER_POINT));
            CHECK_ERROR(vg_lite_finish());

            {
                int tol = get_tolerance(dst_buf.format, VG_LITE_BLEND_NONE);
                vg_lite_expected_buffer_t *eb = vg_lite_expected_create(
                    dst_buf.width, dst_buf.height, dst_buf.format);
                vg_lite_expected_clear(eb, NULL, cc);
                vg_lite_expected_blit(eb, &src_buf, &identity_matrix,
                                      0, 0, 0,
                                      (src_formats[i] == VG_LITE_A8) ? 8 : 0,
                                      image_cc, NULL);
                int fail = vg_lite_expected_verify(eb, &dst_buf, tol);
                if (fail == 0) {
                    printf("PASS\n");
                    case_pass++;
                } else {
                    printf("FAIL (%d pixels)\n", fail);
                    case_fail++;
                    total_fail += fail;
                }
                vg_lite_expected_destroy(eb);
            }

            char fname[160];
            snprintf(fname, sizeof(fname), "Draw_Image_002_%02d_src%s_tgt%s.png",
                     case_idx, format_name(src_formats[i]), format_name(tgt_formats[j]));
            vg_lite_save_png(fname, &dst_buf);

            Free_Buffer(&dst_buf);
            Free_Buffer(&src_buf);
            case_idx++;
        }
    }

    printf("  Draw_Image_002: %d cases, %d passed, %d failed, %d total pixel failures\n",
           case_idx, case_pass, case_fail, total_fail);
    return (case_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;

ErrorHandler:
    if (dst_buf.handle) Free_Buffer(&dst_buf);
    if (src_buf.handle) Free_Buffer(&src_buf);
    return error;
}

/*
 * Draw_Image_003: Fixed-function blend matrix.
 * All 9 fixed-blend-capable modes, every src format against every non-A8
 * target format (A8 targets lack a real dst-alpha channel, so blend modes
 * that reference Da/SRC_COLOR are skipped there), image_mode=NORMAL,
 * filter=POINT to isolate blend behavior.
 */
#define NUM_BLEND003_MODES 9

static vg_lite_blend_t blend003_modes[NUM_BLEND003_MODES] = {
    VG_LITE_BLEND_SRC_OVER,
    VG_LITE_BLEND_DST_OVER,
    VG_LITE_BLEND_SRC_IN,
    VG_LITE_BLEND_DST_IN,
    VG_LITE_BLEND_SCREEN,
    VG_LITE_BLEND_ADDITIVE,
    VG_LITE_BLEND_SUBTRACT,
    VG_LITE_BLEND_NORMAL_LVGL,
    VG_LITE_BLEND_ADDITIVE_LVGL,
};

static vg_lite_buffer_format_t tgt003_formats[] = {
    VG_LITE_RGB565,
    VG_LITE_RGBA8888,
    VG_LITE_RGBX8888,
    VG_LITE_RGBA5551,
};

static vg_lite_error_t Draw_Image_003(void)
{
    vg_lite_buffer_t src_buf, dst_buf;
    int i, j, n;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_color_t cc = 0xffa0a0a0;
    vg_lite_color_t image_cc = 0xff00ffff;
    int total_fail = 0;
    int case_idx = 0;
    int case_pass = 0;
    int case_fail = 0;
    int num_tgts = (int)(sizeof(tgt003_formats) / sizeof(tgt003_formats[0]));

    for (n = 0; n < NUM_BLEND003_MODES; n++) {
        for (i = 0; i < NUM_SRC_FORMATS; i++) {
            for (j = 0; j < num_tgts; j++) {
                printf("  [%03d] src=%-8s dst=%-8s imode=NORMAL   filter=POINT    blend=%-13s ",
                       case_idx,
                       format_name(src_formats[i]),
                       format_name(tgt003_formats[j]),
                       blend_name(blend003_modes[n]));
                fflush(stdout);

                memset(&src_buf, 0, sizeof(src_buf));
                memset(&dst_buf, 0, sizeof(dst_buf));

                CHECK_GEN(gen_buffer(i % 2, &src_buf, src_formats[i], ALIGN(256, 128), 256));
                CHECK_ERROR(Allocate_Buffer(&dst_buf, tgt003_formats[j], 256, 256));
                CHECK_ERROR(vg_lite_clear(&dst_buf, NULL, cc));

                src_buf.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
                CHECK_ERROR(vg_lite_blit(&dst_buf, &src_buf, &identity_matrix,
                                          blend003_modes[n], image_cc, VG_LITE_FILTER_POINT));
                CHECK_ERROR(vg_lite_finish());

                {
                    int tol = get_tolerance(dst_buf.format, blend003_modes[n]);
                    vg_lite_expected_buffer_t *eb = vg_lite_expected_create(
                        dst_buf.width, dst_buf.height, dst_buf.format);
                    vg_lite_expected_clear(eb, NULL, cc);
                    vg_lite_expected_blit(eb, &src_buf, &identity_matrix,
                                          (int)blend003_modes[n], (int)VG_LITE_FILTER_POINT,
                                          (int)VG_LITE_NORMAL_IMAGE_MODE,
                                          (src_formats[i] == VG_LITE_A8) ? 8 : 0,
                                          image_cc, NULL);
                    int fail = vg_lite_expected_verify(eb, &dst_buf, tol);
                    if (fail == 0) {
                        printf("PASS\n");
                        case_pass++;
                    } else {
                        printf("FAIL (%d pixels)\n", fail);
                        case_fail++;
                        total_fail += fail;
                    }
                    vg_lite_expected_destroy(eb);
                }

                char fname[160];
                snprintf(fname, sizeof(fname),
                         "Draw_Image_003_%03d_src%s_tgt%s_bl%s.png",
                         case_idx,
                         format_name(src_formats[i]),
                         format_name(tgt003_formats[j]),
                         blend_name(blend003_modes[n]));
                vg_lite_save_png(fname, &dst_buf);

                Free_Buffer(&dst_buf);
                Free_Buffer(&src_buf);
                case_idx++;
            }
        }
    }

    printf("  Draw_Image_003: %d cases, %d passed, %d failed, %d total pixel failures\n",
           case_idx, case_pass, case_fail, total_fail);
    return (case_fail == 0) ? VG_LITE_SUCCESS : VG_LITE_INVALID_ARGUMENT;

ErrorHandler:
    if (dst_buf.handle) Free_Buffer(&dst_buf);
    if (src_buf.handle) Free_Buffer(&src_buf);
    return error;
}

int main(int argc, char *argv[])
{
    vg_lite_error_t error;
    int pass_count = 0, fail_count = 0;
    int i;

    (void)argc; (void)argv;

    typedef vg_lite_error_t (*test_fn)(void);
    struct {
        const char *name;
        test_fn fn;
    } tests[] = {
        {"Draw_Image_001", Draw_Image_001},
        {"Draw_Image_002", Draw_Image_002},
        {"Draw_Image_003", Draw_Image_003},
    };

    int num_tests = sizeof(tests) / sizeof(tests[0]);

    printf("Initializing vg_lite...\n");
    fflush(stdout);
    CHECK_ERROR(vg_lite_init(0, 0));
    vg_lite_identity(&identity_matrix);
    printf("vg_lite initialized.\n");
    fflush(stdout);

    printf("\n=== Draw_Image Tests (Vulkan Pipeline Blend) ===\n");
    printf("Src formats: A8, RGB565, RGBX8888, ARGB8888, ARGB1555\n");
    printf("Dst formats: A8, RGB565, RGBA8888, RGBX8888, RGBA5551\n");
    printf("Blend modes: NONE (pipeline blendEnable=FALSE), SRC_OVER (pipeline blend)\n\n");

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
