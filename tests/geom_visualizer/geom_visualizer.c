/*
 * geom_visualizer - render a VGLITE geometry dump as one aggregated PNG
 *
 * Usage: geom_visualizer <dump_file> [output.png] [selection]
 * Default output: <dump_file minus extension>_vis.png
 *
 * selection:
 *   all            all records aggregated into one PNG (default)
 *   each           one PNG per record (<out base>_<seq>.png)
 *   list           comma-separated DRAW sequence numbers and/or inclusive
 *                  ranges, e.g. "2" or "1,3" or "1-2" or "1,3-4" - only
 *                  those records are aggregated into one PNG
 *
 * Reads the text record format produced by src/geom_dump.c:
 *   DRAW <seq> <entry_name>
 *   F <fb_width> <fb_height>
 *   V <vertex_count> x0 y0 x1 y1 ...
 *   T <tri_count> i0 i1 i2 i0 i1 i2 ...
 *   B <min_x> <min_y> <max_x> <max_y>
 *
 * Renders all triangles (translucent fill + solid 1px wireframe), all AABBs
 * (dashed rectangle outlines) and all vertices (2x2 white dots) onto a dark
 * background. Path coordinates are y-down screen space - drawn directly.
 *
 * Canvas size comes from the dump: the F line records the target framebuffer
 * dimensions of the test that produced it. Mapping is 1:1 - pixel (x,y) =
 * path coordinate (x,y), origin top-left - so the PNG lines up exactly with
 * the test's own framebuffer output. All selection modes (all/each/subset)
 * use the same canvas so PNGs are directly comparable. If a record has no F
 * line (old dump), a 256x256 fallback is used. Geometry outside the canvas
 * is clipped; a warning is printed if the dump bounds exceed the canvas.
 *
 * Pure C99, no Vulkan, no platform APIs. Builds on Linux and Windows.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "stb_image_write.h"

/* ------------------------------------------------------------------ */
/* Data model                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[64];
    unsigned int seq;
    int fb_w, fb_h;      /* F line: target framebuffer size (0 = absent) */
    float* verts;        /* 2 floats per vertex */
    int vert_count;
    unsigned int* idx;   /* 3 per triangle */
    int idx_count;
    float bbminx, bbminy, bbmaxx, bbmaxy;
    float quad[8];        /* C line: cover quad corners, 4 x (x,y) */
    int has_quad;
} Record;

static Record* g_records = NULL;
static int g_record_count = 0;
static int g_record_cap = 0;

static int g_total_tris = 0;

/* ------------------------------------------------------------------ */
/* Parsing helpers                                                     */
/* ------------------------------------------------------------------ */

/* Skip leading whitespace; return pointer at first non-space char or NULL at end. */
static const char* skip_ws(const char* s)
{
    if (!s) return NULL;
    while (*s == ' ' || *s == '\t') s++;
    return (*s == '\0') ? NULL : s;
}

/* Parse a float token; returns advanced pointer or NULL on failure. */
static const char* parse_float(const char* s, float* out)
{
    char* end;
    double v;
    s = skip_ws(s);
    if (!s) return NULL;
    v = strtod(s, &end);
    if (end == s) return NULL;
    *out = (float)v;
    return end;
}

/* Parse an unsigned int token; returns advanced pointer or NULL on failure. */
static const char* parse_uint(const char* s, unsigned int* out)
{
    char* end;
    unsigned long v;
    s = skip_ws(s);
    if (!s) return NULL;
    v = strtoul(s, &end, 10);
    if (end == s) return NULL;
    *out = (unsigned int)v;
    return end;
}

/* Grow a float array; returns 0 on success, -1 on out-of-memory. */
static int float_array_push(float** arr, int* count, int* cap, float v)
{
    if (*count >= *cap) {
        int ncap = *cap ? *cap * 2 : 64;
        float* np = (float*)realloc(*arr, (size_t)ncap * sizeof(float));
        if (!np) return -1;
        *arr = np;
        *cap = ncap;
    }
    (*arr)[(*count)++] = v;
    return 0;
}

