/* window.c */

/* Main window definition */

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
#include "window.h"

#include <gtk/gtk.h>

#include "about.h"
#include "callbacks.h"
#include "camera.h"
#include "color.h"
#include "dialog.h"
#include "dirtree.h"
#include "filelist.h"
#include "fsv.h"
#include "gui.h"
#include "search.h"
#include "viewport.h"

/* Toolbar button icons */
#include "xmaps/back.xpm"
#include "xmaps/cd-root.xpm"
#include "xmaps/cd-up.xpm"
#include "xmaps/birdseye_view.xpm"


/* GAction for vis mode and color mode radio (for state changes) */
static GSimpleAction *vis_mode_action = NULL;
static GSimpleAction *color_mode_action = NULL;

/* Bird's-eye view button (on toolbar) */
static GtkWidget *birdseye_view_tbutton_w;

/* List of widgets that can be enabled or disabled on the fly */
static GList *sw_widget_list = NULL;

/* Main window widget (for busy cursor) */
static GtkWidget *main_window_w_saved;

/* Left and right statusbar widgets */
static GtkWidget *left_statusbar_w;
static GtkWidget *right_statusbar_w;


/* Build the GMenu model for the menu bar */
static GMenuModel *
build_menu_model( void )
{
	GMenu *menubar;
	GMenu *menu;
	GMenu *section;

	menubar = g_menu_new( );

	/* File menu */
	menu = g_menu_new( );
	g_menu_append( menu, _("Change root..."), "win.change-root" );
	g_menu_append( menu, _("Save settings"), "win.save-settings" );
	section = g_menu_new( );
	g_menu_append( section, _("Exit"), "win.exit" );
	g_menu_append_section( menu, NULL, G_MENU_MODEL(section) );
	g_object_unref( section );
	g_menu_append_submenu( menubar, _("File"), G_MENU_MODEL(menu) );
	g_object_unref( menu );

	/* Vis menu */
	menu = g_menu_new( );
	g_menu_append( menu, _("DiscV"), "win.vis-mode::discv" );
	g_menu_append( menu, _("MapV"), "win.vis-mode::mapv" );
	g_menu_append( menu, _("TreeV"), "win.vis-mode::treev" );
	g_menu_append_submenu( menubar, _("Vis"), G_MENU_MODEL(menu) );
	g_object_unref( menu );

	/* Colors menu */
	menu = g_menu_new( );
	section = g_menu_new( );
	g_menu_append( section, _("By wildcards"), "win.color-mode::wildcards" );
	g_menu_append( section, _("By node type"), "win.color-mode::nodetype" );
	g_menu_append( section, _("By timestamp"), "win.color-mode::timestamp" );
	g_menu_append_section( menu, NULL, G_MENU_MODEL(section) );
	g_object_unref( section );
	section = g_menu_new( );
	g_menu_append( section, _("Setup..."), "win.color-setup" );
	g_menu_append_section( menu, NULL, G_MENU_MODEL(section) );
	g_object_unref( section );
	g_menu_append_submenu( menubar, _("Colors"), G_MENU_MODEL(menu) );
	g_object_unref( menu );

	/* Help menu */
	menu = g_menu_new( );
	g_menu_append( menu, _("About"), "win.about" );
	g_menu_append_submenu( menubar, _("Help"), G_MENU_MODEL(menu) );
	g_object_unref( menu );

	return G_MENU_MODEL(menubar);
}


