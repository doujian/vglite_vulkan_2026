/*
 * vg_lite_path.c - Ported from gpu-vglite VGLite reference implementation.
 *
 * Provides path API functions used by the stroke code and tests:
 *   - get_data_count (static helper)
 *   - get_data_size (extern, also used by vg_lite_stroke.c)
 *   - compute_pathbounds (static helper)
 *   - vg_lite_set_path_type
 *   - vg_lite_clear_path
 *   - vg_lite_get_path_length
 *   - vg_lite_append_path
 *
 * Adaptation notes for the Vulkan backend:
 *   - Replaced vg_lite_os_malloc/vg_lite_os_free with malloc/free.
 *   - Removed all #if (CHIPID==0x355) kernel ioctl blocks.
 *   - Removed vg_lite_kernel ioctl call for uploaded.handle free;
 *     zero the uploaded fields directly instead.
 *   - Removed #if gcFEATURE_VG_STROKE_PATH guard — stroke cleanup
 *     is always compiled.
 *   - Removed #if gcFEATURE_VG_ARC_PATH guard — arc path handling
 *     is always compiled.
 *   - Replaced s_context.path_lastX/path_lastY with local variables.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vg_lite.h"
#include "vg_lite_util.h"

/* Macros for path data alignment and min/max. */
#define CDALIGN(value, by) (((value) + (by) - 1) & ~((by) - 1))
#define CDMIN(x, y) ((x) > (y) ? (y) : (x))
#define CDMAX(x, y) ((x) > (y) ? (x) : (y))

static int32_t get_data_count(uint8_t cmd)
{
    static int32_t count[] = {
        0,
        0,
        2,
        2,
        2,
        2,
        4,
        4,
        6,
        6,
        0,
        1,
        1,
        1,
        1,
        2,
        2,
        4,
        4,
        5,
        5,
        5,
        5,
        5,
        5,
        5,
        5
    };

    if (cmd > VLC_OP_LCWARC_REL) {
        return -1;
    }
    else {
        return count[cmd];
    }
}

static void compute_pathbounds(float* xmin, float* ymin, float* xmax, float* ymax, float x, float y)
{
    if (xmin != NULL)
    {
        *xmin = *xmin < x ? *xmin : x;
    }

    if (xmax != NULL)
    {
        *xmax = *xmax > x ? *xmax : x;
    }

    if (ymin != NULL)
    {
        *ymin = *ymin < y ? *ymin : y;
    }

    if (ymax != NULL)
    {
        *ymax = *ymax > y ? *ymax : y;
    }
}

int32_t get_data_size(vg_lite_format_t format)
{
    int32_t data_size = 0;

    switch (format) {
    case VG_LITE_S8:
        data_size = sizeof(int8_t);
        break;

    case VG_LITE_S16:
        data_size = sizeof(int16_t);
        break;

    case VG_LITE_S32:
        data_size = sizeof(int32_t);
        break;

    default:
        data_size = sizeof(vg_lite_float_t);
        break;
    }

    return data_size;
}

