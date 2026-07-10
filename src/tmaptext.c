/* tmaptext.c */

/* Texture-mapped text */

/* fsv - 3D File System Visualizer
 * Copyright (C)1999 Daniel Richard G. <skunk@mit.edu>
 * Updates (c) 2026 sterlingphoenix <fsv@freakzilla.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "common.h"
#include "tmaptext.h"

#include <epoxy/gl.h>
#include <string.h>

#include "camera.h" /* camera->distance (cache invalidation heuristic) */
#include "frameprof.h"
#include "glmath.h"
#include "shader.h"
#include "ogl.h"

/* Bitmap font definition */
#define char_width 16
#define char_height 32
#include "xmaps/charset.xbm"


/* Text can be squeezed to at most half its normal width */
#define TEXT_MAX_SQUEEZE 2.0
/* Mipmaps make faraway text look nice */
#define TEXT_USE_MIPMAPS

/* Initial capacity for text vertex staging buffer */
#define TEXT_INITIAL_CAPACITY 256


/* Normal character aspect ratio */
static const double char_aspect_ratio = (double)char_width / (double)char_height;

/* Font texture object */
static GLuint text_tobj;

/* Per-vertex data for text rendering */
typedef struct {
	float position[3];
	float texcoord[2];
	float color[3];
} TextVertex;

/* Scratch VBO/VAO for text rendering (direct mode: about/splash) */
static GLuint text_vao = 0;
static GLuint text_vbo = 0;
static TextVertex *text_vertices = NULL;
static int text_capacity = 0;
static int text_count = 0;

/* Current text color (set via text_set_color) */
static float text_cur_color[3] = { 1.0f, 1.0f, 1.0f };

/* Label-vertex cache (Phase 39.4 follow-up).
 *
 * Persistent GL buffer holding world-space label vertices for the
 * geometry-mode label walks. Filled once when something changes
 * (mode switch, expand, scan), then replayed every frame with the
 * current camera's projection×view as the shader uniform. Camera
 * moves don't invalidate the buffer — they just update the uniform.
 *
 * cache_filling is set TRUE for the duration of begin_emit..end_emit,
 * which switches text_add_quad's per-vertex transform from
 * "pre-multiply by current modelview" (view space, direct mode) to
 * "pre-multiply by node-only modelview" (world space, cache mode).
 * The node-only modelview is computed as
 *   node_only = camera_modelview_inverse * current_modelview
 * The inverse is computed once at begin_emit (camera_mv_inv) and the
 * multiplication is done per quad inside text_add_quad. */
static GLuint cache_vao = 0;
static GLuint cache_vbo = 0;
static int    cache_vertex_count = 0;
static boolean cache_is_valid = FALSE;
static boolean cache_filling = FALSE;
static mat4   cache_camera_mv_inv;
/* Camera distance when the cache was last built. Significant zoom
 * changes since then mean per-leaf cull decisions are stale (labels
 * that would now pass the cull after zooming in won't be in the
 * cache), so we force a rebuild. See text_cache_replay. */
static double cache_built_distance = 0.0;

/* Soft staleness: the cached labels are still perfectly drawable
 * (world-space vertices, re-projected per frame), just based on
 * slightly out-of-date cull/layout decisions. A stale cache keeps
 * being replayed — labels must NEVER vanish during animation (hard
 * Phase 39.2 constraint) — and is rebuilt at most once per
 * TEXT_CACHE_REFRESH_US. Hard invalidation (text_cache_invalidate:
 * geometry replaced, viewport resized, labels toggled) still forces
 * an immediate rebuild. */
static boolean cache_stale = FALSE;
static gint64 cache_built_time = 0;
/* Camera modelview at build time: any difference marks the cache
 * stale (the frustum/screen-size cull decisions moved with it) */
static float cache_built_cam[16];
#define TEXT_CACHE_REFRESH_US (150 * 1000)

/* Diagnostics for the frameprof summary */
static int cache_diag_hard = 0;
static int cache_diag_soft = 0;
static int cache_diag_rebuilds = 0;


/* Simple XBM parser - bits to bytes. Caller assumes responsibility for
 * freeing the returned pixel buffer */
