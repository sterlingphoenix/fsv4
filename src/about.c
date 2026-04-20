/* about.c */

/* Help -> About... */

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

/* TODO: Replace this with a regular ol' popup window. */

#include "common.h"
#include "about.h"

#include <epoxy/gl.h>

#include "animation.h"
#include "geometry.h"
#include "glmath.h"
#include "ogl.h"
#include "tmaptext.h"


/* Interval normalization macro */
#define INTERVAL_PART(x,x0,x1)	(((x) - (x0)) / ((x1) - (x0)))


/* Normalized time variable (in range [0, 1]) */
static double about_part;

/* TRUE while giving About presentation */
static boolean about_active = FALSE;


/* Draws the "fsv" 3D letters */
static void
draw_fsv( void )
{
	double dy, p, q;

	/* Set up projection matrix */
	glmath_push_projection( );
	glmath_load_identity_projection( );
	dy = 80.0 / ogl_aspect_ratio( );
	glmath_frustum( - 80.0, 80.0, - dy, dy, 80.0, 2000.0 );

	/* Set up modelview matrix */
	glmath_push_modelview( );
	glmath_load_identity_modelview( );
	if (about_part < 0.2) {
		/* Zooming in to final position (no spinning) */
		p = INTERVAL_PART(about_part, 0.0, 0.2);
		q = pow( 1.0 - p, 1.5 );
		glmath_translated( 0.0, 250.0 * (1.0 - q), -480.0 - 1000.0 * q );
		glmath_rotated( 5.0, 1.0, 0.0, 0.0 );
	}
	else {
		/* Holding at final position */
		glmath_translated( 0.0, 250.0, -480.0 );
		glmath_rotated( 5.0, 1.0, 0.0, 0.0 );
	}

	/* Draw "fsv" geometry */
	geometry_gldraw_fsv( );

	/* Restore previous matrices */
	glmath_pop_projection( );
	glmath_pop_modelview( );
}


/* Draws the lines of text */
static void
draw_text( void )
{
        XYZvec tpos;
	XYvec tdims;
	double dy, p, q;

	if (about_part < 0.2)
		return;

	/* Set up projection matrix */
	glmath_push_projection( );
	glmath_load_identity_projection( );
	dy = 1.0 / ogl_aspect_ratio( );
	glmath_frustum( - 1.0, 1.0, - dy, dy, 1.0, 205.0 );

	/* Set up modelview matrix */
	glmath_push_modelview( );
	glmath_load_identity_modelview( );

	if (about_part < 0.4)
		p = INTERVAL_PART(about_part, 0.2, 0.4);
	else
		p = 1.0;
	q = (1.0 - SQR(1.0 - p));

	text_pre( );

	tdims.x = 400.0;
	tdims.y = 10.0;
	tpos.x = 0.0;
	tpos.y = 68.0;
	tpos.z = -200.0 * q;
	text_set_color( 1.0, 1.0, 1.0 );
	text_draw_straight( "A 3D File System Visualiser", &tpos, &tdims );

	tdims.y = 8.0;
	tpos.y = 32.0 * q + 25.0;
	text_draw_straight( "Version " VERSION, &tpos, &tdims );

	text_post( );

	/* Restore previous matrices */
	glmath_pop_projection( );
	glmath_pop_modelview( );
}


/* Wraps src into lines of at most max_chars, breaking at spaces.
 * Appends each wrapped line into out[], bumping *pcount. Returns the
 * length of the longest line produced. */
#define HELP_MAX_LINES		32
#define HELP_LINE_BUFFER	160

static int
wrap_line( const char *src, int max_chars, char out[][HELP_LINE_BUFFER], int *pcount, int cur_max )
{
	int slen, start, end, last_space, llen;

	slen = (int)strlen( src );
	if (slen == 0) {
		if (*pcount < HELP_MAX_LINES) {
			out[*pcount][0] = '\0';
			(*pcount)++;
		}
		return cur_max;
	}

	start = 0;
	while (start < slen && *pcount < HELP_MAX_LINES) {
		end = start + max_chars;
		if (end >= slen) {
			end = slen;
		}
		else {
			last_space = end;
			while (last_space > start && src[last_space] != ' ')
				last_space--;
			if (last_space == start)
				last_space = end; /* no space; hard break */
			end = last_space;
		}
		llen = end - start;
		if (llen >= HELP_LINE_BUFFER)
			llen = HELP_LINE_BUFFER - 1;
		memcpy( out[*pcount], src + start, llen );
		out[*pcount][llen] = '\0';
		if (llen > cur_max)
			cur_max = llen;
		(*pcount)++;
		while (end < slen && src[end] == ' ')
			end++;
		start = end;
	}
	return cur_max;
}


