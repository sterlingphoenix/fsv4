/* tmaptext.h */

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


#ifdef FSV_TMAPTEXT_H
	#error
#endif
#define FSV_TMAPTEXT_H


void text_init( void );
void text_pre( void );
void text_pre_matrix_change( void );
void text_post( void );
void text_set_color( float r, float g, float b );
void text_draw_straight( const char *text, const XYZvec *text_pos, const XYvec *text_max_dims );
void text_draw_straight_rotated( const char *text, const RTZvec *text_pos, const XYvec *text_max_dims );
void text_draw_curved( const char *text, const RTZvec *text_pos, const RTvec *text_max_dims );


/* Label-vertex cache (Phase 39.4 follow-up). Geometry callers wrap
 * their *_draw_recursive label walks like this:
 *
 *   if (!text_cache_replay( )) {
 *     text_cache_begin_emit( );
 *     <walk: text_set_color(), text_draw_*(), etc.>
 *     text_cache_end_emit( );
 *   }
 *
 * The walk's text_add_quad calls store world-space vertices into a
 * persistent GL buffer; subsequent frames replay it with no walk.
 * The cache is invalidated by anything that moves labels (mode
 * switch, scan, expand/collapse step, label-visibility toggle, etc.) */
boolean text_cache_replay( void );
void    text_cache_begin_emit( void );
void    text_cache_end_emit( void );
void    text_cache_invalidate( void );


/* end tmaptext.h */
