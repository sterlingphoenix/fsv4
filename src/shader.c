/* shader.c - Shader compilation, linking, and uniform location caching */

#include "shader.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>


static const char *gl_preamble = "#version 330 core\n";
static const char *gles_preamble =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n";

/* Try to compile a shader with the given preamble.
 * Returns the shader ID on success, 0 on failure (silently). */
static GLuint
try_compile(GLenum type, const char *preamble, const char *src)
{
	GLuint shader = glCreateShader(type);
	const char *sources[2] = { preamble, src };
	glShaderSource(shader, 2, sources, NULL);
	glCompileShader(shader);

	GLint ok;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

/* Preamble selected on first successful compile, then reused. */
static const char *active_preamble = NULL;

static GLuint
compile_shader(GLenum type, const char *src)
{
	/* If we already know which preamble works, use it directly. */
	if (active_preamble) {
		GLuint shader = try_compile(type, active_preamble, src);
		if (shader)
			return shader;
	} else {
		/* Try desktop GL first, then GLES. */
		GLuint shader = try_compile(type, gl_preamble, src);
		if (shader) {
			active_preamble = gl_preamble;
			return shader;
		}
		shader = try_compile(type, gles_preamble, src);
		if (shader) {
			active_preamble = gles_preamble;
			return shader;
		}
	}

	/* Both failed — recompile with GL preamble to get the error log. */
	GLuint shader = glCreateShader(type);
	const char *sources[2] = { active_preamble ? active_preamble : gl_preamble, src };
	glShaderSource(shader, 2, sources, NULL);
	glCompileShader(shader);

	char log[1024];
	glGetShaderInfoLog(shader, sizeof(log), NULL, log);
	fprintf(stderr, "Shader compile error (%s):\n%s\n",
	        type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
	glDeleteShader(shader);
	return 0;
}


gboolean
shader_program_create(ShaderProgram *prog, const char *vert_src,
                      const char *frag_src)
{
	memset(prog, 0, sizeof(*prog));

	GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src);
	if (!vs)
		return FALSE;

	GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
	if (!fs) {
		glDeleteShader(vs);
		return FALSE;
	}

	prog->program = glCreateProgram();
	glAttachShader(prog->program, vs);
	glAttachShader(prog->program, fs);
	glLinkProgram(prog->program);

	/* Shaders can be deleted after linking */
	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint ok;
	glGetProgramiv(prog->program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(prog->program, sizeof(log), NULL, log);
		fprintf(stderr, "Shader link error:\n%s\n", log);
		glDeleteProgram(prog->program);
		prog->program = 0;
		return FALSE;
	}

	return TRUE;
}


GLint
shader_program_add_uniform(ShaderProgram *prog, const char *name)
{
	g_assert(prog->num_uniforms < SHADER_MAX_UNIFORMS);

	GLint loc = glGetUniformLocation(prog->program, name);
	if (loc < 0)
		fprintf(stderr, "Warning: uniform '%s' not found in shader\n", name);

	prog->uniforms[prog->num_uniforms].name = name;
	prog->uniforms[prog->num_uniforms].location = loc;
	prog->num_uniforms++;

	return loc;
}


GLint
shader_program_get_uniform(const ShaderProgram *prog, const char *name)
{
	for (int i = 0; i < prog->num_uniforms; i++) {
		if (strcmp(prog->uniforms[i].name, name) == 0)
			return prog->uniforms[i].location;
	}
	return -1;
}


void
shader_program_use(const ShaderProgram *prog)
{
	glUseProgram(prog->program);
}


void
shader_program_unuse(void)
{
	glUseProgram(0);
}


void
shader_program_destroy(ShaderProgram *prog)
{
	if (prog->program) {
		glDeleteProgram(prog->program);
		prog->program = 0;
	}
	prog->num_uniforms = 0;
}