static byte *
xbm_pixels( const byte *xbm_bits, int pixel_count )
{
	int in_byte = 0;
	int bitmask = 1;
	int i;
	byte *pixels;

	pixels = NEW_ARRAY(byte, pixel_count);

	for (i = 0; i < pixel_count; i++) {
		/* Note: a 1 bit is black */
		if ((int)xbm_bits[in_byte] & bitmask)
			pixels[i] = 0;
		else
			pixels[i] = 255;

		if (bitmask & 128) {
			++in_byte;
			bitmask = 1;
		}
		else
			bitmask <<= 1;
	}

	return pixels;
}


/* Initializes texture-mapping state for drawing text */
void
text_init( void )
{
	byte *charset_pixels;

	/* Set up text texture object */
	glGenTextures( 1, &text_tobj );
	glBindTexture( GL_TEXTURE_2D, text_tobj );

	/* Set up texture-mapping parameters */
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
#ifdef TEXT_USE_MIPMAPS
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR );
#else
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
#endif
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

	/* Load texture: use GL_R8 format (core profile compatible) */
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	charset_pixels = xbm_pixels( charset_bits, charset_width * charset_height );
#ifdef TEXT_USE_MIPMAPS
	glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, charset_width, charset_height, 0, GL_RED, GL_UNSIGNED_BYTE, charset_pixels );
	glGenerateMipmap( GL_TEXTURE_2D );
#else
	glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, charset_width, charset_height, 0, GL_RED, GL_UNSIGNED_BYTE, charset_pixels );
#endif
	xfree( charset_pixels );

	/* Create VAO/VBO for text rendering */
	glGenVertexArrays( 1, &text_vao );
	glGenBuffers( 1, &text_vbo );

	glBindVertexArray( text_vao );
	glBindBuffer( GL_ARRAY_BUFFER, text_vbo );

	/* position: location 0 */
	glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE,
	                       sizeof(TextVertex),
	                       (void *)offsetof(TextVertex, position) );
	glEnableVertexAttribArray( 0 );

	/* texcoord: location 1 */
	glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE,
	                       sizeof(TextVertex),
	                       (void *)offsetof(TextVertex, texcoord) );
	glEnableVertexAttribArray( 1 );

	/* color: location 2 */
	glVertexAttribPointer( 2, 3, GL_FLOAT, GL_FALSE,
	                       sizeof(TextVertex),
	                       (void *)offsetof(TextVertex, color) );
	glEnableVertexAttribArray( 2 );

	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	/* Persistent VAO/VBO for the label-vertex cache. Same vertex
	 * layout as the scratch VBO; the difference is just lifetime
	 * (this buffer holds world-space vertices across frames). */
	glGenVertexArrays( 1, &cache_vao );
	glGenBuffers( 1, &cache_vbo );

	glBindVertexArray( cache_vao );
	glBindBuffer( GL_ARRAY_BUFFER, cache_vbo );

	glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE,
	                       sizeof(TextVertex),
	                       (void *)offsetof(TextVertex, position) );
	glEnableVertexAttribArray( 0 );

	glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE,
	                       sizeof(TextVertex),
	                       (void *)offsetof(TextVertex, texcoord) );
	glEnableVertexAttribArray( 1 );

	glVertexAttribPointer( 2, 3, GL_FLOAT, GL_FALSE,
	                       sizeof(TextVertex),
	                       (void *)offsetof(TextVertex, color) );
	glEnableVertexAttribArray( 2 );

	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
}


/* Ensures the staging buffer has room for n more vertices */
static void
text_ensure_capacity( int n )
{
	int needed = text_count + n;
	if (needed > text_capacity) {
		int new_cap = text_capacity ? text_capacity * 2 : TEXT_INITIAL_CAPACITY;
		while (new_cap < needed)
			new_cap *= 2;
		text_vertices = g_realloc( text_vertices, new_cap * sizeof(TextVertex) );
		text_capacity = new_cap;
	}
}


