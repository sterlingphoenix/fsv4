/* frameprof.h */

/* Per-frame profiling — temporary instrumentation (Phase 39.1).
 *
 * Off by default. Toggled by F11 in the viewport. When on, the
 * collector accumulates per-bucket time over a rolling window of
 * frames and prints a summary to stderr each window.
 *
 * Zero cost when off (each entry point starts with a single load +
 * branch on `frameprof_active`).
 *
 * Bucket semantics:
 *   FRUSTUM_EXTRACT — frustum_extract() inside *_draw
 *   VBO_REBUILD     — *_rebuild_batch() (no-op when not dirty)
 *   VBO_SOLID       — vbo_batch_draw of the solid batch
 *   VBO_OUTLINE     — vbo_batch_draw_lines of the outline batch
 *   LABEL_WALK      — *_draw_recursive between text_pre and text_post.
 *                     Includes nested TEXT_FLUSH time (overlap is
 *                     reported in the summary).
 *   TEXT_FLUSH      — GL upload + draw inside tmaptext's text_flush
 *   PICK_RENDER     — re-render block inside ogl_color_pick (only
 *                     when the pick FBO is invalidated)
 *
 * Pick fires on mouse events, not on the GtkGLArea render clock, so
 * its frequency may differ from the frame rate. The summary expresses
 * everything as ms/frame for direct comparability; pick numbers
 * therefore represent average pick cost per frame, not per pick. */

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


#ifdef FSV_FRAMEPROF_H
	#error
#endif
#define FSV_FRAMEPROF_H

/* common.h must be included by the translation unit before this
 * header (the project convention — see e.g. scanfs.h, color.h).
 * Provides `boolean`. */


typedef enum {
	FRAMEPROF_FRUSTUM_EXTRACT,
	FRAMEPROF_VBO_REBUILD,
	FRAMEPROF_VBO_SOLID,
	FRAMEPROF_VBO_OUTLINE,
	FRAMEPROF_LABEL_WALK,
	FRAMEPROF_TEXT_FLUSH,
	FRAMEPROF_PICK_RENDER,
	FRAMEPROF_NUM_BUCKETS
} FrameProfBucket;


/* Toggle the profiler on/off. Bound to F11. */
void    frameprof_toggle( void );
boolean frameprof_enabled( void );

/* Frame boundaries (called from ogl_draw). */
void frameprof_frame_begin( void );
void frameprof_frame_end( void );

/* Bucket boundaries. begin/end pairs must nest properly; no
 * re-entrant timing of the same bucket. */
void frameprof_bucket_begin( FrameProfBucket b );
void frameprof_bucket_end( FrameProfBucket b );

/* Counters published from tmaptext. */
void frameprof_text_upload( int bytes );
void frameprof_text_draw_call( void );

/* Counters published from elsewhere. */
void frameprof_pick_invoked( void );        /* call once per ogl_color_pick */
void frameprof_vbo_rebuild_done( void );    /* call once per real rebuild */


/* end frameprof.h */
