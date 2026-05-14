#include "vg_lite.h"
#include "vg_lite_util.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
    case VG_LITE_RGBA8888:
        return r | (g << 8) | (b << 16) | (a << 24);
    case VG_LITE_BGRA8888:
        return b | (g << 8) | (r << 16) | (a << 24);
    case VG_LITE_RGB565:
        return ((r & 0xf8) >> 3) | ((g & 0xfc) << 3) | ((b & 0xf8) << 8);
    case VG_LITE_BGR565:
        return ((b & 0xf8) >> 3) | ((g & 0xfc) << 3) | ((r & 0xf8) << 8);
    case VG_LITE_RGBA4444:
        return ((r & 0xf0) >> 4) | (g & 0xf0) | ((b & 0xf0) << 4) | ((a & 0xf0) << 8);
    case VG_LITE_BGRA4444:
        return ((b & 0xf0) >> 4) | (g & 0xf0) | ((r & 0xf0) << 4) | ((a & 0xf0) << 8);
    case VG_LITE_A8:
        return a;
    case VG_LITE_L8:
        return (uint32_t)(0.2126f * r + 0.7152f * g + 0.0722f * b);
    case VG_LITE_RGBX8888:
        return r | (g << 8) | (b << 16);
    case VG_LITE_BGRX8888:
        return b | (g << 8) | (r << 16);
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
        return ((r << 3) | (r >> 2)) | (((g << 2) | (g >> 4)) << 8) | (((b << 3) | (b >> 2)) << 16) | (0xFF << 24);
    }
    case VG_LITE_BGR565: {
        uint16_t p = *(uint16_t*)(ptr + y * buffer->stride + x * 2);
        uint8_t b = (p >> 11) & 0x1F;
        uint8_t g = (p >> 5) & 0x3F;
        uint8_t r = p & 0x1F;
        return ((r << 3) | (r >> 2)) | (((g << 2) | (g >> 4)) << 8) | (((b << 3) | (b >> 2)) << 16) | (0xFF << 24);
    }
    case VG_LITE_RGBA4444: {
        uint16_t p = *(uint16_t*)(ptr + y * buffer->stride + x * 2);
        uint8_t r = (p >> 4) & 0xF;
        uint8_t g = p & 0xF;
        uint8_t b = (p >> 8) & 0xF;
        uint8_t a = (p >> 12) & 0xF;
        return ((r << 4) | (r >> 0)) | (((g << 4) | (g >> 0)) << 8) |
               (((b << 4) | (b >> 0)) << 16) | (((a << 4) | (a >> 0)) << 24);
    }
    case VG_LITE_BGRA4444: {
        uint16_t p = *(uint16_t*)(ptr + y * buffer->stride + x * 2);
        uint8_t b = (p >> 4) & 0xF;
        uint8_t g = p & 0xF;
        uint8_t r = (p >> 8) & 0xF;
        uint8_t a = (p >> 12) & 0xF;
        return ((r << 4) | (r >> 0)) | (((g << 4) | (g >> 0)) << 8) |
               (((b << 4) | (b >> 0)) << 16) | (((a << 4) | (a >> 0)) << 24);
    }
    case VG_LITE_BGRA8888: {
        uint32_t p = *(uint32_t*)(ptr + y * buffer->stride + x * 4);
        uint8_t b = p & 0xFF;
        uint8_t g = (p >> 8) & 0xFF;
        uint8_t r = (p >> 16) & 0xFF;
        uint8_t a = (p >> 24) & 0xFF;
        return r | (g << 8) | (b << 16) | (a << 24);
    }
    case VG_LITE_BGRX8888: {
        uint32_t p = *(uint32_t*)(ptr + y * buffer->stride + x * 4);
        uint8_t b = p & 0xFF;
        uint8_t g = (p >> 8) & 0xFF;
        uint8_t r = (p >> 16) & 0xFF;
        return r | (g << 8) | (b << 16) | (0xFF << 24);
    }
    case VG_LITE_RGBX8888: {
        uint32_t p = *(uint32_t*)(ptr + y * buffer->stride + x * 4);
        uint8_t r = p & 0xFF;
        uint8_t g = (p >> 8) & 0xFF;
        uint8_t b = (p >> 16) & 0xFF;
        return r | (g << 8) | (b << 16) | (0xFF << 24);
    }
    case VG_LITE_A8: {
        uint8_t a = *(ptr + y * buffer->stride + x);
        return a | (a << 8) | (a << 16) | (a << 24);
    }
    case VG_LITE_L8: {
        uint8_t l = *(ptr + y * buffer->stride + x);
        return l | (l << 8) | (l << 16) | (0xFF << 24);
    }
    case VG_LITE_RGBA8888: {
        uint32_t p = *(uint32_t*)(ptr + y * buffer->stride + x * 4);
        uint8_t r = p & 0xFF;
        uint8_t g = (p >> 8) & 0xFF;
        uint8_t b = (p >> 16) & 0xFF;
        uint8_t a = (p >> 24) & 0xFF;
        return r | (g << 8) | (b << 16) | (a << 24);
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

void unpack_rgba(uint32_t pixel, int *r, int *g, int *b, int *a)
{
    *r = pixel & 0xFF;
    *g = (pixel >> 8) & 0xFF;
    *b = (pixel >> 16) & 0xFF;
    *a = (pixel >> 24) & 0xFF;
}

static int mat3_inverse(vg_lite_float_t m[3][3], vg_lite_float_t inv[3][3])
{
    float det = m[0][0]*(m[1][1]*m[2][2] - m[1][2]*m[2][1])
              - m[0][1]*(m[1][0]*m[2][2] - m[1][2]*m[2][0])
              + m[0][2]*(m[1][0]*m[2][1] - m[1][1]*m[2][0]);
    if (fabsf(det) < 1e-6f) return 0;

    float idet = 1.0f / det;
    inv[0][0] = (m[1][1]*m[2][2] - m[1][2]*m[2][1]) * idet;
    inv[0][1] = (m[0][2]*m[2][1] - m[0][1]*m[2][2]) * idet;
    inv[0][2] = (m[0][1]*m[1][2] - m[0][2]*m[1][1]) * idet;
    inv[1][0] = (m[1][2]*m[2][0] - m[1][0]*m[2][2]) * idet;
    inv[1][1] = (m[0][0]*m[2][2] - m[0][2]*m[2][0]) * idet;
    inv[1][2] = (m[0][2]*m[1][0] - m[0][0]*m[1][2]) * idet;
    inv[2][0] = (m[1][0]*m[2][1] - m[1][1]*m[2][0]) * idet;
    inv[2][1] = (m[0][1]*m[2][0] - m[0][0]*m[2][1]) * idet;
    inv[2][2] = (m[0][0]*m[1][1] - m[0][1]*m[1][0]) * idet;
    return 1;
}

static int transform_point(vg_lite_float_t inv[3][3], float dx, float dy, float *sx, float *sy)
{
    float w = inv[2][0]*dx + inv[2][1]*dy + inv[2][2];
    if (fabsf(w) < 1e-6f) return 0;
    *sx = (inv[0][0]*dx + inv[0][1]*dy + inv[0][2]) / w;
    *sy = (inv[1][0]*dx + inv[1][1]*dy + inv[1][2]) / w;
    return 1;
}

/* Vulkan spec §15.1 LINEAR sampling:
 *   texel center at (i+0.5)/size, i = floor(u - 0.5), alpha = frac(u - 0.5)
 *   result = tex[i]*(1-a) + tex[i+1]*a, clamp-to-edge: i,i+1 ∈ [0,size-1] */
static void vulkan_linear_sample(vg_lite_buffer_t *src, float sx, float sy,
                                  int *sr, int *sg, int *sb, int *sa)
{
    int w = (int)src->width, h = (int)src->height;

    float ux = sx - 0.5f, uy = sy - 0.5f;
    int ix = (int)floorf(ux), iy = (int)floorf(uy);
    float fx = ux - floorf(ux), fy = uy - floorf(uy);

    int x0 = ix < 0 ? 0 : (ix >= w ? w - 1 : ix);
    int x1 = ix + 1; if (x1 < 0) x1 = 0; else if (x1 >= w) x1 = w - 1;
    int y0 = iy < 0 ? 0 : (iy >= h ? h - 1 : iy);
    int y1 = iy + 1; if (y1 < 0) y1 = 0; else if (y1 >= h) y1 = h - 1;

    int r00, g00, b00, a00, r10, g10, b10, a10;
    int r01, g01, b01, a01, r11, g11, b11, a11;
    unpack_rgba(vg_lite_read_pixel(src, x0, y0), &r00, &g00, &b00, &a00);
    unpack_rgba(vg_lite_read_pixel(src, x1, y0), &r10, &g10, &b10, &a10);
    unpack_rgba(vg_lite_read_pixel(src, x0, y1), &r01, &g01, &b01, &a01);
    unpack_rgba(vg_lite_read_pixel(src, x1, y1), &r11, &g11, &b11, &a11);

    float w00 = (1-fx)*(1-fy), w10 = fx*(1-fy), w01 = (1-fx)*fy, w11 = fx*fy;
    *sr = (int)(r00*w00 + r10*w10 + r01*w01 + r11*w11 + 0.5f);
    *sg = (int)(g00*w00 + g10*w10 + g01*w01 + g11*w11 + 0.5f);
    *sb = (int)(b00*w00 + b10*w10 + b01*w01 + b11*w11 + 0.5f);
    *sa = (int)(a00*w00 + a10*w10 + a01*w01 + a11*w11 + 0.5f);
}

/* Nearest neighbor sampling: snap to nearest texel center */
static void vulkan_nearest_sample(vg_lite_buffer_t *src, float sx, float sy,
                                   int *sr, int *sg, int *sb, int *sa)
{
    int w = (int)src->width, h = (int)src->height;
    int ix = (int)floorf(sx + 0.5f);
    int iy = (int)floorf(sy + 0.5f);
    if (ix < 0) ix = 0; else if (ix >= w) ix = w - 1;
    if (iy < 0) iy = 0; else if (iy >= h) iy = h - 1;
    unpack_rgba(vg_lite_read_pixel(src, ix, iy), sr, sg, sb, sa);
}

static uint32_t compute_expected_blit_pixel(vg_lite_buffer_t *src,
                                             vg_lite_float_t inv[3][3],
                                             int x, int y,
                                             uint32_t dst_px,
                                             int blend_mode, int is_bilinear)
{
    float sx, sy;
    int has_src = transform_point(inv, (float)x + 0.5f, (float)y + 0.5f, &sx, &sy);
    if (!has_src) return dst_px;

    float sw = (float)src->width, sh = (float)src->height;
    float src_uv_x = sx / sw, src_uv_y = sy / sh;
    if (src_uv_x < -0.001f || src_uv_x > 1.001f || src_uv_y < -0.001f || src_uv_y > 1.001f)
        return dst_px;
    if (src_uv_x < 0.0f) src_uv_x = 0.0f;
    else if (src_uv_x > 1.0f) src_uv_x = 1.0f;
    if (src_uv_y < 0.0f) src_uv_y = 0.0f;
    else if (src_uv_y > 1.0f) src_uv_y = 1.0f;
    sx = src_uv_x * sw;
    sy = src_uv_y * sh;

    int sr, sg, sb, sa;
    if (is_bilinear)
        vulkan_linear_sample(src, sx, sy, &sr, &sg, &sb, &sa);
    else
        vulkan_nearest_sample(src, sx, sy, &sr, &sg, &sb, &sa);

    int dr, dg, db, da;
    unpack_rgba(dst_px, &dr, &dg, &db, &da);

    int or_, og, ob, oa;
    switch (blend_mode) {
    case 0: /* NONE: S */
        or_ = sr; og = sg; ob = sb; oa = sa;
        break;
    case 1: /* SRC_OVER: S + D*(1-Sa) */
        or_ = sr + (dr * (255 - sa) + 127) / 255;
        og  = sg + (dg * (255 - sa) + 127) / 255;
        ob  = sb + (db * (255 - sa) + 127) / 255;
        oa  = sa + (da * (255 - sa) + 127) / 255;
        break;
    case 2: /* DST_OVER: S*(1-Da) + D */
        or_ = (sr * (255 - da) + 127) / 255 + dr;
        og  = (sg * (255 - da) + 127) / 255 + dg;
        ob  = (sb * (255 - da) + 127) / 255 + db;
        oa  = (sa * (255 - da) + 127) / 255 + da;
        break;
    case 3: /* SRC_IN: S*Da */
        or_ = (sr * da + 127) / 255;
        og  = (sg * da + 127) / 255;
        ob  = (sb * da + 127) / 255;
        oa  = (sa * da + 127) / 255;
        break;
    case 4: /* DST_IN: D*Sa */
        or_ = (dr * sa + 127) / 255;
        og  = (dg * sa + 127) / 255;
        ob  = (db * sa + 127) / 255;
        oa  = (da * sa + 127) / 255;
        break;
    case 5: /* MULTIPLY: S*(1-Da) + D*(1-Sa) + S*D */
        or_ = (sr * (255 - da) + dr * (255 - sa) + sr * dr + 127) / 255;
        og  = (sg * (255 - da) + dg * (255 - sa) + sg * dg + 127) / 255;
        ob  = (sb * (255 - da) + db * (255 - sa) + sb * db + 127) / 255;
        oa  = (sa * (255 - da) + da * (255 - sa) + sa * da + 127) / 255;
        break;
    case 6: /* SCREEN: S + D - S*D */
        or_ = (sr * 255 + dr * 255 - sr * dr + 127) / 255;
        og  = (sg * 255 + dg * 255 - sg * dg + 127) / 255;
        ob  = (sb * 255 + db * 255 - sb * db + 127) / 255;
        oa  = (sa * 255 + da * 255 - sa * da + 127) / 255;
        break;
    case 9: /* ADDITIVE: S + D */
        or_ = sr + dr;
        og  = sg + dg;
        ob  = sb + db;
        oa  = sa + da;
        break;
    case 10: /* SUBTRACT: D*(1-Sa) */
        or_ = (dr * (255 - sa) + 127) / 255;
        og  = (dg * (255 - sa) + 127) / 255;
        ob  = (db * (255 - sa) + 127) / 255;
        oa  = (da * (255 - sa) + 127) / 255;
        break;
    case 11: /* NORMAL_LVGL (premultiplied): S*Sa + D*(1-Sa) */
        or_ = (sr * sa + dr * (255 - sa)) / 255;
        og  = (sg * sa + dg * (255 - sa)) / 255;
        ob  = (sb * sa + db * (255 - sa)) / 255;
        oa  = 0xFF;
        break;
    default:
        or_ = sr; og = sg; ob = sb; oa = sa;
        break;
    }
    if (or_ > 255) or_ = 255; if (or_ < 0) or_ = 0;
    if (og > 255) og = 255;  if (og < 0) og = 0;
    if (ob > 255) ob = 255;  if (ob < 0) ob = 0;
    if (oa > 255) oa = 255;  if (oa < 0) oa = 0;
    return or_ | (og << 8) | (ob << 16) | (oa << 24);
}

struct vg_lite_expected_buffer {
    uint32_t *pixels;
    int width;
    int height;
    vg_lite_buffer_format_t format;
};

vg_lite_expected_buffer_t *vg_lite_expected_create(int width, int height,
                                                    vg_lite_buffer_format_t format)
{
    vg_lite_expected_buffer_t *eb = calloc(1, sizeof(*eb));
    if (!eb) return NULL;
    eb->width = width;
    eb->height = height;
    eb->format = format;
    eb->pixels = calloc((size_t)width * height, sizeof(uint32_t));
    if (!eb->pixels) { free(eb); return NULL; }
    return eb;
}

void vg_lite_expected_destroy(vg_lite_expected_buffer_t *eb)
{
    if (!eb) return;
    free(eb->pixels);
    free(eb);
}

void vg_lite_expected_clear(vg_lite_expected_buffer_t *eb,
                             vg_lite_rectangle_t *rect,
                             vg_lite_color_t color)
{
    if (!eb) return;

    /* VGLite color is 0xAARRGGBB: A at bits 24-31, R at bits 16-23, G at bits 8-15, B at bits 0-7 */
    uint8_t a = (color >> 24) & 0xFF;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b = (color)       & 0xFF;
    uint32_t rgba8888 = r | (g << 8) | (b << 16) | (a << 24);

    uint32_t c;
    switch (eb->format) {
    case VG_LITE_RGB565:
    case VG_LITE_BGR565:
    case VG_LITE_RGBX8888:
    case VG_LITE_BGRX8888:
        c = rgba8888 | 0xFF000000;
        break;
    case VG_LITE_A8:
        c = a * 0x01010101u;
        break;
    case VG_LITE_L8: {
        uint8_t lum = (uint8_t)(0.2126f * r + 0.7152f * g + 0.0722f * b + 0.5f);
        c = lum | (lum << 8) | (lum << 16) | (0xFFu << 24);
        break;
    }
    default:
        c = rgba8888;
        break;
    }

    int x0 = 0, y0 = 0, x1 = eb->width, y1 = eb->height;
    if (rect) {
        x0 = rect->x; y0 = rect->y;
        x1 = rect->x + rect->width;
        y1 = rect->y + rect->height;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > eb->width)  x1 = eb->width;
    if (y1 > eb->height) y1 = eb->height;

    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            eb->pixels[y * eb->width + x] = c;
}

void vg_lite_expected_blit(vg_lite_expected_buffer_t *eb,
                            vg_lite_buffer_t *src,
                            vg_lite_matrix_t *matrix,
                            int blend_mode, int filter)
{
    if (!eb || !src) return;
    vg_lite_float_t inv[3][3];
    if (!mat3_inverse(matrix->m, inv)) return;

    int is_bilinear = (filter != 0 && filter != VG_LITE_FILTER_POINT);

    for (int y = 0; y < eb->height; y++) {
        for (int x = 0; x < eb->width; x++) {
            uint32_t dst_px = eb->pixels[y * eb->width + x];
            eb->pixels[y * eb->width + x] = compute_expected_blit_pixel(
                src, inv, x, y, dst_px, blend_mode, is_bilinear);
        }
    }
}

int vg_lite_expected_verify(vg_lite_expected_buffer_t *eb,
                             vg_lite_buffer_t *actual,
                             int tolerance)
{
    if (!eb || !actual) return -1;
    int total = eb->width * eb->height;
    int fail = 0;
    int max_print = 10;
    int is_l8 = (actual->format == VG_LITE_L8 || actual->format == VG_LITE_A8);
    int is_565 = (actual->format == VG_LITE_RGB565 || actual->format == VG_LITE_BGR565);
    int is_4444 = (actual->format == VG_LITE_RGBA4444 || actual->format == VG_LITE_BGRA4444);

    for (int y = 0; y < eb->height; y++) {
        for (int x = 0; x < eb->width; x++) {
            uint32_t expected = eb->pixels[y * eb->width + x];
            uint32_t got = vg_lite_read_pixel(actual, x, y);

            int ar, ag, ab, aa, er, eg, eb_, ea;
            unpack_rgba(got, &ar, &ag, &ab, &aa);
            unpack_rgba(expected, &er, &eg, &eb_, &ea);

            if (is_l8) {
                int el = (int)(0.2126f * er + 0.7152f * eg + 0.0722f * eb_ + 0.5f);
                if (actual->format == VG_LITE_A8) el = ea;
                er = el; eg = el; eb_ = el; ea = 0xFF;
                ar = ar; ag = ar; ab = ar; aa = 0xFF;
            } else if (is_565) {
                int r5 = er >> 3, g6 = eg >> 2, b5 = eb_ >> 3;
                er = (r5 << 3) | (r5 >> 2); eg = (g6 << 2) | (g6 >> 4); eb_ = (b5 << 3) | (b5 >> 2);
                ea = 0xFF; aa = 0xFF;
            } else if (is_4444) {
                er = (er >> 4) << 4; eg = (eg >> 4) << 4;
                eb_ = (eb_ >> 4) << 4; ea = (ea >> 4) << 4;
            }

            if (abs(ar - er) > tolerance || abs(ag - eg) > tolerance ||
                abs(ab - eb_) > tolerance || abs(aa - ea) > tolerance) {
                if (fail < max_print)
                    printf("  MISMATCH (%d,%d): got R=%d G=%d B=%d A=%d, exp R=%d G=%d B=%d A=%d\n",
                           x, y, ar, ag, ab, aa, er, eg, eb_, ea);
                fail++;
            }
        }
    }

    if (fail > max_print) printf("  ... %d more mismatches\n", fail - max_print);
    int pass_rate = (total > 0) ? ((total - fail) * 100) / total : 100;
    printf("  VERIFY: %d/%d pixels match (%d%% pass rate)\n", total - fail, total, pass_rate);
    return fail;
}

void vg_lite_expected_copy(vg_lite_expected_buffer_t *eb, vg_lite_buffer_t *buf)
{
    if (!eb || !buf) return;
    for (int y = 0; y < eb->height; y++)
        for (int x = 0; x < eb->width; x++)
            eb->pixels[y * eb->width + x] = vg_lite_read_pixel(buf, x, y);
}

static void *gen_checker(vg_lite_buffer_format_t format, uint32_t width, uint32_t height)
{
    int checker = 20;
    int color0[4], color1[4];
    int bpp = get_bpp(format);
    uint32_t *pdata32;
    uint16_t *pdata16;
    uint8_t  *pdata8;
    void     *pdata;
    uint32_t pixel[2];
    int x, y, idx;

    color0[0] = (int)Random_r(0, 255);
    color0[1] = (int)Random_r(0, 255);
    color0[2] = (int)Random_r(0, 255);
    color0[3] = (int)Random_r(0, 255);
    color1[0] = (int)Random_r(0, 255);
    color1[1] = (int)Random_r(0, 255);
    color1[2] = (int)Random_r(0, 255);
    color1[3] = (int)Random_r(0, 255);

    pdata = malloc((size_t)(bpp / 8) * width * height);
    pdata32 = (uint32_t *)pdata;
    pdata16 = (uint16_t *)pdata;
    pdata8  = (uint8_t  *)pdata;

    pixel[0] = pack_pixel(format, color0[0], color0[1], color0[2], color0[3]);
    pixel[1] = pack_pixel(format, color1[0], color1[1], color1[2], color1[3]);

    for (y = 0; y < (int)height; y++) {
        for (x = 0; x < (int)width; x++) {
            idx = ((x / checker) + (y / checker)) % 2;
            switch (bpp) {
            case 32: *pdata32++ = pixel[idx];            break;
            case 16: *pdata16++ = (uint16_t)pixel[idx];  break;
            case 8:  *pdata8++  = (uint8_t)pixel[idx];   break;
            default: break;
            }
        }
    }
    return pdata;
}

static void *gen_gradient(vg_lite_buffer_format_t format, uint32_t width, uint32_t height)
{
    int orient;
    int color0[4], color1[4], color[4];
    int x, y;
    float ratio;
    uint32_t pixel;
    uint32_t *pdata32;
    uint16_t *pdata16;
    uint8_t  *pdata8;
    void     *pdata;
    int bpp = get_bpp(format);

    pdata = malloc((size_t)(bpp / 8) * width * height);
    pdata32 = (uint32_t *)pdata;
    pdata16 = (uint16_t *)pdata;
    pdata8  = (uint8_t  *)pdata;

    orient = (int)Random_r(0, 1);

    color0[0] = (int)Random_r(0, 255);
    color0[1] = (int)Random_r(0, 255);
    color0[2] = (int)Random_r(0, 255);
    color0[3] = (int)Random_r(0, 255);
    color1[0] = (int)Random_r(0, 255);
    color1[1] = (int)Random_r(0, 255);
    color1[2] = (int)Random_r(0, 255);
    color1[3] = (int)Random_r(0, 255);

    for (y = 0; y < (int)height; y++) {
        for (x = 0; x < (int)width; x++) {
            if (orient == 0)
                ratio = (float)x / (width - 1);
            else
                ratio = (float)y / (height - 1);

            color[0] = (int)(ratio * color1[0] + (1.0f - ratio) * color0[0]);
            color[1] = (int)(ratio * color1[1] + (1.0f - ratio) * color0[1]);
            color[2] = (int)(ratio * color1[2] + (1.0f - ratio) * color0[2]);
            color[3] = (int)(ratio * color1[3] + (1.0f - ratio) * color0[3]);

            pixel = pack_pixel(format, color[0], color[1], color[2], color[3]);
            switch (bpp) {
            case 32: *pdata32++ = pixel;                   break;
            case 16: *pdata16++ = (uint16_t)pixel;         break;
            case 8:  *pdata8++  = (uint8_t)pixel;          break;
            default: break;
            }
        }
    }
    return pdata;
}

static void *gen_solid(vg_lite_buffer_format_t format, uint32_t width, uint32_t height)
{
    uint32_t r, g, b, a, pixel;
    void *data;
    uint8_t  *pdata8;
    uint32_t *pdata32;
    uint16_t *pdata16;
    uint32_t bpp = get_bpp(format);
    int i, j;

    r = (uint32_t)Random_r(0, 255);
    g = (uint32_t)Random_r(0, 255);
    b = (uint32_t)Random_r(0, 255);
    a = (uint32_t)Random_r(0, 255);

    data = malloc((size_t)width * height * bpp / 8);
    pdata8  = (uint8_t  *)data;
    pdata32 = (uint32_t *)data;
    pdata16 = (uint16_t *)data;

    pixel = pack_pixel(format, r, g, b, a);
    for (i = 0; i < (int)height; i++) {
        for (j = 0; j < (int)width; j++) {
            switch (bpp) {
            case 32: *pdata32++ = pixel;                   break;
            case 16: *pdata16++ = (uint16_t)pixel;         break;
            case 8:  *pdata8++  = (uint8_t)pixel;          break;
            default: break;
            }
        }
    }
    return data;
}

void *gen_image(int type, vg_lite_buffer_format_t format, uint32_t width, uint32_t height)
{
    switch (type) {
    case 0: return gen_checker(format, width, height);
    case 1: return gen_gradient(format, width, height);
    case 2: return gen_solid(format, width, height);
    default: return NULL;
    }
}

int gen_buffer(int type, vg_lite_buffer_t *buf, vg_lite_buffer_format_t format, uint32_t width, uint32_t height)
{
    vg_lite_error_t error;
    uint32_t bpp = get_bpp(format);
    void *data;

    memset(buf, 0, sizeof(*buf));
    buf->width  = width;
    buf->height = height;
    buf->format = format;

    error = vg_lite_allocate(buf);
    if (error != VG_LITE_SUCCESS) return -1;

    data = gen_image(type, format, width, height);
    if (!data) { vg_lite_free(buf); return -1; }

    /* Copy generated pixel data into the GPU buffer's memory.
     * The stride may include padding, so copy row-by-row. */
    {
        uint8_t *src = (uint8_t *)data;
        uint8_t *dst = (uint8_t *)buf->memory;
        uint32_t row_bytes = width * bpp / 8;
        for (uint32_t y = 0; y < height; y++) {
            memcpy(dst + y * buf->stride, src + y * row_bytes, row_bytes);
        }
    }

    free(data);
    return 0;
}
