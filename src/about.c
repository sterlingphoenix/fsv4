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
		break;

		case ABOUT_CHECK:
		/* Return current presentation status */
		return about_active;

		SWITCH_FAIL
	}

	return FALSE;
}


/* end about.c */
