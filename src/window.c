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
#include "geometry.h"
#include "gui.h"
#include "search.h"
#include "viewport.h"

/* GAction for vis mode and color mode radio (for state changes) */
static GSimpleAction *vis_mode_action = NULL;
static GSimpleAction *color_mode_action = NULL;

/* Bird's-eye view button (on toolbar) */
static GtkWidget *birdseye_view_tbutton_w;

/* Vis mode toolbar toggle buttons */
static GtkWidget *vis_mapv_tbutton_w;
static GtkWidget *vis_treev_tbutton_w;
static GtkWidget *vis_discv_tbutton_w;

/* Color mode toolbar toggle buttons */
static GtkWidget *color_wildcard_tbutton_w;
static GtkWidget *color_nodetype_tbutton_w;
static GtkWidget *color_timestamp_tbutton_w;

/* Scale mode toolbar toggle button */
static GtkWidget *scale_tbutton_w;

/* List of widgets that can be enabled or disabled on the fly */
static GList *sw_widget_list = NULL;

/* Forward declarations for toolbar toggle callbacks */
static void on_vis_mode_toggled( GtkToggleButton *tbutton, gpointer user_data );
static void on_color_mode_toggled( GtkToggleButton *tbutton, gpointer user_data );
static void on_scale_mode_toggled( GtkCheckButton *check, gpointer user_data );

/* Handler for window close button (X).  Quit the application cleanly
 * so that idle/animation callbacks don't fire against destroyed widgets. */
static gboolean
on_close_request( G_GNUC_UNUSED GtkWindow *window, G_GNUC_UNUSED gpointer data )
{
	GApplication *app = g_application_get_default( );
	if (app != NULL)
		g_application_quit( app );
	return TRUE; /* we handled it — don't let GTK destroy piecemeal */
}


/* Main window widget (for busy cursor) */
static GtkWidget *main_window_w_saved;

