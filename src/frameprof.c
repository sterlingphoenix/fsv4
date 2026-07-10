/* frameprof.c */

/* See frameprof.h for design rationale. */

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
#include "frameprof.h"
#include "tmaptext.h" /* text_cache_stats( ) */


/* How many frames per summary print. */
#define FRAMEPROF_WINDOW 60

/* Also flush the window when this much wall time has passed with at
 * least one frame in it — otherwise very slow stretches (1-2 fps
 * expand-all) never reach FRAMEPROF_WINDOW frames and print nothing */
#define FRAMEPROF_MAX_WINDOW_US (2 * G_USEC_PER_SEC)

/* Monotonic time of the current window's start */
static gint64 window_start_us = 0;

static boolean active = FALSE;

/* Per-bucket accumulated time (microseconds) and the open-bucket
 * start times. bucket_open[b] >= 0 means a begin() is pending an end(). */
static gint64 bucket_us_total[FRAMEPROF_NUM_BUCKETS];
static gint64 bucket_open[FRAMEPROF_NUM_BUCKETS];

/* Frame totals. */
static gint64 frame_start_us = 0;
static gint64 frame_total_us = 0;
static int    frames = 0;

/* Side counters. text_bytes_uploaded must be 64-bit — heavy scenes
 * push GB/window through GL_STREAM_DRAW and overflow `int`. */
static gint64 text_bytes_uploaded = 0;
static int    text_draw_calls = 0;
static int    pick_count = 0;
static int    vbo_rebuilds = 0;


static const char *bucket_names[FRAMEPROF_NUM_BUCKETS] = {
	"frustum_extract",
	"vbo_rebuild    ",
	"vbo_solid      ",
	"vbo_outline    ",
	"label_walk     ",
	"text_flush     ",
	"pick_render    "
};


static void
reset_window( void )
{
	int i;

	for (i = 0; i < FRAMEPROF_NUM_BUCKETS; i++) {
		bucket_us_total[i] = 0;
		bucket_open[i] = -1;
	}
	frame_total_us = 0;
	frames = 0;
	text_bytes_uploaded = 0;
	text_draw_calls = 0;
	pick_count = 0;
	vbo_rebuilds = 0;
	window_start_us = g_get_monotonic_time( );
}


void
frameprof_toggle( void )
{
	active = !active;
	if (active) {
		reset_window( );
		g_printerr( "[frameprof] enabled — window=%d frames\n",
		            FRAMEPROF_WINDOW );
	}
	else {
		g_printerr( "[frameprof] disabled\n" );
	}
}


boolean
frameprof_enabled( void )
{
	return active;
}


void
frameprof_frame_begin( void )
{
	if (!active)
		return;
	frame_start_us = g_get_monotonic_time( );
}


static void
print_summary( void )
{
	gint64 accounted_us = 0;
	gint64 other_us;
	int i;

	g_printerr( "[frameprof] %d frames, avg %.2f ms/frame "
	            "(%.1f fps target)\n",
	            frames,
	            (double)frame_total_us / 1000.0 / (double)frames,
	            frames > 0 && frame_total_us > 0
	                ? 1000000.0 * (double)frames / (double)frame_total_us
	                : 0.0 );
	for (i = 0; i < FRAMEPROF_NUM_BUCKETS; i++) {
		double per_frame_ms =
			(double)bucket_us_total[i] / 1000.0 / (double)frames;
		switch (i) {
			case FRAMEPROF_VBO_REBUILD:
			g_printerr( "  %s : %6.2f ms/frame  (%d rebuilds)\n",
			            bucket_names[i], per_frame_ms, vbo_rebuilds );
			break;

			case FRAMEPROF_TEXT_FLUSH:
			g_printerr( "  %s : %6.2f ms/frame  "
			            "(%.1f draws/frame, %.1f KB/frame)\n",
			            bucket_names[i], per_frame_ms,
			            (double)text_draw_calls / (double)frames,
			            (double)text_bytes_uploaded / 1024.0
			                / (double)frames );
			break;

			case FRAMEPROF_PICK_RENDER:
			g_printerr( "  %s : %6.2f ms/frame  (%d picks)\n",
			            bucket_names[i], per_frame_ms, pick_count );
			break;

			default:
			g_printerr( "  %s : %6.2f ms/frame\n",
			            bucket_names[i], per_frame_ms );
			break;
		}
		/* LABEL_WALK already contains TEXT_FLUSH time (text_flush
		 * is called from inside the label walk); don't count it
		 * twice in the "accounted" rollup. */
		if (i != FRAMEPROF_TEXT_FLUSH)
			accounted_us += bucket_us_total[i];
	}

	other_us = frame_total_us - accounted_us;
	g_printerr( "  other          : %6.2f ms/frame\n",
	            (double)other_us / 1000.0 / (double)frames );

	/* Label cache behavior this window (Phase 46.C) */
	{
		int hard, soft, rebuilds;
		text_cache_stats( &hard, &soft, &rebuilds );
		g_printerr( "  label cache    : %d rebuilds "
		            "(%d hard invalidations, %d soft stalings)\n",
		            rebuilds, hard, soft );
	}
}


void
frameprof_frame_end( void )
{
	if (!active)
		return;

	gint64 now = g_get_monotonic_time( );

	frame_total_us += now - frame_start_us;
	frames++;

	if (frames >= FRAMEPROF_WINDOW
	    || (now - window_start_us) >= FRAMEPROF_MAX_WINDOW_US) {
		print_summary( );
		reset_window( );
	}
}


void
frameprof_bucket_begin( FrameProfBucket b )
{
	if (!active)
		return;
	bucket_open[b] = g_get_monotonic_time( );
}


void
frameprof_bucket_end( FrameProfBucket b )
{
	if (!active)
		return;
	if (bucket_open[b] < 0)
		return; /* unmatched end — silently ignore */
	bucket_us_total[b] += g_get_monotonic_time( ) - bucket_open[b];
	bucket_open[b] = -1;
}


void
frameprof_text_upload( gint64 bytes )
{
	if (!active)
		return;
	text_bytes_uploaded += bytes;
}


void
frameprof_text_draw_call( void )
{
	if (!active)
		return;
	text_draw_calls++;
}


void
frameprof_pick_invoked( void )
{
	if (!active)
		return;
	pick_count++;
}


void
frameprof_vbo_rebuild_done( void )
{
	if (!active)
		return;
	vbo_rebuilds++;
}


/* end frameprof.c */
