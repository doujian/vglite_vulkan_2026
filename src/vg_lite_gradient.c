#include "vg_lite.h"
#include "vg_lite_vulkan.h"
#include "vg_lite_format.h"
#include <stdlib.h>
#include <string.h>

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
    if (!grad) return VG_LITE_INVALID_ARGUMENT;

    /* Working arrays for validation + implicit stop insertion.
     * Max entries: user stops (up to 16) + 2 implicit (head + tail). */
    uint32_t valid_colors[VLC_MAX_GRADIENT_STOPS + 2];
    uint32_t valid_stops[VLC_MAX_GRADIENT_STOPS + 2];
    uint32_t valid_count = 0;

    /* Filter invalid stops: out-of-range (> 255) stops are dropped.
     * Out-of-order is handled by sort_stops() later in update_grad. */
    if (colors && stops && count > 0) {
        for (uint32_t i = 0; i < count && valid_count < VLC_MAX_GRADIENT_STOPS; i++) {
            if (stops[i] <= 255) {
                valid_stops[valid_count] = stops[i];
                valid_colors[valid_count] = colors[i];
                valid_count++;
            }
        }
    }

    if (valid_count == 0) {
        /* Rule 1: No valid stops → default black(0,0,0,255) at stop 0
         *         and white(255,255,255,255) at stop 255. */
        grad->count = 2;
        grad->stops[0] = 0;
        grad->colors[0] = 0xFF000000;   /* opaque black (A,R,G,B) */
        grad->stops[1] = 255;
        grad->colors[1] = 0xFFFFFFFF;   /* opaque white */
        return VG_LITE_SUCCESS;
    }

    /* Sort valid stops ascending (needed to determine first/last for rules 2,3). */
    for (uint32_t i = 0; i < valid_count - 1; i++) {
        for (uint32_t j = i + 1; j < valid_count; j++) {
            if (valid_stops[i] > valid_stops[j]) {
                uint32_t tmp_s = valid_stops[i];
                valid_stops[i] = valid_stops[j];
                valid_stops[j] = tmp_s;
                uint32_t tmp_c = valid_colors[i];
                valid_colors[i] = valid_colors[j];
                valid_colors[j] = tmp_c;
            }
        }
    }

    /* Rule 2: No stop at offset 0 → prepend implicit stop with first color. */
    int need_head = (valid_stops[0] != 0);
    /* Rule 3: No stop at offset 255 → append implicit stop with last color. */
    int need_tail = (valid_stops[valid_count - 1] != 255);

    uint32_t out_idx = 0;

    if (need_head) {
        grad->stops[out_idx] = 0;
        grad->colors[out_idx] = valid_colors[0];
        out_idx++;
    }

    for (uint32_t i = 0; i < valid_count; i++) {
        grad->stops[out_idx] = valid_stops[i];
        grad->colors[out_idx] = valid_colors[i];
        out_idx++;
    }

    if (need_tail) {
        grad->stops[out_idx] = 255;
        grad->colors[out_idx] = valid_colors[valid_count - 1];
        out_idx++;
    }

    grad->count = out_idx;
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

    vg_lite_buffer_flush(&grad->image);
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

/* Spread and ramp sampling for radial gradients are handled entirely on the
 * GPU (radial.frag) and in the CPU reference (util.c). The 1D ramp LUT baked
 * here contains no spread — see vg_lite_update_radial_grad below. */

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

    /* Build converted_ramp: a copy of the user ramp with explicit stops at
     * 0.0 and 1.0 so that the 1D LUT (whose index maps linearly to [0,1])
     * has well-defined endpoints. Matches gpu-vglite vg_lite.c L6442-6485.
     * If the user's first stop is > 0, prepend a copy of stop[0] at 0.0.
     * If the user's last stop is < 1, append a copy of stop[n-1] at 1.0. */
    vg_lite_color_ramp_t converted[VLC_MAX_COLOR_RAMP_STOPS + 2];
    uint32_t converted_length = 0;

    if (grad->color_ramp[0].stop > 0.0f) {
        converted[0] = grad->color_ramp[0];
        converted[0].stop = 0.0f;
        converted_length = 1;
    }
    for (uint32_t i = 0; i < grad->ramp_length; i++) {
        converted[converted_length++] = grad->color_ramp[i];
    }
    if (grad->color_ramp[grad->ramp_length - 1].stop < 1.0f) {
        converted[converted_length] = grad->color_ramp[grad->ramp_length - 1];
        converted[converted_length].stop = 1.0f;
        converted_length++;
    }

    /* 1D ramp LUT: width = converted_length * 128, height = 1.
     * Index i maps linearly to gradient = i/(width-1) in [0,1].
     * No spread applied here — spread is delegated to the GPU shader which
     * operates in the normalized [0,1] domain (1.0 == r boundary). This
     * matches the official gpu-vglite architecture. See FIXES.md #26. */
    int width = (int)(converted_length * 128);
    if (width < 2) width = 2;

    grad->image.width = width;
    grad->image.height = 1;
    grad->image.format = VG_LITE_BGRA8888;

    vg_lite_error_t err = vg_lite_allocate(&grad->image);
    if (err != VG_LITE_SUCCESS) return err;

    uint32_t *pixels = (uint32_t *)grad->image.memory;
    if (!pixels) {
        vg_lite_free(&grad->image);
        return VG_LITE_INVALID_ARGUMENT;
    }

    /* pre_multiplied: if set, color channels are multiplied by alpha before
     * packing, matching gpu-vglite L6919-6922 behavior. */
    int pre_mult = (grad->pre_multiplied != 0);

    for (int x = 0; x < width; x++) {
        float g = (width > 1) ? (float)x / (float)(width - 1) : 0.0f;

        /* sample ramp at g (no spread; g is already in [0,1]) */
        float alpha, red, green, blue;
        if (g <= converted[0].stop) {
            alpha = converted[0].alpha; red = converted[0].red;
            green = converted[0].green; blue = converted[0].blue;
        } else if (g >= converted[converted_length - 1].stop) {
            alpha = converted[converted_length - 1].alpha;
            red   = converted[converted_length - 1].red;
            green = converted[converted_length - 1].green;
            blue  = converted[converted_length - 1].blue;
        } else {
            uint32_t idx = 0;
            for (uint32_t i = 0; i < converted_length - 1; i++) {
                if (g >= converted[i].stop && g <= converted[i + 1].stop) {
                    idx = i;
                    break;
                }
            }
            float range = converted[idx + 1].stop - converted[idx].stop;
            float frac = (range > 0.0f) ? (g - converted[idx].stop) / range : 0.0f;
            alpha = converted[idx].alpha + (converted[idx + 1].alpha - converted[idx].alpha) * frac;
            red   = converted[idx].red   + (converted[idx + 1].red   - converted[idx].red)   * frac;
            green = converted[idx].green + (converted[idx + 1].green - converted[idx].green) * frac;
            blue  = converted[idx].blue  + (converted[idx + 1].blue  - converted[idx].blue)  * frac;
        }

        if (pre_mult) {
            red   *= alpha;
            green *= alpha;
            blue  *= alpha;
        }

        pixels[x] = pack_pixel(
            clamp_u8(alpha * 255.0f),
            clamp_u8(red   * 255.0f),
            clamp_u8(green * 255.0f),
            clamp_u8(blue  * 255.0f)
        );
    }

    /* Save converted ramp for the CPU reference path (util.c) if needed. */
    grad->converted_length = converted_length;
    for (uint32_t i = 0; i < converted_length; i++) {
        grad->converted_ramp[i] = converted[i];
    }

    vg_lite_buffer_flush(&grad->image);
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
