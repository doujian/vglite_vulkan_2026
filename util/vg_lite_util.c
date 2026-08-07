#include "vg_lite.h"
#include "vg_lite_util.h"
#include "vg_lite_config.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <direct.h>
#endif

/* Configuration-specific dump directory name (string literal).
 * Encodes all 3 compile-time axes: tiling + MSAA + OBB.
 * Examples: "dump_lin_msaa_obb", "dump_opt_nomsaa_noobb" */
#define DUMP_NAME0(t, m, o)  "dump_" #t "_" #m "_" #o
#define DUMP_NAME(t, m, o)    DUMP_NAME0(t, m, o)
#if VGLITE_TARGET_OPTIMAL
    #define DUMP_TILING opt
#else
    #define DUMP_TILING lin
#endif
#if VGLITE_BLIT_MSAA
    #define DUMP_MSAA msaa
#else
    #define DUMP_MSAA nomsaa
#endif
#if VGLITE_BLIT_OBB
    #define DUMP_OBB obb
#else
    #define DUMP_OBB noobb
#endif
#define DUMP_SUBDIR DUMP_NAME(DUMP_TILING, DUMP_MSAA, DUMP_OBB)

/* Read a little-endian 32-bit integer from file */
static int read_long(FILE *fp)
{
    unsigned char buf[4];
    if (fread(buf, 1, 4, fp) != 4) return 0;
    return (int)((unsigned)buf[0] |
                 ((unsigned)buf[1] << 8) |
                 ((unsigned)buf[2] << 16) |
                 ((unsigned)buf[3] << 24));
}

int vg_lite_load_raw(vg_lite_buffer_t *buffer, const char *name)
{
    FILE *fp = fopen(name, "rb");
    if (!fp) return 1;

    int format;
    int raw_stride;

    buffer->width  = read_long(fp);
    buffer->height = read_long(fp);
    raw_stride     = read_long(fp);
    format         = read_long(fp);

    switch (format) {
    case 0:
    case 1024: /* VG_LITE_RGBA8888 */
        buffer->format = VG_LITE_RGBA8888;
        break;
    case 1:
    case 1025: /* VG_LITE_BGRA8888 */
        buffer->format = VG_LITE_BGRA8888;
        break;
    case 4:
    case 1028: /* VG_LITE_RGB565 */
        buffer->format = VG_LITE_RGB565;
        break;
    case 5:
    case 1029: /* VG_LITE_BGR565 */
        buffer->format = VG_LITE_BGR565;
        break;
    default:
        buffer->format = VG_LITE_RGBA8888;
        break;
    }

    if (vg_lite_allocate(buffer) != VG_LITE_SUCCESS) {
        fclose(fp);
        return -1;
    }

    fseek(fp, 16, SEEK_SET);

    {
        unsigned char *temp = (unsigned char *)malloc(buffer->stride * buffer->height);
        if (!temp) { fclose(fp); vg_lite_free(buffer); return -1; }

        if (raw_stride == 0 || raw_stride == (int)buffer->stride) {
            int flag = fread(temp, buffer->stride * buffer->height, 1, fp);
            fclose(fp);
            if (flag != 1) { free(temp); vg_lite_free(buffer); return -1; }
        } else {
            unsigned char *dst = temp;
            for (int y = 0; y < buffer->height; y++) {
                if (fread(dst, raw_stride, 1, fp) != 1) {
                    fclose(fp); free(temp); vg_lite_free(buffer);
                    return -1;
                }
                dst += buffer->stride;
            }
            fclose(fp);
        }

        vg_lite_buffer_write(buffer, temp);
        free(temp);
    }
    return 0;
}

