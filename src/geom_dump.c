/*
 * geom_dump.c - Optional tessellation geometry dumper for vg_lite_draw
 *
 * See geom_dump.h for the record format and activation rules.
 * Gated by env var VGLITE_DUMP_GEOMETRY: unset/empty = completely inactive.
 */

#include "geom_dump.h"
#include "vg_lite_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cached activation state: 0 = unchecked, 1 = off, 2 = on */
static int g_geom_dump_state = 0;

static int geom_dump_enabled(void)
{
    if (g_geom_dump_state == 0) {
        const char* val = getenv("VGLITE_DUMP_GEOMETRY");
        if (val && val[0] != '\0') {
            g_geom_dump_state = 2;
        } else {
            g_geom_dump_state = 1;
        }
    }
    return g_geom_dump_state == 2;
}

/* TRUE when env value selects the default filename (not a custom name). */
static int geom_dump_default_name(void)
{
    const char* val = getenv("VGLITE_DUMP_GEOMETRY");
    size_t i;

    if (!val) return 1;
    if (val[0] == '\0') return 1;
    if (strcmp(val, "1") == 0) return 1;
    /* case-insensitive "on" */
    if ((val[0] == 'o' || val[0] == 'O') &&
        (val[1] == 'n' || val[1] == 'N') &&
        val[2] == '\0') {
        return 1;
    }
    /* sanity: a filename must not contain separators or drive specs */
    for (i = 0; val[i] != '\0'; i++) {
        char c = val[i];
        if (c == '/' || c == '\\' || c == ':') return 1; /* fall back to default */
    }
    return 0;
}

void geom_dump_tessellation(const TessGeometry* geom,
                             const vg_lite_matrix_t* matrix,
                             int fb_width, int fb_height,
                             const char* entry_name)
{
    static unsigned int seq = 0;      /* incrementing record counter, starts at 1 */
    char outpath[512];
    const char* envval;
    FILE* f;
    int i;

    if (!geom_dump_enabled()) return;
    if (!geom) return;

    envval = getenv("VGLITE_DUMP_GEOMETRY");
    if (!envval) envval = "";

    if (geom_dump_default_name()) {
        snprintf(outpath, sizeof(outpath), "%s/%s",
                 vg_lite_dump_dir(), "geometry_dump.txt");
    } else {
        snprintf(outpath, sizeof(outpath), "%s/%s",
                 vg_lite_dump_dir(), envval);
    }

    f = fopen(outpath, "a");
    if (!f) return;

    seq++;

    /* Cover-pass-consistent snapshot: the pipeline's cover pass transforms
     * the 4 corners of the path-space bbox by the user matrix and uses the
     * resulting (oriented) quad. We dump exactly that quad (C line, corners
     * in order (minx,miny)(maxx,miny)(maxx,maxy)(minx,maxy)) and its
     * axis-aligned envelope (B line) - identical math to the shader path
     * (vg_lite_draw.c cover pass / draw.vert). Vertices are transformed the
     * same way. */
    {
        float* tx = NULL;
        float cx[8];   /* transformed bbox corners, 4 x (x,y) */
        float minx, miny, maxx, maxy;
        int c;

        /* path-space bbox corners (same source the cover pass uses) */
        {
            float px[4] = { geom->bbox_min_x, geom->bbox_max_x,
                            geom->bbox_max_x, geom->bbox_min_x };
            float py[4] = { geom->bbox_min_y, geom->bbox_min_y,
                            geom->bbox_max_y, geom->bbox_max_y };
            for (c = 0; c < 4; c++) {
                if (matrix) {
                    cx[c * 2]     = matrix->m[0][0] * px[c] + matrix->m[0][1] * py[c] + matrix->m[0][2];
                    cx[c * 2 + 1] = matrix->m[1][0] * px[c] + matrix->m[1][1] * py[c] + matrix->m[1][2];
                } else {
                    cx[c * 2]     = px[c];
                    cx[c * 2 + 1] = py[c];
                }
            }
        }
        minx = maxx = cx[0]; miny = maxy = cx[1];
        for (c = 1; c < 4; c++) {
            if (cx[c * 2]     < minx) minx = cx[c * 2];
            if (cx[c * 2]     > maxx) maxx = cx[c * 2];
            if (cx[c * 2 + 1] < miny) miny = cx[c * 2 + 1];
            if (cx[c * 2 + 1] > maxy) maxy = cx[c * 2 + 1];
        }

        if (matrix && geom->vertex_count > 0) {
            tx = (float*)malloc((size_t)geom->vertex_count * 2 * sizeof(float));
            if (tx) {
                for (i = 0; i < geom->vertex_count; i++) {
                    float x = geom->vertices[i].x;
                    float y = geom->vertices[i].y;
                    tx[i * 2]     = matrix->m[0][0] * x + matrix->m[0][1] * y + matrix->m[0][2];
                    tx[i * 2 + 1] = matrix->m[1][0] * x + matrix->m[1][1] * y + matrix->m[1][2];
                }
            }
        }

        fprintf(f, "DRAW %u %s\n", seq, entry_name ? entry_name : "?");
        fprintf(f, "F %d %d\n", fb_width, fb_height);

        fprintf(f, "V %d", geom->vertex_count);
        if (tx) {
            for (i = 0; i < geom->vertex_count; i++)
                fprintf(f, " %.4f %.4f", tx[i * 2], tx[i * 2 + 1]);
        } else {
            for (i = 0; i < geom->vertex_count; i++)
                fprintf(f, " %.4f %.4f", geom->vertices[i].x, geom->vertices[i].y);
        }
        fprintf(f, "\n");

        fprintf(f, "T %d", geom->index_count / 3);
        for (i = 0; i < geom->index_count; i++) {
            fprintf(f, " %u", (unsigned int)geom->indices[i]);
        }
        fprintf(f, "\n");

        fprintf(f, "C %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",
                cx[0], cx[1], cx[2], cx[3], cx[4], cx[5], cx[6], cx[7]);

        fprintf(f, "B %.4f %.4f %.4f %.4f\n", minx, miny, maxx, maxy);

        free(tx);
    }

    fclose(f);   /* close after each record: crash-safe */
}
