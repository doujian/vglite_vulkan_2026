/*
 * VLC Path Parser - decodes VLC-encoded paths from vg_lite_path_t
 *
 * Parses all 27 VLC opcodes (0x00-0x1A):
 *   END/CLOSE/BREAK, MOVE/LINE/QUAD/CUBIC (+ REL),
 *   HLINE/VLINE (+ REL),
 *   SQUAD/SCUBIC (+ REL)  [smooth: reflected control point],
 *   SCCWARC/SCWARC/LCCWARC/LCWARC (+ REL) [SVG-style elliptic arcs].
 *
 * Arcs and smooth curves are converted to cubic beziers in-place, so the
 * downstream tessellator only ever sees MOVE/LINE/CUBIC/CLOSE commands.
 *
 * Handles coordinate formats: VG_LITE_S8, VG_LITE_S16, VG_LITE_S32, VG_LITE_FP32
 */

#ifndef VLC_PARSER_H
#define VLC_PARSER_H

#include "vg_lite.h"

/* Point structure */
typedef struct {
    float x;
    float y;
} VlcPoint;

/* Command types */
typedef enum {
    VLC_CMD_MOVE,       /* Move to point */
    VLC_CMD_LINE,       /* Line to point */
    VLC_CMD_CUBIC,      /* Cubic bezier curve */
    VLC_CMD_CLOSE       /* Close path */
} VlcCommandType;

/* Path command */
typedef struct {
    VlcCommandType type;
    VlcPoint pts[4];    /* Max 4 points for cubic (start implicit + 3 explicit) */
    int pt_count;       /* Number of points used */
} VlcCommand;

/* Parsed path */
typedef struct {
    VlcCommand* cmds;       /* Array of commands */
    int cmd_count;          /* Number of commands */
    int cmd_capacity;       /* Capacity of commands array */
    VlcPoint bbox_min;      /* Bounding box minimum */
    VlcPoint bbox_max;      /* Bounding box maximum */
    /* Smooth-curve state: previous command type and the control point used
     * for reflection by SQUAD / SCUBIC. Updated on every emitted command. */
    int   last_cmd_type;    /* VLC_CMD_* of the last emitted command, or -1 */
    float last_ctrl_x;      /* Reflector control X (previous control point) */
    float last_ctrl_y;      /* Reflector control Y (previous control point) */
} VlcPath;

/* Initialize VlcPath structure */
void vlc_path_init(VlcPath* path);

/* Free VlcPath structure */
void vlc_path_free(VlcPath* path);

/* Parse VLC-encoded path from vg_lite_path_t */
int vlc_parse_path(const vg_lite_path_t* vg_path, VlcPath* out_path);

/* Get format byte size */
uint8_t vlc_format_size(vg_lite_format_t format);

/* Get opcode argument count */
uint8_t vlc_op_arg_count(uint8_t opcode);

#endif /* VLC_PARSER_H */