# Stroke (Path Outlining) — Design & Implementation

This document describes how the Vulkan VGLite implementation turns a path +
stroke parameters into outlined, rasterized geometry. The implementation is
a CPU-side port of the official `gpu-vglite` Nuke stoke path; the GPU only
sees the resulting outline polygon and renders it as a regular fill.

---

## 1. Architecture Overview

Stroke is implemented as a **two-stage pipeline**:

```
Stage 1 (CPU, vg_lite_stroke.c)            Stage 2 (GPU, vg_lite_draw.c)
┌──────────────────────────────┐           ┌────────────────────────────┐
│ vg_lite_set_stroke           │           │ vg_lite_set_path_type      │
│   (set cap/join/width/dash)  │           │   (DRAW_STROKE_PATH /      │
│ vg_lite_update_stroke        │           │    DRAW_FILL_STROKE_PATH)  │
│   ├─ _flatten_path           │           │ vg_lite_draw               │
│   │    (VLC → polyline)      │           │   ├─ tessellate stroke_path│
│   ├─ _create_stroke_sub_path │  stroke_  │   │    (stencil pass INVERT)│
│   │    ├─ dash state machine │  path     │   ├─ cover pass            │
│   │    ├─ _process_line_joint│  (VLC)    │   │    (NOT_EQUAL, color)   │
│   │    │    (join geometry)  │ ────────► │   └─ stroke_color fill     │
│   │    ├─ _end_stroke_sub_   │           │                            │
│   │    │    path (cap geom)  │           │                            │
│   │    └─ swing pie patch    │           │                            │
│   └─ _copy_stroke_path       │           │                            │
│        (points → VLC stream) │           │                            │
└──────────────────────────────┘           └────────────────────────────┘
```

**Key insight**: Stroke is "line-to-area conversion." `vg_lite_update_stroke`
produces a new closed path (the outline polygon). The GPU has no knowledge
that this path is a stroke — it renders it via the normal fill pipeline
(stencil + cover, `vg_lite_draw.c`).

---

## 2. Public API

Declared in `inc/vg_lite.h`:

```c
/* Set stroke parameters on a path. */
vg_lite_error_t vg_lite_set_stroke(
    vg_lite_path_t       *path,
    vg_lite_cap_style_t   cap_style,      /* BUTT / ROUND / SQUARE        */
    vg_lite_join_style_t  join_style,     /* MITER / ROUND / BEVEL        */
    vg_lite_float_t       line_width,
    vg_lite_float_t       miter_limit,
    vg_lite_uint32_t     *dash_pattern,   /* on/off pairs, >= 2 entries   */
    vg_lite_uint32_t      pattern_count,  /* forced even internally       */
    vg_lite_uint32_t      dash_phase,     /* initial offset into pattern  */
    vg_lite_uint32_t      stroke_color);  /* ARGB8888                     */

/* Generate the outline geometry. Must be called after set_stroke. */
vg_lite_error_t vg_lite_update_stroke(vg_lite_path_t *path);

/* Path type selects what vg_lite_draw renders. */
vg_lite_path_type_t:
    VG_LITE_DRAW_FILL          = 0          /* default                    */
    VG_LITE_DRAW_STROKE_PATH   = 1 << 0     /* stroke outline only        */
    VG_LITE_DRAW_FILL_STROKE_PATH = 0x3     /* fill + stroke in one draw  */
```

### OpenVG compatibility

The cap/join enums and the dash pattern semantics match OpenVG:

| OpenVG `VGCapStyle`   | VGLite `vg_lite_cap_style_t`  |
|-----------------------|-------------------------------|
| `VG_CAP_BUTT`         | `VG_LITE_CAP_BUTT`            |
| `VG_CAP_ROUND`        | `VG_LITE_CAP_ROUND`           |
| `VG_CAP_SQUARE`       | `VG_LITE_CAP_SQUARE`          |

| OpenVG `VGJoinStyle`  | VGLite `vg_lite_join_style_t` |
|-----------------------|-------------------------------|
| `VG_JOIN_MITER`       | `VG_LITE_JOIN_MITER`          |
| `VG_JOIN_ROUND`       | `VG_LITE_JOIN_ROUND`          |
| `VG_JOIN_BEVEL`       | `VG_LITE_JOIN_BEVEL`          |

---

## 3. Stroke State: `vg_lite_stroke_t`

Defined in `inc/vg_lite.h` L815-881. A path carries one of these:

