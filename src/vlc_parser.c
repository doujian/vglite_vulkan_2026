#define _USE_MATH_DEFINES
#include "vlc_parser.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* VLC opcodes (from vg_lite.h) */
#define VLC_OP_END                  0x00
#define VLC_OP_CLOSE                0x01
#define VLC_OP_MOVE                 0x02
#define VLC_OP_MOVE_REL             0x03
#define VLC_OP_LINE                 0x04
#define VLC_OP_LINE_REL             0x05
#define VLC_OP_QUAD                 0x06
#define VLC_OP_QUAD_REL             0x07
#define VLC_OP_CUBIC                0x08
#define VLC_OP_CUBIC_REL            0x09
#define VLC_OP_BREAK                0x0A
#define VLC_OP_HLINE                0x0B
#define VLC_OP_HLINE_REL            0x0C
#define VLC_OP_VLINE                0x0D
#define VLC_OP_VLINE_REL            0x0E
#define VLC_OP_SQUAD                0x0F
#define VLC_OP_SQUAD_REL            0x10
#define VLC_OP_SCUBIC               0x11
#define VLC_OP_SCUBIC_REL           0x12
#define VLC_OP_SCCWARC              0x13
#define VLC_OP_SCCWARC_REL          0x14
#define VLC_OP_SCWARC               0x15
#define VLC_OP_SCWARC_REL           0x16
#define VLC_OP_LCCWARC              0x17
#define VLC_OP_LCCWARC_REL          0x18
#define VLC_OP_LCWARC               0x19
#define VLC_OP_LCWARC_REL           0x1A

/* VLC macros from LVGL reference */
#define VLC_GET_OP_CODE(cur)        (*(uint8_t*)(cur))
#define VLC_GET_ARG(cur, idx, fmt, fmt_size) vlc_get_arg((cur) + (idx) * (fmt_size), (fmt))

static float vlc_get_arg(const void* data, vg_lite_format_t format)
{
    switch(format) {
        case VG_LITE_S8:    return (float)(*((int8_t*)data));
        case VG_LITE_S16:   return (float)(*((int16_t*)data));
        case VG_LITE_S32:   return (float)(*((int32_t*)data));
        case VG_LITE_FP32:  return *((float*)data);
        default:            return 0.0f;
    }
}

uint8_t vlc_format_size(vg_lite_format_t format)
{
    switch(format) {
        case VG_LITE_S8:    return 1;
        case VG_LITE_S16:   return 2;
        case VG_LITE_S32:   return 4;
        case VG_LITE_FP32:  return 4;
        default:            return 0;
    }
}

/* Argument count per opcode (matches gpu-vglite get_data_count). */
uint8_t vlc_op_arg_count(uint8_t opcode)
{
    switch(opcode) {
        case VLC_OP_END:            return 0;
        case VLC_OP_CLOSE:          return 0;
        case VLC_OP_MOVE:           return 2;
        case VLC_OP_MOVE_REL:       return 2;
        case VLC_OP_LINE:           return 2;
        case VLC_OP_LINE_REL:       return 2;
        case VLC_OP_QUAD:           return 4;
        case VLC_OP_QUAD_REL:       return 4;
        case VLC_OP_CUBIC:          return 6;
        case VLC_OP_CUBIC_REL:      return 6;
        case VLC_OP_BREAK:          return 0;
        case VLC_OP_HLINE:          return 1;
        case VLC_OP_HLINE_REL:      return 1;
        case VLC_OP_VLINE:          return 1;
        case VLC_OP_VLINE_REL:      return 1;
        case VLC_OP_SQUAD:          return 2;
        case VLC_OP_SQUAD_REL:      return 2;
        case VLC_OP_SCUBIC:         return 4;
        case VLC_OP_SCUBIC_REL:     return 4;
        /* All 8 ARC variants: rh, rv, rot, end_x, end_y */
        case VLC_OP_SCCWARC:
        case VLC_OP_SCCWARC_REL:
        case VLC_OP_SCWARC:
        case VLC_OP_SCWARC_REL:
        case VLC_OP_LCCWARC:
        case VLC_OP_LCCWARC_REL:
        case VLC_OP_LCWARC:
        case VLC_OP_LCWARC_REL:     return 5;
        default:                    return 0;
    }
}