/* Set up window actions (GAction entries) */
static void
setup_actions( GtkWindow *window, FsvMode fsv_mode )
{
	GSimpleActionGroup *group;
	GSimpleAction *action;
	const char *vis_modes[] = { "discv", "mapv", "treev" };

	group = g_simple_action_group_new( );

	/* Simple actions */
	action = g_simple_action_new( "change-root", NULL );
	g_signal_connect( action, "activate", G_CALLBACK(on_file_change_root_activate), NULL );
	g_action_map_add_action( G_ACTION_MAP(group), G_ACTION(action) );
	g_object_unref( action );

	action = g_simple_action_new( "save-settings", NULL );
	g_signal_connect( action, "activate", G_CALLBACK(on_file_save_settings_activate), NULL );
	g_action_map_add_action( G_ACTION_MAP(group), G_ACTION(action) );
	g_object_unref( action );

	action = g_simple_action_new( "exit", NULL );
	g_signal_connect( action, "activate", G_CALLBACK(on_file_exit_activate), NULL );
	g_action_map_add_action( G_ACTION_MAP(group), G_ACTION(action) );
	g_object_unref( action );

	action = g_simple_action_new( "color-setup", NULL );
	g_signal_connect( action, "activate", G_CALLBACK(on_color_setup_activate), NULL );
	g_action_map_add_action( G_ACTION_MAP(group), G_ACTION(action) );
	g_object_unref( action );

	action = g_simple_action_new( "about", NULL );
	g_signal_connect( action, "activate", G_CALLBACK(on_help_about_fsv_activate), NULL );
	g_action_map_add_action( G_ACTION_MAP(group), G_ACTION(action) );
	g_object_unref( action );

	/* Vis mode radio action */
	vis_mode_action = g_simple_action_new_stateful( "vis-mode",
		G_VARIANT_TYPE_STRING, g_variant_new_string( vis_modes[fsv_mode] ) );
	g_signal_connect( vis_mode_action, "change-state", G_CALLBACK(on_vis_mode_change), NULL );
	g_action_map_add_action( G_ACTION_MAP(group), G_ACTION(vis_mode_action) );

	/* Color mode radio action (default: wildcards) */
	color_mode_action = g_simple_action_new_stateful( "color-mode",
		G_VARIANT_TYPE_STRING, g_variant_new_string( "wildcards" ) );
	g_signal_connect( color_mode_action, "change-state", G_CALLBACK(on_color_mode_change), NULL );
	g_action_map_add_action( G_ACTION_MAP(group), G_ACTION(color_mode_action) );

	gtk_widget_insert_action_group( GTK_WIDGET(window), "win", G_ACTION_GROUP(group) );
	g_object_unref( group );
}