vg_lite_error_t vg_lite_set_path_type(vg_lite_path_t* path, vg_lite_path_type_t path_type)
{
    if (!path ||
        (path_type != VG_LITE_DRAW_ZERO &&
         path_type != VG_LITE_DRAW_FILL_PATH &&
         path_type != VG_LITE_DRAW_STROKE_PATH &&
         path_type != VG_LITE_DRAW_FILL_STROKE_PATH)
       )
        return VG_LITE_INVALID_ARGUMENT;

    path->path_type = path_type;

    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_clear_path(vg_lite_path_t* path)
{
    if (path == NULL) {
        return VG_LITE_INVALID_ARGUMENT;
    }

    path->uploaded.address = 0;
    path->uploaded.bytes = 0;
    path->uploaded.handle = NULL;
    path->uploaded.memory = NULL;

    if (path->pdata_internal == 1 && path->path != NULL) {
        free(path->path);
    }
    path->path = NULL;

    if (path->stroke_path) {
        free(path->stroke_path);
        path->stroke_path = NULL;
    }

    if (path->stroke) {
        if (path->stroke->path_list_divide) {
            vg_lite_path_list_ptr cur_list;
            while (path->stroke->path_list_divide) {
                cur_list = path->stroke->path_list_divide->next;
                if (path->stroke->path_list_divide->path_points) {
                    vg_lite_path_point_ptr temp_point;
                    while (path->stroke->path_list_divide->path_points) {
                        temp_point = path->stroke->path_list_divide->path_points->next;
                        free(path->stroke->path_list_divide->path_points);
                        path->stroke->path_list_divide->path_points = temp_point;
                    }
                    temp_point = NULL;
                }
                free(path->stroke->path_list_divide);
                path->stroke->path_list_divide = cur_list;
            }
            cur_list = 0;
        }

        if (path->stroke->stroke_paths) {
            vg_lite_sub_path_ptr temp_sub_path;
            while (path->stroke->stroke_paths) {
                temp_sub_path = path->stroke->stroke_paths->next;
                if (path->stroke->stroke_paths->point_list) {
                    vg_lite_path_point_ptr temp_point;
                    while (path->stroke->stroke_paths->point_list) {
                        temp_point = path->stroke->stroke_paths->point_list->next;
                        free(path->stroke->stroke_paths->point_list);
                        path->stroke->stroke_paths->point_list = temp_point;
                    }
                    temp_point = NULL;
                }
                free(path->stroke->stroke_paths);
                path->stroke->stroke_paths = temp_sub_path;
            }
            temp_sub_path = NULL;
        }

        if (path->stroke->dash_pattern)
            free(path->stroke->dash_pattern);

        free(path->stroke);
        path->stroke = NULL;
        path->stroke_valid = 0;

        path->stroke_size = 0;
    }

    return VG_LITE_SUCCESS;
}

vg_lite_uint32_t vg_lite_get_path_length(vg_lite_uint8_t *cmd, vg_lite_uint32_t count, vg_lite_format_t format)
{
    uint32_t size = 0;
    int32_t dCount = 0;
    uint32_t i = 0;
    int32_t data_size = 0;

    data_size = get_data_size(format);

    for (i = 0; i < count; i++) {
        size++;     /* OP CODE. */

        dCount = get_data_count(cmd[i]);
        size = CDALIGN(size, data_size);
        size += dCount * data_size;

    }
    if (cmd[count - 1] != VLC_OP_END || cmd[count - 1] != VLC_OP_CLOSE) {
        size++;
        size = CDALIGN(size, data_size);
    }

    return size;
}

vg_lite_error_t vg_lite_append_path(vg_lite_path_t *path,
                                    vg_lite_uint8_t *cmd,
                                    vg_lite_pointer data,
                                    vg_lite_uint32_t seg_count)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;
    uint32_t i;
    int32_t j;
    int32_t offset = 0;
    int32_t dataCount = 0;
    float *dataf = (float*) data;
    float *pathf = NULL;
    int32_t *data_s32 = (int32_t*) data;
    int32_t *path_s32 = NULL;
    int16_t *data_s16 = (int16_t*) data;
    int16_t *path_s16 = NULL;
    int8_t *data_s8 = (int8_t*) data;
    int8_t *path_s8 = NULL;
    uint8_t *pathc = NULL;
    int32_t data_size;
    uint8_t arc_path = 0;
    uint8_t h_v_path = 0;
    uint8_t smooth_path = 0;
    float px = 0.0f, py = 0.0f, cx = 0.0f, cy = 0.0f;
    /* Track final cursor position (replaces s_context.path_lastX/Y). */
    float lastX, lastY;
    (void)lastX;
    (void)lastY;
    int rel = 0;

    if (cmd == NULL || data == NULL || path == NULL)
        return VG_LITE_INVALID_ARGUMENT;

    for(i = 0; i < seg_count; i++) {
        if (cmd[i] > VLC_OP_LCWARC_REL)
            return VG_LITE_INVALID_ARGUMENT;
    }

    /* Support NULL path->path case for OpenVG */
    if (!path->path) {
        data_size = vg_lite_get_path_length(cmd, seg_count, path->format);
        path->path = (vg_lite_pointer)malloc(data_size);
        if (!path->path)
        {
            return VG_LITE_OUT_OF_RESOURCES;
        }
        path->pdata_internal = 1;
        memset(path->path, 0, data_size);
    }

    data_size = get_data_size(path->format);
    path->path_changed= 1;
    pathf = (float *)path->path;
    path_s32 = (int32_t *)path->path;
    path_s16 = (int16_t *)path->path;
    path_s8 = (int8_t *)path->path;
    pathc = (uint8_t *)path->path;
    /* Set bounding box if the first opcode is VLC_OP_MOVE_* */
    if ((cmd[0] & 0xfe) == VLC_OP_MOVE) {
        switch (path->format)
        {
        case VG_LITE_S8:
            cx = (float)data_s8[0];
            cy = (float)data_s8[1];
            break;
        case VG_LITE_S16:
            cx = (float)data_s16[0];
            cy = (float)data_s16[1];
            break;
        case VG_LITE_S32:
            cx = (float)data_s32[0];
            cy = (float)data_s32[1];
            break;
        case VG_LITE_FP32:
            cx = (float)dataf[0];
            cy = (float)dataf[1];
            break;
        }
        path->bounding_box[0] = path->bounding_box[2] = cx;
        path->bounding_box[1] = path->bounding_box[3] = cy;
    }

    /* Loop to fill path data. */
    for (i = 0; i < seg_count; i++) {
        *(pathc + offset) = cmd[i];
        offset++;

        dataCount = get_data_count(cmd[i]);
        /* compute the bounding_box. */
        if (dataCount >= 0) {
            offset = CDALIGN(offset, data_size);
            if ((cmd[i] > VLC_OP_CLOSE) &&
                (cmd[i] < VLC_OP_HLINE) &&
                ((cmd[i] & 0x01) == 1)) {
                rel = 1;
            }
            else if ((cmd[i] >= VLC_OP_HLINE) &&
                ((cmd[i] & 0x01) == 0)) {
                rel = 1;
            }
            else {
                rel = 0;
            }
            if (cmd[i] >= VLC_OP_HLINE && cmd[i] <= VLC_OP_VLINE_REL) {
                switch (path->format) {
                case VG_LITE_S8:
                    path_s8 = (int8_t*)(pathc + offset);
                    path_s8[0] = *data_s8;
                    data_s8++;
                    if (rel) {
                        cx = px + (float)path_s8[0];
                        cy = py + (float)path_s8[1];
                    }
                    else {
                        cx = (float)path_s8[0];
                        cy = (float)path_s8[1];
                    }
                    break;

                case VG_LITE_S16:
                    path_s16 = (int16_t*)(pathc + offset);
                    path_s16[0] = *data_s16;
                    data_s16++;
                    if (rel) {
                        cx = px + (float)path_s16[0];
                        cy = py + (float)path_s16[1];
                    }
                    else {
                        cx = (float)path_s16[0];
                        cy = (float)path_s16[1];
                    }
                    break;

                case VG_LITE_S32:
                    path_s32 = (int32_t*)(pathc + offset);
                    path_s32[0] = *data_s32;
                    data_s32++;
                    if (rel) {
                        cx = px + (float)path_s32[0];
                        cy = py + (float)path_s32[1];
                    }
                    else {
                        cx = (float)path_s32[0];
                        cy = (float)path_s32[1];
                    }
                    break;

                case VG_LITE_FP32:
                    pathf = (float*)(pathc + offset);
                    pathf[0] = *dataf;
                    dataf++;
                    if (rel) {
                        cx = px + (float)pathf[0];
                        cy = py + (float)pathf[1];
                    }
                    else {
                        cx = (float)pathf[0];
                        cy = (float)pathf[1];
                    }
                    break;
                }
                h_v_path = 1;
                /* Update path bounds. */
                path->bounding_box[0] = CDMIN(path->bounding_box[0], cx);
                path->bounding_box[2] = CDMAX(path->bounding_box[2], cx);
                path->bounding_box[1] = CDMIN(path->bounding_box[1], cy);
                path->bounding_box[3] = CDMAX(path->bounding_box[3], cy);
            }
            else if (cmd[i] < VLC_OP_SCCWARC) {
                /* Mark smooth path,convert it in next step. */
                if (cmd[i] <= VLC_OP_SCUBIC_REL && cmd[i] >= VLC_OP_SQUAD) {
                    smooth_path = 1;
                }
                for (j = 0; j < dataCount / 2; j++) {
                    switch (path->format) {
                    case VG_LITE_S8:
                        path_s8 = (int8_t *)(pathc + offset);
                        path_s8[j * 2] = *data_s8;
                        data_s8++;
                        path_s8[j * 2 + 1] = *data_s8;
                        data_s8++;

                        if (rel) {
                            cx = px + path_s8[j * 2];
                            cy = py + path_s8[j * 2 + 1];
                        }
                        else {
                            cx = path_s8[j * 2];
                            cy = path_s8[j * 2 + 1];
                        }
                        break;
                    case VG_LITE_S16:
                        path_s16 = (int16_t *)(pathc + offset);
                        path_s16[j * 2] = *data_s16;
                        data_s16++;
                        path_s16[j * 2 + 1] = *data_s16;
                        data_s16++;

                        if (rel) {
                            cx = px + path_s16[j * 2];
                            cy = py + path_s16[j * 2 + 1];
                        }
                        else {
                            cx = path_s16[j * 2];
                            cy = path_s16[j * 2 + 1];
                        }
                        break;
                    case VG_LITE_S32:
                        path_s32 = (int32_t *)(pathc + offset);
                        path_s32[j * 2] = *data_s32;
                        data_s32++;
                        path_s32[j * 2 + 1] = *data_s32;
                        data_s32++;

                        if (rel) {
                            cx = px + path_s32[j * 2];
                            cy = py + path_s32[j * 2 + 1];
                        }
                        else {
                            cx = (float)path_s32[j * 2];
                            cy = (float)path_s32[j * 2 + 1];
                        }
                        break;
                    case VG_LITE_FP32:
                        pathf = (float *)(pathc + offset);
                        pathf[j * 2] = *dataf;
                        dataf++;
                        pathf[j * 2 + 1] = *dataf;
                        dataf++;

                        if (rel) {
                            cx = px + pathf[j * 2];
                            cy = py + pathf[j * 2 + 1];
                        }
                        else {
                            cx = pathf[j * 2];
                            cy = pathf[j * 2 + 1];
                        }
                        break;

                    default:
                        return VG_LITE_INVALID_ARGUMENT;
                    }
                    /* Update move to and line path bounds. */
                    path->bounding_box[0] = CDMIN(path->bounding_box[0], cx);
                    path->bounding_box[2] = CDMAX(path->bounding_box[2], cx);
                    path->bounding_box[1] = CDMIN(path->bounding_box[1], cy);
                    path->bounding_box[3] = CDMAX(path->bounding_box[3], cy);
                }
            }
            else {
                arc_path = 1;
                switch (path->format) {
                case VG_LITE_S8:
                    path_s8 = (int8_t*)(pathc + offset);
                    path_s8[0] = *data_s8;
                    data_s8++;
                    path_s8[1] = *data_s8;
                    data_s8++;
                    path_s8[2] = *data_s8;
                    data_s8++;
                    path_s8[3] = *data_s8;
                    data_s8++;
                    path_s8[4] = *data_s8;
                    data_s8++;

                    if (rel) {
                        cx = px + path_s8[3];
                        cy = py + path_s8[4];
                    }
                    else {
                        cx = path_s8[3];
                        cy = path_s8[4];
                    }
                    /* Update path bounds. */
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],cx + 2 * path_s8[0],cy + 2 * path_s8[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],px + 2 * path_s8[1],py + 2 * path_s8[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],cx - 2 * path_s8[0],cy - 2 * path_s8[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],px - 2 * path_s8[1],py - 2 * path_s8[1]);
                    break;

                case VG_LITE_S16:
                    path_s16 = (int16_t*)(pathc + offset);
                    path_s16[0] = *data_s16;
                    data_s16++;
                    path_s16[1] = *data_s16;
                    data_s16++;
                    path_s16[2] = *data_s16;
                    data_s16++;
                    path_s16[3] = *data_s16;
                    data_s16++;
                    path_s16[4] = *data_s16;
                    data_s16++;

                    if (rel) {
                        cx = px + path_s16[3];
                        cy = py + path_s16[4];
                    }
                    else {
                        cx = path_s16[3];
                        cy = path_s16[4];
                    }
                    /* Update path bounds. */
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],cx + 2 * path_s16[0],cy + 2 * path_s16[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],px + 2 * path_s16[1],py + 2 * path_s16[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],cx - 2 * path_s16[0],cy - 2 * path_s16[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],px - 2 * path_s16[1],py - 2 * path_s16[1]);
                    break;

                case VG_LITE_S32:
                    path_s32 = (int32_t*)(pathc + offset);
                    path_s32[0] = *data_s32;
                    data_s32++;
                    path_s32[1] = *data_s32;
                    data_s32++;
                    path_s32[2] = *data_s32;
                    data_s32++;
                    path_s32[3] = *data_s32;
                    data_s32++;
                    path_s32[4] = *data_s32;
                    data_s32++;

                    if (rel) {
                        cx = px + path_s32[3];
                        cy = py + path_s32[4];
                    }
                    else {
                        cx = (float)path_s32[3];
                        cy = (float)path_s32[4];
                    }
                    /* Update path bounds. */
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],cx + 2 * path_s32[0],cy + 2 * path_s32[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],px + 2 * path_s32[1],py + 2 * path_s32[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],cx - 2 * path_s32[0],cy - 2 * path_s32[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],px - 2 * path_s32[1],py - 2 * path_s32[1]);
                    break;

                case VG_LITE_FP32:
                    pathf = (float*)(pathc + offset);
                    pathf[0] = *dataf;
                    dataf++;
                    pathf[1] = *dataf;
                    dataf++;
                    pathf[2] = *dataf;
                    dataf++;
                    pathf[3] = *dataf;
                    dataf++;
                    pathf[4] = *dataf;
                    dataf++;

                    if (rel) {
                        cx = px + pathf[3];
                        cy = py + pathf[4];
                    }
                    else {
                        cx = pathf[3];
                        cy = pathf[4];
                    }
                    /* Update path bounds. */
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],cx + 2 * pathf[0],cy + 2 * pathf[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],px + 2 * pathf[1],py + 2 * pathf[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],cx - 2 * pathf[0],cy - 2 * pathf[1]);
                    compute_pathbounds(&path->bounding_box[0], &path->bounding_box[1], &path->bounding_box[2], &path->bounding_box[3],px - 2 * pathf[1],py - 2 * pathf[1]);
                    break;
                }

            }
            px = cx;
            py = cy;

            offset += dataCount * data_size;
        }
    }
    if (cmd[seg_count - 1] == VLC_OP_END
        || (cmd[seg_count - 1] == VLC_OP_CLOSE  && (arc_path | h_v_path | smooth_path))
        ) {
        path->path_length = offset;
    }
    else {
        path->path_length = offset + data_size;
        path->add_end = 1;
        ((uint8_t*)(path->path))[offset] = 0;
    }

    if (arc_path | h_v_path | smooth_path) {
        error = vg_lite_init_arc_path(path,
                    path->format,
                    path->quality,
                    path->path_length,
                    path->path,
                    path->bounding_box[0], path->bounding_box[1],
                    path->bounding_box[2], path->bounding_box[3]);
    }
    lastX = cx;
    lastY = cy;
    return error;
}
