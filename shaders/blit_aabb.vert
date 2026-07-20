#version 450

/* AABB-driven blit vertex shader.
 * Reads AABB rectangle (min_xy, max_xy in NDC) via push constants,
 * generates a triangle strip covering exactly the rectangle (zero waste).
 * Uses 4 vertices = 2 triangles, rasterizing only the AABB area. */

layout(push_constant) uniform AABBPC {
    layout(offset = 80) vec4 aabb;  /* xy = min_ndc, zw = max_ndc; bytes 0-79 hold fragment data */
} pc;

layout(location = 0) out vec2 frag_pos;

void main()
{
    vec2 minp = pc.aabb.xy;
    vec2 maxp = pc.aabb.zw;

    /* Triangle strip forming the AABB rectangle (4 vertices, 2 triangles).
     * Strip order: v0→v1→v2→v3 produces △(v0,v1,v2) + △(v2,v1,v3).
     *   v0 = bottom-left   v1 = bottom-right
     *   v2 = top-left      v3 = top-right
     * Total rasterized area = rectangle area (no waste vs degenerate triangle's 2×). */
    vec2 positions[4] = vec2[4](
        vec2(minp.x, minp.y),   /* v0: bottom-left  */
        vec2(maxp.x, minp.y),   /* v1: bottom-right */
        vec2(minp.x, maxp.y),   /* v2: top-left     */
        vec2(maxp.x, maxp.y)    /* v3: top-right    */
    );

    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);
    frag_pos = pos * 0.5 + 0.5;
}