/* Constructs the main program window */
void
window_init( GtkApplication *app, FsvMode fsv_mode )
{
	GtkWidget *main_window_w;
	GtkWidget *main_vbox_w;
	GtkWidget *menu_bar_w;
	GtkWidget *hpaned_w;
	GtkWidget *left_vbox_w;
	GtkWidget *right_vbox_w;
	GtkWidget *hbox_w;
	GtkWidget *button_w;
	GtkWidget *frame_w;
	GtkWidget *dir_ctree_w;
	GtkWidget *file_clist_w;
	GtkWidget *gl_area_w;
	GtkWidget *x_scrollbar_w;
	GtkWidget *y_scrollbar_w;
	GtkWidget *search_entry_w;
	GtkWidget *search_next_button_w;
	GMenuModel *menu_model;
	int window_width, window_height;

	/* Main window widget */
	main_window_w = gtk_application_window_new( app );
	gtk_window_set_title( GTK_WINDOW(main_window_w), "fsv" );
	gtk_window_set_resizable( GTK_WINDOW(main_window_w), TRUE );
	{
		GdkDisplay *display = gdk_display_get_default( );
		GListModel *monitors = gdk_display_get_monitors( display );
		GdkMonitor *monitor = g_list_model_get_item( monitors, 0 );
		GdkRectangle geom;
		gdk_monitor_get_geometry( monitor, &geom );
		window_width = 3 * geom.width / 4;
		g_object_unref( monitor );
	}
	window_height = 2584 * window_width / 4181;
	gtk_window_set_default_size( GTK_WINDOW(main_window_w), window_width, window_height );

	/* Set up actions */
	setup_actions( GTK_WINDOW(main_window_w), fsv_mode );

	/* Main vertical box widget */
	main_vbox_w = gui_vbox_add( main_window_w, 0 );

	/* Build menu bar from GMenu model */
	menu_model = build_menu_model( );
	menu_bar_w = gtk_popover_menu_bar_new_from_model( menu_model );
	g_object_unref( menu_model );
	gtk_box_append( GTK_BOX(main_vbox_w), menu_bar_w );

	/* Main horizontal paned widget */
	hpaned_w = gui_hpaned_add( main_vbox_w, window_width / 5 );
	gtk_widget_set_vexpand( hpaned_w, TRUE );
	gtk_widget_set_valign( hpaned_w, GTK_ALIGN_FILL );

	/* Vertical box for everything in the left pane */
	left_vbox_w = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
	gtk_paned_set_start_child( GTK_PANED(hpaned_w), left_vbox_w );
	gtk_paned_set_resize_start_child( GTK_PANED(hpaned_w), FALSE );
	gtk_paned_set_shrink_start_child( GTK_PANED(hpaned_w), TRUE );

	/* Horizontal box for toolbar buttons */
	hbox_w = gui_hbox_add( left_vbox_w, 2 );

	/* "back" button */
	button_w = gui_button_add( hbox_w, NULL, G_CALLBACK(on_back_button_clicked), NULL );
	gui_pixmap_xpm_add( button_w, back_xpm );
	G_LIST_APPEND(sw_widget_list, button_w);
	/* "cd /" button */
	button_w = gui_button_add( hbox_w, NULL, G_CALLBACK(on_cd_root_button_clicked), NULL );
	gui_pixmap_xpm_add( button_w, cd_root_xpm );
	G_LIST_APPEND(sw_widget_list, button_w);
	/* "cd .." button */
	button_w = gui_button_add( hbox_w, NULL, G_CALLBACK(on_cd_up_button_clicked), NULL );
	gui_pixmap_xpm_add( button_w, cd_up_xpm );
	G_LIST_APPEND(sw_widget_list, button_w);
	/* "bird's-eye view" toggle button */
	button_w = gui_toggle_button_add( hbox_w, NULL, FALSE, G_CALLBACK(on_birdseye_view_togglebutton_toggled), NULL );
	gui_pixmap_xpm_add( button_w, birdseye_view_xpm );
	G_LIST_APPEND(sw_widget_list, button_w);
	birdseye_view_tbutton_w = button_w;

	/* Search bar */
	hbox_w = gui_hbox_add( left_vbox_w, 2 );
	gui_box_set_packing( hbox_w, EXPAND, FILL, AT_START );
	search_entry_w = gui_entry_add( hbox_w, NULL, NULL, NULL );
	search_next_button_w = gui_button_add( hbox_w, _("Next"), NULL, NULL );
	gtk_widget_set_sensitive( search_next_button_w, FALSE );
	search_pass_widgets( search_entry_w, search_next_button_w );

	/* Frame to encase the directory tree / file list */
	frame_w = gui_frame_add( left_vbox_w, NULL );
	gtk_widget_set_vexpand( frame_w, TRUE );
	gtk_widget_set_valign( frame_w, GTK_ALIGN_FILL );

	/* Vertical paned widget for directory tree / file list */
	GtkWidget *vpaned_w = gui_vpaned_add( frame_w, window_height / 3 );

	/* Directory tree goes in start pane */
	dir_ctree_w = gui_ctree_add( NULL );
	gtk_paned_set_start_child( GTK_PANED(vpaned_w), gtk_widget_get_parent( dir_ctree_w ) );
	gtk_paned_set_resize_start_child( GTK_PANED(vpaned_w), TRUE );
	gtk_paned_set_shrink_start_child( GTK_PANED(vpaned_w), TRUE );

	/* File list goes in end pane */
	file_clist_w = gui_clist_add( NULL, 3, NULL );
	gtk_paned_set_end_child( GTK_PANED(vpaned_w), gtk_widget_get_parent( file_clist_w ) );
	gtk_paned_set_resize_end_child( GTK_PANED(vpaned_w), TRUE );
	gtk_paned_set_shrink_end_child( GTK_PANED(vpaned_w), TRUE );

	/* Left statusbar */
	left_statusbar_w = gui_statusbar_add( left_vbox_w );

	/* Vertical box for everything in the right pane */
	right_vbox_w = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
	gtk_paned_set_end_child( GTK_PANED(hpaned_w), right_vbox_w );
	gtk_paned_set_resize_end_child( GTK_PANED(hpaned_w), TRUE );
	gtk_paned_set_shrink_end_child( GTK_PANED(hpaned_w), TRUE );

	/* Horizontal box for viewport and y-scrollbar */
	hbox_w = gui_hbox_add( right_vbox_w, 0 );
	gui_widget_packing( hbox_w, EXPAND, FILL, AT_START );

	/* Main viewport (OpenGL area widget) */
	gl_area_w = gui_gl_area_add( hbox_w );

	/* Set up GTK4 event controllers for the viewport */
	viewport_setup_controllers( gl_area_w );

	/* y-scrollbar */
	y_scrollbar_w = gui_vscrollbar_add( hbox_w, NULL );
	G_LIST_APPEND(sw_widget_list, y_scrollbar_w);
	/* x-scrollbar */
	x_scrollbar_w = gui_hscrollbar_add( right_vbox_w, NULL );
	G_LIST_APPEND(sw_widget_list, x_scrollbar_w);

	/* Right statusbar */
	right_statusbar_w = gui_statusbar_add( right_vbox_w );

	/* Save main window reference for busy cursor */
	main_window_w_saved = main_window_w;

	/* Send out the widgets to their respective modules */
	dialog_pass_main_window_widget( main_window_w );
	dirtree_pass_widget( dir_ctree_w );
	filelist_pass_widget( file_clist_w );
	camera_pass_scrollbar_widgets( x_scrollbar_w, y_scrollbar_w );

	/* Set up keyboard shortcuts via application */
	{
		const char *change_root_accels[] = { "<Control>n", NULL };
		const char *exit_accels[] = { "<Control>q", NULL };
		gtk_application_set_accels_for_action( app, "win.change-root", change_root_accels );
		gtk_application_set_accels_for_action( app, "win.exit", exit_accels );
	}

	/* Showtime! */
	gtk_window_present( GTK_WINDOW(main_window_w) );
}


