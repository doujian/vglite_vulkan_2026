# Radial Gradient — Design & Implementation

This document describes how the Vulkan VGLite implementation renders OpenVG
radial gradients (`vg_lite_draw_radial_grad`). The design mirrors the official
`gpu-vglite` hardware architecture: CPU bakes a 1D ramp LUT, GPU evaluates the
radial gradient formula per pixel and applies spread in the normalized `[0,1]`
domain.

> **Migration note**: This pipeline was rebuilt in FIXES.md #26 to replace the
> earlier CPU-baked 2D texture + UV-domain spread design (#25), which produced
> visually wrong REFLECT/REPEAT output. See §10 for the design history.

---

## 1. Architecture Overview

Radial gradient uses a **dedicated Vulkan pipeline** (independent from
`vg_lite_draw_pattern`) with two CPU/GPU division points:

```
Stage 1 (CPU, vg_lite_gradient.c)          Stage 2 (CPU, vg_lite_draw.c)
┌──────────────────────────────┐           ┌────────────────────────────┐
│ vg_lite_set_radial_grad      │           │ vg_lite_draw_radial_grad   │
│   (ramp stops, cx/cy/r/fx/fy,│           │   ├─ inverse(grad->matrix) │
│    spread_mode, pre_mult)    │           │   ├─ focal-in-circle clamp │
│ vg_lite_update_radial_grad   │           │   ├─ compute 9 coefficients│
│   ├─ sort_color_ramp         │           │   │   (gLin × 3, gRad × 6) │
│   ├─ build converted_ramp    │  1D LUT   │   ├─ spread_mode enum map │
│   │    (force stop=0/0/1)    │  (width N)│   └─ draw_radial_internal │
│   └─ bake 1D LUT             │ ────────► │       (computes & uploads │
│        (linear interp,       │           │        push constant,     │
│         pre-mult alpha)      │           │        binds LUT texture) │
└──────────────────────────────┘           └─────────────┬──────────────┘
                                                         │
                           Stage 3 (GPU, radial.vert + radial.frag)
                           ┌─────────────────────────────┴──────────────┐
                           │ Stencil pass (path tessellation INVERT)     │
                           │ Cover pass (bbox quad, stencil NOT_EQUAL):  │
                           │   ├─ g = gLin + sqrt(gRad)                  │
                           │   ├─ spread in [0,1] domain                 │
                           │   └─ texture(radial_lut, vec2(u, 0.5))      │
                           └─────────────────────────────────────────────┘
```

**Key principle**: The CPU stage produces a **1D ramp LUT with no spread
baked in**. The GPU computes the radial gradient parameter `g` per pixel
(the full focal-point-aware formula) and applies spread in the normalized
`[0,1]` domain. This matches the official gpu-vglite hardware semantics
(`rad_tile` register).

---

## 2. Public API

Declared in `inc/vg_lite.h`:

```c
/* Configure the radial gradient. */
vg_lite_error_t vg_lite_set_radial_grad(
    vg_lite_radial_gradient_t           *grad,
    uint32_t                             count,         /* ramp stop count    */
    vg_lite_color_ramp_t                *color_ramp,    /* stop array         */
    vg_lite_radial_gradient_parameter_t  grad_param,    /* cx,cy,r,fx,fy      */
    vg_lite_gradient_spreadmode_t        spread_mode,   /* FILL/PAD/REPEAT/...*/
    uint8_t                              pre_multiplied);/* 1: color *= alpha */

/* Bake the 1D ramp LUT. Must be called after set_radial_grad. */
vg_lite_error_t vg_lite_update_radial_grad(vg_lite_radial_gradient_t *grad);

/* Render. path is the clip mask; grad supplies LUT + geometry params. */
vg_lite_error_t vg_lite_draw_radial_grad(
    vg_lite_buffer_t             *target,
    vg_lite_path_t               *path,
    vg_lite_fill_t                fill_rule,       /* EVEN_ODD / NON_ZERO */
    vg_lite_matrix_t             *matrix,          /* path transform      */
    vg_lite_radial_gradient_t    *grad,
    vg_lite_color_t               paint_color,     /* reserved (unused)  */
    vg_lite_blend_t               blend,
    vg_lite_filter_t              filter);         /* reserved (unused)  */
```

### Geometry parameters

```c
typedef struct {
    vg_lite_float_t cx, cy;   /* center */
    vg_lite_float_t r;        /* radius */
    vg_lite_float_t fx, fy;   /* focal point */
} vg_lite_radial_gradient_parameter_t;
```

When `fx == cx && fy == cy` (centered focal), the gradient is a pure concentric
radial fill: `g == dist_from_focal / r`. When the focal point is offset, the
gradient locus becomes a circle family defined by the focal equation (see §5).

### Spread modes

| Enum                                       | Value   | Shader mode | Behavior                          |
|--------------------------------------------|---------|-------------|-----------------------------------|
| `VG_LITE_GRADIENT_SPREAD_FILL`             | `0`     | 0           | Treat as PAD (matches ThorVG)     |
| `VG_LITE_GRADIENT_SPREAD_PAD`              | `0x1C00`| 1           | Clamp `g` to `[0,1]`              |
| `VG_LITE_GRADIENT_SPREAD_REPEAT`           | `0x1C01`| 2           | `fract(g)`                        |
| `VG_LITE_GRADIENT_SPREAD_REFLECT`          | `0x1C02`| 3           | `m=mod(g,2); mix(m, 2-m, step(1,m))` |

> **FILL = PAD**: `vg_lite_tvg.cpp` `fill_spread_conv` maps
> `VG_LITE_GRADIENT_SPREAD_FILL → FillSpread::Pad`. We match this in shader and
> CPU reference (FIXES.md #26).

### OpenVG compatibility

The gradient parameter semantics and spread modes match OpenVG §9.1.2 (Radial
Gradients) and §9.1.1 (Color Ramp). One intentional deviation: OpenVG leaves
FILL behavior implementation-defined; we follow ThorVG's interpretation.

---

## 3. Data Structures

### `vg_lite_radial_gradient_t` (`inc/vg_lite.h` L997-1012)

```c
typedef struct {
    vg_lite_matrix_t                     matrix;          /* grad->screen xform   */
    vg_lite_buffer_t                     image;           /* 1D ramp LUT texture  */
    vg_lite_radial_gradient_parameter_t  radial_grad;     /* cx,cy,r,fx,fy        */
    uint32_t                             ramp_length;     /* user stop count      */
    vg_lite_color_ramp_t                 color_ramp[VLC_MAX_COLOR_RAMP_STOPS];
    uint32_t                             converted_length;/* +endpoint stops     */
    vg_lite_color_ramp_t                 converted_ramp[VLC_MAX_COLOR_RAMP_STOPS+2];
    uint8_t                              pre_multiplied;
    vg_lite_gradient_spreadmode_t        spread_mode;
} vg_lite_radial_gradient_t;
```

The `matrix` is the **inverse gradient matrix**: it maps screen pixels back
into gradient-local space before the focal equation is applied.

### `vg_lite_color_ramp_t`

```c
typedef struct {
    vg_lite_float_t stop;        /* [0, 1]                       */
    vg_lite_float_t red, green, blue, alpha;  /* [0, 1]          */
} vg_lite_color_ramp_t;
```

---

## 4. CPU Stage 1 — 1D Ramp LUT Baking

File: `src/vg_lite_gradient.c`, `vg_lite_update_radial_grad` (L234).

### 4.1 Sort

`sort_color_ramp` (L196-209) sorts user stops ascending by `stop`. The shader
binary search assumes monotone stops.

### 4.2 Build `converted_ramp` (L241-261)

The user's ramp may not include stops at exactly `0.0` and `1.0`. To make the
LUT well-defined across the full `[0,1]` range, duplicate the first/last stop
at the endpoints:

```
if user_ramp[0].stop > 0:
    prepend copy of user_ramp[0] with stop = 0.0
append all user_ramp[i] as-is
if user_ramp[last].stop < 1:
    append copy of user_ramp[last] with stop = 1.0
```

This mirrors gpu-vglite `vg_lite.c` L6442-6485. The CTS ramp
`[(0.0,...), (0.2,...), (0.4,...), (0.55,...), (0.95,...)]` becomes 6 entries,
with the last segment `[0.95, 1.0]` having constant color (both endpoints
equal to ramp[4]).

### 4.3 LUT allocation (L263-282)

```
width  = converted_length × 128        /* CTS: 6 × 128 = 768 */
height = 1
format = VG_LITE_BGRA8888
```

The 128 multiplier matches gpu-vglite and gives smooth interpolation between
adjacent stops (sub-stop resolution).

### 4.4 Pixel fill (L288-329)

For each `x ∈ [0, width)`:

```
g = x / (width - 1)                    /* normalized [0, 1]   */
find segment [converted[i], converted[i+1]] containing g
frac = (g - converted[i].stop) / (converted[i+1].stop - converted[i].stop)
(r,g,b,a) = lerp(converted[i], converted[i+1], frac)

if pre_multiplied:
    r *= a; g *= a; b *= a

pixels[x] = pack_pixel(a, r, g, b)     /* BGRA8888 byte order */
```

**No spread is applied here.** The LUT contains only the ramp color profile.
Spread happens entirely on the GPU.

The `converted_ramp` array is saved back to `grad` so the CPU reference
renderer in `util.c` can sample identically without rebuilding it.

---

## 5. CPU Stage 2 — Nine Coefficient Computation

File: `src/vg_lite_draw.c`, `vg_lite_draw_radial_grad` (L1433).

### 5.1 The radial gradient formula

Following gpu-vglite `vg_lite_path.c` L4759-4991, the normalized gradient
position `g` at screen pixel `(x, y)` is:

```
dx = F(x) - focalX
dy = F(y) - focalY
fx = focalX - centerX
fy = focalY - centerY

g = (dx·fx + dy·fy  +  sqrt(r²·(dx² + dy²) - (dx·fy - dy·fx)²))
      / (r² - fx² - fy²)
  = gLin + sqrt(gRad)
```

Where `F` is the inverse gradient matrix applied to the pixel coordinate
(with a `+0.5` pixel-center offset). When the focal point equals the center
(`fx = fy = 0`), this simplifies to `g = sqrt((dx² + dy²) / r²) = dist / r`.

### 5.2 Focal-in-circle clamp (L478-1486)

vg11 spec pg125: if the focal point lies outside the radius (`fx² + fy² > r²`),
slide it inward by the `0.9` rule:

```
scale = 0.9 × r / sqrt(fx² + fy²)
ofx *= scale; ofy *= scale
```

This guarantees `r² - fx² - fy² > 0` (no division by zero).

### 5.3 Precomputed polynomial form (L488-1513)

To let the GPU evaluate `g` per pixel with 9 multiply-adds + 1 sqrt, expand
`gLin` and `gRad` as polynomials in `(x, y)`:

```
Let m[i][j] = inverse(grad->matrix) entries
Let cx = 0.5*(m00+m01) + m02 - focalX     (gradient-local coord of pixel (0,0))
Let cy = 0.5*(m10+m11) + m12 - focalY

r2_fx2_fy2   = r² - ofx² - ofy²           (clamp to ±1e-12 if zero)
r2_fx2_fy2sq = (r2_fx2_fy2)²
r2_fx2 = r² - ofx²,    r2_fy2 = r² - ofy²
r2_fx2_2 = 2·r2_fx2,   r2_fy2_2 = 2·r2_fy2,   fxfy_2 = 2·ofx·ofy

gLin = x·StepXLin + y·StepYLin + ConstantLin
gRad = x²·StepXXRad + y²·StepYYRad + x·y·StepXYRad
     + x·StepXRad  + y·StepYRad  + ConstantRad

StepXLin    = (m00·ofx + m10·ofy) / r2_fx2_fy2
StepYLin    = (m01·ofx + m11·ofy) / r2_fx2_fy2
ConstantLin = (cx ·ofx + cy ·ofy) / r2_fx2_fy2

StepXXRad    = (m00²·r2_fy2 + m10²·r2_fx2 + m00·m10·fxfy_2) / r2_fx2_fy2sq
StepYYRad    = (m01²·r2_fy2 + m11²·r2_fx2 + m01·m11·fxfy_2) / r2_fx2_fy2sq
StepXYRad    = (m00·m01·r2_fy2_2 + m10·m11·r2_fx2_2
                + (m00·m11 + m01·m10)·fxfy_2)            / r2_fx2_fy2sq
StepXRad     = (m00·cx ·r2_fy2_2 + m10·cy ·r2_fx2_2
                + (m00·cy + m10·cx)·fxfy_2)              / r2_fx2_fy2sq
StepYRad     = (m01·cx ·r2_fy2_2 + m11·cy ·r2_fx2_2
                + (m01·cy + m11·cx)·fxfy_2)              / r2_fx2_fy2sq
ConstantRad  = (cx²·r2_fy2 + cy²·r2_fx2 + cx·cy·fxfy_2) / r2_fx2_fy2sq
```

### 5.4 Packing into push constant (L519-1522)

The 9 coefficients pack column-major into a `mat3` for std140 layout:

```
col0 = (StepXLin,    StepYLin,    ConstantLin)    = radial_coef[0..2]
col1 = (StepXXRad,   StepYYRad,   StepXYRad)      = radial_coef[3..5]
col2 = (StepXRad,    StepYRad,    ConstantRad)    = radial_coef[6..8]
```

### 5.5 Spread mode mapping (L1529-1536)

```
VG_LITE_GRADIENT_SPREAD_FILL    -> shader 0  (treated as PAD)
VG_LITE_GRADIENT_SPREAD_PAD     -> shader 1
VG_LITE_GRADIENT_SPREAD_REPEAT  -> shader 2
VG_LITE_GRADIENT_SPREAD_REFLECT -> shader 3
```

### 5.6 Dispatch

```c
return draw_radial_internal(target, path, fill_rule, matrix,
                            &grad->image, radial_coef, shader_spread, blend);
```

---

## 6. GPU Stage 3 — Vulkan Pipeline

Files: `src/vg_lite_vulkan.c` (pipeline creation) + `shaders/radial.vert` +
`shaders/radial.frag`.

The radial pipeline is **fully independent** from `vg_lite_draw_pattern`:
own pipeline layout, descriptor set layout, stencil pipeline, cover VBO/IBO,
and per-format × blend-group cover pipeline cache.

### 6.1 Push constant layout (124 bytes, `radial.vert` L18-28)

```c
layout(push_constant) uniform RadialPushConstants {
    mat3 path_matrix;        /* 48B — transforms path verts to NDC          */
    mat3 radial_coef;        /* 48B — nine gLin/gRad coeffs (col-major)    */
    int  spread_mode;        /* 4B  — 0=FILL, 1=PAD, 2=REPEAT, 3=REFLECT    */
    uint paint_color;        /* 4B  — reserved                             */
    int  target_width;       /* 4B                                          */
    int  target_height;      /* 4B                                          */
    int  lut_width;          /* 4B                                          */
    int  lut_height;         /* 4B  (== 1)                                  */
    int  blend_mode;         /* 4B                                          */
} pc;                        /* total 124B (fits Vulkan 128B minimum)       */
```

Stage flags: `VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT`.

### 6.2 Descriptor set

```
set 0, binding 0: uniform sampler2D radial_lut   (FRAGMENT)
                  — the 1D ramp LUT (width × 1)
```

The sampler is LINEAR filtering for smooth inter-texel interpolation.

### 6.3 Stencil + Cover dual-pass (`draw_radial_internal`, vg_lite_draw.c L962)

Both passes share the same pipeline layout and push constants.

**Stencil pass** (L1116-1121):
- Pipeline: `radial_stencil_pipeline` (color writes off, stencil op = INVERT)
- Vertex input: tessellated path triangles (VBO + IBO)
- `path_matrix = path_screen_to_ndc × path_matrix` (path → NDC)
- Marks path interior in stencil buffer

**Cover pass** (L1123-1172):
- Pipeline: `vg_lite_vulkan_get_radial_cover_pipeline(format, blend_group)`
  (lazy-created, cached per format × blend group; stencil compare = NOT_EQUAL)
- Vertex input: path bounding-box quad (4 NDC verts, IBO `[0,1,2,0,2,3]`)
- `path_matrix = identity` (verts already in NDC)
- Fragment shader evaluates radial gradient only on pixels inside the path

### 6.4 Vertex shader (`radial.vert`)

```glsl
vec3 transformed = pc.path_matrix * vec3(in_pos, 1.0);
gl_Position = vec4(transformed.xy, 0, 1);

/* Y-flip: undo NDC y-up so screen_pos_pixel uses top-left +y-down */
vec2 spn = vec2(transformed.x + 1, 1 - transformed.y) * 0.5;
screen_pos_pixel = spn * vec2(pc.target_width, pc.target_height);
```

Note: `radial.frag` actually uses `gl_FragCoord.xy` (see below), not the
varying. The varying is kept for documentation/debugging.

### 6.5 Fragment shader (`radial.frag`)

```glsl
vec2 px = gl_FragCoord.xy;     /* pixel center (x+0.5, y+0.5), y-down */

/* Unpack 9 coeffs from mat3 (column-major: [col][row]) */
float stepXLin    = pc.radial_coef[0][0];
float stepYLin    = pc.radial_coef[0][1];
float constantLin = pc.radial_coef[0][2];
float stepXXRad   = pc.radial_coef[1][0];
float stepYYRad   = pc.radial_coef[1][1];
float stepXYRad   = pc.radial_coef[1][2];
float stepXRad    = pc.radial_coef[2][0];
float stepYRad    = pc.radial_coef[2][1];
float constantRad = pc.radial_coef[2][2];

/* g = gLin + sqrt(gRad) */
float gLin = px.x*stepXLin + px.y*stepYLin + constantLin;
float gRad = px.x²*stepXXRad + px.y²*stepYYRad + px.x*px.y*stepXYRad
           + px.x*stepXRad  + px.y*stepYRad  + constantRad;
float g = (gRad < 0) ? gLin : gLin + sqrt(gRad);

/* Spread in [0,1] domain (g == 1.0 == radius boundary) */
float u;
if (spread == PAD || spread == FILL)   u = clamp(g, 0, 1);
else if (spread == REPEAT)             u = fract(g);
else if (spread == REFLECT) { float m = mod(g, 2); u = mix(m, 2-m, step(1, m)); }

out_color = texture(radial_lut, vec2(u, 0.5));
```

#### Why `gl_FragCoord` instead of the varying

`path_screen_to_ndc` flips y (screen `y=0`/top maps to NDC `y=+1`). The
vertex shader varying needs an explicit un-flip; getting that wrong silently
shifts the focal point to `(fx, height - fy)` and breaks the gradient.

`gl_FragCoord.xy` in Vulkan is guaranteed `(x+0.5, y+0.5)` in framebuffer
space, which has the same top-left `+y-down` convention as the CPU reference
(`util.c` uses `sx=x+0.5, sy=y+0.5` directly). Using it avoids the entire
y-axis ambiguity class of bugs.

#### gRad clamping

When a pixel sits exactly on a locus where `gRad == 0` analytically,
floating-point error can push it slightly negative. Clamp to zero and fall
back to `gLin` (which equals the correct `g` when `gRad == 0`):

```glsl
if (gRad < 0.0f) g = gLin;
else             g = gLin + sqrt(gRad);
```

---

## 7. CPU Reference Renderer

File: `util/util.c`, `vg_lite_expected_draw_radial_grad` (L1010+).

The test harness (`tests/radialGrad/radialGrad.c`) calls this function to
generate a CPU-side reference image, then compares it pixel-by-pixel with the
GPU output (tolerance 16).

The reference renderer mirrors the GPU shader exactly:

1. Compute the same 9 coefficients from `grad->matrix`, `r`, `fx`, `fy`,
   `centerX`, `centerY` (including focal-in-circle clamp).
2. For each pixel `(sx, sy)` in path bounding box:
   - If outside path (point-in-polygon test): skip.
   - `g = gLin + sqrt(max(gRad, 0))` with `px = (sx+0.5, sy+0.5)`.
   - Apply spread in `[0,1]` domain (FILL = PAD).
   - Sample `converted_ramp` (not the LUT, to avoid double-interpolation):
     `tex_x = u × (converted_length - 1)`, linear interp.
3. Blend with target using the requested blend mode.

Both paths produce bit-identical output (modulo float precision), so the
self-check catches any GPU/CPU divergence.

---

## 8. Test Coverage

`tests/radialGrad/radialGrad.c` (204 lines) renders 4 frames on a 480×640
ARGB8888 canvas with a 240×240 path (120×120 scaled ×2):

| Frame | spread_mode | LUT width | Expected behavior                       |
|-------|-------------|-----------|-----------------------------------------|
| 0     | FILL        | 768       | Clamp at g=1.0                          |
| 1     | PAD         | 768       | Clamp at g=1.0                          |
| 2     | REPEAT      | 768       | Cycle ramp outside circle               |
| 3     | REFLECT     | 768       | Mirror ramp at g=1.0                    |

CTS ramp (5 user stops → 6 converted):
```
0.00 → (0.95, 0.20, 0.00, 0.90)   dark red
0.20 → (0.85, 0.30, 0.20, 1.00)   red-orange
0.40 → (0.75, 0.10, 0.30, 1.00)   red-pink
0.55 → (0.90, 0.20, 0.20, 1.00)   red
0.95 → (0.90, 0.90, 0.90, 1.00)   light gray
1.00 → (0.90, 0.90, 0.90, 1.00)   light gray (auto-appended)
```

Geometry: `cx=cy=fx=fy=120, r=115`. Focal at center → concentric radial.
Path corner distance from focal = `sqrt(120² + 120²) ≈ 170`, so
`g_max ≈ 170/115 ≈ 1.48` (REFLECT/REPEAT triggered at corners).

### Current results (post-fix)

| Mode    | CPU vs GPU mismatches | vs ThorVG ref mismatch% |
|---------|-----------------------|-------------------------|
| FILL    | 0                     | 0.85%                   |
| PAD     | 0                     | 0.85%                   |
| REPEAT  | 4 (boundary precision)| 12.26% (pre-mult alpha) |
| REFLECT | 0                     | 0.85%                   |

REPEAT vs ThorVG residual diff is a pre-multiplied alpha mismatch (Vulkan LUT
stores `alpha=0.9×255=230`, ThorTV displays `alpha=255`); RGB spread behavior
matches.

---

## 9. File Map

| File                              | Role                                                |
|-----------------------------------|-----------------------------------------------------|
| `inc/vg_lite.h`                   | Public API, `vg_lite_radial_gradient_t`, enums      |
| `src/vg_lite_gradient.c`          | 1D ramp LUT baking (L234-339)                       |
| `src/vg_lite_draw.c`              | 9-coefficient computation (L1433) + `draw_radial_internal` (L962) |
| `src/vg_lite_vulkan.c`            | Pipeline creation (`init_radial_pipeline`, `get_radial_cover_pipeline`) |
| `src/vg_lite_vulkan.h`            | Pipeline fields in `g_vk_ctx`, function decls       |
| `shaders/radial.vert`             | Path → NDC + screen_pos_pixel varying               |
| `shaders/radial.frag`             | `g = gLin + sqrt(gRad)` + `[0,1]` spread + LUT sample|
| `util/util.c`                     | CPU reference renderer for self-check               |
| `util/util.h`                     | `vg_lite_expected_draw_radial_grad` signature       |
| `tests/radialGrad/radialGrad.c`   | 4-frame test                                        |
| `docs/radialGrad_thorvg_*.png`    | ThorVG reference images for cross-validation        |

---

## 10. Design History & Rationale

### Why a dedicated pipeline (not `vg_lite_draw_pattern`)

Earlier versions (FIXES #25 and prior) forwarded radial gradient rendering
through `vg_lite_draw_pattern`, reusing the pattern pipeline. This caused
three compounding errors (FIXES #26):

1. **Double spread** — CPU baked spread into a 2D texture, then GPU shader
   applied spread again in the UV domain.
2. **Wrong spread domain** — Mirrored around `ramp[last].stop = 0.95` instead
   of the normalized `g = 1.0` (radius boundary).
3. **CPU-baked 2D texture** — Precomputed `dist/r` for a 2D grid, which fails
   for non-centered focal points (`g ≠ dist/r` there).

A dedicated pipeline was the cleanest fix: it lets the GPU compute `g` per
pixel via the full focal formula and apply spread in the correct `[0,1]`
domain, exactly matching the gpu-vglite `rad_tile` hardware semantics.

### Why not use the orphan `gradient.frag` pipeline

`gradient.vert/frag` + `draw_grad_internal` exist in the codebase but are
**never called** (verified by grep). They were written as a stub but never
wired into the dispatch. The radial pipeline was built fresh instead of
retrofitting them, because:

- Their push constant layout has no room for the 9 radial coefficients.
- Their shader applies spread in the 2D UV domain (same bug as pattern.frag).
- Retrofitting would touch linear gradient code paths, expanding blast radius.

### Performance notes

- The 9 coefficients are computed once per draw call (not per pixel).
- Per-pixel GPU cost: 9 multiply-adds + 1 sqrt + 1 texture sample ≈ same as
  pattern fill.
- The 1D LUT is tiny (CTS: 768 × 1 BGRA8888 = 3 KB) and stays in L1 cache.

---

## 11. Known Limitations & Future Work

1. **FILL = PAD**: We follow ThorVG's interpretation. If a strict OpenVG
   reading (FILL = transparent outside) is ever needed, change the shader's
   FILL branch to `discard` or write `vec4(0)`.
2. **REPEAT pre-mult alpha**: The Vulkan LUT stores pre-multiplied colors.
   Some downstream consumers (e.g. ThorVG comparison) display non-pre-mult,
   causing a residual alpha diff. Investigation pending.
3. **No support for conic gradients**: Out of scope; OpenVG VGLite subset
   does not include them.
4. **`paint_color` reserved**: The radial shader does not use `paint_color`.
   A future solid-color modulator could be added without changing the layout.
5. **No pipeline-level MSAA toggle**: The cover pipeline uses the same
   MSAA configuration as the render target. Explicit radial-specific MSAA
   control would require extending the cover pipeline cache key.
