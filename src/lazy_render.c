/* lazy_render.c */

/* Settings module for the lazy-render limits (Phase 39).
 *
 * Reads/writes its values to the existing fsvrc config file under the
 * [LazyRender] group, alongside the other settings groups managed by
 * fsv.c, color.c, etc. */

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
#include "lazy_render.h"

#include <glib.h>


#define GROUP "LazyRender"
#define KEY_ENABLED       "enabled"
#define KEY_DEPTH         "render_depth"
#define KEY_READAHEAD     "readahead_depth"
#define KEY_OBJECT_LIMIT  "object_limit"


static boolean current_enabled        = LAZY_RENDER_DEFAULT_ENABLED;
static int     current_depth          = LAZY_RENDER_DEFAULT_DEPTH;
static int     current_readahead      = LAZY_RENDER_DEFAULT_READAHEAD;
static int     current_object_limit   = LAZY_RENDER_DEFAULT_OBJECT_LIMIT;


void
lazy_render_load_config( void )
{
	GKeyFile *kf;
	gchar *path;
	GError *err;

	kf = g_key_file_new( );
	path = config_file_path( );

	if (g_key_file_load_from_file( kf, path, G_KEY_FILE_NONE, NULL )) {
		if (g_key_file_has_key( kf, GROUP, KEY_ENABLED, NULL )) {
			err = NULL;
			gboolean v = g_key_file_get_boolean( kf, GROUP, KEY_ENABLED, &err );
			if (err == NULL)
				current_enabled = v ? TRUE : FALSE;
			else
				g_error_free( err );
		}
		if (g_key_file_has_key( kf, GROUP, KEY_DEPTH, NULL )) {
			err = NULL;
			int v = g_key_file_get_integer( kf, GROUP, KEY_DEPTH, &err );
			if (err == NULL && v > 0)
				current_depth = v;
			else if (err != NULL)
				g_error_free( err );
		}
		if (g_key_file_has_key( kf, GROUP, KEY_READAHEAD, NULL )) {
			err = NULL;
			int v = g_key_file_get_integer( kf, GROUP, KEY_READAHEAD, &err );
			if (err == NULL && v >= 0)
				current_readahead = v;
			else if (err != NULL)
				g_error_free( err );
		}
		if (g_key_file_has_key( kf, GROUP, KEY_OBJECT_LIMIT, NULL )) {
			err = NULL;
			int v = g_key_file_get_integer( kf, GROUP, KEY_OBJECT_LIMIT, &err );
			if (err == NULL && v > 0)
				current_object_limit = v;
			else if (err != NULL)
				g_error_free( err );
		}
	}

	g_free( path );
	g_key_file_free( kf );
}


void
lazy_render_write_config( void )
{
	GKeyFile *kf;
	gchar *path, *data, *dir;

	kf = g_key_file_new( );
	path = config_file_path( );

	g_key_file_load_from_file( kf, path, G_KEY_FILE_KEEP_COMMENTS, NULL );

	g_key_file_set_boolean( kf, GROUP, KEY_ENABLED,      current_enabled );
	g_key_file_set_integer( kf, GROUP, KEY_DEPTH,        current_depth );
	g_key_file_set_integer( kf, GROUP, KEY_READAHEAD,    current_readahead );
	g_key_file_set_integer( kf, GROUP, KEY_OBJECT_LIMIT, current_object_limit );

	dir = g_path_get_dirname( path );
	g_mkdir_with_parents( dir, 0755 );
	g_free( dir );

	data = g_key_file_to_data( kf, NULL, NULL );
	g_file_set_contents( path, data, -1, NULL );
	g_free( data );
	g_free( path );
	g_key_file_free( kf );
}


boolean lazy_render_enabled( void )      { return current_enabled; }
int     lazy_render_depth( void )        { return current_depth; }
int     lazy_readahead_depth( void )     { return current_readahead; }
int     lazy_object_limit( void )        { return current_object_limit; }


void
lazy_render_set_enabled( boolean enabled )
{
	current_enabled = enabled ? TRUE : FALSE;
}

void
lazy_render_set_depth( int depth )
{
	if (depth > 0)
		current_depth = depth;
}

void
lazy_render_set_readahead( int readahead )
{
	if (readahead >= 0)
		current_readahead = readahead;
}

void
lazy_render_set_object_limit( int limit )
{
	if (limit > 0)
		current_object_limit = limit;
}


/* end lazy_render.c */
