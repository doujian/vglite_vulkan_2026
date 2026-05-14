/*
 * Fan Tessellator - converts parsed path commands to triangles
 * 
 * Implements fan tessellation for convex polygons and curve flattening for beziers.
 */

#ifndef TESSELLATOR_H
#define TESSELLATOR_H

#include "vlc_parser.h"

/* Tessellated vertex */
typedef struct {
    float x;
    float y;
} TessVertex;

/* Tessellated geometry */
typedef struct {
    TessVertex* vertices;       /* Vertex array */
    uint32_t* indices;          /* Triangle indices (3 per triangle) */
    int vertex_count;           /* Number of vertices */
    int index_count;            /* Number of indices */
    float bbox_min_x;           /* Bounding box */
    float bbox_min_y;
    float bbox_max_x;
    float bbox_max_y;
} TessGeometry;

/* Initialize TessGeometry */
void tess_geometry_init(TessGeometry* geom);

/* Free TessGeometry */
void tess_geometry_free(TessGeometry* geom);

/* Tessellate VlcPath to TessGeometry */
int tessellate_path(const VlcPath* path, TessGeometry* geom);

/* Compute number of bezier segments needed */
int bezier_segment_count(float x0, float y0, float x1, float y1, 
                         float x2, float y2, float x3, float y3);

#endif /* TESSELLATOR_H */