/* vbobatch.h - VBO/VAO management for batched geometry rendering */

#ifndef FSV_VBOBATCH_H
#define FSV_VBOBATCH_H

#include <glib.h>
#include <epoxy/gl.h>

/* Per-vertex data layout */
typedef struct {
	float position[3];
	float normal[3];
	float color[3];
	unsigned int node_id;
} VBOVertex;

/* A batch of vertices uploaded to the GPU */
typedef struct {
	GLuint vao;
	GLuint vbo;
	int vertex_count;      /* number of vertices uploaded to GPU */
	gboolean dirty;        /* TRUE = needs rebuild + re-upload */

	/* CPU-side staging buffer (used during building) */
	VBOVertex *vertices;
	int capacity;          /* allocated slots in vertices[] */
	int build_count;       /* vertices added during current build */
} VBOBatch;

/* Initialize a batch (zero everything, mark dirty) */
void vbo_batch_init(VBOBatch *batch);

/* Begin building: reset the staging buffer for new data */
void vbo_batch_begin(VBOBatch *batch);

/* Append a single vertex to the staging buffer */
void vbo_batch_add_vertex(VBOBatch *batch,
                          float px, float py, float pz,
                          float nx, float ny, float nz,
                          float r, float g, float b,
                          unsigned int node_id);

/* Upload the staging buffer to the GPU (GL_STATIC_DRAW).
 * Creates VAO/VBO on first call. Clears the dirty flag. */
void vbo_batch_upload(VBOBatch *batch);

/* Draw the batch (glDrawArrays GL_TRIANGLES) */
void vbo_batch_draw(const VBOBatch *batch);

/* Mark the batch as needing a rebuild */
void vbo_batch_invalidate(VBOBatch *batch);

/* Free GPU resources and CPU staging buffer */
void vbo_batch_destroy(VBOBatch *batch);

#endif /* FSV_VBOBATCH_H */