/* Left and right statusbar widgets */
static GtkWidget *left_statusbar_w;
static GtkWidget *right_statusbar_w;


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
	GtkWidget *toolbar_vbox_w;
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
	int window_width, window_height;

	/* Custom CSS for toolbar toggle buttons and checkbox */
	{
		GtkCssProvider *css = gtk_css_provider_new( );
		gtk_css_provider_load_from_string( css,
			"button.toggle:checked { "
			"  background: alpha(@accent_color, 0.3); "
			"  border-color: @accent_color; "
			"} "
			"checkbutton.toolbar-check { "
			"  padding-left: 0; "
			"  margin-left: -4px; "
			"} "
			"checkbutton.toolbar-check indicator { "
			"  margin-left: 0; "
			"  padding-left: 0; "
			"}" );
		gtk_style_context_add_provider_for_display(
			gdk_display_get_default( ),
			GTK_STYLE_PROVIDER(css),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION );
		g_object_unref( css );
	}

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

	/* Clean shutdown when user clicks the window close button */
	g_signal_connect( main_window_w, "close-request",
		G_CALLBACK(on_close_request), NULL );

	/* Set up actions */
	setup_actions( GTK_WINDOW(main_window_w), fsv_mode );

	/* Main vertical box widget */
	main_vbox_w = gui_vbox_add( main_window_w, 0 );

	/* Toolbar lives at the very top of the window, spanning the full
	 * window width (the icon-based two-row layout will be replaced
	 * with a text-based single-row layout in later steps). */
	toolbar_vbox_w = gui_vbox_add( main_vbox_w, 0 );

	/* Main horizontal paned widget */
	hpaned_w = gui_hpaned_add( main_vbox_w, window_width / 5 );
	gtk_widget_set_vexpand( hpaned_w, TRUE );
	gtk_widget_set_valign( hpaned_w, GTK_ALIGN_FILL );

	/* Vertical box for everything in the left pane */
	left_vbox_w = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
	gtk_paned_set_start_child( GTK_PANED(hpaned_w), left_vbox_w );
	gtk_paned_set_resize_start_child( GTK_PANED(hpaned_w), FALSE );
	gtk_paned_set_shrink_start_child( GTK_PANED(hpaned_w), FALSE );

	/* === Toolbar: FlowBox for wrappable clusters + utility at right === */
	{
		GtkWidget *toolbar_hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
		GtkWidget *flowbox_w;

		gtk_widget_set_margin_start( toolbar_hbox, 2 );
		gtk_widget_set_margin_end( toolbar_hbox, 2 );
		gtk_widget_set_margin_top( toolbar_hbox, 2 );
		gtk_widget_set_margin_bottom( toolbar_hbox, 2 );
		gtk_box_append( GTK_BOX(toolbar_vbox_w), toolbar_hbox );

		/* FlowBox holds the three left-aligned clusters */
		flowbox_w = gtk_flow_box_new( );
		gtk_flow_box_set_selection_mode( GTK_FLOW_BOX(flowbox_w), GTK_SELECTION_NONE );
		gtk_flow_box_set_homogeneous( GTK_FLOW_BOX(flowbox_w), FALSE );
		gtk_flow_box_set_max_children_per_line( GTK_FLOW_BOX(flowbox_w), 3 );
		gtk_flow_box_set_row_spacing( GTK_FLOW_BOX(flowbox_w), 2 );
		gtk_flow_box_set_column_spacing( GTK_FLOW_BOX(flowbox_w), 12 );
		gtk_widget_set_hexpand( flowbox_w, TRUE );
		gtk_widget_set_halign( flowbox_w, GTK_ALIGN_START );
		gtk_box_append( GTK_BOX(toolbar_hbox), flowbox_w );

		/* -- Navigation cluster -- */
		{
			GtkWidget *nav_box = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 4 );

			gui_label_add( nav_box, _("Navigation") );

			button_w = gui_button_add( nav_box, _("Root"), G_CALLBACK(on_cd_root_button_clicked), NULL );
			gtk_widget_set_tooltip_text( button_w, _("Jump to filesystem root") );
			G_LIST_APPEND(sw_widget_list, button_w);

			button_w = gui_button_add( nav_box, _("Back"), G_CALLBACK(on_back_button_clicked), NULL );
			gtk_widget_set_tooltip_text( button_w, _("Go back") );
			G_LIST_APPEND(sw_widget_list, button_w);

			button_w = gui_button_add( nav_box, _("Up"), G_CALLBACK(on_cd_up_button_clicked), NULL );
			gtk_widget_set_tooltip_text( button_w, _("Go up one directory") );
			G_LIST_APPEND(sw_widget_list, button_w);

			button_w = gui_toggle_button_add( nav_box, _("Top-Down"), FALSE, G_CALLBACK(on_birdseye_view_togglebutton_toggled), NULL );
			gtk_widget_set_tooltip_text( button_w, _("Toggle top-down camera") );
			gtk_widget_add_css_class( button_w, "birdseye-toggle" );
			G_LIST_APPEND(sw_widget_list, button_w);
			birdseye_view_tbutton_w = button_w;

			button_w = gui_button_add( nav_box, _("Open\u2026"), G_CALLBACK(on_open_button_clicked), NULL );
			gtk_widget_set_margin_start( button_w, 12 );
			gtk_widget_set_tooltip_text( button_w, _("Open a different root directory") );
			G_LIST_APPEND(sw_widget_list, button_w);

			gtk_flow_box_append( GTK_FLOW_BOX(flowbox_w), nav_box );
		}

		/* -- Visualisation cluster -- */
		{
			GtkWidget *vis_box = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 4 );

			gui_label_add( vis_box, _("Visualisation") );

			button_w = gui_toggle_button_add( vis_box, _("MapV"),
				fsv_mode == FSV_MAPV, G_CALLBACK(on_vis_mode_toggled), "mapv" );
			gtk_widget_set_tooltip_text( button_w, _("Map View") );
			G_LIST_APPEND(sw_widget_list, button_w);
			vis_mapv_tbutton_w = button_w;

			button_w = gui_toggle_button_add( vis_box, _("DiscV"),
				fsv_mode == FSV_DISCV, G_CALLBACK(on_vis_mode_toggled), "discv" );
			gtk_widget_set_tooltip_text( button_w, _("Disc View") );
			gtk_toggle_button_set_group( GTK_TOGGLE_BUTTON(button_w),
				GTK_TOGGLE_BUTTON(vis_mapv_tbutton_w) );
			G_LIST_APPEND(sw_widget_list, button_w);
			vis_discv_tbutton_w = button_w;

			button_w = gui_toggle_button_add( vis_box, _("TreeV"),
				fsv_mode == FSV_TREEV, G_CALLBACK(on_vis_mode_toggled), "treev" );
			gtk_widget_set_tooltip_text( button_w, _("Tree View") );
			gtk_toggle_button_set_group( GTK_TOGGLE_BUTTON(button_w),
				GTK_TOGGLE_BUTTON(vis_mapv_tbutton_w) );
			G_LIST_APPEND(sw_widget_list, button_w);
			vis_treev_tbutton_w = button_w;

			/* Scale mode checkbox — sits right after TreeV */
			{
				GtkWidget *log_check = gtk_check_button_new_with_label( _("Log") );
				gtk_check_button_set_active( GTK_CHECK_BUTTON(log_check), TRUE );
				gtk_widget_add_css_class( log_check, "toolbar-check" );
				gtk_widget_set_tooltip_text( log_check,
					_("Logarithmic vs representative TreeV scale") );
				gtk_widget_set_sensitive( log_check, fsv_mode == FSV_TREEV );
				g_signal_connect( log_check, "toggled",
					G_CALLBACK(on_scale_mode_toggled), NULL );
				gtk_box_append( GTK_BOX(vis_box), log_check );
				scale_tbutton_w = log_check;
			}

			gtk_flow_box_append( GTK_FLOW_BOX(flowbox_w), vis_box );
		}

		/* -- Color Mode cluster -- */
		{
			GtkWidget *color_box = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 4 );

			gui_label_add( color_box, _("Color Mode") );

			button_w = gui_toggle_button_add( color_box, _("Wildcard"),
				TRUE, G_CALLBACK(on_color_mode_toggled), "wildcards" );
			gtk_widget_set_tooltip_text( button_w, _("Color by wildcard pattern") );
			G_LIST_APPEND(sw_widget_list, button_w);
			color_wildcard_tbutton_w = button_w;

			button_w = gui_toggle_button_add( color_box, _("Node Type"),
				FALSE, G_CALLBACK(on_color_mode_toggled), "nodetype" );
			gtk_widget_set_tooltip_text( button_w, _("Color by node type") );
			gtk_toggle_button_set_group( GTK_TOGGLE_BUTTON(button_w),
				GTK_TOGGLE_BUTTON(color_wildcard_tbutton_w) );
			G_LIST_APPEND(sw_widget_list, button_w);
			color_nodetype_tbutton_w = button_w;

			button_w = gui_toggle_button_add( color_box, _("Timestamp"),
				FALSE, G_CALLBACK(on_color_mode_toggled), "timestamp" );
			gtk_widget_set_tooltip_text( button_w, _("Sorted by modification time") );
			gtk_toggle_button_set_group( GTK_TOGGLE_BUTTON(button_w),
				GTK_TOGGLE_BUTTON(color_wildcard_tbutton_w) );
			G_LIST_APPEND(sw_widget_list, button_w);
			color_timestamp_tbutton_w = button_w;

			gtk_flow_box_append( GTK_FLOW_BOX(flowbox_w), color_box );
		}

		/* -- Utility cluster (right-aligned, no label) -- */
		{
			GtkWidget *util_box = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 4 );
			gtk_widget_set_margin_start( util_box, 4 );
			gtk_widget_set_margin_end( util_box, 4 );

			button_w = gui_button_add( util_box, _("Preferences"), G_CALLBACK(on_preferences_button_clicked), NULL );
			gtk_widget_set_tooltip_text( button_w, _("Open preferences") );

			button_w = gui_button_add( util_box, _("Help"), G_CALLBACK(on_about_button_clicked), NULL );
			gtk_widget_set_tooltip_text( button_w, _("About fsv") );

			button_w = gui_button_add( util_box, _("Exit"), G_CALLBACK(on_exit_button_clicked), NULL );
			gtk_widget_set_tooltip_text( button_w, _("Exit fsv") );

			gtk_box_append( GTK_BOX(toolbar_hbox), util_box );
		}
	}

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

	/* Log checkbox: only sensitive in TreeV, regardless of access state */
	if (scale_tbutton_w != NULL)
		gtk_widget_set_sensitive( scale_tbutton_w,
			enabled && globals.fsv_mode == FSV_TREEV );

	/* Show busy cursor when access is disabled */
	if (main_window_w_saved != NULL)
		gui_cursor( main_window_w_saved, enabled ? NULL : "wait" );
}


