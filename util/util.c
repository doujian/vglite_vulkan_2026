#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float WINDSIZEX = 256.0f;
float WINDSIZEY = 256.0f;

char *error_type[] = {
    "VG_LITE_SUCCESS",
    "VG_LITE_INVALID_ARGUMENT",
    "VG_LITE_OUT_OF_MEMORY",
    "VG_LITE_NO_CONTEXT",
    "VG_LITE_TIMEOUT",
    "VG_LITE_OUT_OF_RESOURCES",
    "VG_LITE_GENERIC_IO",
    "VG_LITE_NOT_SUPPORT",
    "VG_LITE_ALREADY_EXISTS",
    "VG_LITE_NOT_ALIGNED",
    "VG_LITE_FLEXA_TIME_OUT",
    "VG_LITE_FLEXA_HANDSHAKE_FAIL",
};

static unsigned long int random_value = 32557;
#define RANDOM_MAX 32767

void random_srand(unsigned int seed)
{
    random_value = seed;
}

int random_rand(void)
{
    random_value = random_value * 1103515245 + 12345;
    return (unsigned int)(random_value / 65536) % 32768;
}

float Random_r(float Random_low, float Random_hi)
{
    float x;
    float y, z;

    x = (float)random_rand()/(RANDOM_MAX);
    y = (1 - x)*Random_low;
    z = x * Random_hi;
    return y + z ;
}

unsigned long int Random_i(unsigned long int Random_low, unsigned long int Random_hi)
{
    unsigned long int x, y;
    x = random_rand();
    if ( (y = (Random_hi - Random_low) ) > RANDOM_MAX)
        y = Random_low + y / (RANDOM_MAX) * x;
    else
        y = Random_low + (y * x + (RANDOM_MAX)/y) / (RANDOM_MAX);
    return y;
}

vg_lite_color_t GenColor_r(void)
{
    int r, g, b, a;
    vg_lite_color_t result;

    r = (int)Random_i(0, 255);
    g = (int)Random_i(0, 255);
    b = (int)Random_i(0, 255);
    a = (int)Random_i(0, 255);

    result = r | (g << 8) | (b << 16) | (a << 24);

    return result;
}

static int BMP_counter = 0;

void BMP_File_Name(char *name, char *new_name)
{
    char index_str[8];
    char surf[16];

    strcpy(new_name, name);
    sprintf(index_str, "%d", BMP_counter);
    strcat(new_name, index_str);
    BMP_counter++;

    sprintf(surf, ".png");
    strcat(new_name, surf);
}

void SaveBMP_SFT(char * name, vg_lite_buffer_t *buffer, BOOL save)
{
    char new_name[256];

    if (save)
    {
        BMP_File_Name(name, new_name);
        vg_lite_save_png(new_name, buffer);
    }
}

uint32_t get_bpp(vg_lite_buffer_format_t format)
{
    switch (format) {
    case VG_LITE_RGBA8888: case VG_LITE_BGRA8888: case VG_LITE_RGBX8888:
    case VG_LITE_BGRX8888: case VG_LITE_ARGB8888: case VG_LITE_ABGR8888:
        return 32;
    case VG_LITE_RGB565: case VG_LITE_BGR565:
    case VG_LITE_RGBA4444: case VG_LITE_BGRA4444:
        return 16;
    case VG_LITE_A8: case VG_LITE_L8: return 8;
    default: return 32;
    }
}

uint32_t pack_pixel(vg_lite_buffer_format_t format, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    switch (format) {
    case VG_LITE_RGB565:
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    case VG_LITE_BGRA8888:
        return b | (g << 8) | (r << 16) | (a << 24);
    case VG_LITE_RGBA8888:
    default:
        return r | (g << 8) | (b << 16) | (a << 24);
    }
}

int InitBMP(int width, int height) { (void)width; (void)height; return 0; }
void DestroyBMP(void) {}
int SaveBMP(char *image_name, unsigned char* p, int width, int height, vg_lite_buffer_format_t format, int stride)
{
    (void)image_name; (void)p; (void)width; (void)height; (void)format; (void)stride;
    return 0;
}

uint32_t vg_lite_read_pixel(vg_lite_buffer_t *buffer, int x, int y)
{
    unsigned char *ptr = (unsigned char *)buffer->memory;
    if (!ptr || x < 0 || y < 0 || x >= (int)buffer->width || y >= (int)buffer->height)
        return 0;

    switch (buffer->format) {
    case VG_LITE_RGB565: {
        uint16_t p = *(uint16_t*)(ptr + y * buffer->stride + x * 2);
        uint8_t r = (p >> 11) & 0x1F;
        uint8_t g = (p >> 5) & 0x3F;
        uint8_t b = p & 0x1F;
        return (r << 3) | ((g << 2) << 8) | ((b << 3) << 16) | (0xFF << 24);
    }
    case VG_LITE_BGRA8888: {
        uint32_t p = *(uint32_t*)(ptr + y * buffer->stride + x * 4);
        uint8_t b = p & 0xFF;
        uint8_t g = (p >> 8) & 0xFF;
        uint8_t r = (p >> 16) & 0xFF;
        uint8_t a = (p >> 24) & 0xFF;
        return r | (g << 8) | (b << 16) | (a << 24);
    }
    case VG_LITE_RGBA8888: {
        return *(uint32_t*)(ptr + y * buffer->stride + x * 4);
    }
    default:
        return *(uint32_t*)(ptr + y * buffer->stride + x * 4);
    }
}

int vg_lite_check_pixel(vg_lite_buffer_t *buffer, int x, int y, uint32_t expected, int tolerance)
{
    uint32_t actual = vg_lite_read_pixel(buffer, x, y);
    int r_act = actual & 0xFF, g_act = (actual >> 8) & 0xFF, b_act = (actual >> 16) & 0xFF, a_act = (actual >> 24) & 0xFF;
    int r_exp = expected & 0xFF, g_exp = (expected >> 8) & 0xFF, b_exp = (expected >> 16) & 0xFF, a_exp = (expected >> 24) & 0xFF;

    if (buffer->format == VG_LITE_RGB565) {
        a_exp = 0xFF;
        a_act = 0xFF;
    }

    if (abs(r_act - r_exp) > tolerance || abs(g_act - g_exp) > tolerance ||
        abs(b_act - b_exp) > tolerance || abs(a_act - a_exp) > tolerance) {
        printf("  PIXEL MISMATCH at (%d,%d): got R=%d G=%d B=%d A=%d, expected R=%d G=%d B=%d A=%d (tol=%d)\n",
               x, y, r_act, g_act, b_act, a_act, r_exp, g_exp, b_exp, a_exp, tolerance);
        return 0;
    }
    return 1;
}