int vg_lite_save_png(const char *name, vg_lite_buffer_t *buffer)
{
    int channels = 4;
    int bpp = 32;
    switch (buffer->format) {
    case VG_LITE_RGB565: case VG_LITE_BGR565:
    case VG_LITE_RGBA4444: case VG_LITE_BGRA4444: bpp = 16; channels = 3; break;
    case VG_LITE_A8: case VG_LITE_L8: bpp = 8; channels = 1; break;
    default: break;
    }

    unsigned char *rgba = malloc(buffer->width * buffer->height * 4);
    if (!rgba) return 0;

    int is_bgra = (buffer->format == VG_LITE_BGRA8888 ||
                    buffer->format == VG_LITE_BGRX8888 ||
                    buffer->format == VG_LITE_BGR565 ||
                    buffer->format == VG_LITE_BGRA4444);
    int is_argb = (buffer->format == VG_LITE_ARGB8888 ||
                   buffer->format == VG_LITE_ABGR8888);

    unsigned char *img_data = (unsigned char *)malloc(buffer->stride * buffer->height);
    if (!img_data) { free(rgba); return 0; }
    vg_lite_buffer_download(buffer, img_data);

    unsigned char *src = img_data;
    for (int y = 0; y < buffer->height; y++) {
        for (int x = 0; x < buffer->width; x++) {
            int di = (y * buffer->width + x) * 4;
            int si = y * buffer->stride;
            if (bpp == 32) {
                si += x * 4;
                if (is_bgra) {
                    rgba[di+0] = src[si+2];
                    rgba[di+1] = src[si+1];
                    rgba[di+2] = src[si+0];
                    rgba[di+3] = src[si+3];
                } else if (is_argb) {
                    /* ARGB8888 maps to VK_FORMAT_R8G8B8A8 → mem [R,G,B,A] (identity).
                     * ABGR8888 maps to VK_FORMAT_A8B8G8R8 → mem [A,B,G,R]. */
                    if (buffer->format == VG_LITE_ARGB8888) {
                        rgba[di+0] = src[si+0]; /* R */
                        rgba[di+1] = src[si+1]; /* G */
                        rgba[di+2] = src[si+2]; /* B */
                        rgba[di+3] = src[si+3]; /* A */
                    } else { /* ABGR8888 mem [A,B,G,R] */
                        rgba[di+0] = src[si+3]; /* R */
                        rgba[di+1] = src[si+2]; /* G */
                        rgba[di+2] = src[si+1]; /* B */
                        rgba[di+3] = src[si+0]; /* A */
                    }
                } else {
                    rgba[di+0] = src[si+0];
                    rgba[di+1] = src[si+1];
                    rgba[di+2] = src[si+2];
                    rgba[di+3] = src[si+3];
                }
            } else if (bpp == 16) {
                si += x * 2;
                uint16_t p = *(uint16_t*)(src + si);
                if (buffer->format == VG_LITE_RGBA4444 || buffer->format == VG_LITE_BGRA4444) {
                    /* VGLite 4444: channel at bits 3:0 is first-named, 7:4 is G, 11:8 is third, 15:12 is A */
                    if (buffer->format == VG_LITE_BGRA4444) {
                        /* B in 3:0, G in 7:4, R in 11:8, A in 15:12 */
                        rgba[di+2] = (unsigned char)((p & 0xF) * 17);
                        rgba[di+1] = (unsigned char)(((p >> 4) & 0xF) * 17);
                        rgba[di+0] = (unsigned char)(((p >> 8) & 0xF) * 17);
                    } else {
                        /* RGBA4444: R in 3:0, G in 7:4, B in 11:8, A in 15:12 */
                        rgba[di+0] = (unsigned char)((p & 0xF) * 17);
                        rgba[di+1] = (unsigned char)(((p >> 4) & 0xF) * 17);
                        rgba[di+2] = (unsigned char)(((p >> 8) & 0xF) * 17);
                    }
                } else if (is_bgra) {
                    /* VG_LITE_BGR565 -> VK_FORMAT_R5G6B5: R in bits 15-11, G in 10-5, B in 4-0 */
                    rgba[di+0] = (unsigned char)(((p >> 11) & 0x1F) * 255 / 31);
                    rgba[di+1] = (unsigned char)(((p >> 5) & 0x3F) * 255 / 63);
                    rgba[di+2] = (unsigned char)((p & 0x1F) * 255 / 31);
                } else {
                    /* VG_LITE_RGB565 -> VK_FORMAT_B5G6R5: B in bits 15-11, G in 10-5, R in 4-0 */
                    rgba[di+0] = (unsigned char)((p & 0x1F) * 255 / 31);
                    rgba[di+1] = (unsigned char)(((p >> 5) & 0x3F) * 255 / 63);
                    rgba[di+2] = (unsigned char)(((p >> 11) & 0x1F) * 255 / 31);
                }
                rgba[di+3] = 255;
            } else {
                si += x;
                rgba[di+0] = src[si];
                rgba[di+1] = src[si];
                rgba[di+2] = src[si];
                rgba[di+3] = 255;
            }
        }
    }

    /* Route PNG output to configuration-specific subdirectory for visual inspection. */
#if defined(_WIN32)
    _mkdir(DUMP_SUBDIR);
#else
    mkdir(DUMP_SUBDIR, 0755);
#endif
    char outpath[512];
    snprintf(outpath, sizeof(outpath), "%s/%s", DUMP_SUBDIR, name);

    int ok = stbi_write_png(outpath, buffer->width, buffer->height, 4, rgba, buffer->width * 4);
    printf("stbi_write_png result: %d (w=%d, h=%d) -> %s\n", ok, buffer->width, buffer->height, outpath);
    free(rgba);
    free(img_data);
    return ok;
}

