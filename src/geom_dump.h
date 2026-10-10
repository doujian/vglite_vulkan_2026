/*
 * geom_dump.h - Optional tessellation geometry dumper for vg_lite_draw
 *
 * Appends structured triangle/AABB records to a text dump file whenever a
 * path is tessellated. Completely inactive unless the environment variable
 * VGLITE_DUMP_GEOMETRY is set to a non-empty value.
 *
 * Dump file routing:
 *   VGLITE_DUMP_GEOMETRY unset or empty        -> inactive (zero overhead)
 *   VGLITE_DUMP_GEOMETRY=1 or "on" (any case)  -> <dumpdir>/geometry_dump.txt
 *   VGLITE_DUMP_GEOMETRY=<name>                -> <dumpdir>/<name>
 * where <dumpdir> is vg_lite_dump_dir().
 *
 * Record format (one record per tessellation, appended, crash-safe):
 *   DRAW <seq> <entry_name>
 *   F <fb_width> <fb_height>            (target framebuffer size)
 *   V <vertex_count> x0 y0 x1 y1 ...
 *   T <tri_count> i0 i1 i2 i0 i1 i2 ...
 *   C cx0 cy0 cx1 cy1 cx2 cy2 cx3 cy3   (cover quad: path bbox corners
 *       transformed by the user matrix, exactly what the pipeline's cover
 *       pass uses; order minx,miny -> maxx,miny -> maxx,maxy -> minx,maxy)
 *   B <min_x> <min_y> <max_x> <max_y>   (axis-aligned envelope of C)
 *
 * Coordinates are POST-TRANSFORM (screen space): the caller-supplied user
 * matrix is applied to a copy of the vertices before writing; B is the
 * axis-aligned envelope of the transformed path-bbox corners (C line).
 * Indices are unchanged.
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

#endif /* GEOM_DUMP_H */
