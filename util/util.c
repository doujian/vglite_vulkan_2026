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

/* VGLite blit pixel semantics: inv(M)*(x+0.5,y+0.5) -> source coord.
 * In source range: sample src + blend with dst_px. Out of range: keep dst_px.
 * is_bilinear: 0=POINT (nearest), 1=BI_LINEAR (bilinear interpolation). */
static uint32_t compute_expected_blit_pixel(vg_lite_buffer_t *src,
                                             vg_lite_float_t inv[3][3],
                                             int x, int y,
                                             uint32_t dst_px,
                                             int blend_mode, int is_bilinear)
{
    float sx, sy;
    int inside_src = transform_point(inv, (float)x + 0.5f, (float)y + 0.5f, &sx, &sy)
                  && sx >= 0 && sy >= 0
                  && sx < (float)src->width && sy < (float)src->height;

    if (!inside_src) return dst_px;

    int sr, sg, sb, sa;

    if (!is_bilinear) {
        int ix = (int)sx, iy = (int)sy;
        if (ix >= (int)src->width)  ix = src->width - 1;
        if (iy >= (int)src->height) iy = src->height - 1;
        unpack_rgba(vg_lite_read_pixel(src, ix, iy), &sr, &sg, &sb, &sa);
    } else {
        int x0 = (int)sx, y0 = (int)sy;
        int x1 = x0 + 1, y1 = y0 + 1;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= (int)src->width)  x1 = src->width - 1;
        if (y1 >= (int)src->height) y1 = src->height - 1;

        float fx = sx - (int)sx, fy = sy - (int)sy;
        if (fx < 0) fx = 0;
        if (fy < 0) fy = 0;

        int r00, g00, b00, a00, r10, g10, b10, a10;
        int r01, g01, b01, a01, r11, g11, b11, a11;
        unpack_rgba(vg_lite_read_pixel(src, x0, y0), &r00, &g00, &b00, &a00);
        unpack_rgba(vg_lite_read_pixel(src, x1, y0), &r10, &g10, &b10, &a10);
        unpack_rgba(vg_lite_read_pixel(src, x0, y1), &r01, &g01, &b01, &a01);
        unpack_rgba(vg_lite_read_pixel(src, x1, y1), &r11, &g11, &b11, &a11);

        sr = (int)(r00*(1-fx)*(1-fy) + r10*fx*(1-fy) + r01*(1-fx)*fy + r11*fx*fy + 0.5f);
        sg = (int)(g00*(1-fx)*(1-fy) + g10*fx*(1-fy) + g01*(1-fx)*fy + g11*fx*fy + 0.5f);
        sb = (int)(b00*(1-fx)*(1-fy) + b10*fx*(1-fy) + b01*(1-fx)*fy + b11*fx*fy + 0.5f);
        sa = (int)(a00*(1-fx)*(1-fy) + a10*fx*(1-fy) + a01*(1-fx)*fy + a11*fx*fy + 0.5f);
    }

    int dr, dg, db, da;
    unpack_rgba(dst_px, &dr, &dg, &db, &da);

    int or_, og, ob, oa;
    switch (blend_mode) {
    case 0: /* BLEND_NONE */
        or_ = sr; og = sg; ob = sb; oa = sa;
        break;
    case 1: /* BLEND_SRC_OVER: S + D*(1-Sa), A: Sa + Da*(1-Sa) */
        or_ = (sr * sa + dr * (255 - sa)) / 255;
        og  = (sg * sa + dg * (255 - sa)) / 255;
        ob  = (sb * sa + db * (255 - sa)) / 255;
        oa  = sa + da * (255 - sa) / 255;
        break;
    case 11: /* BLEND_NORMAL_LVGL / PREMULTIPLY_SRC_OVER: S*Sa + D*(1-Sa), A: 0xFF */
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
    uint32_t c = color;
    if (eb->format == VG_LITE_RGB565) c |= 0xFF000000;

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

    for (int y = 0; y < eb->height; y++) {
        for (int x = 0; x < eb->width; x++) {
            uint32_t expected = eb->pixels[y * eb->width + x];
            uint32_t got = vg_lite_read_pixel(actual, x, y);

            int ar, ag, ab, aa, er, eg, eb_, ea;
            unpack_rgba(got, &ar, &ag, &ab, &aa);
            unpack_rgba(expected, &er, &eg, &eb_, &ea);
            if (actual->format == VG_LITE_RGB565) { aa = 0xFF; ea = 0xFF; }

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