| Field group         | Purpose                                                       |
|---------------------|---------------------------------------------------------------|
| **Parameters**      | `cap_style`, `join_style`, `line_width`, `miter_limit`,       |
|                     | `dash_pattern[]`, `pattern_count`, `dash_phase`               |
| **Derived**         | `half_width = line_width / 2`,                                |
|                     | `miter_square = miter_limit²` (compared vs `ratio²`)          |
|                     | `pattern_length`, `dash_length`, `dash_index` (dash state)    |
| **Path subdivision**| `path_list_divide` — sub-paths split at MOVE                  |
|                     | (prevents implicit close across disjoint subpaths)           |
| **Outline points**  | `stroke_points` / `stroke_end` / `stroke_count`               |
|                     | `left_point` / `right_point` — left/right contour cursors     |
| **Sub-path chain**  | `stroke_paths` / `last_stroke`                                |
| **Swing patching**  | `swing_handling`, `swing_deltax/y`, `swing_start`,            |
|                     | `swing_stroke`, `swing_length`, `swing_centlen`,              |
|                     | `swing_count`, `need_swing`, `swing_ccw`                      |
| **Flags**           | `fattened` (line_width >= 2.5), `closed`, `add_end`,          |
|                     | `dash_reset`, `uploaded`                                      |

### Curve type tags (`curve_type` field on outline points)

| Value | Meaning                                  | Serialized as        |
|-------|------------------------------------------|----------------------|
| 0     | `CURVE_LINE`     — straight segment      | `VLC_OP_LINE`        |
| 1     | `CURVE_QUAD_CONTROL` — quad ctrl point   | `VLC_OP_QUAD` mid    |
| 2     | `CURVE_QUAD_ANCHOR`  — quad anchor point | `VLC_OP_QUAD` end    |
| 3     | `ARC_SCCW`       — swing pie arc         | `VLC_OP_SCWARC`      |
| 4     | `ARC_SCCW_HALF`  — round cap half-arc    | `VLC_OP_QUAD` (half) |

---

## 4. CPU Stage 1 — Outline Geometry Generation

File: `src/vg_lite_stroke.c` (3912 lines, ported from `gpu-vglite`).

### 4.1 Entry: `vg_lite_update_stroke` (L2630-2683)

```
1. Free previous path->stroke_path.
2. fattened = (line_width >= 2.5 && line_width >= 1.0)
3. _initialize_stroke_dash_parameters
4. _flatten_path            (VLC command stream → polyline point list,
                              split at MOVE into path_list_divide)
5. for each sub-path in path_list_divide:
       _create_stroke_sub_path  (dash state machine + joint + cap + swing)
6. _copy_stroke_path        (point list → VLC command stream → path->stroke_path)
7. If stroke_size == 0, write a single VLC_OP_END to prevent empty data.
```

### 4.2 Path flattening — `_flatten_path` (L959-1257)

Parses the VLC opcode stream (`VLC_OP_MOVE / LINE / QUAD / CUBIC / SCWARC /
LCARC / ...`) and emits a list of `vg_lite_path_point_t`:

```c
typedef struct {
    vg_lite_float x, y;
    vg_lite_float tangentX, tangentY;   /* unit vector to next point */
    vg_lite_float length;               /* distance to next point    */
    vg_lite_uint8_t flatten_flag;       /* 0 = original vertex,
                                            1 = curve flatten point  */
} vg_lite_path_point_t;
```

Curves are recursively subdivided by `_flatten_quad_bezier` (L647-790) and
`_flatten_cubic_bezier` (L792-957). Each subdivision stops when the chord
deviation falls below `FLOAT_EPSILON = 0.001`.

**Subpath splitting**: Each `VLC_OP_MOVE` starts a new list node in
`path_list_divide`. This prevents the join generator from connecting
the end of one sub-path to the start of the next (which would corrupt
outlines when a path contains disjoint figures).

### 4.3 Dash state machine — `_initialize_stroke_dash_parameters` (L2568-2629)

```
pattern_count &= 0xFFFFFFFE              /* force even: on/off pairs */
pattern_length = sum(dash_pattern[0..N])
dash_index     = index after skipping whole cycles from dash_phase
dash_length    = remaining length in the first on/off segment
```

`_get_next_dash_length` (L2076) advances through the pattern as the
outline walker consumes flatten points.

### 4.4 Outline walker — `_create_stroke_path` (L2092-2424)

Iterates the flatten point list and produces the outline:

