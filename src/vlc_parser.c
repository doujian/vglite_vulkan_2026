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

uint8_t vlc_op_arg_count(uint8_t opcode)
{
    switch(opcode) {
        case VLC_OP_END:        return 0;
        case VLC_OP_CLOSE:      return 0;
        case VLC_OP_MOVE:       return 2;
        case VLC_OP_MOVE_REL:   return 2;
        case VLC_OP_LINE:       return 2;
        case VLC_OP_LINE_REL:   return 2;
        case VLC_OP_QUAD:       return 4;
        case VLC_OP_QUAD_REL:   return 4;
        case VLC_OP_CUBIC:      return 6;
        case VLC_OP_CUBIC_REL:  return 6;
        default:                return 0;
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
        
        switch(opcode) {
            case VLC_OP_END:
                break;
                
            case VLC_OP_CLOSE: {
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
                break;
            }
            
            case VLC_OP_LINE: {
                float x = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float y = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                VlcCommand cmd;
                cmd.type = VLC_CMD_LINE;
                cmd.pts[0].x = x;
                cmd.pts[0].y = y;
                cmd.pt_count = 1;
                vlc_add_command(out_path, cmd);
                prev_x = x;
                prev_y = y;
                break;
            }
            
            case VLC_OP_LINE_REL: {
                float x = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float y = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                VlcCommand cmd;
                cmd.type = VLC_CMD_LINE;
                cmd.pts[0].x = prev_x + x;
                cmd.pts[0].y = prev_y + y;
                cmd.pt_count = 1;
                vlc_add_command(out_path, cmd);
                prev_x += x;
                prev_y += y;
                break;
            }
            
            case VLC_OP_QUAD: {
                float qx1 = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float qy1 = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float x = VLC_GET_ARG(cur, 2, vg_path->format, fmt_size);
                float y = VLC_GET_ARG(cur, 3, vg_path->format, fmt_size);
                VlcCommand cmd;
                quad_to_cubic(prev_x, prev_y, qx1, qy1, x, y, &cmd);
                vlc_add_command(out_path, cmd);
                prev_x = x;
                prev_y = y;
                break;
            }
            
            case VLC_OP_QUAD_REL: {
                float qx1 = prev_x + VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float qy1 = prev_y + VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float x = prev_x + VLC_GET_ARG(cur, 2, vg_path->format, fmt_size);
                float y = prev_y + VLC_GET_ARG(cur, 3, vg_path->format, fmt_size);
                VlcCommand cmd;
                quad_to_cubic(prev_x, prev_y, qx1, qy1, x, y, &cmd);
                vlc_add_command(out_path, cmd);
                prev_x = x;
                prev_y = y;
                break;
            }
            
            case VLC_OP_CUBIC: {
                float cx1 = VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float cy1 = VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float cx2 = VLC_GET_ARG(cur, 2, vg_path->format, fmt_size);
                float cy2 = VLC_GET_ARG(cur, 3, vg_path->format, fmt_size);
                float x = VLC_GET_ARG(cur, 4, vg_path->format, fmt_size);
                float y = VLC_GET_ARG(cur, 5, vg_path->format, fmt_size);
                VlcCommand cmd;
                cmd.type = VLC_CMD_CUBIC;
                cmd.pts[0].x = cx1;
                cmd.pts[0].y = cy1;
                cmd.pts[1].x = cx2;
                cmd.pts[1].y = cy2;
                cmd.pts[2].x = x;
                cmd.pts[2].y = y;
                cmd.pt_count = 3;
                vlc_add_command(out_path, cmd);
                prev_x = x;
                prev_y = y;
                break;
            }
            
            case VLC_OP_CUBIC_REL: {
                float cx1 = prev_x + VLC_GET_ARG(cur, 0, vg_path->format, fmt_size);
                float cy1 = prev_y + VLC_GET_ARG(cur, 1, vg_path->format, fmt_size);
                float cx2 = prev_x + VLC_GET_ARG(cur, 2, vg_path->format, fmt_size);
                float cy2 = prev_y + VLC_GET_ARG(cur, 3, vg_path->format, fmt_size);
                float x = prev_x + VLC_GET_ARG(cur, 4, vg_path->format, fmt_size);
                float y = prev_y + VLC_GET_ARG(cur, 5, vg_path->format, fmt_size);
                VlcCommand cmd;
                cmd.type = VLC_CMD_CUBIC;
                cmd.pts[0].x = cx1;
                cmd.pts[0].y = cy1;
                cmd.pts[1].x = cx2;
                cmd.pts[1].y = cy2;
                cmd.pts[2].x = x;
                cmd.pts[2].y = y;
                cmd.pt_count = 3;
                vlc_add_command(out_path, cmd);
                prev_x = x;
                prev_y = y;
                break;
            }
            
            default:
                break;
        }
        
        cur += arg_count * fmt_size;
    }
    
    return out_path->cmd_count;
}