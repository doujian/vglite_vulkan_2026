#version 450

/* AABB-driven blit vertex shader.
 * Reads AABB rectangle (min_xy, max_xy in NDC) via push constants,
 * generates a degenerate triangle that covers the rectangle.
 * With AABB = (-1,-1,3,3) the output is identical to blit.vert. */

layout(push_constant) uniform AABBPC {
    layout(offset = 80) vec4 aabb;  /* xy = min_ndc, zw = max_ndc; bytes 0-79 hold fragment data */
} pc;

layout(location = 0) out vec2 frag_pos;

void main()
{
    vec2 minp = pc.aabb.xy;
    vec2 maxp = pc.aabb.zw;

    /* Degenerate triangle covering the AABB rectangle.
     * Same technique as blit.vert: a single oversized triangle that
     * encloses the full rectangle. Vertices:
     *   v0 = (minx, miny)         bottom-left
     *   v1 = (maxx + w, miny)      extends w past bottom-right
     *   v2 = (minx, maxy + h)      extends h past top-left
     * where w = maxx-minx, h = maxy-miny. */
    float w = maxp.x - minp.x;
    float h = maxp.y - minp.y;

    vec2 positions[3] = vec2[3](
        vec2(minp.x,       minp.y),       /* bottom-left  */
        vec2(maxp.x + w,   minp.y),       /* bottom-right + w  */
        vec2(minp.x,       maxp.y + h)    /* top-left + h      */
    );

    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);
    frag_pos = pos * 0.5 + 0.5;
}
