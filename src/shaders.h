/* shaders.h - GLSL shader source strings */

#ifndef FSV_SHADERS_H
#define FSV_SHADERS_H

/*
 * Lit rendering shader.
 *
 * Matches the legacy fixed-function lighting:
 * - Light at eye-space origin (0,0,0), positional (w=1)
 * - GL_COLOR_MATERIAL: vertex color = ambient + diffuse material
 * - Specular material is black (no specular contribution)
 * - GL_FLAT shading: last vertex's normal/color used for whole primitive
 *
 * Lighting equation:
 *   result = light_ambient * vertex_color
 *          + light_diffuse * vertex_color * max(dot(N, L), 0)
 */

/*
 * GPU deployment transform (Phase 46.B).
 *
 * Per-directory 2D affine (+ z scale) applied in the vertex shader:
 * vertices stay static in the VBO while expand/collapse animation
 * only updates a small per-directory transform texture. Lookup:
 * node id (bits 23-0 of the pick attribute) -> u_deploy_index
 * (R32UI, node id -> directory slot) -> u_deploy_xform (RGBA32F,
 * 2 texels per slot: [m00 m01 m10 m11] and [tx ty sz -]).
 * u_deploy_on gates the whole path (0 = identity: other modes, CPU
 * fallback). Textures are 1024 texels wide, row-major.
 */
#define DEPLOY_XFORM_GLSL \
"uniform int u_deploy_on;\n" \
"uniform usampler2D u_deploy_index;\n" \
"uniform sampler2D u_deploy_xform;\n" \
"\n" \
"void deploy_fetch(uint node_id, out vec4 m, out vec4 tr) {\n" \
"    uint id = node_id & 0xFFFFFFu;\n" \
"    uint slot = texelFetch(u_deploy_index,\n" \
"        ivec2(int(id % 1024u), int(id / 1024u)), 0).r;\n" \
"    uint t = slot * 2u;\n" \
"    m  = texelFetch(u_deploy_xform,\n" \
"        ivec2(int(t % 1024u), int(t / 1024u)), 0);\n" \
"    tr = texelFetch(u_deploy_xform,\n" \
"        ivec2(int((t + 1u) % 1024u), int((t + 1u) / 1024u)), 0);\n" \
"}\n" \
"\n" \
"vec3 deploy_position(vec3 p, vec4 m, vec4 tr) {\n" \
"    return vec3(m.x * p.x + m.y * p.y + tr.x,\n" \
"                m.z * p.x + m.w * p.y + tr.y,\n" \
"                p.z * tr.z);\n" \
"}\n"

static const char lit_vert_src[] =
"\n"
"layout(location = 0) in vec3 a_position;\n"
"layout(location = 1) in vec3 a_normal;\n"
"layout(location = 2) in vec3 a_color;\n"
"layout(location = 3) in uint a_node_id;\n"
"\n"
"uniform mat4 u_mvp;\n"
"uniform mat4 u_modelview;\n"
"uniform mat3 u_normal_matrix;\n"
"\n"
DEPLOY_XFORM_GLSL
"\n"
"flat out vec3 v_color;\n"
"flat out vec3 v_normal_eye;\n"
"out vec3 v_pos_eye;\n"
"\n"
"void main() {\n"
"    vec3 pos = a_position;\n"
"    vec3 nrm = a_normal;\n"
"    if (u_deploy_on != 0) {\n"
"        vec4 m, tr;\n"
"        deploy_fetch(a_node_id, m, tr);\n"
"        pos = deploy_position(pos, m, tr);\n"
"        /* rotate the normal's xy by the same 2x2 (uniform scale\n"
"         * washes out in the normalize) */\n"
"        nrm = vec3(m.x * a_normal.x + m.y * a_normal.y,\n"
"                   m.z * a_normal.x + m.w * a_normal.y,\n"
"                   a_normal.z);\n"
"    }\n"
"    gl_Position = u_mvp * vec4(pos, 1.0);\n"
"    v_pos_eye = (u_modelview * vec4(pos, 1.0)).xyz;\n"
"    v_normal_eye = normalize(u_normal_matrix * nrm);\n"
"    v_color = a_color;\n"
"}\n";

