#include "tessellator.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#define BEZIER_FLATTEN_TOLERANCE 0.125f
#define MAX_BEZIER_SEGMENTS 256

void tess_geometry_init(TessGeometry* geom)
{
    geom->vertices = NULL;
    geom->indices = NULL;
    geom->vertex_count = 0;
    geom->index_count = 0;
    geom->bbox_min_x = FLT_MAX;
    geom->bbox_min_y = FLT_MAX;
    geom->bbox_max_x = -FLT_MAX;
    geom->bbox_max_y = -FLT_MAX;
}

void tess_geometry_free(TessGeometry* geom)
{
    if (geom->vertices) {
        free(geom->vertices);
        geom->vertices = NULL;
    }
    if (geom->indices) {
        free(geom->indices);
        geom->indices = NULL;
    }
    geom->vertex_count = 0;
    geom->index_count = 0;
}

static uint32_t push_vertex(TessGeometry* geom, float x, float y)
{
    int new_count = geom->vertex_count + 1;
    TessVertex* new_vertices = realloc(geom->vertices, new_count * sizeof(TessVertex));
    if (!new_vertices) return UINT32_MAX;
    
    geom->vertices = new_vertices;
    geom->vertices[geom->vertex_count].x = x;
    geom->vertices[geom->vertex_count].y = y;
    
    geom->bbox_min_x = fminf(geom->bbox_min_x, x);
    geom->bbox_min_y = fminf(geom->bbox_min_y, y);
    geom->bbox_max_x = fmaxf(geom->bbox_max_x, x);
    geom->bbox_max_y = fmaxf(geom->bbox_max_y, y);
    
    return (uint32_t)geom->vertex_count++;
}

static void push_triangle(TessGeometry* geom, uint32_t a, uint32_t b, uint32_t c)
{
    int new_count = geom->index_count + 3;
    uint32_t* new_indices = realloc(geom->indices, new_count * sizeof(uint32_t));
    if (!new_indices) return;
    
    geom->indices = new_indices;
    geom->indices[geom->index_count++] = a;
    geom->indices[geom->index_count++] = b;
    geom->indices[geom->index_count++] = c;
}

/* Bezier point evaluation: B(t) = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3 */
static void bezier_point(float x0, float y0, float x1, float y1, 
                         float x2, float y2, float x3, float y3,
                         float t, float* out_x, float* out_y)
{
    float t2 = t * t;
    float t3 = t2 * t;
    float mt = 1.0f - t;
    float mt2 = mt * mt;
    float mt3 = mt2 * mt;
    
    *out_x = mt3 * x0 + 3.0f * mt2 * t * x1 + 3.0f * mt * t2 * x2 + t3 * x3;
    *out_y = mt3 * y0 + 3.0f * mt2 * t * y1 + 3.0f * mt * t2 * y2 + t3 * y3;
}

int bezier_segment_count(float x0, float y0, float x1, float y1, 
                         float x2, float y2, float x3, float y3)
{
    /* Estimate based on curve deviation from straight line */
    float deviation = fabsf(x1 - (x0 + x3) / 2.0f) + fabsf(y1 - (y0 + y3) / 2.0f) +
                      fabsf(x2 - (x0 + x3) / 2.0f) + fabsf(y2 - (y0 + y3) / 2.0f);
    
    float len = fabsf(x3 - x0) + fabsf(y3 - y0);
    if (len < 1.0f) len = 1.0f;
    
    int segments = (int)(deviation / BEZIER_FLATTEN_TOLERANCE) + 1;
    if (segments > MAX_BEZIER_SEGMENTS) segments = MAX_BEZIER_SEGMENTS;
    if (segments < 2) segments = 2;
    
    return segments;
}

static void flatten_cubic(float x0, float y0, float x1, float y1,
                          float x2, float y2, float x3, float y3,
                          TessGeometry* geom, uint32_t first_idx, uint32_t* prev_idx)
{
    int segments = bezier_segment_count(x0, y0, x1, y1, x2, y2, x3, y3);
    float step = 1.0f / (float)segments;
    
    for (int s = 1; s <= segments; s++) {
        float t = step * (float)s;
        float x, y;
        bezier_point(x0, y0, x1, y1, x2, y2, x3, y3, t, &x, &y);
        
        uint32_t curr_idx = push_vertex(geom, x, y);
        if (curr_idx == UINT32_MAX) return;
        
        if (*prev_idx != UINT32_MAX && first_idx != UINT32_MAX) {
            push_triangle(geom, first_idx, *prev_idx, curr_idx);
        }
        
        *prev_idx = curr_idx;
    }
}

int tessellate_path(const VlcPath* path, TessGeometry* geom)
{
    if (!path || !geom || path->cmd_count < 2) return -1;
    
    uint32_t first_idx = UINT32_MAX;
    uint32_t prev_idx = UINT32_MAX;
    float prev_x = 0.0f, prev_y = 0.0f;
    
    for (int i = 0; i < path->cmd_count; i++) {
        const VlcCommand* cmd = &path->cmds[i];
        
        switch(cmd->type) {
            case VLC_CMD_MOVE: {
                float x = cmd->pts[0].x;
                float y = cmd->pts[0].y;
                first_idx = push_vertex(geom, x, y);
                prev_idx = UINT32_MAX;
                prev_x = x;
                prev_y = y;
                break;
            }
            
            case VLC_CMD_LINE: {
                float x = cmd->pts[0].x;
                float y = cmd->pts[0].y;
                uint32_t curr_idx = push_vertex(geom, x, y);
                if (curr_idx == UINT32_MAX) break;
                
                if (prev_idx != UINT32_MAX && first_idx != UINT32_MAX) {
                    push_triangle(geom, first_idx, prev_idx, curr_idx);
                }
                
                prev_idx = curr_idx;
                prev_x = x;
                prev_y = y;
                break;
            }
            
            case VLC_CMD_CUBIC: {
                float cx1 = cmd->pts[0].x;
                float cy1 = cmd->pts[0].y;
                float cx2 = cmd->pts[1].x;
                float cy2 = cmd->pts[1].y;
                float x = cmd->pts[2].x;
                float y = cmd->pts[2].y;
                
                flatten_cubic(prev_x, prev_y, cx1, cy1, cx2, cy2, x, y, 
                             geom, first_idx, &prev_idx);
                prev_x = x;
                prev_y = y;
                break;
            }
            
            case VLC_CMD_CLOSE: {
                if (prev_idx != UINT32_MAX && first_idx != UINT32_MAX && prev_idx != first_idx) {
                    push_triangle(geom, first_idx, prev_idx, first_idx);
                }
                prev_idx = UINT32_MAX;
                break;
            }
        }
    }
    
    return geom->vertex_count;
}