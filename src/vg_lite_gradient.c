#include "vg_lite.h"
#include "vg_lite_vulkan.h"
#include "vg_lite_format.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define GRAD_TEX_WIDTH 256

static uint8_t clamp_u8(float v)
{
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (uint8_t)(v + 0.5f);
}

static void unpack_color(uint32_t argb, uint8_t *a, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *a = (argb >> 24) & 0xFF;
    *r = (argb >> 16) & 0xFF;
    *g = (argb >> 8) & 0xFF;
    *b = argb & 0xFF;
}

static uint32_t pack_pixel(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    /* Image format is VG_LITE_BGRA8888 -> VK_FORMAT_B8G8R8A8_UNORM.
     * Memory byte order on little-endian: B(byte0), G(byte1), R(byte2), A(byte3). */
    return (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16) | ((uint32_t)a << 24);
}

vg_lite_error_t vg_lite_init_grad(vg_lite_linear_gradient_t *grad)
{
    if (!grad) return VG_LITE_INVALID_ARGUMENT;
    memset(grad, 0, sizeof(*grad));
    grad->image.width = GRAD_TEX_WIDTH;
    grad->image.height = 1;
    grad->image.format = VG_LITE_BGRA8888;
    return vg_lite_allocate(&grad->image);
}

vg_lite_error_t vg_lite_set_grad(vg_lite_linear_gradient_t *grad,
                                  uint32_t count,
                                  uint32_t *colors,
                                  uint32_t *stops)
{
    if (!grad || !colors || !stops || count == 0) return VG_LITE_INVALID_ARGUMENT;
    if (count > VLC_MAX_GRADIENT_STOPS) count = VLC_MAX_GRADIENT_STOPS;
    grad->count = count;
    memcpy(grad->colors, colors, count * sizeof(uint32_t));
    memcpy(grad->stops, stops, count * sizeof(uint32_t));
    return VG_LITE_SUCCESS;
}

static void sort_stops(vg_lite_linear_gradient_t *grad)
{
    for (uint32_t i = 0; i < grad->count - 1; i++) {
        for (uint32_t j = i + 1; j < grad->count; j++) {
            if (grad->stops[i] > grad->stops[j]) {
                uint32_t tmp_s = grad->stops[i];
                grad->stops[i] = grad->stops[j];
                grad->stops[j] = tmp_s;
                uint32_t tmp_c = grad->colors[i];
                grad->colors[i] = grad->colors[j];
                grad->colors[j] = tmp_c;
            }
        }
    }
}

vg_lite_error_t vg_lite_update_grad(vg_lite_linear_gradient_t *grad)
{
    if (!grad || !grad->image.handle) return VG_LITE_INVALID_ARGUMENT;
    if (grad->count == 0) return VG_LITE_SUCCESS;

    sort_stops(grad);

    uint32_t *pixels = (uint32_t *)grad->image.memory;
    if (!pixels) return VG_LITE_INVALID_ARGUMENT;

    uint32_t max_stop = grad->stops[grad->count - 1];
    if (max_stop == 0) max_stop = 1;
    float scale = (float)GRAD_TEX_WIDTH / (float)max_stop;

    for (int x = 0; x < GRAD_TEX_WIDTH; x++) {
        float pos = (float)x / scale;

        uint32_t idx = 0;
        for (uint32_t i = 0; i < grad->count; i++) {
            if (pos >= grad->stops[i]) idx = i;
        }

        uint8_t a0, r0, g0, b0, a1, r1, g1, b1;
        unpack_color(grad->colors[idx], &a0, &r0, &g0, &b0);

        if (idx >= grad->count - 1) {
            pixels[x] = pack_pixel(a0, r0, g0, b0);
        } else {
            unpack_color(grad->colors[idx + 1], &a1, &r1, &g1, &b1);
            float range = (float)(grad->stops[idx + 1] - grad->stops[idx]);
            float t = (range > 0.0f) ? (pos - (float)grad->stops[idx]) / range : 0.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            pixels[x] = pack_pixel(
                clamp_u8(a0 + (a1 - a0) * t),
                clamp_u8(r0 + (r1 - r0) * t),
                clamp_u8(g0 + (g1 - g0) * t),
                clamp_u8(b0 + (b1 - b0) * t)
            );
        }
    }

    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_clear_grad(vg_lite_linear_gradient_t *grad)
{
    if (!grad) return VG_LITE_INVALID_ARGUMENT;
    if (grad->image.handle) {
        vg_lite_free(&grad->image);
    }
    memset(grad, 0, sizeof(*grad));
    return VG_LITE_SUCCESS;
}

vg_lite_matrix_t *vg_lite_get_grad_matrix(vg_lite_linear_gradient_t *grad)
{
    if (!grad) return NULL;
    return &grad->matrix;
}

static void sort_color_ramp(vg_lite_color_ramp_t *ramp, uint32_t count)
{
    for (uint32_t i = 0; i < count - 1; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            if (ramp[i].stop > ramp[j].stop) {
                vg_lite_color_ramp_t tmp = ramp[i];
                ramp[i] = ramp[j];
                ramp[j] = tmp;
            }
        }
    }
}