void vlc_path_init(VlcPath* path)
{
    path->cmds = NULL;
    path->cmd_count = 0;
    path->cmd_capacity = 0;
    path->bbox_min.x = FLT_MAX;
    path->bbox_min.y = FLT_MAX;
    path->bbox_max.x = -FLT_MAX;
    path->bbox_max.y = -FLT_MAX;
    path->last_cmd_type = -1;
    path->last_ctrl_x = 0.0f;
    path->last_ctrl_y = 0.0f;
}

void vlc_path_free(VlcPath* path)
{
    if (path->cmds) {
        free(path->cmds);
        path->cmds = NULL;
    }
    path->cmd_count = 0;
    path->cmd_capacity = 0;
}

static void vlc_add_command(VlcPath* path, VlcCommand cmd)
{
    if (path->cmd_count >= path->cmd_capacity) {
        int new_capacity = path->cmd_capacity == 0 ? 16 : path->cmd_capacity * 2;
        VlcCommand* new_cmds = realloc(path->cmds, new_capacity * sizeof(VlcCommand));
        if (!new_cmds) return;
        path->cmds = new_cmds;
        path->cmd_capacity = new_capacity;
    }
    path->cmds[path->cmd_count++] = cmd;
    
    for (int i = 0; i < cmd.pt_count; i++) {
        path->bbox_min.x = fminf(path->bbox_min.x, cmd.pts[i].x);
        path->bbox_min.y = fminf(path->bbox_min.y, cmd.pts[i].y);
        path->bbox_max.x = fmaxf(path->bbox_max.x, cmd.pts[i].x);
        path->bbox_max.y = fmaxf(path->bbox_max.y, cmd.pts[i].y);
    }
}

/* Convert quadratic bezier to cubic using 2/3 approximation */
static void quad_to_cubic(float prev_x, float prev_y, float qx1, float qy1, 
                          float x, float y, VlcCommand* cmd)
{
    /* Cubic control points: qcx0 = prev + 2/3*(q1-prev), qcx1 = end + 2/3*(q1-end) */
    float qcx0 = prev_x + (qx1 - prev_x) * 2.0f / 3.0f;
    float qcy0 = prev_y + (qy1 - prev_y) * 2.0f / 3.0f;
    float qcx1 = x + (qx1 - x) * 2.0f / 3.0f;
    float qcy1 = y + (qy1 - y) * 2.0f / 3.0f;
    
    cmd->type = VLC_CMD_CUBIC;
    cmd->pts[0].x = qcx0;
    cmd->pts[0].y = qcy0;
    cmd->pts[1].x = qcx1;
    cmd->pts[1].y = qcy1;
    cmd->pts[2].x = x;
    cmd->pts[2].y = y;
    cmd->pt_count = 3;
}

/* Emit a cubic command and update smooth-control tracking. */
static void emit_cubic(VlcPath* path,
                       float cx1, float cy1, float cx2, float cy2,
                       float x, float y)
{
    VlcCommand cmd;
    cmd.type = VLC_CMD_CUBIC;
    cmd.pts[0].x = cx1; cmd.pts[0].y = cy1;
    cmd.pts[1].x = cx2; cmd.pts[1].y = cy2;
    cmd.pts[2].x = x;   cmd.pts[2].y = y;
    cmd.pt_count = 3;
    vlc_add_command(path, cmd);
    path->last_cmd_type = VLC_CMD_CUBIC;
    path->last_ctrl_x = cx2;
    path->last_ctrl_y = cy2;
}

static void emit_line(VlcPath* path, float x, float y)
{
    VlcCommand cmd;
    cmd.type = VLC_CMD_LINE;
    cmd.pts[0].x = x;
    cmd.pts[0].y = y;
    cmd.pt_count = 1;
    vlc_add_command(path, cmd);
    path->last_cmd_type = VLC_CMD_LINE;
    /* last_ctrl is the previous endpoint for LINE (reflection of itself). */
    path->last_ctrl_x = x;
    path->last_ctrl_y = y;
}

/* ---- SVG arc to cubic bezier conversion ----
 *
 * Implements the endpoint-to-center conversion described in the SVG spec
 * (Appendix F.6.5). Emits one or more cubic bezier segments into the path.
 *
 *   x1,y1   = current point (start)
 *   rx,rv   = radii (may be scaled up if too small)
 *   phi     = x-axis rotation in radians
 *   fa      = large-arc flag (1 = sweep >= 180 deg)
 *   fs      = sweep flag (1 = CCW)
 *   x2,y2   = end point
 *
 * After VGLite opcode conventions:
 *   CCW opcodes (SCCWARC/LCCWARC)  -> fs = 1
 *   CW  opcodes (SCWARC /LCWARC )  -> fs = 0
 *   SC  opcodes (small)            -> fa = 0
 *   LC  opcodes (large)            -> fa = 1
 */
