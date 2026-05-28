#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"

static vg_lite_buffer_t target;
static vg_lite_buffer_t src_index8;
static uint32_t g_clut[256];

static void create_index8_image(vg_lite_buffer_t *buffer)
{
    uint8_t *p = (uint8_t*)buffer->memory;
    for (uint32_t i = 0; i < buffer->height; i++) {
        memset(p, i % 256, buffer->stride);
        p += buffer->stride;
    }
}

static void create_clut_table(void)
{
    g_clut[0]  = 0xff000000;
    g_clut[1]  = 0xffffffff;
    g_clut[2]  = 0xffff0000;
    g_clut[3]  = 0xff00ff00;
    g_clut[4]  = 0xff0000ff;
    g_clut[5]  = 0xffffff00;
    g_clut[6]  = 0xffff00ff;
    g_clut[7]  = 0xff00ffff;
    
    g_clut[15] = 0xff000000;
    g_clut[14] = 0xffffffff;
    g_clut[13] = 0xffff0000;
    g_clut[12] = 0xff00ff00;
    g_clut[11] = 0xff0000ff;
    g_clut[10] = 0xffffff00;
    g_clut[9]  = 0xffff00ff;
    g_clut[8]  = 0xff00ffff;
    
    for (int i = 16; i < 256; i++) {
        g_clut[i] = g_clut[i % 16];
    }
}

int main(int argc, const char *argv[])
{
    vg_lite_error_t error;
    vg_lite_matrix_t matrix;
    
    printf("=== imgIndex Test (INDEX_8 only) ===\n");
    
    /* Initialize vg_lite */
    error = vg_lite_init(320, 480);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed: %d\n", error);
        return 1;
    }
    printf("vg_lite_init OK\n");
    
    create_clut_table();
    error = vg_lite_set_CLUT(256, g_clut);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_set_CLUT failed: %d\n", error);
        vg_lite_close();
        return 1;
    }
    printf("CLUT set OK (256 colors)\n");
    
    src_index8.format = VG_LITE_INDEX_8;
    src_index8.width  = 256;
    src_index8.height = 256;
    error = vg_lite_allocate(&src_index8);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_allocate src_index8 failed: %d\n", error);
        vg_lite_close();
        return 1;
    }
    create_index8_image(&src_index8);
    printf("INDEX_8 source buffer OK (%ux%u)\n", src_index8.width, src_index8.height);
    
    target.format = VG_LITE_RGB565;
    target.width  = 320;
    target.height = 480;
    error = vg_lite_allocate(&target);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_allocate target failed: %d\n", error);
        vg_lite_free(&src_index8);
        vg_lite_close();
        return 1;
    }
    printf("RGB565 target buffer OK (%ux%u)\n", target.width, target.height);
    
    vg_lite_identity(&matrix);
    vg_lite_translate(target.width / 2.0f, target.height / 2.0f, &matrix);
    vg_lite_rotate(33.0f, &matrix);
    vg_lite_translate(-target.width / 2.0f, -target.height / 2.0f, &matrix);
    vg_lite_scale((vg_lite_float_t)target.width / (vg_lite_float_t)src_index8.width,
                  (vg_lite_float_t)target.height / (vg_lite_float_t)src_index8.height, &matrix);
    
    vg_lite_clear(&target, NULL, 0xFFaabbcc);
    printf("Target cleared\n");
    
    error = vg_lite_blit(&target, &src_index8, &matrix,
                         VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_BI_LINEAR);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_blit failed: %d\n", error);
        vg_lite_free(&target);
        vg_lite_free(&src_index8);
        vg_lite_close();
        return 1;
    }
    
    error = vg_lite_finish();
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_finish failed: %d\n", error);
        vg_lite_free(&target);
        vg_lite_free(&src_index8);
        vg_lite_close();
        return 1;
    }
printf("Blit finished\n");

vg_lite_save_png("imgIndex8_output.png", &target);
printf("Output saved to imgIndex8_output.png\n");

{
    vg_lite_expected_buffer_t *eb = vg_lite_expected_create(target.width, target.height, target.format);
    vg_lite_expected_clear(eb, NULL, 0xFFaabbcc);
    vg_lite_expected_blit(eb, &src_index8, &matrix, VG_LITE_BLEND_NONE, VG_LITE_FILTER_BI_LINEAR,
                           VG_LITE_NORMAL_IMAGE_MODE, 16, 0, g_clut);
    int fail = vg_lite_expected_verify(eb, &target, 16);
    vg_lite_expected_destroy(eb);
    printf("Golden verification: %d pixels mismatched\n", fail);
    if (fail == 0) printf("imgIndex test PASSED\n");
    else           printf("imgIndex test FAILED\n");
}

vg_lite_free(&target);
vg_lite_free(&src_index8);
vg_lite_close();
printf("=== Test Complete ===\n");
    
    return 0;
}