static uint32_t sample_ramp(vg_lite_color_ramp_t *ramp, uint32_t count, float t)
{
    if (count == 0) return 0xFF000000;
    if (count == 1) {
        uint8_t a = clamp_u8(ramp[0].alpha * 255.0f);
        uint8_t r = clamp_u8(ramp[0].red * 255.0f);
        uint8_t g = clamp_u8(ramp[0].green * 255.0f);
        uint8_t b = clamp_u8(ramp[0].blue * 255.0f);
        return pack_pixel(a, r, g, b);
    }

    if (t <= ramp[0].stop) {
        uint8_t a = clamp_u8(ramp[0].alpha * 255.0f);
        uint8_t r = clamp_u8(ramp[0].red * 255.0f);
        uint8_t g = clamp_u8(ramp[0].green * 255.0f);
        uint8_t b = clamp_u8(ramp[0].blue * 255.0f);
        return pack_pixel(a, r, g, b);
    }
    if (t >= ramp[count - 1].stop) {
        uint8_t a = clamp_u8(ramp[count - 1].alpha * 255.0f);
        uint8_t r = clamp_u8(ramp[count - 1].red * 255.0f);
        uint8_t g = clamp_u8(ramp[count - 1].green * 255.0f);
        uint8_t b = clamp_u8(ramp[count - 1].blue * 255.0f);
        return pack_pixel(a, r, g, b);
    }

    uint32_t idx = 0;
    for (uint32_t i = 0; i < count - 1; i++) {
        if (t >= ramp[i].stop && t <= ramp[i + 1].stop) {
            idx = i;
            break;
        }
    }

    float range = ramp[idx + 1].stop - ramp[idx].stop;
    float frac = (range > 0.0f) ? (t - ramp[idx].stop) / range : 0.0f;

    uint8_t a = clamp_u8((ramp[idx].alpha + (ramp[idx + 1].alpha - ramp[idx].alpha) * frac) * 255.0f);
    uint8_t r = clamp_u8((ramp[idx].red + (ramp[idx + 1].red - ramp[idx].red) * frac) * 255.0f);
    uint8_t g = clamp_u8((ramp[idx].green + (ramp[idx + 1].green - ramp[idx].green) * frac) * 255.0f);
    uint8_t b = clamp_u8((ramp[idx].blue + (ramp[idx + 1].blue - ramp[idx].blue) * frac) * 255.0f);
    return pack_pixel(a, r, g, b);
}

vg_lite_error_t vg_lite_set_radial_grad(vg_lite_radial_gradient_t *grad,
                                         uint32_t count,
                                         vg_lite_color_ramp_t *color_ramp,
                                         vg_lite_radial_gradient_parameter_t grad_param,
                                         vg_lite_gradient_spreadmode_t spread_mode,
                                         uint8_t pre_multiplied)
{
    if (!grad || !color_ramp || count == 0) return VG_LITE_INVALID_ARGUMENT;
    if (count > VLC_MAX_COLOR_RAMP_STOPS) count = VLC_MAX_COLOR_RAMP_STOPS;

    grad->count = count;
    grad->ramp_length = count;
    memcpy(grad->color_ramp, color_ramp, count * sizeof(vg_lite_color_ramp_t));
    grad->radial_grad = grad_param;
    grad->spread_mode = spread_mode;
    grad->pre_multiplied = pre_multiplied;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_update_radial_grad(vg_lite_radial_gradient_t *grad)
{
    if (!grad) return VG_LITE_INVALID_ARGUMENT;
    if (grad->ramp_length == 0) return VG_LITE_SUCCESS;

    sort_color_ramp(grad->color_ramp, grad->ramp_length);

    float cx = grad->radial_grad.cx;
    float cy = grad->radial_grad.cy;
    float r = grad->radial_grad.r;
    float fx = grad->radial_grad.fx;
    float fy = grad->radial_grad.fy;

    if (r <= 0.0f) r = 1.0f;

    int size = (int)(r * 2.0f);
    if (size < 2) size = 2;

    grad->image.width = size;
    grad->image.height = size;
    grad->image.format = VG_LITE_BGRA8888;

    vg_lite_error_t err = vg_lite_allocate(&grad->image);
    if (err != VG_LITE_SUCCESS) return err;

    uint32_t *pixels = (uint32_t *)grad->image.memory;
    if (!pixels) {
        vg_lite_free(&grad->image);
        return VG_LITE_INVALID_ARGUMENT;
    }

    uint32_t stride_u32 = grad->image.stride / 4;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = (float)x - fx;
            float dy = (float)y - fy;
            float dist = sqrtf(dx * dx + dy * dy);
            float t = dist / r;

            uint32_t c = sample_ramp(grad->color_ramp, grad->ramp_length, t);

            pixels[y * stride_u32 + x] = c;
        }
    }

    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_clear_radial_grad(vg_lite_radial_gradient_t *grad)
{
    if (!grad) return VG_LITE_INVALID_ARGUMENT;
    if (grad->image.handle) {
        vg_lite_free(&grad->image);
    }
    memset(grad, 0, sizeof(*grad));
    return VG_LITE_SUCCESS;
}

vg_lite_matrix_t *vg_lite_get_radial_grad_matrix(vg_lite_radial_gradient_t *grad)
{
    if (!grad) return NULL;
    return &grad->matrix;
}
