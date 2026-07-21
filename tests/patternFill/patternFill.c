#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vg_lite.h"
#include "vg_lite_util.h"

#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 480

static vg_lite_buffer_t buffer;
static vg_lite_buffer_t image;

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

static vg_lite_path_t path = {
    {-10, -10, 10, 10},
    VG_LITE_HIGH,
    VG_LITE_S8,
    {0},
    sizeof(path_data),
    path_data,
    1
};

void cleanup(void)
{
    if (buffer.handle != NULL) {
        vg_lite_free(&buffer);
    }
    if (image.handle != NULL) {
        vg_lite_free(&image);
    }
    vg_lite_close();
}

int main(int argc, const char* argv[])
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    vg_lite_filter_t filter = VG_LITE_FILTER_POINT;
    vg_lite_matrix_t matrix, matPath;
    char filename[64];
    int frames = 2;
    
    vg_lite_pattern_mode_t pattern_mode[4] = {
        VG_LITE_PATTERN_REPEAT,
        VG_LITE_PATTERN_REFLECT,
        VG_LITE_PATTERN_COLOR,
        VG_LITE_PATTERN_PAD,
    };
    
    error = vg_lite_init(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed: %d\n", error);
        return -1;
    }
    
    if (vg_lite_load_raw(&image, "data/landscape.raw") != 0) {
        printf("load raw file error\n");
        cleanup();
        return -1;
    }
    
    buffer.width = DEFAULT_WIDTH;
    buffer.height = DEFAULT_HEIGHT;
    buffer.format = VG_LITE_RGB565;
    
    error = vg_lite_allocate(&buffer);
    if (error == VG_LITE_NOT_SUPPORT) {
        printf("[fallback] RGB565 linear color-att unsupported on this GPU, retry with BGRA8888\n");
        buffer.format = VG_LITE_BGRA8888;
        error = vg_lite_allocate(&buffer);
    }
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_allocate failed: %d\n", error);
        cleanup();
        return -1;
    }
    
    printf("Framebuffer size: %d x %d\n", DEFAULT_WIDTH, DEFAULT_HEIGHT);
    
    for (int i = 0; i < frames; i++)
    {
        error = vg_lite_clear(&buffer, NULL, 0xFFFF0000);
        if (error != VG_LITE_SUCCESS) {
            printf("vg_lite_clear failed: %d\n", error);
            goto ErrorHandler;
        }
        
        vg_lite_identity(&matrix);
        vg_lite_translate(DEFAULT_WIDTH / 2.0f, DEFAULT_HEIGHT / 4.0f, &matrix);
        vg_lite_rotate(33.0f, &matrix);
        vg_lite_scale(0.4f, 0.4f, &matrix);
        vg_lite_translate(DEFAULT_WIDTH / -2.0f, DEFAULT_HEIGHT / -4.0f, &matrix);
        
        vg_lite_identity(&matPath);
        vg_lite_translate(DEFAULT_WIDTH / 2.0f, DEFAULT_HEIGHT / 4.0f, &matPath);
        vg_lite_scale(10, 10, &matPath);
        
        error = vg_lite_draw_pattern(&buffer, &path, VG_LITE_FILL_EVEN_ODD, &matPath,
                                      &image, &matrix, VG_LITE_BLEND_NONE,
                                      pattern_mode[i % 4], 0xffaabbcc, 0, filter);
        if (error != VG_LITE_SUCCESS) {
            printf("vg_lite_draw_pattern failed: %d\n", error);
            goto ErrorHandler;
        }
        
        vg_lite_identity(&matrix);
        vg_lite_translate(DEFAULT_WIDTH / 2.0f, DEFAULT_HEIGHT / 1.2f, &matrix);
        vg_lite_rotate(33.0f, &matrix);
        vg_lite_scale(0.4f, 0.4f, &matrix);
        vg_lite_translate(DEFAULT_WIDTH / -1.5f, DEFAULT_HEIGHT / -2.0f, &matrix);
        
        vg_lite_identity(&matPath);
        vg_lite_translate(DEFAULT_WIDTH / 2.0f, DEFAULT_HEIGHT / 1.3f, &matPath);
        vg_lite_scale(10, 10, &matPath);
        
        error = vg_lite_draw_pattern(&buffer, &path, VG_LITE_FILL_EVEN_ODD, &matPath,
                                      &image, &matrix, VG_LITE_BLEND_NONE,
                                      pattern_mode[(i + 1) % 4], 0xffaabbcc, 0, filter);
        if (error != VG_LITE_SUCCESS) {
            printf("vg_lite_draw_pattern (2nd) failed: %d\n", error);
            goto ErrorHandler;
        }
        
        error = vg_lite_finish();
        if (error != VG_LITE_SUCCESS) {
            printf("vg_lite_finish failed: %d\n", error);
            goto ErrorHandler;
        }
        
        sprintf(filename, "pattern_%d.png", i);
        vg_lite_save_png(filename, &buffer);
        printf("frame:%d Done! Saved to %s\n", i, filename);
    }
    
ErrorHandler:
    cleanup();
    return (error == VG_LITE_SUCCESS) ? 0 : -1;
}