static void arc_to_cubics(VlcPath* path,
                          float x1, float y1,
                          float rx, float ry, float phi_deg,
                          int large_arc, int sweep_ccw,
                          float x2, float y2)
{
    /* Degenerate: no radius or zero-length arc -> straight line. */
    if (rx == 0.0f || ry == 0.0f || (x1 == x2 && y1 == y2)) {
        emit_line(path, x2, y2);
        return;
    }

    /* Negative radii are illegal; take absolute value. */
    if (rx < 0.0f) rx = -rx;
    if (ry < 0.0f) ry = -ry;

    float phi = phi_deg * (float)M_PI / 180.0f;
    float cphi = cosf(phi), sphi = sinf(phi);

    /* Step 1: compute (x1',y1') in the rotated frame centered at midpoint. */
    float dx = (x1 - x2) * 0.5f;
    float dy = (y1 - y2) * 0.5f;
    float x1p =  cphi * dx + sphi * dy;
    float y1p = -sphi * dx + cphi * dy;

    /* Step 2: ensure radii are large enough; scale if needed. */
    float rx2 = rx * rx;
    float ry2 = ry * ry;
    float x1p2 = x1p * x1p;
    float y1p2 = y1p * y1p;
    float lambda = x1p2 / rx2 + y1p2 / ry2;
    if (lambda > 1.0f) {
        float sq_lambda = sqrtf(lambda);
        rx *= sq_lambda;
        ry *= sq_lambda;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }

    /* Step 3: compute center (cx',cy') in the rotated frame. */
    float sign = (large_arc != sweep_ccw) ? 1.0f : -1.0f;
    float num = rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
    float den = rx2 * y1p2 + ry2 * x1p2;
    float coef = (den > 0.0f) ? sqrtf((num > 0.0f ? num : 0.0f) / den) : 0.0f;
    float cxp =  sign * coef * (rx * y1p) / ry;
    float cyp = -sign * coef * (ry * x1p) / rx;

    /* Step 4: rotate center back to original frame. */
    float cx = cphi * cxp - sphi * cyp + (x1 + x2) * 0.5f;
    float cy = sphi * cxp + cphi * cyp + (y1 + y2) * 0.5f;

    /* Step 5: compute start and sweep angles. */
    float ux = (x1p - cxp) / rx;
    float uy = (y1p - cyp) / ry;
    float vx = (-x1p - cxp) / rx;
    float vy = (-y1p - cyp) / ry;

    float len_u = sqrtf(ux * ux + uy * uy);
    float theta1 = (uy < 0.0f) ? -acosf(ux / len_u) : acosf(ux / len_u);

    float len_v = sqrtf(vx * vx + vy * vy);
    float dtheta = (ux * vy - uy * vx) / (len_u * len_v);
    dtheta = (dtheta < -1.0f) ? -1.0f : (dtheta > 1.0f) ? 1.0f : dtheta;
    dtheta = (uy < 0.0f) ? -acosf(dtheta) : acosf(dtheta);

    if (!sweep_ccw && dtheta > 0.0f)
        dtheta -= 2.0f * (float)M_PI;
    else if (sweep_ccw && dtheta < 0.0f)
        dtheta += 2.0f * (float)M_PI;

    /* Step 6: split into segments of <= 90 degrees and emit cubic beziers. */
    int segments = (int)ceilf(fabsf(dtheta) / ((float)M_PI / 2.0f));
    if (segments < 1) segments = 1;

    float alpha = dtheta / (float)segments;
    float t = tanf(alpha * 0.5f);
    float k = (4.0f / 3.0f) * t;

    float px = x1, py = y1;
    for (int i = 0; i < segments; i++) {
        float a0 = theta1 + alpha * (float)i;
        float a1 = a0 + alpha;

        float cos0 = cosf(a0), sin0 = sinf(a0);
        float cos1 = cosf(a1), sin1 = sinf(a1);

        /* End point of this segment on the unit-rotated ellipse, mapped back. */
        float ex = cx + rx * ( cphi * cos1 - sphi * sin1);
        float ey = cy + ry * ( sphi * cos1 + cphi * sin1);

        /* Tangent at start: derivative of (rx*cos, ry*sin) rotated by phi.
         * P1 = P0 + k * tangent_dir_start. */
        float tx0 = -rx * sin0;
        float ty0 =  ry * cos0;
        float p1x = px + k * (cphi * tx0 - sphi * ty0);
        float p1y = py + k * (sphi * tx0 + cphi * ty0);

        /* Tangent at end (incoming): P2 = P3 - k * tangent_dir_end. */
        float tx1 = -rx * sin1;
        float ty1 =  ry * cos1;
        float p2x = ex - k * (cphi * tx1 - sphi * ty1);
        float p2y = ey - k * (sphi * tx1 + cphi * ty1);

        emit_cubic(path, p1x, p1y, p2x, p2y, ex, ey);
        px = ex;
        py = ey;
    }

    /* Snap last endpoint to the exact target to avoid drift. */
    if (path->cmd_count > 0) {
        VlcCommand* last = &path->cmds[path->cmd_count - 1];
        last->pts[2].x = x2;
        last->pts[2].y = y2;
    }
}

