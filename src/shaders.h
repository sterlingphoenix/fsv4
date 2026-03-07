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

static const char lit_vert_src[] =
"#version 330 core\n"
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
"flat out vec3 v_color;\n"
"flat out vec3 v_normal_eye;\n"
"out vec3 v_pos_eye;\n"
"\n"
"void main() {\n"
"    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
"    v_pos_eye = (u_modelview * vec4(a_position, 1.0)).xyz;\n"
"    v_normal_eye = normalize(u_normal_matrix * a_normal);\n"
"    v_color = a_color;\n"
"}\n";

static const char lit_frag_src[] =
"#version 330 core\n"
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
"    frag_color = vec4(ambient + diffuse, 1.0);\n"
"}\n";


/*
 * Pick rendering shader.
 *
 * Node ID encoded as colour: R = (id >> 16) & 0xFF, G = (id >> 8) & 0xFF,
 * B = id & 0xFF. Alpha channel carries the face index.
 * No lighting — just pass through the ID colour.
 */

static const char pick_vert_src[] =
"#version 330 core\n"
"\n"
"layout(location = 0) in vec3 a_position;\n"
"layout(location = 1) in vec3 a_normal;\n"
"layout(location = 2) in vec3 a_color;\n"
"layout(location = 3) in uint a_node_id;\n"
"\n"
"uniform mat4 u_mvp;\n"
"\n"
"flat out vec4 v_pick_color;\n"
"\n"
"void main() {\n"
"    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
"    /* Decode node_id: bits 23-0 = ID, bits 31-24 = face */\n"
"    float r = float((a_node_id >> 16u) & 0xFFu) / 255.0;\n"
"    float g = float((a_node_id >>  8u) & 0xFFu) / 255.0;\n"
"    float b = float( a_node_id         & 0xFFu) / 255.0;\n"
"    float a = float((a_node_id >> 24u) & 0xFFu) / 255.0;\n"
"    v_pick_color = vec4(r, g, b, a);\n"
"}\n";

static const char pick_frag_src[] =
"#version 330 core\n"
"\n"
"flat in vec4 v_pick_color;\n"
"\n"
"out vec4 frag_color;\n"
"\n"
"void main() {\n"
"    frag_color = v_pick_color;\n"
"}\n";

#endif /* FSV_SHADERS_H */
