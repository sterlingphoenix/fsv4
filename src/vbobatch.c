/* vbobatch.c - VBO/VAO management for batched geometry rendering */

#include "vbobatch.h"
#include <string.h>

/* Initial capacity for the staging buffer */
#define INITIAL_CAPACITY 4096


void
vbo_batch_init(VBOBatch *batch)
{
	memset(batch, 0, sizeof(*batch));
	batch->dirty = TRUE;
}


void
vbo_batch_begin(VBOBatch *batch)
{
	batch->build_count = 0;
}


void
vbo_batch_add_vertex(VBOBatch *batch,
                     float px, float py, float pz,
                     float nx, float ny, float nz,
                     float r, float g, float b,
                     unsigned int node_id)
{
	/* Grow staging buffer if needed */
	if (batch->build_count >= batch->capacity) {
		int new_cap = batch->capacity ? batch->capacity * 2 : INITIAL_CAPACITY;
		batch->vertices = g_realloc(batch->vertices,
		                            new_cap * sizeof(VBOVertex));
		batch->capacity = new_cap;
	}

	VBOVertex *v = &batch->vertices[batch->build_count];
	v->position[0] = px;  v->position[1] = py;  v->position[2] = pz;
	v->normal[0] = nx;    v->normal[1] = ny;    v->normal[2] = nz;
	v->color[0] = r;      v->color[1] = g;      v->color[2] = b;
	v->node_id = node_id;

	batch->build_count++;
}


/* Creates the VAO/VBO with the standard vertex layout on first use */
static void
vbo_batch_ensure_vao(VBOBatch *batch)
{
	if (batch->vao != 0)
		return;

	glGenVertexArrays(1, &batch->vao);
	glGenBuffers(1, &batch->vbo);

	glBindVertexArray(batch->vao);
	glBindBuffer(GL_ARRAY_BUFFER, batch->vbo);

	/* position: location 0 */
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
	                      sizeof(VBOVertex),
	                      (void *)offsetof(VBOVertex, position));
	glEnableVertexAttribArray(0);

	/* normal: location 1 */
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
	                      sizeof(VBOVertex),
	                      (void *)offsetof(VBOVertex, normal));
	glEnableVertexAttribArray(1);

	/* color: location 2 */
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE,
	                      sizeof(VBOVertex),
	                      (void *)offsetof(VBOVertex, color));
	glEnableVertexAttribArray(2);

	/* node_id: location 3 (integer attribute) */
	glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT,
	                       sizeof(VBOVertex),
	                       (void *)offsetof(VBOVertex, node_id));
	glEnableVertexAttribArray(3);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}


void
vbo_batch_upload(VBOBatch *batch)
{
	vbo_batch_ensure_vao(batch);

	/* Upload vertex data */
	glBindBuffer(GL_ARRAY_BUFFER, batch->vbo);
	glBufferData(GL_ARRAY_BUFFER,
	             batch->build_count * sizeof(VBOVertex),
	             batch->vertices,
	             GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	batch->vertex_count = batch->build_count;
	batch->dirty = FALSE;
}


void
vbo_batch_upload_gather(VBOBatch *dst, VBOBatch *const *parts, int n_parts)
{
	GLsizeiptr total = 0;
	GLintptr offset = 0;
	int i;

	for (i = 0; i < n_parts; i++)
		total += parts[i]->build_count;

	vbo_batch_ensure_vao(dst);

	glBindBuffer(GL_ARRAY_BUFFER, dst->vbo);
	glBufferData(GL_ARRAY_BUFFER, total * (GLsizeiptr)sizeof(VBOVertex),
	             NULL, GL_STATIC_DRAW);
	for (i = 0; i < n_parts; i++) {
		if (parts[i]->build_count == 0)
			continue;
		glBufferSubData(GL_ARRAY_BUFFER,
		                offset * (GLintptr)sizeof(VBOVertex),
		                (GLsizeiptr)parts[i]->build_count * sizeof(VBOVertex),
		                parts[i]->vertices);
		offset += parts[i]->build_count;
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	dst->vertex_count = (int)total;
	dst->dirty = FALSE;
}


void
vbo_batch_draw(const VBOBatch *batch)
{
	if (batch->vertex_count == 0 || batch->vao == 0)
		return;

	glBindVertexArray(batch->vao);
	glDrawArrays(GL_TRIANGLES, 0, batch->vertex_count);
	glBindVertexArray(0);
}


void
vbo_batch_draw_lines(const VBOBatch *batch)
{
	if (batch->vertex_count == 0 || batch->vao == 0)
		return;

	glBindVertexArray(batch->vao);
	glDrawArrays(GL_LINES, 0, batch->vertex_count);
	glBindVertexArray(0);
}


void
vbo_batch_invalidate(VBOBatch *batch)
{
	batch->dirty = TRUE;
}


void
vbo_batch_destroy(VBOBatch *batch)
{
	if (batch->vbo) {
		glDeleteBuffers(1, &batch->vbo);
		batch->vbo = 0;
	}
	if (batch->vao) {
		glDeleteVertexArrays(1, &batch->vao);
		batch->vao = 0;
	}
	g_free(batch->vertices);
	batch->vertices = NULL;
	batch->capacity = 0;
	batch->build_count = 0;
	batch->vertex_count = 0;
	batch->dirty = TRUE;
}
