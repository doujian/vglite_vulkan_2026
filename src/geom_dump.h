/*
 * geom_dump.h - Optional geometry dumper for vg_lite_draw and vg_lite_blit
 *
 * Appends structured triangle/AABB records to a text dump file whenever a
 * path is tessellated (DRAW records) or a blit is issued (BLIT records).
 * Completely inactive unless the environment variable
 * VGLITE_DUMP_GEOMETRY is set to a non-empty value.
 *
 * Dump file routing:
 *   VGLITE_DUMP_GEOMETRY unset or empty        -> inactive (zero overhead)
 *   VGLITE_DUMP_GEOMETRY=1 or "on" (any case)  -> <dumpdir>/geometry_dump.txt
 *   VGLITE_DUMP_GEOMETRY=<name>                -> <dumpdir>/<name>
 * where <dumpdir> is vg_lite_dump_dir().
 *
 * Record format (one record per call, appended, crash-safe). DRAW and BLIT
 * records interleave in the same file with a shared increasing sequence
 * number:
 *   DRAW <seq> <entry_name>
 *   F <fb_width> <fb_height>            (target framebuffer size)
 *   V <vertex_count> x0 y0 x1 y1 ...
 *   T <tri_count> i0 i1 i2 i0 i1 i2 ...
 *   C cx0 cy0 cx1 cy1 cx2 cy2 cx3 cy3   (cover quad: path bbox corners
 *       transformed by the user matrix, exactly what the pipeline's cover
 *       pass uses; order minx,miny -> maxx,miny -> maxx,maxy -> minx,maxy)
 *   B <min_x> <min_y> <max_x> <max_y>   (axis-aligned envelope of C)
 *
 *   BLIT <seq> obb|fullscreen           (blit rasterization mode)
 *   F <dst_width> <dst_height>
 *   V 4 x0 y0 ... x3 y3                 (obb: the 4 transformed source
 *       corners in pixel space; or V 3 with the fullscreen-triangle pixel
 *       corners)
 *   T 2 0 1 2 2 1 3                     (obb quad as two triangles;
 *       or T 1 0 1 2 for the fullscreen triangle)
 *   C cx0 cy0 ... cx3 cy3               (obb: same 4 transformed corners;
 *       fullscreen: the 4 screen corners (0,0)(w,0)(w,h)(0,h))
 *   B <min_x> <min_y> <max_x> <max_y>   (axis-aligned envelope of C)
 *
 * Coordinates are POST-TRANSFORM (screen space): the caller-supplied user
 * matrix is applied to a copy of the vertices before writing; B is the
 * axis-aligned envelope of the transformed path-bbox corners (C line).
 * Indices are unchanged. BLIT coordinates are destination pixel space
 * (y-down, origin top-left), 1:1 with the framebuffer.
 */

#ifndef GEOM_DUMP_H
#define GEOM_DUMP_H

#include "tessellator.h"
#include "vg_lite.h"

/* Append one tessellation record to the geometry dump file.
 * No-op (returns immediately) unless VGLITE_DUMP_GEOMETRY is active.
 * Handles geom == NULL gracefully. matrix == NULL means identity.
 * fb_width/fb_height are the draw target's dimensions (F line). */
void geom_dump_tessellation(const TessGeometry* geom,
                              const vg_lite_matrix_t* matrix,
                              int fb_width, int fb_height,
                              const char* entry_name);

/* Quick activation check: non-zero when VGLITE_DUMP_GEOMETRY is active.
 * Lets callers skip pixel-space geometry computation when dumping is off. */
int geom_dump_active(void);

/* Append one blit record to the geometry dump file.
 * No-op (returns immediately) unless VGLITE_DUMP_GEOMETRY is active.
 * corners_px[8] = 4 corner (x,y) pairs in DESTINATION PIXEL SPACE:
 *   use_obb != 0  -> the 4 source corners (0,0)(w,0)(w,h)(0,h) transformed
 *                    by the blit matrix (same math as compute_blit_obb,
 *                    before the NDC conversion). Emitted as V 4 / T 2 / C.
 *   use_obb == 0  -> the fullscreen-triangle corners mapped back from NDC
 *                    ((-1,-1)(3,-1)(3,3)) to pixels; only the first 3 are
 *                    used (V 3 / T 1). C becomes the 4 screen corners.
 * dst_w/dst_h are the target dimensions (F line and fullscreen C corners).
 * entry_name overrides the mode tag ("obb"/"fullscreen") in the header. */
void geom_dump_blit(const float corners_px[8],
                    int use_obb,
                    int dst_w, int dst_h,
                    const char* entry_name);

#endif /* GEOM_DUMP_H */
