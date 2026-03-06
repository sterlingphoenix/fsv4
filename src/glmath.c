/* glmath.c - Matrix stack utilities wrapping cglm */

#include "glmath.h"
#include <glib.h>

/* Projection matrix stack */
static mat4 proj_stack[GLMATH_MAX_STACK_DEPTH];
static int proj_top = 0;

/* Modelview matrix stack */
static mat4 mv_stack[GLMATH_MAX_STACK_DEPTH];
static int mv_top = 0;


void
glmath_init(void)
{
	glm_mat4_identity(proj_stack[0]);
	glm_mat4_identity(mv_stack[0]);
	proj_top = 0;
	mv_top = 0;
}


/* --- Projection matrix --- */

void
glmath_frustum(double left, double right, double bottom, double top,
               double near_val, double far_val)
{
	mat4 f;
	glm_frustum((float)left, (float)right, (float)bottom, (float)top,
	            (float)near_val, (float)far_val, f);
	glm_mat4_mul(proj_stack[proj_top], f, proj_stack[proj_top]);
}


void
glmath_ortho(double left, double right, double bottom, double top,
             double near_val, double far_val)
{
	mat4 o;
	glm_ortho((float)left, (float)right, (float)bottom, (float)top,
	          (float)near_val, (float)far_val, o);
	glm_mat4_mul(proj_stack[proj_top], o, proj_stack[proj_top]);
}


void
glmath_load_identity_projection(void)
{
	glm_mat4_identity(proj_stack[proj_top]);
}


void
glmath_push_projection(void)
{
	g_assert(proj_top < GLMATH_MAX_STACK_DEPTH - 1);
	glm_mat4_copy(proj_stack[proj_top], proj_stack[proj_top + 1]);
	proj_top++;
}


void
glmath_pop_projection(void)
{
	g_assert(proj_top > 0);
	proj_top--;
}


const float *
glmath_get_projection(void)
{
	return (const float *)proj_stack[proj_top];
}


/* --- Modelview matrix --- */

void
glmath_load_identity_modelview(void)
{
	glm_mat4_identity(mv_stack[mv_top]);
}


void
glmath_push_modelview(void)
{
	g_assert(mv_top < GLMATH_MAX_STACK_DEPTH - 1);
	glm_mat4_copy(mv_stack[mv_top], mv_stack[mv_top + 1]);
	mv_top++;
}


void
glmath_pop_modelview(void)
{
	g_assert(mv_top > 0);
	mv_top--;
}


void
glmath_translated(double x, double y, double z)
{
	vec3 v = { (float)x, (float)y, (float)z };
	glm_translate(mv_stack[mv_top], v);
}


void
glmath_rotated(double angle_deg, double x, double y, double z)
{
	float rad = glm_rad((float)angle_deg);
	vec3 axis = { (float)x, (float)y, (float)z };
	glm_rotate(mv_stack[mv_top], rad, axis);
}


void
glmath_scaled(double x, double y, double z)
{
	vec3 s = { (float)x, (float)y, (float)z };
	glm_scale(mv_stack[mv_top], s);
}


const float *
glmath_get_modelview(void)
{
	return (const float *)mv_stack[mv_top];
}


/* --- Computed matrices --- */

void
glmath_get_mvp(mat4 dest)
{
	glm_mat4_mul(proj_stack[proj_top], mv_stack[mv_top], dest);
}


void
glmath_get_normal_matrix(mat3 dest)
{
	mat4 inv;
	glm_mat4_inv(mv_stack[mv_top], inv);
	/* Normal matrix = transpose of inverse of upper-left 3x3 */
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			dest[i][j] = inv[j][i];
}