int vg_lite_load_png(vg_lite_buffer_t *buffer, const char *name)
{
    int w, h, n;
    /* Force 4 channels (RGBA) */
    unsigned char *data = stbi_load(name, &w, &h, &n, 4);
    if (!data) return 1;

    buffer->width = w;
    buffer->height = h;
    buffer->format = VG_LITE_BGRA8888;

    if (vg_lite_allocate(buffer) != VG_LITE_SUCCESS) {
    stbi_image_free(data);
        return -1;
    }

    /* stb_image returns RGBA order, VGLite BGRA8888 expects B,G,R,A in memory */
    unsigned char *dst = (unsigned char *)malloc(buffer->stride * buffer->height);
    if (!dst) { stbi_image_free(data); return -1; }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int si = (y * w + x) * 4;
            int di = y * buffer->stride + x * 4;
            dst[di+0] = data[si+2]; /* B */
            dst[di+1] = data[si+1]; /* G */
            dst[di+2] = data[si+0]; /* R */
            dst[di+3] = data[si+3]; /* A */
        }
    }
    vg_lite_buffer_write(buffer, dst);
    free(dst);
    stbi_image_free(data);
    return 0;
}

int vg_lite_fb_open(vg_lite_buffer_t *buffer) { (void)buffer; return 0; }
void vg_lite_fb_close(vg_lite_buffer_t *buffer) { (void)buffer; }

void vg_lite_save_raw(const char *name, vg_lite_buffer_t *buffer)
{
    if (!name || !buffer) return;

    /* Route to same configuration-specific subdirectory as PNG output. */
#if defined(_WIN32)
    _mkdir(DUMP_SUBDIR);
#else
    mkdir(DUMP_SUBDIR, 0755);
#endif
    char outpath[512];
    snprintf(outpath, sizeof(outpath), "%s/%s", DUMP_SUBDIR, name);
    FILE *fp = fopen(outpath, "wb");
    if (!fp) return;

    /* Write 16-byte header */
    int32_t header[4];
    header[0] = buffer->width;
    header[1] = buffer->height;
    header[2] = buffer->stride;
    header[3] = (int32_t)buffer->format;
    fwrite(header, 4, 4, fp);

    /* Write pixel data */
    unsigned char *img_data = (unsigned char *)malloc(buffer->stride * buffer->height);
    if (img_data) {
        vg_lite_buffer_download(buffer, img_data);
        unsigned char *ptr = img_data;
        for (int y = 0; y < buffer->height; y++) {
            fwrite(ptr, 1, buffer->stride, fp);
            ptr += buffer->stride;
        }
        free(img_data);
    }
    fclose(fp);
}