static const char lit_frag_src[] =
"\n"
"flat in vec3 v_color;\n"
"flat in vec3 v_normal_eye;\n"
"in vec3 v_pos_eye;\n"
"\n"
"/* Light parameters matching legacy setup */\n"
"const vec3 light_ambient  = vec3(0.2, 0.2, 0.2);\n"
"const vec3 light_diffuse  = vec3(0.5, 0.5, 0.5);\n"
"const vec3 light_pos_eye  = vec3(0.0, 0.0, 0.0);\n"
"\n"
"/* 1.0 = full lighting, 0.0 = ambient only (for outline pass) */\n"
"uniform float u_diffuse_scale;\n"
"\n"
"/* Distance brightening (inverse fog): fragments blend from the\n"
" * lit result toward emissive base color as eye distance grows, so\n"
" * distant geometry reads clearly instead of going dark and muted\n"
" * (max lit brightness is only 0.7 * color, and the diffuse term\n"
" * collapses at grazing angles). Disabled when far <= near.\n"
" * The outline pass (u_diffuse_scale = 0) glows toward a dimmer\n"
" * target so edges keep contrast against their own faces */\n"
"uniform float u_glow_near;\n"
"uniform float u_glow_far;\n"
"\n"
"out vec4 frag_color;\n"
"\n"
"void main() {\n"
"    vec3 N = normalize(v_normal_eye);\n"
"    vec3 L = normalize(light_pos_eye - v_pos_eye);\n"
"    float NdotL = max(dot(N, L), 0.0);\n"
"\n"
"    vec3 ambient = light_ambient * v_color;\n"
"    vec3 diffuse = light_diffuse * v_color * NdotL * u_diffuse_scale;\n"
"\n"
"    float glow = 0.0;\n"
"    if (u_glow_far > u_glow_near)\n"
"        glow = clamp((length(v_pos_eye) - u_glow_near)\n"
"                     / (u_glow_far - u_glow_near), 0.0, 1.0);\n"
"    vec3 glow_target = v_color * (0.6 + 0.4 * u_diffuse_scale);\n"
"\n"
"    frag_color = vec4(mix(ambient + diffuse, glow_target, glow), 1.0);\n"
"}\n";


/*
 * Pick rendering shader.
 *
 * Node ID encoded as colour: R = (id >> 16) & 0xFF, G = (id >> 8) & 0xFF,
 * B = id & 0xFF. Alpha channel carries the face index.
 * No lighting — just pass through the ID colour.
 */

static const char pick_vert_src[] =
"\n"
"layout(location = 0) in vec3 a_position;\n"
"layout(location = 1) in vec3 a_normal;\n"
"layout(location = 2) in vec3 a_color;\n"
"layout(location = 3) in uint a_node_id;\n"
"\n"
"uniform mat4 u_mvp;\n"
"\n"
DEPLOY_XFORM_GLSL
"\n"
"flat out vec4 v_pick_color;\n"
"\n"
"void main() {\n"
"    vec3 pos = a_position;\n"
"    if (u_deploy_on != 0) {\n"
"        vec4 m, tr;\n"
"        deploy_fetch(a_node_id, m, tr);\n"
"        pos = deploy_position(pos, m, tr);\n"
"    }\n"
"    gl_Position = u_mvp * vec4(pos, 1.0);\n"
"    /* Decode node_id: bits 23-0 = ID, bits 31-24 = face */\n"
"    float r = float((a_node_id >> 16u) & 0xFFu) / 255.0;\n"
"    float g = float((a_node_id >>  8u) & 0xFFu) / 255.0;\n"
"    float b = float( a_node_id         & 0xFFu) / 255.0;\n"
"    float a = float((a_node_id >> 24u) & 0xFFu) / 255.0;\n"
"    v_pick_color = vec4(r, g, b, a);\n"
"}\n";

static const char pick_frag_src[] =
"\n"
"flat in vec4 v_pick_color;\n"
"\n"
"out vec4 frag_color;\n"
"\n"
"void main() {\n"
"    frag_color = v_pick_color;\n"
"}\n";

/*
 * Text rendering shader.
 *
 * Renders textured quads for bitmap font text. The font texture
 * stores glyph alpha in the red channel. Fragment discards below
 * a threshold (replacing legacy GL_ALPHA_TEST).
 */

static const char text_vert_src[] =
"\n"
"layout(location = 0) in vec3 a_position;\n"
"layout(location = 1) in vec2 a_texcoord;\n"
"layout(location = 2) in vec3 a_color;\n"
"\n"
"uniform mat4 u_mvp;\n"
"\n"
"out vec2 v_texcoord;\n"
"flat out vec3 v_color;\n"
"\n"
"void main() {\n"
"    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
"    v_texcoord = a_texcoord;\n"
"    v_color = a_color;\n"
"}\n";

static const char text_frag_src[] =
"\n"
"in vec2 v_texcoord;\n"
"flat in vec3 v_color;\n"
"\n"
"uniform sampler2D u_texture;\n"
"\n"
"out vec4 frag_color;\n"
"\n"
"void main() {\n"
"    float alpha = texture(u_texture, v_texcoord).r;\n"
"    if (alpha < 0.0625) discard;\n"
"    frag_color = vec4(v_color, alpha);\n"
"}\n";


/*
 * Flat-color shader.
 *
 * For cursors, highlights, and other unlit colored geometry.
 * Color is passed as a uniform.
 */

static const char flat_vert_src[] =
"\n"
"layout(location = 0) in vec3 a_position;\n"
"\n"
"uniform mat4 u_mvp;\n"
"\n"
"void main() {\n"
"    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
"}\n";

static const char flat_frag_src[] =
"\n"
"uniform vec4 u_color;\n"
"\n"
"out vec4 frag_color;\n"
"\n"
"void main() {\n"
"    frag_color = u_color;\n"
"}\n";

#endif /* FSV_SHADERS_H */