/* This enables/disables the switchable widgets */
void
window_set_access( boolean enabled )
{
	GtkWidget *widget;
	GList *llink;

	llink = sw_widget_list;
	while (llink != NULL) {
		widget = (GtkWidget *)llink->data;

		gtk_widget_set_sensitive( widget, enabled );

		llink = llink->next;
	}

	/* Show busy cursor when access is disabled */
	if (main_window_w_saved != NULL)
		gui_cursor( main_window_w_saved, enabled ? NULL : "wait" );
}


/* Resets the Color radio menu to the given mode (via GAction state) */
void
window_set_color_mode( ColorMode mode )
{
	const char *mode_str;

	switch (mode) {
		case COLOR_BY_NODETYPE:
		mode_str = "nodetype";
		break;

		case COLOR_BY_TIMESTAMP:
		mode_str = "timestamp";
		break;

		case COLOR_BY_WPATTERN:
		mode_str = "wildcards";
		break;

		SWITCH_FAIL
	}

	/* Block the handler, change state, unblock */
	g_signal_handlers_block_by_func( color_mode_action, on_color_mode_change, NULL );
	g_simple_action_set_state( color_mode_action, g_variant_new_string( mode_str ) );
	g_signal_handlers_unblock_by_func( color_mode_action, on_color_mode_change, NULL );
}


/* Pops out the bird's-eye view toggle button */
void
window_birdseye_view_off( void )
{
	g_signal_handlers_block_by_func( G_OBJECT(birdseye_view_tbutton_w), G_CALLBACK(on_birdseye_view_togglebutton_toggled), NULL );
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(birdseye_view_tbutton_w), FALSE );
	g_signal_handlers_unblock_by_func( G_OBJECT(birdseye_view_tbutton_w), G_CALLBACK(on_birdseye_view_togglebutton_toggled), NULL );
}


/* Displays a message in one of the statusbars */
void
window_statusbar( StatusBarID sb_id, const char *message )
{
	switch (sb_id) {
		case SB_LEFT:
		gui_statusbar_message( left_statusbar_w, message );
		break;

		case SB_RIGHT:
		gui_statusbar_message( right_statusbar_w, message );
		break;

		SWITCH_FAIL
	}
}


/* end window.c */