```
for each segment p[i] → p[i+1]:
    seg_len = p[i].length
    while seg_len > 0:
        step = min(seg_len, dash_length)
        if current dash phase is ON:
            add joint at the walked position via _process_line_joint
        else:
            (skip; off phase produces nothing)
        advance dash state; possibly _end_stroke_sub_path + _start_new_stroke_sub_path
        seg_len -= step

if path is closed:
    _close_stroke_sub_path  /* joins first and last outline points */
```

### 4.5 Join generation — `_process_line_joint` (L1728)

**Input**: current point `P`, segment and previous segment lengths, swing
mode, candidate outline points `(X1,Y1)` (right contour) and `(X2,Y2)`
(left contour) — already offset by `±half_width` along the normal.

**Algorithm**:

```
cos_theta = prev_tangent · cur_tangent              (dot product)

if cos_theta > 0.99999:                             (nearly straight)
    push (X1,Y1) to right; push (X2,Y2) to left
    done

if cos_theta < -0.99999:                            (U-turn)
    ratio = FLT_MAX                                  (forces BEVEL)

counter_clockwise = (prev × cur) >= 0               (cross product sign)
ratio            = 2 / (1 + cos_theta)               (miter extension)
min_length²      = half_width² * (1 - cos) / (1 + cos) + 0.02

/* Swing patching: when consecutive segments are shorter than half_width,
 * the naive offset outline self-intersects. Buffer small segments and
 * emit a pie wedge (_draw_swing_pie_area) to cover the turn center. */
if need_swing && length < half_width:
    accumulate into swing buffer
    if swing_centlen > 0.125 || swing_length > half_width:
        _draw_swing_pie_area(center, radius=half_width)
    return

/* Standard join by style: */
switch (join_style):
    case ROUND:
        push (X1,Y1) to outer contour, curve_type = ARC_SCCW
    case MITER:
        if ratio² <= miter_square:
            _adjust_joint_point  /* slide outer point to miter tip */
        else:
            /* fall through to BEVEL */
    case BEVEL:
        push (X1,Y1) to outer contour (single point bevel)
```

The `counter_clockwise` branch determines which contour is "outer"
(receives the join geometry) and which is "inner" (clipped). The
clockwise branch mirrors the logic.

### 4.6 End caps — `_end_stroke_sub_path` (L2043)

Terminates a dash "on" segment. The two contour endpoints at the cap
position are tagged according to `cap_style`:

| `cap_style`   | Geometry added                                            |
|---------------|-----------------------------------------------------------|
| `BUTT`        | none                                                      |
| `ROUND`       | left endpoint tagged `ARC_SCCW_HALF`; shader fills half-arc |
| `SQUARE`      | both endpoints offset by `half_width` along the tangent    |

### 4.7 Output serialization — `_copy_stroke_path` (L2426-2566)

Walks the left and right contour linked lists, emits VLC opcodes:

```
MOVE  left[0]
LINE  / QUAD / SCWARC  for each left[i]   (curve_type driven)
LINE  / QUAD / SCWARC  for each right[i]  (reverse order)
END
```

Result is written to `path->stroke_path` (size in `path->stroke_size`).

---

## 5. GPU Stage 2 — Rendering

Stroke rendering reuses the **standard path fill pipeline** in
`vg_lite_draw.c`:

1. `vg_lite_set_path_type(DRAW_STROKE_PATH)` makes `vg_lite_draw` read
   `path->stroke_path` instead of `path->path`.
2. `tessellate_path` triangulates the outline polygon (same code path
   as a fill — even-odd or non-zero rule).
3. **Stencil pass**: rasterize the triangulated outline with
   `stencilOp = INVERT`, color writes disabled.
4. **Cover pass**: draw the path's bounding box quad with
   `stencilCompare = NOT_EQUAL`, filling only pixels inside the
   outline with `stroke_color`.

For `VG_LITE_DRAW_FILL_STROKE_PATH`, the same path is drawn twice
(once as fill with `path->path`, once as stroke with `path->stroke_path`),
or in one combined pass if the test exercises it.

**No stroke-specific shader or pipeline exists.** The GPU only sees an
ordinary closed path and colors it with `stroke_color`.

---

## 6. Swing Patching — Short Segment Self-intersection Repair

When `line_width` is comparable to or larger than the flatten segment
length (typical for tight curves stroked thick), the offset outlines on
the inner side of a turn cross each other. Naive rendering produces a
visual notch.

The implementation detects this via the **swing heuristic**:

```
need_swing = (cap_style == BUTT || path.closed) && any flatten_flag == 1

on each short segment (length < half_width):
    swing_centlen += chord_length
    swing_length  += segment_length
    if swing_centlen > FLOAT_SWING_CENTER_RANGE (0.125)
       || swing_length > half_width:
        _draw_swing_pie_area:
            emit a triangle fan / SCWARC arc centered at the turn
            apex with radius half_width, covering the self-intersection
        reset swing buffer
```

