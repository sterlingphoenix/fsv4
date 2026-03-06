/* glmath.h - Matrix stack utilities wrapping cglm */

#ifndef FSV_GLMATH_H
#define FSV_GLMATH_H

#include <cglm/cglm.h>

/* Maximum matrix stack depth (matches typical GL implementations) */
#define GLMATH_MAX_STACK_DEPTH 32

/* Initialize the matrix module (call once at startup) */
void glmath_init(void);

/* Projection matrix */
void glmath_frustum(double left, double right, double bottom, double top,
                    double near_val, double far_val);
void glmath_ortho(double left, double right, double bottom, double top,
                  double near_val, double far_val);
void glmath_load_identity_projection(void);
void glmath_push_projection(void);
void glmath_pop_projection(void);
const float *glmath_get_projection(void);

/* Modelview matrix */
void glmath_load_identity_modelview(void);
void glmath_push_modelview(void);
void glmath_pop_modelview(void);
void glmath_translated(double x, double y, double z);
void glmath_rotated(double angle_deg, double x, double y, double z);
void glmath_scaled(double x, double y, double z);
const float *glmath_get_modelview(void);

/* Computed matrices */
void glmath_get_mvp(mat4 dest);
void glmath_get_normal_matrix(mat3 dest);

#endif /* FSV_GLMATH_H */