static int uint_array_push(unsigned int** arr, int* count, int* cap,
                           unsigned int v)
{
    if (*count >= *cap) {
        int ncap = *cap ? *cap * 2 : 64;
        unsigned int* np = (unsigned int*)realloc(*arr,
                                    (size_t)ncap * sizeof(unsigned int));
        if (!np) return -1;
        *arr = np;
        *cap = ncap;
    }
    (*arr)[(*count)++] = v;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Dump file parsing                                                   */
/* ------------------------------------------------------------------ */

static int parse_dump(const char* path)
{
    FILE* f = fopen(path, "r");
    char line[65536];
    int lineno = 0;
    Record cur;
    int have_cur = 0;
    int warned = 0;

    if (!f) {
        fprintf(stderr, "error: cannot open dump file '%s'\n", path);
        return -1;
    }

    memset(&cur, 0, sizeof(cur));

    while (fgets(line, sizeof(line), f)) {
        const char* p;
        lineno++;

        /* strip trailing newline / carriage return */
        p = line + strlen(line);
        while (p > line && (p[-1] == '\n' || p[-1] == '\r')) p--;
        ((char*)line)[(size_t)(p - line)] = '\0'; /* cast: modify local copy */

        if (line[0] == '\0') continue;

        if (strncmp(line, "DRAW ", 5) == 0) {
            /* flush previous record */
            if (have_cur) {
                if (g_record_count >= g_record_cap) {
                    int ncap = g_record_cap ? g_record_cap * 2 : 16;
                    Record* np = (Record*)realloc(g_records,
                                        (size_t)ncap * sizeof(Record));
                    if (!np) { fprintf(stderr, "error: out of memory\n"); fclose(f); return -1; }
                    g_records = np;
                    g_record_cap = ncap;
                }
                g_records[g_record_count++] = cur;
                memset(&cur, 0, sizeof(cur));
            }
            {
                unsigned int seq;
                const char* q = parse_uint(line + 5, &seq);
                const char* nm;
                cur.seq = seq;
                nm = skip_ws(q);
                if (nm) {
                    strncpy(cur.name, nm, sizeof(cur.name) - 1);
                    cur.name[sizeof(cur.name) - 1] = '\0';
                } else {
                    strcpy(cur.name, "?");
                }
            }
            cur.vert_count = 0;
            cur.idx_count = 0;
            cur.bbminx = cur.bbminy = cur.bbmaxx = cur.bbmaxy = 0.0f;
            have_cur = 1;
        } else if (strncmp(line, "F ", 2) == 0) {
            unsigned int fw, fh;
            p = parse_uint(line + 2, &fw);
            p = parse_uint(p, &fh);
            if (p && fw > 0 && fh > 0 && fw <= 16384 && fh <= 16384) {
                cur.fb_w = (int)fw;
                cur.fb_h = (int)fh;
            } else if (++warned < 20) {
                fprintf(stderr, "warning: malformed F line at %d, skipped\n", lineno);
            }
        } else if (strncmp(line, "V ", 2) == 0) {
            unsigned int un;
            int n, i;
            int vcap = 0;
            int nfloats = 0;
            p = parse_uint(line + 2, &un);
            if (!p || un > 100000000u) {
                if (++warned < 20)
                    fprintf(stderr, "warning: malformed V header at line %d, skipped\n", lineno);
                continue;
            }
            n = (int)un;
            free(cur.verts);
            cur.verts = NULL;
            for (i = 0; i < n * 2; i++) {
                float v;
                p = parse_float(p, &v);
                if (!p) break;
                if (float_array_push(&cur.verts, &nfloats, &vcap, v) != 0) {
                    fprintf(stderr, "error: out of memory\n");
                    fclose(f);
                    return -1;
                }
            }
            if (i < n * 2 && ++warned < 20)
                fprintf(stderr, "warning: truncated V line at %d\n", lineno);
            cur.vert_count = nfloats / 2;   /* vertex count, not float count */
        } else if (strncmp(line, "T ", 2) == 0) {
            unsigned int n, v;
            int i;
            int icap = 0;
            p = parse_uint(line + 2, &n);
            if (!p || n > 100000000u) {
                if (++warned < 20)
                    fprintf(stderr, "warning: malformed T header at line %d, skipped\n", lineno);
                continue;
            }
            free(cur.idx);
            cur.idx = NULL;
            cur.idx_count = 0;
            for (i = 0; i < (int)n * 3; i++) {
                p = parse_uint(p, &v);
                if (!p) break;
                if (uint_array_push(&cur.idx, &cur.idx_count, &icap, v) != 0) {
                    fprintf(stderr, "error: out of memory\n");
                    fclose(f);
                    return -1;
                }
            }
            if (i < (int)n * 3 && ++warned < 20)
                fprintf(stderr, "warning: truncated T line at %d\n", lineno);
            g_total_tris += cur.idx_count / 3;
        } else if (strncmp(line, "C ", 2) == 0) {
            int c;
            int ok = 1;
            p = line + 2;
            for (c = 0; c < 8; c++) {
                p = parse_float(p, &cur.quad[c]);
                if (!p) { ok = 0; break; }
            }
            if (ok) cur.has_quad = 1;
            else if (++warned < 20)
                fprintf(stderr, "warning: malformed C line at %d, skipped\n", lineno);
        } else if (strncmp(line, "B ", 2) == 0) {
            p = parse_float(line + 2, &cur.bbminx);
            p = parse_float(p, &cur.bbminy);
            p = parse_float(p, &cur.bbmaxx);
            p = parse_float(p, &cur.bbmaxy);
            if (!p) { if (++warned < 20) fprintf(stderr, "warning: malformed B line at %d, skipped\n", lineno); }
        } else {
            if (++warned < 20)
                fprintf(stderr, "warning: unknown line at %d, skipped: '%.32s'\n", lineno, line);
        }
    }

    if (have_cur) {
        if (g_record_count >= g_record_cap) {
            int ncap = g_record_cap ? g_record_cap * 2 : 16;
            Record* np = (Record*)realloc(g_records, (size_t)ncap * sizeof(Record));
            if (!np) { fprintf(stderr, "error: out of memory\n"); fclose(f); return -1; }
            g_records = np;
            g_record_cap = ncap;
        }
        g_records[g_record_count++] = cur;
    }

    fclose(f);
    return g_record_count > 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Selection                                                           */
/* ------------------------------------------------------------------ */

/* Selection modes */
enum { SEL_ALL = 0, SEL_EACH, SEL_LIST };

/*
 * Parse a selection spec into a per-record "selected" bitmap.
 * Returns SEL_ALL / SEL_EACH / SEL_LIST, or -1 on hard error (nothing valid).
 * Numbers refer to DRAW sequence numbers (record .seq), not file order.
 */
static int parse_selection(const char* spec, unsigned char** sel_out)
{
    unsigned char* sel;
    const char* p = spec;
    int any = 0;

    *sel_out = NULL;

    if (strcmp(spec, "all") == 0) return SEL_ALL;
    if (strcmp(spec, "each") == 0) return SEL_EACH;

    sel = (unsigned char*)calloc((size_t)g_record_count, 1);
    if (!sel) {
        fprintf(stderr, "error: out of memory\n");
        return -1;
    }

    while (*p) {
        char* end;
        unsigned long a, b;
        unsigned int lo, hi;
        int rec;

        /* skip leading commas/spaces */
        while (*p == ',' || *p == ' ') p++;
        if (!*p) break;

        a = strtoul(p, &end, 10);
        if (end == p) {
            fprintf(stderr, "warning: bad token in selection at '%.8s', ignored\n", p);
            while (*p && *p != ',') p++;
            continue;
        }
        p = end;
        if (*p == '-') {
            p++;
            b = strtoul(p, &end, 10);
            if (end == p) {
                fprintf(stderr, "warning: bad range in selection near '%.8s', ignored\n", p);
                while (*p && *p != ',') p++;
                continue;
            }
            p = end;
        } else {
            b = a;
        }
        if (b < a) { unsigned long t = a; a = b; b = t; }
        if (a == 0) a = 1;
        if (b > 100000000u) b = 100000000u;
        lo = (unsigned int)a; hi = (unsigned int)b;

        for (rec = 0; rec < g_record_count; rec++) {
            if (g_records[rec].seq >= lo && g_records[rec].seq <= hi) {
                sel[rec] = 1;
                any = 1;
            }
        }
        if (lo > hi || !any) {
            /* only warn if this exact range matched nothing */
            int matched = 0;
            for (rec = 0; rec < g_record_count; rec++)
                if (g_records[rec].seq >= lo && g_records[rec].seq <= hi) matched = 1;
            if (!matched)
                fprintf(stderr, "warning: selection %u-%u matches no records\n", lo, hi);
        }

        /* skip trailing spaces before comma */
        while (*p == ' ') p++;
    }

    if (!any) {
        fprintf(stderr, "error: selection '%s' selects no records\n", spec);
        free(sel);
        return -1;
    }
    *sel_out = sel;
    return SEL_LIST;
}

/* Sanitize a selection string for a filename: ',' -> '_' (in place, short buf). */
static void sanitize_sel(char* buf, size_t cap, const char* sel)
{
    size_t i;
    for (i = 0; i + 1 < cap && sel[i]; i++)
        buf[i] = (sel[i] == ',') ? '_' : sel[i];
    buf[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned char* px;
    int w, h;
} Canvas;

static void put_pixel(Canvas* c, int x, int y, unsigned char r,
                      unsigned char g, unsigned char b, unsigned char a)
{
    unsigned char* d;
    if (x < 0 || y < 0 || x >= c->w || y >= c->h || a == 0) return;
    d = c->px + ((size_t)y * (size_t)c->w + (size_t)x) * 4;
    /* alpha-blend over existing */
    d[0] = (unsigned char)((r * a + d[0] * (255 - a)) / 255);
    d[1] = (unsigned char)((g * a + d[1] * (255 - a)) / 255);
    d[2] = (unsigned char)((b * a + d[2] * (255 - a)) / 255);
    d[3] = 255;
}

/* Bresenham line, width 1. */
static void draw_line(Canvas* c, int x0, int y0, int x1, int y1,
                      unsigned char r, unsigned char g, unsigned char b)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err, steps, i;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    if (dx >= dy) {
        long long ys;
        err = dx / 2;
        steps = dx;
        ys = y0;
        for (i = 0; i <= steps; i++) {
            put_pixel(c, x0 + sx * i, (int)ys, r, g, b, 255);
            err -= dy;
            if (err < 0) { ys += sy; err += dx; }
        }
    } else {
        long long xs;
        err = dy / 2;
        steps = dy;
        xs = x0;
        for (i = 0; i <= steps; i++) {
            put_pixel(c, (int)xs, y0 + sy * i, r, g, b, 255);
            err -= dx;
            if (err < 0) { xs += sx; err += dy; }
        }
    }
}

/* Dashed rectangle: skip every 4th pixel along each edge. */
static void draw_rect_dashed(Canvas* c, int x0, int y0, int x1, int y1)
{
    int x, y, k = 0;
    for (x = x0; x <= x1; x++, k++) {
        if (k % 4 != 3) {
            put_pixel(c, x, y0, 255, 96, 0, 255);
            put_pixel(c, x, y1, 255, 96, 0, 255);
        }
    }
    k = 0;
    for (y = y0; y <= y1; y++, k++) {
        if (k % 4 != 3) {
            put_pixel(c, x0, y, 255, 96, 0, 255);
            put_pixel(c, x1, y, 255, 96, 0, 255);
        }
    }
}

/* Translucent triangle fill via barycentric coverage of its bounding box. */
static void fill_tri(Canvas* c, int x0, int y0, int x1, int y1,
                     int x2, int y2, unsigned char r, unsigned char g,
                     unsigned char b)
{
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    int area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    int x, y;

    if (area == 0) return;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > c->w - 1) maxx = c->w - 1;
    if (maxy > c->h - 1) maxy = c->h - 1;

    for (y = miny; y <= maxy; y++) {
        for (x = minx; x <= maxx; x++) {
            int w0 = (x1 - x) * (y2 - y) - (x2 - x) * (y1 - y);
            int w1 = (x2 - x) * (y0 - y) - (x0 - x) * (y2 - y);
            int w2 = (x0 - x) * (y1 - y) - (x1 - x) * (y0 - y);
            int inside;
            if (area > 0)
                inside = (w0 >= 0 && w1 >= 0 && w2 >= 0);
            else
                inside = (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (inside) put_pixel(c, x, y, r, g, b, 40);
        }
    }
}

/* 2x2 white dot. */
static void draw_dot(Canvas* c, int x, int y)
{
    put_pixel(c, x, y, 255, 255, 255, 255);
    put_pixel(c, x + 1, y, 255, 255, 255, 255);
    put_pixel(c, x, y + 1, 255, 255, 255, 255);
    put_pixel(c, x + 1, y + 1, 255, 255, 255, 255);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

/* Fallback canvas when no F line exists in the dump (old format). */
#define FALLBACK_W 256
#define FALLBACK_H 256

/* Map a path-space coordinate to a canvas pixel: 1:1, rounded to nearest. */
#define MAPX(v) ((int)((v) + 0.5f))
#define MAPY(v) ((int)((v) + 0.5f))

/* Render the given records (by index into g_records) into one PNG. */
static int render_records(const int* recs, int nrecs, const char* out_path)
{
    static const unsigned char pal[8][3] = {
        {80, 200, 120}, {90, 160, 255}, {255, 210, 80}, {200, 120, 255},
        {80, 230, 230}, {255, 130, 160}, {160, 255, 90}, {140, 170, 255}
    };
    unsigned char* img;
    Canvas cv;
    int i, k;
    float gminx = 0, gminy = 0, gmaxx = 0, gmaxy = 0;
    int have_bounds = 0;
    int total_tris = 0;
    int w, h;

    /* bounds over selected records (vertices and AABBs) */
    for (k = 0; k < nrecs; k++) {
        const Record* r = &g_records[recs[k]];
        total_tris += r->idx_count / 3;
        for (i = 0; i < r->vert_count; i++) {
            float x = r->verts[i * 2], y = r->verts[i * 2 + 1];
            if (!have_bounds) { gminx = gmaxx = x; gminy = gmaxy = y; have_bounds = 1; }
            else {
                if (x < gminx) gminx = x;
                if (x > gmaxx) gmaxx = x;
                if (y < gminy) gminy = y;
                if (y > gmaxy) gmaxy = y;
            }
        }
        if (r->has_quad) {
            for (i = 0; i < 4; i++) {
                float x = r->quad[i * 2], y = r->quad[i * 2 + 1];
                if (!have_bounds) { gminx = gmaxx = x; gminy = gmaxy = y; have_bounds = 1; }
                else {
                    if (x < gminx) gminx = x;
                    if (x > gmaxx) gmaxx = x;
                    if (y < gminy) gminy = y;
                    if (y > gmaxy) gmaxy = y;
                }
            }
        }
        if (r->bbmaxx >= r->bbminx && r->bbmaxy >= r->bbminy) {
            if (!have_bounds) {
                gminx = r->bbminx; gmaxx = r->bbmaxx;
                gminy = r->bbminy; gmaxy = r->bbmaxy;
                have_bounds = 1;
            } else {
                if (r->bbminx < gminx) gminx = r->bbminx;
                if (r->bbmaxx > gmaxx) gmaxx = r->bbmaxx;
                if (r->bbminy < gminy) gminy = r->bbminy;
                if (r->bbmaxy > gmaxy) gmaxy = r->bbmaxy;
            }
        }
    }
    if (!have_bounds) {
        fprintf(stderr, "error: selection contains no geometry\n");
        return -1;
    }

    /* canvas: framebuffer size from the F lines (max across selection;
     * all records of one test share the same size). Fallback for old
     * dumps without F lines. 1:1 mapping, pixel (x,y) = coord (x,y). */
    w = h = 0;
    for (k = 0; k < nrecs; k++) {
        const Record* r = &g_records[recs[k]];
        if (r->fb_w > w) w = r->fb_w;
        if (r->fb_h > h) h = r->fb_h;
    }
    if (w == 0 || h == 0) {
        w = FALLBACK_W;
        h = FALLBACK_H;
        fprintf(stderr, "warning: dump has no F line, using fallback "
                        "%dx%d canvas\n", w, h);
    }

    if (gminx < 0 || gminy < 0 || gmaxx > w || gmaxy > h) {
        fprintf(stderr,
                "warning: geometry bounds [%.2f,%.2f]x[%.2f,%.2f] exceed the "
                "%dx%d canvas - clipped\n",
                (double)gminx, (double)gminy, (double)gmaxx, (double)gmaxy,
                w, h);
    }

    img = (unsigned char*)calloc((size_t)w * (size_t)h, 4);
    if (!img) {
        fprintf(stderr, "error: out of memory for canvas %dx%d\n", w, h);
        return -1;
    }
    {
        size_t n = (size_t)w * (size_t)h, q;
        for (q = 0; q < n; q++) {
            img[q * 4 + 0] = 32;
            img[q * 4 + 1] = 32;
            img[q * 4 + 2] = 36;
            img[q * 4 + 3] = 255;
        }
    }
    cv.px = img; cv.w = w; cv.h = h;

    for (k = 0; k < nrecs; k++) {
        const Record* r = &g_records[recs[k]];
        const unsigned char* col = pal[k % 8];
        unsigned char fr = (unsigned char)(col[0] / 2);
        unsigned char fg = (unsigned char)(col[1] / 2);
        unsigned char fb = (unsigned char)(col[2] / 2);
        int t;

        for (t = 0; t + 2 < r->idx_count; t += 3) {
            unsigned int a = r->idx[t], b = r->idx[t + 1], c2 = r->idx[t + 2];
            int ax, ay, bx, by, cx, cy;
            if ((int)a >= r->vert_count || (int)b >= r->vert_count ||
                (int)c2 >= r->vert_count) continue;
            ax = MAPX(r->verts[a * 2]);
            ay = MAPY(r->verts[a * 2 + 1]);
            bx = MAPX(r->verts[b * 2]);
            by = MAPY(r->verts[b * 2 + 1]);
            cx = MAPX(r->verts[c2 * 2]);
            cy = MAPY(r->verts[c2 * 2 + 1]);
            fill_tri(&cv, ax, ay, bx, by, cx, cy, fr, fg, fb);
            draw_line(&cv, ax, ay, bx, by, col[0], col[1], col[2]);
            draw_line(&cv, bx, by, cx, cy, col[0], col[1], col[2]);
            draw_line(&cv, cx, cy, ax, ay, col[0], col[1], col[2]);
        }

        if (r->bbmaxx >= r->bbminx && r->bbmaxy >= r->bbminy) {
            int x0 = MAPX(r->bbminx);
            int y0 = MAPY(r->bbminy);
            int x1 = MAPX(r->bbmaxx);
            int y1 = MAPY(r->bbmaxy);
            draw_rect_dashed(&cv, x0, y0, x1, y1);
        }

        /* cover quad (C line): the oriented box the shader cover pass uses */
        if (r->has_quad) {
            int qx[4], qy[4];
            int c;
            for (c = 0; c < 4; c++) {
                qx[c] = MAPX(r->quad[c * 2]);
                qy[c] = MAPY(r->quad[c * 2 + 1]);
            }
            for (c = 0; c < 4; c++) {
                int n = (c + 1) % 4;
                draw_line(&cv, qx[c], qy[c], qx[n], qy[n], 255, 220, 40);
            }
        }

        for (i = 0; i < r->vert_count; i++) {
            int x = MAPX(r->verts[i * 2]);
            int y = MAPY(r->verts[i * 2 + 1]);
            draw_dot(&cv, x, y);
        }
    }

    if (!stbi_write_png(out_path, w, h, 4, img, w * 4)) {
        fprintf(stderr, "error: failed to write '%s'\n", out_path);
        free(img);
        return -1;
    }

    printf("records: %d, triangles: %d\n", nrecs, total_tris);
    if (nrecs == 1)
        printf("draw: %u %s\n", g_records[recs[0]].seq, g_records[recs[0]].name);
    printf("bounds: [%.2f, %.2f] x [%.2f, %.2f]\n",
           (double)gminx, (double)gminy, (double)gmaxx, (double)gmaxy);
    printf("canvas: %dx%d\n", w, h);
    printf("output: %s\n", out_path);

    free(img);
    return 0;
}

int main(int argc, char** argv)
{
    const char* dump_path;
    const char* sel_spec = "all";
    char base[960];
    char out_one[1024];
    unsigned char* sel = NULL;
    int mode;
    int* recs;
    int nrecs = 0;
    int i;

    if (argc < 2) {
        printf("geom_visualizer - render a VGLITE geometry dump as PNG(s)\n");
        printf("usage: geom_visualizer <dump_file> [output.png] [selection]\n");
        printf("  selection: all (default) | each | list e.g. 2, 1,3, 1-2, 1,3-4\n");
        return 0;
    }

    dump_path = argv[1];
    if (parse_dump(dump_path) != 0) {
        fprintf(stderr, "error: no usable records in '%s'\n", dump_path);
        return 1;
    }
    if (argc >= 4) sel_spec = argv[3];

    /* default output base: <dump minus extension>_vis */
    {
        size_t n = strlen(dump_path);
        size_t keep = n;
        if (keep >= sizeof(base) - 32) keep = sizeof(base) - 32;
        memcpy(base, dump_path, keep);
        base[keep] = '\0';
        if (keep > 0) {
            char* dot = strrchr(base, '.');
            if (dot && dot != base) *dot = '\0';
        }
        strcat(base, "_vis");
    }

    mode = parse_selection(sel_spec, &sel);
    if (mode < 0) return 1;

    if (argc >= 3) {
        /* explicit output: strip ".png" suffix for per-file naming in each mode */
        size_t n = strlen(argv[2]);
        if (n >= sizeof(base)) n = sizeof(base) - 1;
        memcpy(base, argv[2], n);
        base[n] = '\0';
        if (n > 4 && strcmp(base + n - 4, ".png") == 0) base[n - 4] = '\0';
    }

    recs = (int*)malloc((size_t)g_record_count * sizeof(int));
    if (!recs) {
        fprintf(stderr, "error: out of memory\n");
        free(sel);
        return 1;
    }

    if (mode == SEL_ALL || mode == SEL_LIST) {
        if (mode == SEL_ALL) {
            for (i = 0; i < g_record_count; i++) recs[i] = i;
            nrecs = g_record_count;
        } else {
            for (i = 0; i < g_record_count; i++) if (sel[i]) recs[nrecs++] = i;
        }
        if (argc >= 3) {
            snprintf(out_one, sizeof(out_one), "%s.png", base);
        } else if (mode == SEL_ALL) {
            snprintf(out_one, sizeof(out_one), "%s.png", base);
        } else {
            char sbuf[128];
            sanitize_sel(sbuf, sizeof(sbuf), sel_spec);
            snprintf(out_one, sizeof(out_one), "%s_%s.png", base, sbuf);
        }
        if (render_records(recs, nrecs, out_one) != 0) {
            free(recs); free(sel); return 1;
        }
    } else { /* SEL_EACH: one PNG per record */
        for (i = 0; i < g_record_count; i++) {
            recs[0] = i;
            snprintf(out_one, sizeof(out_one), "%s_%u.png", base, g_records[i].seq);
            if (render_records(recs, 1, out_one) != 0) {
                free(recs); free(sel); return 1;
            }
        }
    }

    free(recs);
    free(sel);

    /* cleanup */
    for (i = 0; i < g_record_count; i++) {
        free(g_records[i].verts);
        free(g_records[i].idx);
    }
    free(g_records);
    return 0;
}
