/* test_optimal_dump_recover:
 *
 * 1. Read tests/data/landscape.raw (400x300 RGBA8888, 16B header).
 * 2. Upload into an OPTIMAL buffer (tiled = VG_LITE_TILED, host-visible
 *    placement via vg_lite_dump_enable_host_optimal so the allocation is
 *    mappable).
 * 3. vg_lite_dump_raw() -> raw bin of the OPTIMAL physical memory.
 * 4. Read the dumped bin back, detect the driver's layout empirically,
 *    and convert (de-tile) it to linear row-major.
 * 5. Compare the recovered linear image with the original raw data.
 *
 * Layout detection: the driver (observed: 16B x 32-row microtiles, group
 * stride 512B, tile-row padded group count) is implementation-defined, so
 * the test first tries a plain LINEAR model (off = base + y*pitch + x*4),
 * then the microtile model:
 *   off(x,y) = 512 * (G*floor(y/32) + floor(x/4)) + 16*(y%32) + 4*(x%4)
 * with G detected from the row-32 jump. Full-image verification must be
 * 100% exact for the test to pass.
 */

#include "vg_lite.h"
#include "vg_lite_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define W 400
#define H 300
#define ROW_BYTES (W * 4)

static unsigned char orig[16 + W * H * 4];

static FILE *open_raw(void)
{
    static const char *paths[] = {
        "landscape.raw",
        "data/landscape.raw",
        "../data/landscape.raw",
        "../../data/landscape.raw",
        "../../../data/landscape.raw",
        "../../../../tests/data/landscape.raw",
    };
    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        FILE *fp = fopen(paths[i], "rb");
        if (fp) return fp;
    }
    return NULL;
}

/* Find first occurrence of an n-byte signature. */
static const unsigned char *find_sig(const unsigned char *hay, int hay_len,
                                     const unsigned char *sig, int n)
{
    for (int i = 0; i + n <= hay_len; i++)
        if (!memcmp(hay + i, sig, n)) return hay + i;
    return NULL;
}

static const unsigned char *px(const unsigned char *img, int x, int y)
{
    return img + 16 + (size_t)y * ROW_BYTES + (size_t)x * 4;
}

int main(void)
{
    FILE *fp = open_raw();
    if (!fp) { printf("cannot open landscape.raw\n"); return -1; }
    if (fread(orig, 1, sizeof(orig), fp) != sizeof(orig)) { printf("short read\n"); fclose(fp); return -1; }
    fclose(fp);

    if (vg_lite_init(32, 32) != VG_LITE_SUCCESS) { printf("init failed\n"); return -1; }

    vg_lite_buffer_t buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.width  = W;
    buffer.height = H;
    buffer.format = VG_LITE_RGBA8888;
    buffer.tiled  = VG_LITE_TILED;             /* force OPTIMAL image */
    vg_lite_dump_enable_host_optimal(1);       /* make allocation mappable */

    if (vg_lite_allocate(&buffer) != VG_LITE_SUCCESS) { printf("allocate failed\n"); return -1; }
    if (vg_lite_buffer_write(&buffer, orig + 16) != VG_LITE_SUCCESS) { printf("write failed\n"); return -1; }
    vg_lite_finish();

    if (vg_lite_dump_raw("recover", &buffer) != VG_LITE_SUCCESS) {
        printf("raw dump unavailable (OPTIMAL not host-visible)\n");
        vg_lite_free(&buffer); vg_lite_close();
        return -1;
    }

    /* Read the dumped bin back: locate newest raw_recover_*.bin in dump dir. */
    char path[600];
    snprintf(path, sizeof(path), "%s/raw_recover_rgba8888_400x300_opt_rp0_off0_sz*", vg_lite_dump_dir());
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE) { printf("dump file not found\n"); return -1; }
    snprintf(path, sizeof(path), "%s/%s", vg_lite_dump_dir(), fd.cFileName);
    FindClose(h);
    printf("dump file: %s\n", path);

    FILE *bf = fopen(path, "rb");
    fseek(bf, 0, SEEK_END);
    long blen = ftell(bf);
    fseek(bf, 0, SEEK_SET);
    unsigned char *bin = malloc((size_t)blen);
    if (fread(bin, 1, (size_t)blen, bf) != (size_t)blen) { printf("bin short read\n"); return -1; }
    fclose(bf);
    printf("bin size: %ld (logical %d)\n", blen, W * H * 4);

    /* ---- Layout model A: plain linear (off = base + y*pitch + x*4) ---- */
    const unsigned char *row0 = px(orig, 0, 0);
    const unsigned char *p0 = find_sig(bin, (int)blen, row0, 16);
    const unsigned char *p1 = find_sig(bin, (int)blen, px(orig, 0, 1), 16);
    if (!p0 || !p1) { printf("anchor rows not found in dump\n"); return -1; }
    long base = p0 - bin;
    long pitch = (p1 - p0);
    long mismatches = 0;
    long total = 0;

    if (pitch == ROW_BYTES) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W * 4; x++) {
                long off = base + (long)y * pitch + x;
                total++;
                if (off >= blen || bin[off] != px(orig, 0, y)[x]) mismatches++;
            }
        printf("model A (linear, base=%ld pitch=%ld)\n", base, pitch);
    } else {
        /* ---- Layout model B: 16B x 32-row microtiles ----
         * off(x,y) = 512 * (G*floor(y/32) + floor(x/4)) + 16*(y%32) + 4*(x%4)
         * Detect G from the row-32 anchor jump. */
        const unsigned char *p32 = find_sig(bin, (int)blen, px(orig, 0, 32), 16);
        if (!p32) { printf("row-32 anchor not found\n"); return -1; }
        long gstride = 512;
        long tile_jump = (p32 - p0);
        if (tile_jump % gstride != 0) { printf("unexpected tile jump %ld\n", tile_jump); return -1; }
        long G = tile_jump / gstride;
        if (G < (W + 3) / 4) { printf("detected G=%ld too small\n", G); return -1; }
        printf("model B (microtile 16B x 32 rows, G=%ld groups/tile-row)\n", G);

        for (int y = 0; y < H; y++) {
            long ty = y / 32, yin = y % 32;
            for (int gx = 0; gx < W / 4; gx++) {
                long off = base + gstride * (G * ty + gx) + 16 * yin;
                for (int b = 0; b < 16; b++) {
                    total++;
                    if (off + b >= blen || bin[off + b] != px(orig, gx * 4, y)[b]) mismatches++;
                }
            }
        }
    }

    printf("recovered: %ld/%ld bytes match (%ld mismatches)\n", total - mismatches, total, mismatches);

    free(bin);
    vg_lite_free(&buffer);
    vg_lite_close();
    return mismatches == 0 ? 0 : -1;
}
