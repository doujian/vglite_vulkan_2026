#version 450

/* OBB-driven blit vertex shader.
 * Reads 4 corner NDC positions via push constants and generates 2 triangles
 * (TRIANGLE_LIST, 6 vertices) covering the source image footprint in target space.
 * UVs computed from frag_pos using the same shader matrix as blit.vert. */

layout(push_constant) uniform BlitParams {
    layout(offset = 0)  mat3 matrix;     /* shader matrix: screen[0,1] -> source UV */
    layout(offset = 80) vec4 corners[2]; /* 4 corners as NDC xy pairs: 8 floats = 32B */
} pc;

layout(location = 0) out vec2 src_uv;

void main()
{
    /* corners packed as: corners[0] = (v0.x, v0.y, v1.x, v1.y)
     *                    corners[1] = (v2.x, v2.y, v3.x, v3.y) */
    vec2 positions[4] = vec2[4](
        pc.corners[0].xy,
        pc.corners[0].zw,
        pc.corners[1].xy,
        pc.corners[1].zw
    );

    /* Two triangles as TRIANGLE_LIST (6 vertices):
     *   T1: v0, v1, v2
     *   T2: v0, v2, v3  */
    int tri_idx[6] = int[6](0, 1, 2, 0, 2, 3);
    int idx = tri_idx[gl_VertexIndex];

    vec2 pos = positions[idx];
    gl_Position = vec4(pos, 0.0, 1.0);

    /* Compute UV the same way as blit.vert: NDC -> screen [0,1] -> source UV */
    vec2 frag_pos = pos * 0.5 + 0.5;
    src_uv = (pc.matrix * vec3(frag_pos, 1.0)).xy;
}