/* Draws the generic help text, left-aligned in a centered block */
static void
draw_help( void )
{
	static const char * const help_src[] = {
		"FSV can display three different visualisation modes:",
		"",
		"MapV: Visualises the filesystem as buildings/city blocks",
		"DiscV: Visualises the filesystem as different size discs",
		"TreeV: Visualises the filesystem as an interconnected tree/bar graph.",
		"",
		"Views can be rotated, panned, and zoomed.",
		"",
		"Nodes can be coloured based on wildcards, timestamps.",
		"",
		"Settings persist between sessions by default. This can be disabled using the Preferences menu."
	};
	static char wrapped[HELP_MAX_LINES][HELP_LINE_BUFFER];
	int src_count, wcount, max_len, i, llen;
	int content_lines, para_breaks, in_para_break, first;
	const double char_w = 4.0; /* tdims.y=8 * 0.5 aspect */
	const int max_chars = 70;
	const double line_spacing = 9.0;
	const double para_spacing = 13.0;
	double total_h, y, dy, p, q;
	XYZvec tpos;
	XYvec tdims;

	if (about_part < 0.4)
		return;

	/* Word-wrap source into physical lines */
	src_count = (int)(sizeof(help_src) / sizeof(help_src[0]));
	wcount = 0;
	max_len = 0;
	for (i = 0; i < src_count; i++)
		max_len = wrap_line( help_src[i], max_chars, wrapped, &wcount, max_len );

	/* Count content lines and paragraph breaks for height calculation */
	content_lines = 0;
	para_breaks = 0;
	for (i = 0; i < wcount; i++) {
		if (wrapped[i][0] != '\0') {
			content_lines++;
		}
		else if (i > 0 && wrapped[i-1][0] != '\0') {
			para_breaks++;
		}
	}

	/* Set up projection matrix */
	glmath_push_projection( );
	glmath_load_identity_projection( );
	dy = 1.0 / ogl_aspect_ratio( );
	glmath_frustum( - 1.0, 1.0, - dy, dy, 1.0, 205.0 );

	/* Set up modelview matrix */
	glmath_push_modelview( );
	glmath_load_identity_modelview( );

	/* Fade in (color scales from black to white) */
	if (about_part < 0.6)
		p = INTERVAL_PART(about_part, 0.4, 0.6);
	else
		p = 1.0;
	q = 1.0 - SQR(1.0 - p);

	text_pre( );
	tdims.x = (max_len + 2) * char_w;
	tdims.y = 8.0;
	tpos.z = -200.0;
	text_set_color( (float)q, (float)q, (float)q );

	total_h = (double)(content_lines - 1 - para_breaks) * line_spacing
	        + (double)para_breaks * para_spacing;
	y = 0.5 * total_h;

	in_para_break = 0;
	first = 1;
	for (i = 0; i < wcount; i++) {
		if (wrapped[i][0] == '\0') {
			in_para_break = 1;
			continue;
		}
		if (!first) {
			if (in_para_break)
				y -= para_spacing;
			else
				y -= line_spacing;
		}
		llen = (int)strlen( wrapped[i] );
		tpos.x = -0.5 * (double)max_len * char_w
		       + 0.5 * (double)llen * char_w;
		tpos.y = y;
		text_draw_straight( wrapped[i], &tpos, &tdims );
		first = 0;
		in_para_break = 0;
	}

	text_post( );

	/* Restore previous matrices */
	glmath_pop_projection( );
	glmath_pop_modelview( );
}