/* Adds a quad (two triangles, 6 vertices) to the text staging buffer.
 *
 * Two transform modes:
 *
 *   Direct mode (cache_filling = FALSE): pre-multiply each corner by
 *   the current modelview, storing view-space coordinates. text_flush
 *   then draws with projection-only. Used by about/splash and any
 *   other immediate-mode text caller.
 *
 *   Cache mode (cache_filling = TRUE): pre-multiply each corner by
 *   node_only = camera_modelview_inverse * current_modelview, storing
 *   world-space coordinates. The cache draw step then uses
 *   projection × camera_modelview as its uniform, so cached vertices
 *   survive camera moves. Used by the geometry-mode label walks
 *   (mapv/discv/treev _draw_recursive). */
static void
text_add_quad( float x0, float y0, float x1, float y1,
               float x2, float y2, float x3, float y3,
               float z,
               float tx0, float ty0, float tx1, float ty1 )
{
	TextVertex *v;
	mat4 effective_mv;
	const float *mv;

	if (cache_filling) {
		/* node_only = camera_mv_inv * current_modelview, applied
		 * to vertex to get world-space coords. */
		mat4 current_mv_copy;
		const float *current_mv = glmath_get_modelview( );
		memcpy( current_mv_copy, current_mv, sizeof(current_mv_copy) );
		glm_mat4_mul( cache_camera_mv_inv, current_mv_copy, effective_mv );
		mv = (const float *)effective_mv;
	}
	else {
		mv = glmath_get_modelview( );
	}

	/* mv * (x, y, z, 1) for affine modelview (no perspective).
	 * cglm is column-major: mv[0..3]=col0, mv[4..7]=col1, etc. */
	#define MV_X(x, y, z) (mv[0]*(x) + mv[4]*(y) + mv[8]*(z)  + mv[12])
	#define MV_Y(x, y, z) (mv[1]*(x) + mv[5]*(y) + mv[9]*(z)  + mv[13])
	#define MV_Z(x, y, z) (mv[2]*(x) + mv[6]*(y) + mv[10]*(z) + mv[14])

	text_ensure_capacity( 6 );
	v = &text_vertices[text_count];

	/* Triangle 1: v0, v1, v2 (lower-left, lower-right, upper-right) */
	v[0].position[0] = MV_X(x0, y0, z); v[0].position[1] = MV_Y(x0, y0, z); v[0].position[2] = MV_Z(x0, y0, z);
	v[0].texcoord[0] = tx0; v[0].texcoord[1] = ty0;
	v[0].color[0] = text_cur_color[0]; v[0].color[1] = text_cur_color[1]; v[0].color[2] = text_cur_color[2];

	v[1].position[0] = MV_X(x1, y1, z); v[1].position[1] = MV_Y(x1, y1, z); v[1].position[2] = MV_Z(x1, y1, z);
	v[1].texcoord[0] = tx1; v[1].texcoord[1] = ty0;
	v[1].color[0] = text_cur_color[0]; v[1].color[1] = text_cur_color[1]; v[1].color[2] = text_cur_color[2];

	v[2].position[0] = MV_X(x2, y2, z); v[2].position[1] = MV_Y(x2, y2, z); v[2].position[2] = MV_Z(x2, y2, z);
	v[2].texcoord[0] = tx1; v[2].texcoord[1] = ty1;
	v[2].color[0] = text_cur_color[0]; v[2].color[1] = text_cur_color[1]; v[2].color[2] = text_cur_color[2];

	/* Triangle 2: v0, v2, v3 (lower-left, upper-right, upper-left) */
	v[3] = v[0];
	v[4] = v[2];

	v[5].position[0] = MV_X(x3, y3, z); v[5].position[1] = MV_Y(x3, y3, z); v[5].position[2] = MV_Z(x3, y3, z);
	v[5].texcoord[0] = tx0; v[5].texcoord[1] = ty1;
	v[5].color[0] = text_cur_color[0]; v[5].color[1] = text_cur_color[1]; v[5].color[2] = text_cur_color[2];

	text_count += 6;

	#undef MV_X
	#undef MV_Y
	#undef MV_Z
}


/* Uploads accumulated quads and draws them. Vertices are already in
 * view space (text_add_quad pre-multiplies by the modelview at emit
 * time), so the shader's u_mvp uniform receives the projection matrix
 * only. */
