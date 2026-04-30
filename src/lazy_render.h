/* lazy_render.h */

/* Settings for the lazy-render limits (Phase 39).
 *
 * Three knobs control how much of a deep filesystem tree is rendered
 * and scanned at any one time:
 *
 *   render_depth     N — max levels rendered below an anchor
 *   readahead_depth  R — extra levels scanned past the visible frontier
 *   object_limit     M — max total nodes per Expand All
 *
 * Plus an enable toggle. Defaults are tuned so that small/medium trees
 * are essentially unaffected; very large trees (~10^6 entries) become
 * usable. */

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


#ifdef FSV_LAZY_RENDER_H
	#error
#endif
#define FSV_LAZY_RENDER_H


#define LAZY_RENDER_DEFAULT_ENABLED       TRUE
#define LAZY_RENDER_DEFAULT_DEPTH         7
#define LAZY_RENDER_DEFAULT_READAHEAD     3
#define LAZY_RENDER_DEFAULT_OBJECT_LIMIT  250000


/* Load values from the config file. Call once at startup. */
void lazy_render_load_config( void );

/* Persist current values to the config file. */
void lazy_render_write_config( void );

/* Accessors */
boolean lazy_render_enabled( void );
int     lazy_render_depth( void );
int     lazy_readahead_depth( void );
int     lazy_object_limit( void );

/* Mutators (in-memory only; call lazy_render_write_config() to persist) */
void lazy_render_set_enabled( boolean enabled );
void lazy_render_set_depth( int depth );
void lazy_render_set_readahead( int readahead );
void lazy_render_set_object_limit( int limit );


/* end lazy_render.h */