int vlc_parse_path(const vg_lite_path_t* vg_path, VlcPath* out_path)
{
    uint8_t fmt_size = vlc_format_size(vg_path->format);
    if (fmt_size == 0) return -1;
    
    uint8_t* cur = (uint8_t*)vg_path->path;
    uint8_t* end = cur + vg_path->path_length;
    
    float prev_x = 0.0f, prev_y = 0.0f;
    
    while (cur < end) {
        uint8_t opcode = VLC_GET_OP_CODE(cur);
        uint8_t arg_count = vlc_op_arg_count(opcode);
        
        cur += fmt_size; /* Skip opcode */
        
        switch (opcode) {
            case VLC_OP_END: {
                /* END closes any open subpath before finishing tessellation. */
                VlcCommand cmd;
                cmd.type = VLC_CMD_CLOSE;
                cmd.pt_count = 0;
                vlc_add_command(out_path, cmd);
                break;
            }
                
            case VLC_OP_CLOSE: {
                VlcCommand cmd;
                cmd.type = VLC_CMD_CLOSE;
                cmd.pt_count = 0;
                vlc_add_command(out_path, cmd);
                break;
            }

            case VLC_OP_BREAK: {
                /* BREAK disconnects the current subpath without closing it. */
                VlcCommand cmd;
                cmd.type = VLC_CMD_CLOSE;
                cmd.pt_count = 0;
                vlc_add_command(out_path, cmd);
                break;
            }
            
            case VLC_OP_MOVE: {
                float x = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float y = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                VlcCommand cmd;
                cmd.type = VLC_CMD_MOVE;
                cmd.pts[0].x = x;
                cmd.pts[0].y = y;
                cmd.pt_count = 1;
                vlc_add_command(out_path, cmd);
                prev_x = x;
                prev_y = y;
                out_path->last_cmd_type = VLC_CMD_MOVE;
                break;
            }
            
            case VLC_OP_MOVE_REL: {
                float x = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float y = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                VlcCommand cmd;
                cmd.type = VLC_CMD_MOVE;
                cmd.pts[0].x = prev_x + x;
                cmd.pts[0].y = prev_y + y;
                cmd.pt_count = 1;
                vlc_add_command(out_path, cmd);
                prev_x += x;
                prev_y += y;
                out_path->last_cmd_type = VLC_CMD_MOVE;
                break;
            }
            
            case VLC_OP_LINE: {
                float x = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float y = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                emit_line(out_path, x, y);
                prev_x = x;
                prev_y = y;
                break;
            }
            
            case VLC_OP_LINE_REL: {
                float x = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float y = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                emit_line(out_path, prev_x + x, prev_y + y);
                prev_x += x;
                prev_y += y;
                break;
            }

            case VLC_OP_HLINE: {
                float x = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                emit_line(out_path, x, prev_y);
                prev_x = x;
                break;
            }

            case VLC_OP_HLINE_REL: {
                float dx = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                emit_line(out_path, prev_x + dx, prev_y);
                prev_x += dx;
                break;
            }

            case VLC_OP_VLINE: {
                float y = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                emit_line(out_path, prev_x, y);
                prev_y = y;
                break;
            }

            case VLC_OP_VLINE_REL: {
                float dy = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                emit_line(out_path, prev_x, prev_y + dy);
                prev_y += dy;
                break;
            }
            
            case VLC_OP_QUAD: {
                float qx1 = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float qy1 = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float x  = VLC_GET_ARG(cur, 2, vg_path->format, fmt_size);
                float y  = VLC_GET_ARG(cur, 3, vg_path->format, fmt_size);
                VlcCommand cmd;
                quad_to_cubic(prev_x, prev_y, qx1, qy1, x, y, &cmd);
                vlc_add_command(out_path, cmd);
                out_path->last_cmd_type = VLC_CMD_CUBIC;
                out_path->last_ctrl_x = qx1;
                out_path->last_ctrl_y = qy1;
                prev_x = x;
                prev_y = y;
                break;
            }
            
            case VLC_OP_QUAD_REL: {
                float qx1 = prev_x + VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float qy1 = prev_y + VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float x  = prev_x + VLC_GET_ARG(cur, 2, vg_path->format, fmt_size);
                float y  = prev_y + VLC_GET_ARG(cur, 3, vg_path->format, fmt_size);
                VlcCommand cmd;
                quad_to_cubic(prev_x, prev_y, qx1, qy1, x, y, &cmd);
                vlc_add_command(out_path, cmd);
                out_path->last_cmd_type = VLC_CMD_CUBIC;
                out_path->last_ctrl_x = qx1;
                out_path->last_ctrl_y = qy1;
                prev_x = x;
                prev_y = y;
                break;
            }

            case VLC_OP_SQUAD: {
                /* Smooth quadratic: control point is reflection of previous
                 * control point about the current point. */
                float x  = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float y  = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float qx1, qy1;
                if (out_path->last_cmd_type == VLC_CMD_CUBIC) {
                    qx1 = 2.0f * prev_x - out_path->last_ctrl_x;
                    qy1 = 2.0f * prev_y - out_path->last_ctrl_y;
                } else {
                    qx1 = prev_x;
                    qy1 = prev_y;
                }
                VlcCommand cmd;
                quad_to_cubic(prev_x, prev_y, qx1, qy1, x, y, &cmd);
                vlc_add_command(out_path, cmd);
                out_path->last_cmd_type = VLC_CMD_CUBIC;
                /* For smooth-quad the stored control is the *implicit* control
                 * (used by reflection), not the emitted cubic control. */
                out_path->last_ctrl_x = qx1;
                out_path->last_ctrl_y = qy1;
                prev_x = x;
                prev_y = y;
                break;
            }

            case VLC_OP_SQUAD_REL: {
                float dx = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float dy = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float x  = prev_x + dx;
                float y  = prev_y + dy;
                float qx1, qy1;
                if (out_path->last_cmd_type == VLC_CMD_CUBIC) {
                    qx1 = 2.0f * prev_x - out_path->last_ctrl_x;
                    qy1 = 2.0f * prev_y - out_path->last_ctrl_y;
                } else {
                    qx1 = prev_x;
                    qy1 = prev_y;
                }
                VlcCommand cmd;
                quad_to_cubic(prev_x, prev_y, qx1, qy1, x, y, &cmd);
                vlc_add_command(out_path, cmd);
                out_path->last_cmd_type = VLC_CMD_CUBIC;
                out_path->last_ctrl_x = qx1;
                out_path->last_ctrl_y = qy1;
                prev_x = x;
                prev_y = y;
                break;
            }
            
            case VLC_OP_CUBIC: {
                float cx1 = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float cy1 = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float cx2 = VLC_GET_ARG(cur, 2, vg_path->format, fmt_size);
                float cy2 = VLC_GET_ARG(cur, 3, vg_path->format, fmt_size);
                float x  = VLC_GET_ARG(cur, 4, vg_path->format, fmt_size);
                float y  = VLC_GET_ARG(cur, 5, vg_path->format, fmt_size);
                emit_cubic(out_path, cx1, cy1, cx2, cy2, x, y);
                prev_x = x;
                prev_y = y;
                break;
            }
            
            case VLC_OP_CUBIC_REL: {
                float cx1 = prev_x + VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float cy1 = prev_y + VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float cx2 = prev_x + VLC_GET_ARG(cur, 2, vg_path->format, fmt_size);
                float cy2 = prev_y + VLC_GET_ARG(cur, 3, vg_path->format, fmt_size);
                float x  = prev_x + VLC_GET_ARG(cur, 4, vg_path->format, fmt_size);
                float y  = prev_y + VLC_GET_ARG(cur, 5, vg_path->format, fmt_size);
                emit_cubic(out_path, cx1, cy1, cx2, cy2, x, y);
                prev_x = x;
                prev_y = y;
                break;
            }

            case VLC_OP_SCUBIC: {
                /* Smooth cubic: first control point is reflection of previous
                 * second control point; second control point and end are given. */
                float cx2 = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float cy2 = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float x  = VLC_GET_ARG(cur, 2, vg_path->format, fmt_size);
                float y  = VLC_GET_ARG(cur, 3, vg_path->format, fmt_size);
                float cx1, cy1;
                if (out_path->last_cmd_type == VLC_CMD_CUBIC) {
                    cx1 = 2.0f * prev_x - out_path->last_ctrl_x;
                    cy1 = 2.0f * prev_y - out_path->last_ctrl_y;
                } else {
                    cx1 = prev_x;
                    cy1 = prev_y;
                }
                emit_cubic(out_path, cx1, cy1, cx2, cy2, x, y);
                prev_x = x;
                prev_y = y;
                break;
            }

            case VLC_OP_SCUBIC_REL: {
                float cx2 = prev_x + VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float cy2 = prev_y + VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float x  = prev_x + VLC_GET_ARG(cur, 2, vg_path->format, fmt_size);
                float y  = prev_y + VLC_GET_ARG(cur, 3, vg_path->format, fmt_size);
                float cx1, cy1;
                if (out_path->last_cmd_type == VLC_CMD_CUBIC) {
                    cx1 = 2.0f * prev_x - out_path->last_ctrl_x;
                    cy1 = 2.0f * prev_y - out_path->last_ctrl_y;
                } else {
                    cx1 = prev_x;
                    cy1 = prev_y;
                }
                emit_cubic(out_path, cx1, cy1, cx2, cy2, x, y);
                prev_x = x;
                prev_y = y;
                break;
            }

            /* ---- Arc opcodes ----
             * Params: [0]=rh [1]=rv [2]=rot(deg) [3]=end_x [4]=end_y
             * Direction mapping (VGLite naming -> SVG fa/fs flags):
             *   SCCWARC : small, CCW -> fa=0, fs=1
             *   SCWARC  : small, CW  -> fa=0, fs=0
             *   LCCWARC : large, CCW -> fa=1, fs=1
             *   LCWARC  : large, CW  -> fa=1, fs=0
             */
            #define PARSE_ARC(large_arc, sweep_ccw, is_rel)                            \
                do {                                                                    \
                    float rh  = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);        \
                    float rv  = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);        \
                    float rot = VLC_GET_ARG(cur, 2, vg_path->format, fmt_size);        \
                    float ex  = VLC_GET_ARG(cur, 3, vg_path->format, fmt_size);        \
                    float ey  = VLC_GET_ARG(cur, 4, vg_path->format, fmt_size);        \
                    if (is_rel) { ex = prev_x + ex; ey = prev_y + ey; }                \
                    arc_to_cubics(out_path, prev_x, prev_y, rh, rv, rot,               \
                                  large_arc, sweep_ccw, ex, ey);                        \
                    prev_x = ex;                                                       \
                    prev_y = ey;                                                       \
                } while (0)

            case VLC_OP_SCCWARC:        PARSE_ARC(0, 1, 0); break;
            case VLC_OP_SCCWARC_REL:    PARSE_ARC(0, 1, 1); break;
            case VLC_OP_SCWARC:         PARSE_ARC(0, 0, 0); break;
            case VLC_OP_SCWARC_REL:     PARSE_ARC(0, 0, 1); break;
            case VLC_OP_LCCWARC:        PARSE_ARC(1, 1, 0); break;
            case VLC_OP_LCCWARC_REL:    PARSE_ARC(1, 1, 1); break;
            case VLC_OP_LCWARC:         PARSE_ARC(1, 0, 0); break;
            case VLC_OP_LCWARC_REL:     PARSE_ARC(1, 0, 1); break;
            #undef PARSE_ARC
            
            default:
                break;
        }
        
        cur += arg_count * fmt_size;
    }
    
    return out_path->cmd_count;
}