/* Toolbar vis mode toggle button handler */
static void
on_vis_mode_toggled( GtkToggleButton *tbutton, gpointer user_data )
{
	const char *mode_str = (const char *)user_data;

	/* Only act when button is being activated (not deactivated) */
	if (!gtk_toggle_button_get_active( tbutton ))
		return;

	g_action_change_state( G_ACTION(vis_mode_action),
		g_variant_new_string( mode_str ) );

	/* Log checkbox only relevant in TreeV */
	if (scale_tbutton_w != NULL)
		gtk_widget_set_sensitive( scale_tbutton_w,
			strcmp( mode_str, "treev" ) == 0 );
}

/* Toolbar color mode toggle button handler */
static void
on_color_mode_toggled( GtkToggleButton *tbutton, gpointer user_data )
{
	const char *mode_str = (const char *)user_data;

	if (!gtk_toggle_button_get_active( tbutton ))
		return;

	g_action_change_state( G_ACTION(color_mode_action),
		g_variant_new_string( mode_str ) );
}

/* Toolbar scale mode toggle button handler */
static void
on_scale_mode_toggled( GtkCheckButton *check, G_GNUC_UNUSED gpointer user_data )
{
	boolean logarithmic = gtk_check_button_get_active( check );
	geometry_treev_set_scale_logarithmic( logarithmic );
}


