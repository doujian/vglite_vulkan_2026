#version 450

/* OBB-driven blit vertex shader.
 * Reads 4 corner NDC positions via push constants and generates a quad
 * via TRIANGLE_STRIP (4 vertices).
 * UVs computed from frag_pos using the same shader matrix as blit.vert. */

/* Shared push constant layout (96B total, written by vg_lite.c as two
 * vkCmdPushConstants calls on native_pipeline_layout):
 *   offset 0:  mat3 matrix      (48B, col-major) — VERTEX reads
 *   offset 48: uint color       (4B)             — FRAGMENT reads
 *   offset 52: int  image_mode  (4B)             — FRAGMENT reads
 *   offset 56: int  flags       (4B)             — FRAGMENT reads
 *   offset 60: int  pad         (4B, align corners to 16)
 *   offset 64: vec4 corners[2]  (32B)            — VERTEX reads
 * Each stage declares only the members it reads.
 * blend_mode and filter_mode were removed: blend is handled by pipeline
 * blend state, filter by sampler state — neither is a shader uniform. */
layout(push_constant) uniform BlitParams {
    layout(offset = 0)  mat3 matrix;     /* shader matrix: screen[0,1] -> source UV */
    layout(offset = 64) vec4 corners[2]; /* 4 corners as NDC xy pairs: 8 floats = 32B */
} pc;

layout(location = 0) out vec2 src_uv;

void main()
{
    /* corners packed as: corners[0] = (v0.x, v0.y, v1.x, v1.y)
     *                    corners[1] = (v2.x, v2.y, v3.x, v3.y)
     * v0=src(0,0) v1=src(w,0) v2=src(w,h) v3=src(0,h) */
    vec2 v0 = pc.corners[0].xy;
    vec2 v1 = pc.corners[0].zw;
    vec2 v2 = pc.corners[1].xy;
    vec2 v3 = pc.corners[1].zw;

    /* TRIANGLE_STRIP order: v0, v3, v1, v2
     * Generates: (v0,v3,v1) + (v3,v1,v2) — both CCW, covers the quad. */
    vec2 strip[4] = vec2[4](v0, v3, v1, v2);

    vec2 pos = strip[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);

    /* Compute UV the same way as blit.vert: NDC -> screen [0,1] -> source UV */
    vec2 frag_pos = pos * 0.5 + 0.5;
    src_uv = (pc.matrix * vec3(frag_pos, 1.0)).xy;
}