static void
text_flush( void )
{
	const float *proj;
	gint64 upload_bytes;

	if (text_count == 0)
		return;

	frameprof_bucket_begin( FRAMEPROF_TEXT_FLUSH );

	proj = glmath_get_projection( );

	upload_bytes = text_count * sizeof(TextVertex);
	glBindBuffer( GL_ARRAY_BUFFER, text_vbo );
	glBufferData( GL_ARRAY_BUFFER, upload_bytes,
	              text_vertices, GL_STREAM_DRAW );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	glUniformMatrix4fv(
		shader_program_get_uniform( &text_shader, "u_mvp" ),
		1, GL_FALSE, proj );

	glBindVertexArray( text_vao );
	glDrawArrays( GL_TRIANGLES, 0, text_count );
	glBindVertexArray( 0 );

	frameprof_text_upload( upload_bytes );
	frameprof_text_draw_call( );

	text_count = 0;

	frameprof_bucket_end( FRAMEPROF_TEXT_FLUSH );
}


/* Sets the current text color for subsequent text_draw_* calls.
 *
 * Does NOT flush. Each vertex emitted by text_add_quad copies
 * text_cur_color into its own per-vertex color attribute at emit time,
 * so changing text_cur_color afterwards does not affect quads already
 * in the buffer. The vertex/fragment shaders pass through the
 * per-vertex color, allowing many colors in one drawn batch.
 *
 * (The flush this used to do was a latent multiplier: TreeV's label
 * loop alternates between platform_label_color and leaf_label_color
 * per visited directory, observed as 60K text_flush calls/frame on
 * a fully-expanded large tree.) */
void
text_set_color( float r, float g, float b )
{
	text_cur_color[0] = r;
	text_cur_color[1] = g;
	text_cur_color[2] = b;
}


/* Call before drawing text.
 * Binds the text shader and font texture; disables polygon offset. */
void
text_pre( void )
{
	glDisable( GL_POLYGON_OFFSET_FILL );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glDisable( GL_CULL_FACE );

	/* Bind text shader and font texture */
	shader_program_use( &text_shader );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, text_tobj );
	glUniform1i(
		shader_program_get_uniform( &text_shader, "u_texture" ),
		0 );

	/* Reset the text batch */
	text_count = 0;
}


/* Flush accumulated text and call before the GL matrix changes */
void
text_pre_matrix_change( void )
{
	text_flush( );
}


/* Call after drawing text — flushes remaining text and restores GL state */
void
text_post( void )
{
	text_flush( );
	shader_program_unuse( );

	glEnable( GL_POLYGON_OFFSET_FILL );
	glEnable( GL_CULL_FACE );
	glDisable( GL_BLEND );
}


/* Given the length of a string, and the dimensions into which that string
 * has to be rendered, this returns the dimensions that should be used
 * for each character */
static void
get_char_dims( int len, const XYvec *max_dims, XYvec *cdims )
{
	double min_width, max_width;

	/* Maximum and minimum widths of the string if it were to occupy
	 * the full depth (y-dimension) available to it */
	max_width = (double)len * max_dims->y * char_aspect_ratio;
	min_width = max_width / TEXT_MAX_SQUEEZE;

	if (max_width > max_dims->x) {
		if (min_width > max_dims->x) {
			/* Text will span full avaiable width, squeezed
			 * horizontally as much as it can take */
			cdims->x = max_dims->x / (double)len;
			cdims->y = TEXT_MAX_SQUEEZE * cdims->x / char_aspect_ratio;
		}
		else {
			/* Text will occupy full available width and
			 * height, squeezed horizontally a bit */
			cdims->x = max_dims->x / (double)len;
			cdims->y = max_dims->y;
		}
	}
	else {
		/* Text will use full available height (characters
		 * will have their natural aspect ratio) */
		cdims->y = max_dims->y;
		cdims->x = cdims->y * char_aspect_ratio;
	}
}


/* Returns the texture-space coordinates of the bottom-left and upper-right
 * corners of the specified character (glyph) */
