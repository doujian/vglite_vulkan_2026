#version 450

/* Push constant: matrix at offset 0. This shader is part of a shared 96B
 * block on native_pipeline_layout (VERTEX|FRAGMENT). Fragment fields at
 * offsets 48/52/56 (color/image_mode/flags) are declared in blit_native_fs.frag.
 * Vertex precomputes src_uv so the fragment shader skips the per-fragment
 * 3x3 matrix multiply. */
layout(push_constant) uniform BlitParams {
    layout(offset = 0) mat3 matrix;
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
