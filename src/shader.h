/* shader.h - Shader compilation, linking, and uniform location caching */

#ifndef FSV_SHADER_H
#define FSV_SHADER_H

#include <glib.h>
#include <epoxy/gl.h>

/* Maximum number of cached uniform locations per program */
#define SHADER_MAX_UNIFORMS 16

/* A compiled and linked shader program with cached uniform locations */
typedef struct {
	GLuint program;
	struct {
		const char *name;
		GLint location;
	} uniforms[SHADER_MAX_UNIFORMS];
	int num_uniforms;
} ShaderProgram;

/* Compile and link a shader program from vertex and fragment source strings.
 * Returns TRUE on success, FALSE on error (errors printed to stderr). */
gboolean shader_program_create(ShaderProgram *prog,
                               const char *vert_src,
                               const char *frag_src);

/* Cache a uniform location by name. Call after shader_program_create.
 * Returns the uniform location, or -1 if not found. */
GLint shader_program_add_uniform(ShaderProgram *prog, const char *name);

/* Look up a previously cached uniform location by name.
 * Returns -1 if not found in the cache. */
GLint shader_program_get_uniform(const ShaderProgram *prog, const char *name);

/* Bind the shader program for use */
void shader_program_use(const ShaderProgram *prog);

/* Unbind (use program 0) */
void shader_program_unuse(void);

/* Delete the shader program and reset the struct */
void shader_program_destroy(ShaderProgram *prog);

#endif /* FSV_SHADER_H */
