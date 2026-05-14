/*
 * VLC Path Parser - decodes VLC-encoded paths from vg_lite_path_t
 * 
 * Parses opcodes: MOVE (0x02), LINE (0x04), QUAD (0x06), CUBIC (0x08), CLOSE (0x01), END (0x00)
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