The pie patch is a small triangle fan arc that fills the gap left by
the crossing offsets. Without it, tight turns render as a notch.

---

## 7. Dash Pattern Semantics

- `dash_pattern[]` is an array of lengths: `[on_0, off_0, on_1, off_1, ...]`.
- `pattern_count` is forced even (`&= 0xFFFFFFFE`); an odd count drops
  the last entry.
- `dash_phase` is an absolute distance offset into the (infinitely
  repeated) pattern. Whole cycles are skipped in
  `_initialize_stroke_dash_parameters`; the residual is the first
  partial segment.
- Each "on" segment produces a complete sub-path with both end caps.
- Each "off" segment produces nothing (gap).
- For closed paths, the dash walker wraps around the closed loop and
  may need to join the first and last "on" segments via
  `_close_stroke_sub_path`.

---

## 8. Numeric Approximations

The implementation uses hand-rolled trig approximations rather than
`libm`:

| Function | Implementation        | Location   |
|----------|-----------------------|------------|
| `_Asin`  | Taylor table lookup   | L1353-1374 |
| `_Cos`   | Taylor table lookup   | L1376-1390 |
| `_Sine`  | Taylor table lookup   | L1392-1400 |
| `_angle` | `acos + cross sign`   | L2781-2797 |

This is a porting artifact from the embedded gpu-vglite target (no
`libm`). The approximations have ~5 digits of accuracy, sufficient for
geometry.

Constants (L60-100):
```
FLOAT_EPSILON             = 0.001    /* flatten tolerance         */
FLOAT_DIFF_EPSILON        = 0.125    /* swing center range        */
FLOAT_SWING_CENTER_RANGE  = 0.125    /* swing threshold           */
FLOAT_ANGLE_EPSILON       = 0.0045   /* nearly-straight threshold */
SWING_NO / OUT / IN       = 0 / 1 / 2
```

---

## 9. Test Coverage

`tests/stroke/stroke.c` (208 lines) exercises the full matrix on a
256×256 BGRA8888 canvas with a 4-arc "petal" path:

| Frame | cap    | join  | dash      | Mode                |
|-------|--------|-------|-----------|---------------------|
| 0-2   | BUTT   | MITER | [32,32]   | STROKE_PATH         |
| 3-5   | ROUND  | ROUND | [32,32]   | STROKE_PATH         |
| 6-8   | SQUARE | BEVEL | [32,32]   | STROKE_PATH         |
| 9-11  | varies | varies| (none)    | FILL + STROKE (2 draws) |
| 12    | varies | varies| (none)    | FILL_STROKE_PATH (1 draw) |

Each frame has a CPU-side reference image (`vg_lite_expected_draw_*`)
and is compared pixel-by-pixel with tolerance 16.

---

## 10. File Map

| File                          | Role                                            |
|-------------------------------|-------------------------------------------------|
| `inc/vg_lite.h`               | Public API, `vg_lite_stroke_t`, path type enum  |
| `src/vg_lite_stroke.c`        | CPU outline generation (3912 lines)             |
| `src/vg_lite_path.c`          | `vg_lite_set_path_type`, stroke_path dispatch   |
| `src/vg_lite_draw.c`          | GPU fill pipeline reused for stroke rendering   |
| `src/vg_lite_vulkan.c`        | Stencil + cover pipeline (shared with fill)     |
| `tests/stroke/stroke.c`       | 13-frame test matrix                            |
| `tests/util/util.c`           | CPU reference renderer for stroke frames        |

---

## 11. Known Limitations & Notes

1. **`fattened` flag**: `line_width >= 2.5` switches to "fat line" mode,
   which affects swing handling. The threshold is empirical, ported from
   gpu-vglite.
2. **Miter limit**: compared as `ratio² <= miter_square` where
   `miter_square = miter_limit²`. OpenVG default `miter_limit = 4.0`
   gives `miter_square = 16`, ratio cutoff ~4.0.
3. **No GPU-side stroke**: the GPU never sees stroke parameters. All
   geometry is pre-baked on CPU. This matches the official gpu-vglite
   design and is not a limitation unique to this port.
4. **Dash phase continuity**: each sub-path restarts the dash walker
   from the residual state of the previous sub-path (OpenVG semantics).
5. **Arc path utilities** (`vg_lite_init_arc_path` etc., L2798-3912) are
   independent path construction helpers, not used by stroke itself.