static void
get_char_tex_coords( int c, XYvec *t_c0, XYvec *t_c1 )
{
	static const XYvec t_char_dims = {
		(double)char_width / (double)charset_width,
		(double)char_height / (double)charset_height
	};
	XYvec gpos;
	int g;

	/* Get position of lower-left corner of glyph
	 * (in bitmap coordinates, w/origin at top-left)
	 * Note: The following code is character-set-specific */
	g = c;
	if ((g < 32) || (g > 127))
		g = 63; /* question mark */
	gpos.x = (double)(((g - 32) & 31) * char_width);
	gpos.y = (double)(((g - 32) >> 5) * char_height);

	/* Texture coordinates */
	t_c0->x = gpos.x / (double)charset_width;
	t_c1->y = gpos.y / (double)charset_height;
	t_c1->x = t_c0->x + t_char_dims.x;
	t_c0->y = t_c1->y + t_char_dims.y;
}


/* Draws a straight line of text centered at the given position,
 * fitting within the dimensions specified */
void
text_draw_straight( const char *text, const XYZvec *text_pos, const XYvec *text_max_dims )
{
	XYvec cdims;
	XYvec t_c0, t_c1;
	float x0, y0, x1, y1;
	int len, i;

	len = strlen( text );
	get_char_dims( len, text_max_dims, &cdims );

	/* Corners of first character */
	x0 = (float)(text_pos->x - 0.5 * (double)len * cdims.x);
	y0 = (float)(text_pos->y - 0.5 * cdims.y);
	x1 = x0 + (float)cdims.x;
	y1 = y0 + (float)cdims.y;

	for (i = 0; i < len; i++) {
		get_char_tex_coords( text[i], &t_c0, &t_c1 );

		text_add_quad( x0, y0, x1, y0,
		               x1, y1, x0, y1,
		               (float)text_pos->z,
		               (float)t_c0.x, (float)t_c0.y,
		               (float)t_c1.x, (float)t_c1.y );

		x0 = x1;
		x1 += (float)cdims.x;
	}

	/* No flush here: text_add_quad has pre-applied the current
	 * modelview, so accumulating across many text_draw_* calls
	 * with different modelview matrices is safe. The batch is
	 * flushed at text_post(), at the next color change, or before
	 * a projection change. */
}


/* Draws a straight line of text centered at the given position, rotated
 * to be tangent to a circle around the origin, and fitting within the
 * dimensions specified (which are also rotated) */
void
text_draw_straight_rotated( const char *text, const RTZvec *text_pos, const XYvec *text_max_dims )
{
	XYvec cdims;
	XYvec t_c0, t_c1;
	XYvec hdelta, vdelta;
	double sin_theta, cos_theta;
	float c0x, c0y, c1x, c1y;
	int len, i;

	len = strlen( text );
	get_char_dims( len, text_max_dims, &cdims );

	sin_theta = sin( RAD(text_pos->theta) );
	cos_theta = cos( RAD(text_pos->theta) );

	/* Vector to move from one character to the next */
	hdelta.x = sin_theta * cdims.x;
	hdelta.y = - cos_theta * cdims.x;
	/* Vector to move from bottom of character to top */
	vdelta.x = cos_theta * cdims.y;
	vdelta.y = sin_theta * cdims.y;

	/* Corners of first character */
	c0x = (float)(cos_theta * text_pos->r - 0.5 * ((double)len * hdelta.x + vdelta.x));
	c0y = (float)(sin_theta * text_pos->r - 0.5 * ((double)len * hdelta.y + vdelta.y));
	c1x = (float)(c0x + hdelta.x + vdelta.x);
	c1y = (float)(c0y + hdelta.y + vdelta.y);

	for (i = 0; i < len; i++) {
		get_char_tex_coords( text[i], &t_c0, &t_c1 );

		text_add_quad( c0x, c0y,
		               (float)(c0x + hdelta.x), (float)(c0y + hdelta.y),
		               c1x, c1y,
		               (float)(c1x - hdelta.x), (float)(c1y - hdelta.y),
		               (float)text_pos->z,
		               (float)t_c0.x, (float)t_c0.y,
		               (float)t_c1.x, (float)t_c1.y );

		c0x += (float)hdelta.x;
		c0y += (float)hdelta.y;
		c1x += (float)hdelta.x;
		c1y += (float)hdelta.y;
	}

	/* No flush here — see text_draw_straight. */
}


