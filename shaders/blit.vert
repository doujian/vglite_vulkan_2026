#version 450
#extension GL_EXT_scalar_block_layout : enable

/* Shared push constant block (92B, one vkCmdPushConstants call).
 * Uses scalar block layout. Declares full block so compiler auto-layouts
 * offsets. This shader only reads matrix; fragment shader reads
 * color/image_mode/flags. Vertex precomputes src_uv so the fragment
 * shader skips the per-fragment 3x3 matrix multiply. */
layout(push_constant, scalar) uniform BlitParams {
    mat3 matrix;
    uint color;
    int  image_mode;
    int  flags;
    vec4 corners[2];
} pc;

const vec2 positions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

layout(location = 0) out vec2 src_uv;

void main()
{
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);
    vec2 frag_pos = pos * 0.5 + 0.5;
    /* 2D affine: matrix*[frag_pos,1] has z==1, src_uv = xy directly.
     * Linear transform commutes with interpolation: interp(M*p) == M*interp(p). */
    src_uv = (pc.matrix * vec3(frag_pos, 1.0)).xy;
}
