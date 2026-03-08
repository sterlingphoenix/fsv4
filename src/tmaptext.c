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

/* Scratch VBO/VAO for text rendering */
static GLuint text_vao = 0;
static GLuint text_vbo = 0;
static TextVertex *text_vertices = NULL;
static int text_capacity = 0;
static int text_count = 0;

/* Current text color (set via text_set_color) */
static float text_cur_color[3] = { 1.0f, 1.0f, 1.0f };


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


/* Adds a quad (two triangles, 6 vertices) to the text staging buffer */
static void
text_add_quad( float x0, float y0, float x1, float y1,
               float x2, float y2, float x3, float y3,
               float z,
               float tx0, float ty0, float tx1, float ty1 )
{
	TextVertex *v;

	text_ensure_capacity( 6 );
	v = &text_vertices[text_count];

	/* Triangle 1: v0, v1, v2 (lower-left, lower-right, upper-right) */
	v[0].position[0] = x0; v[0].position[1] = y0; v[0].position[2] = z;
	v[0].texcoord[0] = tx0; v[0].texcoord[1] = ty0;
	v[0].color[0] = text_cur_color[0]; v[0].color[1] = text_cur_color[1]; v[0].color[2] = text_cur_color[2];

	v[1].position[0] = x1; v[1].position[1] = y1; v[1].position[2] = z;
	v[1].texcoord[0] = tx1; v[1].texcoord[1] = ty0;
	v[1].color[0] = text_cur_color[0]; v[1].color[1] = text_cur_color[1]; v[1].color[2] = text_cur_color[2];

	v[2].position[0] = x2; v[2].position[1] = y2; v[2].position[2] = z;
	v[2].texcoord[0] = tx1; v[2].texcoord[1] = ty1;
	v[2].color[0] = text_cur_color[0]; v[2].color[1] = text_cur_color[1]; v[2].color[2] = text_cur_color[2];

	/* Triangle 2: v0, v2, v3 (lower-left, upper-right, upper-left) */
	v[3] = v[0];
	v[4] = v[2];

	v[5].position[0] = x3; v[5].position[1] = y3; v[5].position[2] = z;
	v[5].texcoord[0] = tx0; v[5].texcoord[1] = ty1;
	v[5].color[0] = text_cur_color[0]; v[5].color[1] = text_cur_color[1]; v[5].color[2] = text_cur_color[2];

	text_count += 6;
}


/* Uploads accumulated quads and draws them with the current MVP */
static void
text_flush( void )
{
	float mv[16], proj[16];
	float mvp[16];
	int i, j, k;

	if (text_count == 0)
		return;

	/* Read current GL matrices */
	glGetFloatv( GL_MODELVIEW_MATRIX, mv );
	glGetFloatv( GL_PROJECTION_MATRIX, proj );

	/* Compute MVP */
	for (j = 0; j < 4; j++)
		for (i = 0; i < 4; i++) {
			float sum = 0.0f;
			for (k = 0; k < 4; k++)
				sum += proj[i + k * 4] * mv[k + j * 4];
			mvp[i + j * 4] = sum;
		}

	/* Upload text vertices */
	glBindBuffer( GL_ARRAY_BUFFER, text_vbo );
	glBufferData( GL_ARRAY_BUFFER,
	              text_count * sizeof(TextVertex),
	              text_vertices, GL_STREAM_DRAW );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	/* Set MVP uniform and draw */
	glUniformMatrix4fv(
		shader_program_get_uniform( &text_shader, "u_mvp" ),
		1, GL_FALSE, mvp );

	glBindVertexArray( text_vao );
	glDrawArrays( GL_TRIANGLES, 0, text_count );
	glBindVertexArray( 0 );

	text_count = 0;
}


/* Sets the current text color for subsequent text_draw_* calls */
void
text_set_color( float r, float g, float b )
{
	/* Flush any pending text that uses the old color */
	text_flush( );
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

	text_flush( );
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

	text_flush( );
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

	text_flush( );
}


/* end tmaptext.c */