/* Helper: set a toggle button active without triggering its signal.
 * Blocks by function pointer only (ignores data) so it works regardless
 * of what callback_data was used when the signal was connected. */
static void
toggle_button_set_active_blocked( GtkWidget *tbutton_w, GCallback handler, boolean active )
{
	g_signal_handlers_block_matched( G_OBJECT(tbutton_w),
		G_SIGNAL_MATCH_FUNC, 0, 0, NULL, (gpointer)handler, NULL );
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(tbutton_w), active );
	g_signal_handlers_unblock_matched( G_OBJECT(tbutton_w),
		G_SIGNAL_MATCH_FUNC, 0, 0, NULL, (gpointer)handler, NULL );
}


/* Syncs the color mode toolbar buttons and GAction state to the given mode */
void
window_set_color_mode( ColorMode mode )
{
	const char *mode_str;
	GtkWidget *active_w;

	switch (mode) {
		case COLOR_BY_NODETYPE:
		mode_str = "nodetype";
		active_w = color_nodetype_tbutton_w;
		break;

		case COLOR_BY_TIMESTAMP:
		mode_str = "timestamp";
		active_w = color_timestamp_tbutton_w;
		break;

		case COLOR_BY_WPATTERN:
		mode_str = "wildcards";
		active_w = color_wildcard_tbutton_w;
		break;

		SWITCH_FAIL
	}

	/* Sync GAction state (may be NULL during early startup) */
	if (color_mode_action != NULL) {
		g_signal_handlers_block_by_func( color_mode_action, on_color_mode_change, NULL );
		g_simple_action_set_state( color_mode_action, g_variant_new_string( mode_str ) );
		g_signal_handlers_unblock_by_func( color_mode_action, on_color_mode_change, NULL );
	}

	/* Sync toolbar button — GTK4 radio grouping handles deactivating others */
	if (active_w != NULL && !gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(active_w) )) {
		toggle_button_set_active_blocked( active_w,
			G_CALLBACK(on_color_mode_toggled), TRUE );
	}
}


/* Syncs the vis mode toolbar buttons to the given mode.
 * Also enables/disables the scale toggle for TreeV. */
void
window_set_vis_mode( FsvMode mode )
{
	GtkWidget *active_w = NULL;

	switch (mode) {
		case FSV_MAPV:  active_w = vis_mapv_tbutton_w;  break;
		case FSV_TREEV: active_w = vis_treev_tbutton_w; break;
		case FSV_DISCV: active_w = vis_discv_tbutton_w; break;
		default: break;
	}

	if (active_w != NULL && !gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(active_w) )) {
		toggle_button_set_active_blocked( active_w,
			G_CALLBACK(on_vis_mode_toggled), TRUE );
	}

	/* Scale toggle only available in TreeV */
	if (scale_tbutton_w != NULL)
		gtk_widget_set_sensitive( scale_tbutton_w, mode == FSV_TREEV );
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