/* Draws a curved arc of text, occupying no more than the depth and arc
 * width specified. text_pos indicates outer edge (not center) of text */
void
text_draw_curved( const char *text, const RTZvec *text_pos, const RTvec *text_max_dims )
{
	XYvec straight_dims, cdims;
	XYvec char_pos, fwsl, bwsl;
	XYvec t_c0, t_c1;
	double char_arc_width, theta;
	double sin_theta, cos_theta;
	double text_r;
	int len, i;

	/* Convert curved dimensions to straight equivalent */
	straight_dims.x = (PI / 180.0) * text_pos->r * text_max_dims->theta;
	straight_dims.y = text_max_dims->r;

	len = strlen( text );
	get_char_dims( len, &straight_dims, &cdims );

	/* Radius of center of text line */
	text_r = text_pos->r - 0.5 * cdims.y;

	/* Arc width occupied by each character */
	char_arc_width = (180.0 / PI) * cdims.x / text_r;

	theta = text_pos->theta + 0.5 * (double)(len - 1) * char_arc_width;
	for (i = 0; i < len; i++) {
		sin_theta = sin( RAD(theta) );
		cos_theta = cos( RAD(theta) );

		/* Center of character and deltas from center to corners */
		char_pos.x = cos_theta * text_r;
		char_pos.y = sin_theta * text_r;
		/* "forward slash / backward slash" */
		fwsl.x = 0.5 * (cdims.y * cos_theta + cdims.x * sin_theta);
		fwsl.y = 0.5 * (cdims.y * sin_theta - cdims.x * cos_theta);
		bwsl.x = 0.5 * (- cdims.y * cos_theta + cdims.x * sin_theta);
		bwsl.y = 0.5 * (- cdims.y * sin_theta - cdims.x * cos_theta);

		get_char_tex_coords( text[i], &t_c0, &t_c1 );

		text_add_quad(
			(float)(char_pos.x - fwsl.x), (float)(char_pos.y - fwsl.y),
			(float)(char_pos.x + bwsl.x), (float)(char_pos.y + bwsl.y),
			(float)(char_pos.x + fwsl.x), (float)(char_pos.y + fwsl.y),
			(float)(char_pos.x - bwsl.x), (float)(char_pos.y - bwsl.y),
			(float)text_pos->z,
			(float)t_c0.x, (float)t_c0.y,
			(float)t_c1.x, (float)t_c1.y );

		theta -= char_arc_width;
	}

	/* No flush here — see text_draw_straight. */
}


/* ---- Label-vertex cache --------------------------------------- */


void
text_cache_invalidate( void )
{
	cache_is_valid = FALSE;
	++cache_diag_hard;
}


/* Marks the cache stale: the labels in it are still drawable, but
 * their membership/positions are based on out-of-date decisions.
 * Replay keeps drawing a stale cache and rebuilds it at most once
 * per TEXT_CACHE_REFRESH_US — labels update at a few Hz during
 * sustained animation instead of costing a rebuild every frame */
void
text_cache_touch( void )
{
	cache_stale = TRUE;
	++cache_diag_soft;
}


/* Reports and resets the diagnostics counters (frameprof summary) */
void
text_cache_stats( int *hard, int *soft, int *rebuilds )
{
	*hard = cache_diag_hard;
	*soft = cache_diag_soft;
	*rebuilds = cache_diag_rebuilds;
	cache_diag_hard = 0;
	cache_diag_soft = 0;
	cache_diag_rebuilds = 0;
}


/* If the cache is usable, replay it with the current camera matrix
 * and return TRUE. Caller skips its tree walk in that case.
 *
 * Significant zoom since the last build only marks the cache stale
 * (per-leaf cull decisions drift) — the stale set keeps drawing,
 * and the rebuild happens on the throttle like any other soft
 * staleness. Only hard invalidation forces an immediate rebuild. */
