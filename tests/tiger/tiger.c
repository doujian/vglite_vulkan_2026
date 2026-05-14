#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vg_lite.h"
#include "vg_lite_util.h"
#include "tiger_paths.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TIGER_WIDTH  640
#define TIGER_HEIGHT 480
#define GOLDEN_PATH  "golden/tiger.png"
#define OUTPUT_PATH  "tiger_output.png"

static vg_lite_buffer_t buffer;

static int compare_images(const char *golden_path, const char *output_path)
{
    int golden_w, golden_h, golden_n;
    int output_w, output_h, output_n;
    unsigned char *golden_data = stbi_load(golden_path, &golden_w, &golden_h, &golden_n, 4);
    unsigned char *output_data = stbi_load(output_path, &output_w, &output_h, &output_n, 4);
    
    if (!golden_data) {
        printf("Failed to load golden image: %s\n", golden_path);
        return -1;
    }
    if (!output_data) {
        printf("Failed to load output image: %s\n", output_path);
        stbi_image_free(golden_data);
        return -1;
    }
    
    if (golden_w != output_w || golden_h != output_h) {
        printf("Size mismatch: golden %dx%d, output %dx%d\n", golden_w, golden_h, output_w, output_h);
        stbi_image_free(golden_data);
        stbi_image_free(output_data);
        return -1;
    }
    
    int diff_pixels = 0;
    int total_pixels = golden_w * golden_h;
    for (int i = 0; i < total_pixels * 4; i += 4) {
        int dr = abs((int)golden_data[i] - (int)output_data[i]);
        int dg = abs((int)golden_data[i+1] - (int)output_data[i+1]);
        int db = abs((int)golden_data[i+2] - (int)output_data[i+2]);
        int da = abs((int)golden_data[i+3] - (int)output_data[i+3]);
        if (dr > 10 || dg > 10 || db > 10 || da > 10) {
            diff_pixels++;
        }
    }
    
    printf("Debug pixels (row, col):\n");
    int rows[] = {0, 50, 100, 150, 200, 250, 300, 350, 400, 450};
    for (int r = 0; r < 10; r++) {
        int row = rows[r];
        int col = 320;
        int idx = (row * golden_w + col) * 4;
        printf("  Row %d, Col %d: output=(%d,%d,%d,%d) golden=(%d,%d,%d,%d)\n",
               row, col,
               output_data[idx], output_data[idx+1], output_data[idx+2], output_data[idx+3],
               golden_data[idx], golden_data[idx+1], golden_data[idx+2], golden_data[idx+3]);
    }
    
    stbi_image_free(golden_data);
    stbi_image_free(output_data);
    
    float diff_percent = (float)diff_pixels / total_pixels * 100.0f;
    printf("Image comparison: %d/%d pixels differ (%.2f%%)\n", diff_pixels, total_pixels, diff_percent);
    
    return diff_percent < 5.0f ? 0 : 1;
}

int main(int argc, const char * argv[])
{
    vg_lite_error_t error;
    vg_lite_matrix_t matrix;
    
    error = vg_lite_init(TIGER_WIDTH, TIGER_HEIGHT);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_init failed: %d\n", error);
        return 1;
    }
    
    printf("Tiger test: %d x %d, %d paths\n", TIGER_WIDTH, TIGER_HEIGHT, pathCount);
    
    buffer.width = TIGER_WIDTH;
    buffer.height = TIGER_HEIGHT;
    buffer.format = VG_LITE_RGBA8888;
    
    error = vg_lite_allocate(&buffer);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_allocate failed: %d\n", error);
        vg_lite_close();
        return 1;
    }
    
    error = vg_lite_clear(&buffer, NULL, 0xFFFF0000);
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_clear failed: %d\n", error);
        goto cleanup;
    }
    
    vg_lite_identity(&matrix);
    /* Match reference implementation transform: translate + scale */
    vg_lite_translate(TIGER_WIDTH / 2 - 20 * TIGER_WIDTH / 640.0f,
                      TIGER_HEIGHT / 2 - 100 * TIGER_HEIGHT / 480.0f, &matrix);
    vg_lite_scale(4, 4, &matrix);
    vg_lite_scale(TIGER_WIDTH / 640.0f, TIGER_HEIGHT / 480.0f, &matrix);
printf("Matrix: translate + 4x scale (matching reference)\n");
    
    int test_limit = pathCount;
    printf("Testing %d paths\n", test_limit);
    for (int i = 0; i < test_limit; i++) {
        error = vg_lite_draw(&buffer, &path[i], VG_LITE_FILL_EVEN_ODD, &matrix, VG_LITE_BLEND_NONE, color_data[i]);
        if (error != VG_LITE_SUCCESS) {
            printf("vg_lite_draw path %d failed: %d\n", i, error);
        }
    }
    
    error = vg_lite_finish();
    if (error != VG_LITE_SUCCESS) {
        printf("vg_lite_finish failed: %d\n", error);
    }
    
    printf("Buffer after finish: width=%d, height=%d, format=%d\n", buffer.width, buffer.height, buffer.format);
    
    vg_lite_save_png(OUTPUT_PATH, &buffer);
    printf("Output saved to %s\n", OUTPUT_PATH);
    
    int result = compare_images(GOLDEN_PATH, OUTPUT_PATH);
    if (result == 0) {
        printf("PASS: Tiger output matches golden within tolerance\n");
    } else if (result == 1) {
        printf("FAIL: Tiger output differs from golden (>5%% pixels)\n");
    } else {
        printf("ERROR: Could not compare images\n");
    }

cleanup:
    vg_lite_free(&buffer);
    vg_lite_close();
    
    return result;
}
