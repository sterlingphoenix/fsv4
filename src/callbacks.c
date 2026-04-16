/* callbacks.c */

/* GUI callbacks */

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
#include "callbacks.h"

#include <gtk/gtk.h>

#include "about.h"
#include "camera.h"
#include "color.h"
#include "dialog.h"
#include "fsv.h"


/**** MAIN WINDOW **************************************/


/** Menu actions **/


/* File -> Change root... (GAction callback) */
void
on_file_change_root_activate( G_GNUC_UNUSED GSimpleAction *action, G_GNUC_UNUSED GVariant *parameter, G_GNUC_UNUSED gpointer user_data )
{
	dialog_change_root( );
}


/* "Open..." toolbar button */
void
on_open_button_clicked( G_GNUC_UNUSED GtkButton *button, G_GNUC_UNUSED gpointer user_data )
{
	dialog_change_root( );
}


/* File -> Exit */
void
on_file_exit_activate( G_GNUC_UNUSED GSimpleAction *action, G_GNUC_UNUSED GVariant *parameter, G_GNUC_UNUSED gpointer user_data )
{
	GApplication *app = g_application_get_default( );
	if (app != NULL)
		g_application_quit( app );
	else
		exit( EXIT_SUCCESS );
}


/* Vis mode radio: change-state handler */
void
on_vis_mode_change( GSimpleAction *action, GVariant *value, G_GNUC_UNUSED gpointer user_data )
{
	const char *mode_str;

	g_simple_action_set_state( action, value );
	mode_str = g_variant_get_string( value, NULL );

	if (strcmp( mode_str, "discv" ) == 0) {
		if (globals.fsv_mode != FSV_DISCV)
			fsv_set_mode( FSV_DISCV );
	}
	else if (strcmp( mode_str, "mapv" ) == 0) {
		if (globals.fsv_mode != FSV_MAPV)
			fsv_set_mode( FSV_MAPV );
	}
	else if (strcmp( mode_str, "treev" ) == 0) {
		if (globals.fsv_mode != FSV_TREEV)
			fsv_set_mode( FSV_TREEV );
	}
}


/* Color mode radio: change-state handler */
void
on_color_mode_change( GSimpleAction *action, GVariant *value, G_GNUC_UNUSED gpointer user_data )
{
	const char *mode_str;

	g_simple_action_set_state( action, value );
	mode_str = g_variant_get_string( value, NULL );

	if (strcmp( mode_str, "nodetype" ) == 0)
		color_set_mode( COLOR_BY_NODETYPE );
	else if (strcmp( mode_str, "timestamp" ) == 0)
		color_set_mode( COLOR_BY_TIMESTAMP );
	else if (strcmp( mode_str, "wildcards" ) == 0)
		color_set_mode( COLOR_BY_WPATTERN );
}


/* Colors -> Setup... */
void
on_color_setup_activate( G_GNUC_UNUSED GSimpleAction *action, G_GNUC_UNUSED GVariant *parameter, G_GNUC_UNUSED gpointer user_data )
{
	dialog_color_setup( );
}


/* Help -> About fsv... */
void
on_help_about_fsv_activate( G_GNUC_UNUSED GSimpleAction *action, G_GNUC_UNUSED GVariant *parameter, G_GNUC_UNUSED gpointer user_data )
{
	about( ABOUT_BEGIN );
}


/** Toolbar **/


/* "Back" button */
void
on_back_button_clicked( G_GNUC_UNUSED GtkButton *button, G_GNUC_UNUSED gpointer user_data )
{
	camera_look_at_previous( );
}


/* "Preferences" toolbar button */
void
on_preferences_button_clicked( G_GNUC_UNUSED GtkButton *button, G_GNUC_UNUSED gpointer user_data )
{
	dialog_color_setup( );
}


/* "About" toolbar button */
void
on_about_button_clicked( G_GNUC_UNUSED GtkButton *button, G_GNUC_UNUSED gpointer user_data )
{
	about( ABOUT_BEGIN );
}


/* "Exit" toolbar button */
void
on_exit_button_clicked( G_GNUC_UNUSED GtkButton *button, G_GNUC_UNUSED gpointer user_data )
{
	GApplication *app = g_application_get_default( );
	if (app != NULL)
		g_application_quit( app );
	else
		exit( EXIT_SUCCESS );
}


/* "cd /" button */
void
on_cd_root_button_clicked( G_GNUC_UNUSED GtkButton *button, G_GNUC_UNUSED gpointer user_data )
{
	camera_look_at( root_dnode );
}


/* "cd .." button */
void
on_cd_up_button_clicked( G_GNUC_UNUSED GtkButton *button, G_GNUC_UNUSED gpointer user_data )
{
	if (NODE_IS_DIR(globals.current_node->parent))
		camera_look_at( globals.current_node->parent );
}


/* "Bird's-eye view" toggle button */
void
on_birdseye_view_togglebutton_toggled( GtkToggleButton *togglebutton, G_GNUC_UNUSED gpointer user_data )
{
	camera_birdseye_view( gtk_toggle_button_get_active( togglebutton ) );
}


/* end callbacks.c */