/* Draws the keyboard shortcuts section in the bottom third */
static void
draw_shortcuts( void )
{
	static const char * const left_col[] = {
		"Visualisation",
		"1: MapV",
		"2: DiscV",
		"3: TreeV",
		"C: Toggle colour mode",
		"L: Toggle logarithmic mode (TreeV only)"
	};
	static const char * const right_col[] = {
		"Navigation",
		"L-Mouse Hold: Rotate",
		"L-Mouse Double-Click: Select/Go To",
		"Mouse Wheel: Zoom in/out",
		"WASD: Pan",
		"R: Reset to initial view"
	};
	const int left_count = (int)(sizeof(left_col) / sizeof(left_col[0]));
	const int right_count = (int)(sizeof(right_col) / sizeof(right_col[0]));
	const char *title = "Keyboard Shortcuts etc.";
	const double char_w = 4.0;	/* tdims.y=8 * 0.5 aspect */
	const double line_spacing = 8.0;
	const double title_gap = 4.0;	/* extra below title */
	const double col_gap = 28.0;	/* gap between columns (world units) */
	XYZvec tpos;
	XYvec tdims;
	double dy, p, q;
	double y_title, y_entry;
	double col_left_x, col_right_x;
	double block_w;
	int i, llen, max_left, max_right, max_count, li;

	if (about_part < 0.6)
		return;

	/* Find column widths (longest line in each column) */
	max_left = (int)strlen( left_col[0] );
	for (i = 1; i < left_count; i++) {
		li = (int)strlen( left_col[i] );
		if (li > max_left)
			max_left = li;
	}
	max_right = (int)strlen( right_col[0] );
	for (i = 1; i < right_count; i++) {
		li = (int)strlen( right_col[i] );
		if (li > max_right)
			max_right = li;
	}

	/* Set up projection matrix */
	glmath_push_projection( );
	glmath_load_identity_projection( );
	dy = 1.0 / ogl_aspect_ratio( );
	glmath_frustum( - 1.0, 1.0, - dy, dy, 1.0, 205.0 );

	/* Set up modelview matrix */
	glmath_push_modelview( );
	glmath_load_identity_modelview( );

	/* Fade in (color scales from black up) */
	if (about_part < 0.8)
		p = INTERVAL_PART(about_part, 0.6, 0.8);
	else
		p = 1.0;
	q = 1.0 - SQR(1.0 - p);

	text_pre( );
	tdims.x = 400.0;
	tdims.y = 8.0;
	tpos.z = -200.0;

	/* Title: centered, white (fades from black) */
	y_title = -57.0;
	text_set_color( (float)q, (float)q, (float)q );
	tpos.x = 0.0;
	tpos.y = y_title;
	text_draw_straight( title, &tpos, &tdims );

	/* Compute column left-edge positions: two blocks + gap, centered */
	block_w = (double)max_left * char_w + col_gap + (double)max_right * char_w;
	col_left_x = -0.5 * block_w;
	col_right_x = col_left_x + (double)max_left * char_w + col_gap;

	/* Headers: cyan-ish, one line below title */
	y_entry = y_title - line_spacing - title_gap;
	text_set_color( 0.5f * (float)q, 0.9f * (float)q, 1.0f * (float)q );

	llen = (int)strlen( left_col[0] );
	tpos.x = col_left_x + 0.5 * (double)llen * char_w;
	tpos.y = y_entry;
	text_draw_straight( left_col[0], &tpos, &tdims );

	llen = (int)strlen( right_col[0] );
	tpos.x = col_right_x + 0.5 * (double)llen * char_w;
	tpos.y = y_entry;
	text_draw_straight( right_col[0], &tpos, &tdims );

	/* Entries: white */
	text_set_color( (float)q, (float)q, (float)q );
	max_count = left_count > right_count ? left_count : right_count;
	for (i = 1; i < max_count; i++) {
		y_entry -= line_spacing;
		if (i < left_count) {
			llen = (int)strlen( left_col[i] );
			tpos.x = col_left_x + 0.5 * (double)llen * char_w;
			tpos.y = y_entry;
			text_draw_straight( left_col[i], &tpos, &tdims );
		}
		if (i < right_count) {
			llen = (int)strlen( right_col[i] );
			tpos.x = col_right_x + 0.5 * (double)llen * char_w;
			tpos.y = y_entry;
			text_draw_straight( right_col[i], &tpos, &tdims );
		}
	}

	text_post( );

	/* Restore previous matrices */
	glmath_pop_projection( );
	glmath_pop_modelview( );
}


/* Progress callback; keeps viewport updated during presentation */
static void
about_progress_cb( G_GNUC_UNUSED Morph *unused )
{
	globals.need_redraw = TRUE;
}


/* Control routine */
boolean
about( AboutMesg mesg )
{
	switch (mesg) {
		case ABOUT_BEGIN:
		/* Begin the presentation */
		morph_break( &about_part );
		about_part = 0.0;
		morph_full( &about_part, MORPH_LINEAR, 1.0, 2.0, about_progress_cb, about_progress_cb, NULL );
		about_active = TRUE;
		break;

		case ABOUT_END:
		if (!about_active)
			return FALSE;
		/* We now return you to your regularly scheduled program */
		morph_break( &about_part );
		redraw( );
		about_active = FALSE;
		return TRUE;

		case ABOUT_DRAW:
		/* Draw all presentation elements */
		draw_fsv( );
		draw_text( );
		draw_help( );
		draw_shortcuts( );
		break;

		case ABOUT_CHECK:
		/* Return current presentation status */
		return about_active;

		SWITCH_FAIL
	}

	return FALSE;
}


/* end about.c */