boolean
text_cache_replay( void )
{
	const float *proj;
	const float *cam;
	mat4 mvp;

	if (!cache_is_valid)
		return FALSE;

	/* Any camera motion (pan, orbit, zoom) changes which labels
	 * pass the frustum/screen-size culls — mark stale so the set
	 * refreshes on the throttle. An idle camera compares equal and
	 * never triggers a rebuild. (Subsumes the old zoom-ratio
	 * heuristic.) */
	if (!cache_stale
	    && memcmp( cache_built_cam, ogl_get_camera_modelview( ),
	               sizeof(cache_built_cam) ) != 0)
		cache_stale = TRUE;

	if (cache_stale
	    && (g_get_monotonic_time( ) - cache_built_time) >= TEXT_CACHE_REFRESH_US)
		return FALSE; /* refresh window open — rebuild now */

	if (cache_vertex_count == 0)
		return TRUE; /* nothing to draw, but no walk needed */

	frameprof_bucket_begin( FRAMEPROF_TEXT_FLUSH );

	proj = glmath_get_projection( );
	cam = ogl_get_camera_modelview( );
	/* mvp = projection * camera_modelview */
	{
		mat4 proj_copy, cam_copy;
		memcpy( proj_copy, proj, sizeof(proj_copy) );
		memcpy( cam_copy, cam, sizeof(cam_copy) );
		glm_mat4_mul( proj_copy, cam_copy, mvp );
	}

	/* GL state — same as text_pre but no buffer rebinding for
	 * the shader-bound text VAO (we use cache_vao). */
	glDisable( GL_POLYGON_OFFSET_FILL );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glDisable( GL_CULL_FACE );

	shader_program_use( &text_shader );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, text_tobj );
	glUniform1i(
		shader_program_get_uniform( &text_shader, "u_texture" ), 0 );
	glUniformMatrix4fv(
		shader_program_get_uniform( &text_shader, "u_mvp" ),
		1, GL_FALSE, (const float *)mvp );

	glBindVertexArray( cache_vao );
	glDrawArrays( GL_TRIANGLES, 0, cache_vertex_count );
	glBindVertexArray( 0 );

	shader_program_unuse( );

	glEnable( GL_POLYGON_OFFSET_FILL );
	glEnable( GL_CULL_FACE );
	glDisable( GL_BLEND );

	frameprof_text_draw_call( );

	frameprof_bucket_end( FRAMEPROF_TEXT_FLUSH );

	return TRUE;
}


/* Called before the tree walk that will populate the cache. Switches
 * text_add_quad into world-space transform mode, captures the camera
 * modelview's inverse for use during that mode, and resets the
 * scratch buffer so the walk can fill it. */
void
text_cache_begin_emit( void )
{
	mat4 cam_copy;
	memcpy( cam_copy, ogl_get_camera_modelview( ), sizeof(cam_copy) );
	glm_mat4_inv( cam_copy, cache_camera_mv_inv );

	text_count = 0;
	cache_filling = TRUE;
}


/* Called after the tree walk has finished populating the scratch
 * buffer (via text_add_quad). Uploads to the persistent cache buffer,
 * marks the cache valid, and draws it. */
void
text_cache_end_emit( void )
{
	gint64 upload_bytes;

	cache_filling = FALSE;

	upload_bytes = text_count * sizeof(TextVertex);

	frameprof_bucket_begin( FRAMEPROF_TEXT_FLUSH );

	/* STREAM_DRAW: this buffer is rewritten on every refresh */
	glBindBuffer( GL_ARRAY_BUFFER, cache_vbo );
	glBufferData( GL_ARRAY_BUFFER, upload_bytes,
	              text_vertices, GL_STREAM_DRAW );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	cache_vertex_count = text_count;
	cache_is_valid = TRUE;
	cache_stale = FALSE;
	cache_built_time = g_get_monotonic_time( );
	cache_built_distance = camera->distance;
	memcpy( cache_built_cam, ogl_get_camera_modelview( ),
	        sizeof(cache_built_cam) );
	++cache_diag_rebuilds;

	frameprof_text_upload( upload_bytes );

	frameprof_bucket_end( FRAMEPROF_TEXT_FLUSH );

	text_count = 0;

	/* Now actually draw what we just uploaded. */
	text_cache_replay( );
}


/* end tmaptext.c */
