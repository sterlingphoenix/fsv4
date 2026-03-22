/* dialog.c */

/* Dialog windows */

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
#include "dialog.h"

#include <time.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include "animation.h"
#include "camera.h"
#include "colexp.h"
#include "color.h"
#include "dirtree.h" /* dirtree_entry_expanded( ) */
#include "filelist.h" /* dir_contents_list_add( ) */
#include "fsv.h"
#include "geometry.h"
#include "gui.h"
#include "window.h"

/* Main window widget */
static GtkWidget *main_window_w;


/* Correspondence from window_init( ) */
void
dialog_pass_main_window_widget( GtkWidget *window_w )
{
	main_window_w = window_w;
}


/* Callback to close a dialog window */
static void
close_cb( G_GNUC_UNUSED GtkWidget *unused, GtkWidget *window_w )
{
	gtk_window_destroy( GTK_WINDOW(window_w) );
}



/**** File -> Change root... ****/

/* Callback for the OK button */
static void
change_root_cb( const char *dir )
{
	if (globals.fsv_mode != FSV_SPLASH)
		fsv_load( dir );
}


void
dialog_change_root( void )
{
	const char *root_name;
	char *dir;

	/* Build initial directory name (with trailing slash) */
	root_name = node_absname( root_dnode );
	dir = NEW_ARRAY(char, strlen( root_name ) + 2);
	strcpy( dir, root_name );
	strcat( dir, "/" );

	/* On networked filesystems, the file selection window can be
	 * sloooow in coming up (as each directory component in the default
	 * location has to be stat( )'ed-- takes >10 sec on MIT AFS!) */
	gui_cursor( main_window_w, "wait" );
	gui_update( );

	gui_filesel_window( _("Change Root Directory"), dir, G_CALLBACK(change_root_cb), NULL, TRUE );
	xfree( dir );

	gui_cursor( main_window_w, NULL );
	gui_update( );
}


/**** Colors -> Setup... ****/

static struct ColorSetupDialog {
	/* Scratch copy of color configuration */
	struct ColorConfig color_config;

	/* Top-level notebook (General / Colors) */
	GtkWidget *top_notebook_w;

	/* Color sub-notebook widget (each page dedicated to a color mode) */
	GtkWidget *notebook_w;

	/* General tab widgets */
	struct {
		GtkWidget *remember_check_w;
		GtkWidget *vis_mode_dropdown_w;
		GtkWidget *color_mode_dropdown_w;
		GtkWidget *scale_mode_dropdown_w;
	} general;

	/* Date/time configuration page */
	struct {
		/* Date edit widgets */
		GtkWidget *old_dateedit_w;
		GtkWidget *new_dateedit_w;

		/* Spectrum preview widget */
		GtkWidget *spectrum_preview_w;

		/* Color pickers for interpolated spectrum setup */
		GtkWidget *old_colorpicker_w;
		GtkWidget *new_colorpicker_w;
	} time;

	/* Wildcard pattern configuration page */
	struct {
		/* Container that holds all group rows */
		GtkWidget *groups_box_w;
		/* Scrolled window containing groups_box_w */
		GtkWidget *scroll_w;
		/* Default color picker */
		GtkWidget *default_colorpicker_w;
		/* Size groups for column alignment between header and rows */
		GtkSizeGroup *name_sg;
		GtkSizeGroup *color_sg;
	} wpattern;
} csdialog;


/* Forward declarations */
static void csdialog_wpattern_rebuild_ui( void );
static void csdialog_wpattern_rebuild_ui_full( boolean focus_last );


/* Callback for the node type color pickers */
static void
csdialog_node_type_color_picker_cb( RGBcolor *picked_color, RGBcolor *node_type_color )
{
	/* node_type_color points to the appropriate member of
	 * csdialog.color_config.by_nodetype.colors[] */
	node_type_color->r = picked_color->r;
	node_type_color->g = picked_color->g;
	node_type_color->b = picked_color->b;
}


/* Callback for the date edit widgets on the "By date/time" page */
static void
csdialog_time_edit_cb( GtkWidget *dateedit_w )
{
	time_t old_time, new_time;
	time_t cur_time;

	old_time = gui_dateedit_get_time( csdialog.time.old_dateedit_w );
	new_time = gui_dateedit_get_time( csdialog.time.new_dateedit_w );
	cur_time = time( NULL );

	/* Check that neither time is in the future */
	if (difftime( cur_time, new_time ) < 0.0)
		new_time = cur_time;
	if (difftime( cur_time, old_time ) < 0.0)
		old_time = cur_time;

	/* Check that old time is at least one minute before new time */
	if (difftime( new_time, old_time ) < 60.0) {
		if (dateedit_w == csdialog.time.old_dateedit_w)
			new_time = old_time + (time_t)60;
		else if (dateedit_w == csdialog.time.new_dateedit_w)
			old_time = new_time - (time_t)60;
		else {
			g_assert_not_reached( );
			return;
		}
	}

	/* Reset old and new times */
	g_signal_handlers_block_by_func( G_OBJECT(csdialog.time.old_dateedit_w), csdialog_time_edit_cb, NULL );
	g_signal_handlers_block_by_func( G_OBJECT(csdialog.time.new_dateedit_w), csdialog_time_edit_cb, NULL );
	gui_dateedit_set_time( csdialog.time.old_dateedit_w, old_time );
	gui_dateedit_set_time( csdialog.time.new_dateedit_w, new_time );
	g_signal_handlers_unblock_by_func( G_OBJECT(csdialog.time.old_dateedit_w), csdialog_time_edit_cb, NULL );
	g_signal_handlers_unblock_by_func( G_OBJECT(csdialog.time.new_dateedit_w), csdialog_time_edit_cb, NULL );

	csdialog.color_config.by_timestamp.old_time = old_time;
	csdialog.color_config.by_timestamp.new_time = new_time;
}


/* Callback for the "Color by:" timestamp combo box */
static void
csdialog_time_timestamp_option_menu_cb( GtkWidget *dropdown_w )
{
	guint active = gtk_drop_down_get_selected( GTK_DROP_DOWN(dropdown_w) );

	/* Drop-down indices match TimeStampType enum values */
	csdialog.color_config.by_timestamp.timestamp_type = (TimeStampType)active;
}


/* This is the spectrum function used to paint the preview widget */
static RGBcolor
csdialog_time_spectrum_func( double x )
{
	RGBcolor *boundary_colors[2];
	void *data = NULL;

	if (csdialog.color_config.by_timestamp.spectrum_type == SPECTRUM_GRADIENT) {
		boundary_colors[0] = &csdialog.color_config.by_timestamp.old_color;
		boundary_colors[1] = &csdialog.color_config.by_timestamp.new_color;
		data = boundary_colors;
	}

	return color_spectrum_color( csdialog.color_config.by_timestamp.spectrum_type, x, data );
}


/* Helper function for spectrum_option_menu_cb( ). This enables or
 * disables the color picker buttons as necessary */
static void
csdialog_time_color_picker_set_access( boolean enabled )
{
	RGBcolor disabled_color;
	RGBcolor *color;

	gtk_widget_set_sensitive( csdialog.time.old_colorpicker_w, enabled );
	gtk_widget_set_sensitive( csdialog.time.new_colorpicker_w, enabled );

	/* Change the color pickers' colors, as simply enabling/disabling
	 * them isn't enough to make the state change obvious */
	if (enabled) {
		color = &csdialog.color_config.by_timestamp.old_color;
		gui_colorpicker_set_color( csdialog.time.old_colorpicker_w, color );
		color = &csdialog.color_config.by_timestamp.new_color;
		gui_colorpicker_set_color( csdialog.time.new_colorpicker_w, color );
	}
	else {
		/* Neutral gray to indicate disabled state */
		disabled_color.r = 0.75f;
		disabled_color.g = 0.75f;
		disabled_color.b = 0.75f;
		gui_colorpicker_set_color( csdialog.time.old_colorpicker_w, &disabled_color );
		gui_colorpicker_set_color( csdialog.time.new_colorpicker_w, &disabled_color );
	}
}


/* Callback for the spectrum type combo box */
static void
csdialog_time_spectrum_option_menu_cb( GtkWidget *dropdown_w )
{
	guint active = gtk_drop_down_get_selected( GTK_DROP_DOWN(dropdown_w) );
	SpectrumType type = (SpectrumType)active;

	/* Set new spectrum type and draw it */
	csdialog.color_config.by_timestamp.spectrum_type = type;
	gui_preview_spectrum( csdialog.time.spectrum_preview_w, csdialog_time_spectrum_func );
	csdialog_time_color_picker_set_access( type == SPECTRUM_GRADIENT );
}


/* Callback for the spectrum's color picker buttons */
static void
csdialog_time_color_picker_cb( RGBcolor *picked_color, RGBcolor *end_color )
{
	/* end_color points to either old_color or new_color in
	 * csdialog.color_config.by_timestamp */
	end_color->r = picked_color->r;
	end_color->g = picked_color->g;
	end_color->b = picked_color->b;

	/* Redraw spectrum */
	gui_preview_spectrum( csdialog.time.spectrum_preview_w, csdialog_time_spectrum_func );
}


/**** Wildcard pattern editor ****/

/* Helper: join a WPatternGroup's wp_list into a single semicolon-delimited
 * string for display in the patterns text entry */
static char *
wpgroup_patterns_to_string( struct WPatternGroup *wpgroup )
{
	GString *str = g_string_new( NULL );
	GList *llink = wpgroup->wp_list;

	while (llink != NULL) {
		if (str->len > 0)
			g_string_append( str, "; " );
		g_string_append( str, (const char *)llink->data );
		llink = llink->next;
	}

	return g_string_free( str, FALSE );
}


/* Helper: parse a semicolon/comma/space-delimited patterns string back
 * into a GList of individual pattern strings */
static GList *
string_to_patterns( const char *text )
{
	GList *list = NULL;
	gchar **tokens;
	int i;

	/* Split on semicolons, commas, and spaces */
	tokens = g_regex_split_simple( "[;,\\s]+", text, 0, 0 );
	if (tokens == NULL)
		return NULL;

	for (i = 0; tokens[i] != NULL; i++) {
		gchar *t = g_strstrip( tokens[i] );
		if (t[0] != '\0')
			G_LIST_APPEND(list, xstrdup( t ));
	}
	g_strfreev( tokens );

	return list;
}


/* Callback for the wildcard group color picker */
static void
csdialog_wpgroup_color_cb( RGBcolor *picked_color, RGBcolor *group_color )
{
	group_color->r = picked_color->r;
	group_color->g = picked_color->g;
	group_color->b = picked_color->b;
}


/* Callback for the default wildcard color picker */
static void
csdialog_wp_default_color_cb( RGBcolor *picked_color, G_GNUC_UNUSED RGBcolor *unused )
{
	csdialog.color_config.by_wpattern.default_color.r = picked_color->r;
	csdialog.color_config.by_wpattern.default_color.g = picked_color->g;
	csdialog.color_config.by_wpattern.default_color.b = picked_color->b;
}


/* Callback for the "Remove" button on a wildcard group row */
static void
csdialog_wpgroup_remove_cb( G_GNUC_UNUSED GtkButton *button, struct WPatternGroup *wpgroup )
{
	GList *wp_llink;

	/* Free all patterns in this group */
	wp_llink = wpgroup->wp_list;
	while (wp_llink != NULL) {
		xfree( wp_llink->data );
		wp_llink = wp_llink->next;
	}
	g_list_free( wpgroup->wp_list );

	/* Remove from list and free */
	G_LIST_REMOVE(csdialog.color_config.by_wpattern.wpgroup_list, wpgroup);
	xfree( wpgroup->name );
	xfree( wpgroup );

	/* Rebuild the UI */
	csdialog_wpattern_rebuild_ui( );
}


/* Callback for the "Add Group" button */
static void
csdialog_wpgroup_add_cb( G_GNUC_UNUSED GtkButton *button, G_GNUC_UNUSED gpointer user_data )
{
	struct WPatternGroup *wpgroup;

	wpgroup = NEW(struct WPatternGroup);
	wpgroup->name = xstrdup( _("New Group") );
	wpgroup->color.r = 0.0;
	wpgroup->color.g = 0.0;
	wpgroup->color.b = 0.75;
	wpgroup->wp_list = NULL;

	G_LIST_APPEND(csdialog.color_config.by_wpattern.wpgroup_list, wpgroup);

	csdialog_wpattern_rebuild_ui_full( TRUE );
}


/* Sync the name entry text back into the WPatternGroup */
static void
csdialog_wpgroup_name_changed_cb( GtkEditable *editable, G_GNUC_UNUSED gpointer user_data )
{
	struct WPatternGroup *wpgroup = g_object_get_data( G_OBJECT(editable), "wpgroup" );
	const char *text;

	if (wpgroup == NULL)
		return;

	text = gtk_editable_get_text( editable );
	xfree( wpgroup->name );
	wpgroup->name = xstrdup( text );
}


/* Sync the patterns entry text back into the WPatternGroup */
static void
csdialog_wpgroup_patterns_changed_cb( GtkEditable *editable, G_GNUC_UNUSED gpointer user_data )
{
	struct WPatternGroup *wpgroup = g_object_get_data( G_OBJECT(editable), "wpgroup" );
	const char *text;
	GList *new_list, *old_llink;

	if (wpgroup == NULL)
		return;

	text = gtk_editable_get_text( editable );

	/* Free old pattern list */
	old_llink = wpgroup->wp_list;
	while (old_llink != NULL) {
		xfree( old_llink->data );
		old_llink = old_llink->next;
	}
	g_list_free( wpgroup->wp_list );

	/* Parse new pattern list */
	new_list = string_to_patterns( text );
	wpgroup->wp_list = new_list;
}


/* Deferred callback: scroll the wildcard list to the bottom and focus the widget.
 * Uses a short timeout so GTK has completed the layout pass first. */
static gboolean
csdialog_wpattern_scroll_to_end( gpointer user_data )
{
	GtkWidget *focus_w = GTK_WIDGET(user_data);
	GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(
		GTK_SCROLLED_WINDOW(csdialog.wpattern.scroll_w) );
	gtk_adjustment_set_value( vadj, gtk_adjustment_get_upper( vadj ) );
	gtk_widget_grab_focus( focus_w );
	return G_SOURCE_REMOVE;
}


/* Build (or rebuild) the wildcard group rows in the UI.
 * If focus_last is TRUE, grabs focus on the last row's name entry. */
static void
csdialog_wpattern_rebuild_ui_full( boolean focus_last )
{
	GtkWidget *groups_box = csdialog.wpattern.groups_box_w;
	GList *wpgroup_llink;
	struct WPatternGroup *wpgroup;
	GtkWidget *row_w, *name_w, *patterns_w, *remove_w;
	GtkWidget *last_name_w = NULL;
	char *patterns_str;

	/* Remove all existing children */
	while (gtk_widget_get_first_child( groups_box ) != NULL)
		gtk_box_remove( GTK_BOX(groups_box),
			gtk_widget_get_first_child( groups_box ) );

	/* Create a row for each wildcard group */
	wpgroup_llink = csdialog.color_config.by_wpattern.wpgroup_list;
	while (wpgroup_llink != NULL) {
		wpgroup = (struct WPatternGroup *)wpgroup_llink->data;

		row_w = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 6 );
		gtk_widget_set_margin_top( row_w, 2 );
		gtk_widget_set_margin_bottom( row_w, 2 );

		/* Name entry */
		name_w = gtk_entry_new( );
		gtk_entry_set_placeholder_text( GTK_ENTRY(name_w), _("Group name") );
		if (wpgroup->name != NULL)
			gtk_editable_set_text( GTK_EDITABLE(name_w), wpgroup->name );
		gtk_size_group_add_widget( csdialog.wpattern.name_sg, name_w );
		g_object_set_data( G_OBJECT(name_w), "wpgroup", wpgroup );
		g_signal_connect( name_w, "changed",
			G_CALLBACK(csdialog_wpgroup_name_changed_cb), NULL );
		gtk_box_append( GTK_BOX(row_w), name_w );
		last_name_w = name_w;

		/* Color picker */
		{
			GtkWidget *cp = gui_colorpicker_add( row_w, &wpgroup->color,
				_("Group Color"),
				G_CALLBACK(csdialog_wpgroup_color_cb), &wpgroup->color );
			gtk_size_group_add_widget( csdialog.wpattern.color_sg, cp );
		}

		/* Patterns entry */
		patterns_str = wpgroup_patterns_to_string( wpgroup );
		patterns_w = gtk_entry_new( );
		gtk_entry_set_placeholder_text( GTK_ENTRY(patterns_w), _("*.c; *.h; *.cpp") );
		gtk_editable_set_text( GTK_EDITABLE(patterns_w), patterns_str );
		g_free( patterns_str );
		gtk_widget_set_hexpand( patterns_w, TRUE );
		g_object_set_data( G_OBJECT(patterns_w), "wpgroup", wpgroup );
		g_signal_connect( patterns_w, "changed",
			G_CALLBACK(csdialog_wpgroup_patterns_changed_cb), NULL );
		gtk_box_append( GTK_BOX(row_w), patterns_w );

		/* Remove button */
		remove_w = gtk_button_new_from_icon_name( "list-remove-symbolic" );
		gtk_widget_set_tooltip_text( remove_w, _("Remove group") );
		g_signal_connect( remove_w, "clicked",
			G_CALLBACK(csdialog_wpgroup_remove_cb), wpgroup );
		gtk_box_append( GTK_BOX(row_w), remove_w );

		gtk_box_append( GTK_BOX(groups_box), row_w );
		wpgroup_llink = wpgroup_llink->next;
	}

	if (focus_last && last_name_w != NULL) {
		/* Defer focus + scroll to an idle callback so the new
		 * widgets have been allocated and the adjustment is up
		 * to date */
		g_timeout_add( 50, csdialog_wpattern_scroll_to_end, last_name_w );
	}
}


static void
csdialog_wpattern_rebuild_ui( void )
{
	csdialog_wpattern_rebuild_ui_full( FALSE );
}


/* Callback for the "Remember settings" checkbox */
static void
csdialog_remember_toggled( GtkCheckButton *check, G_GNUC_UNUSED gpointer user_data )
{
	boolean sensitive = !gtk_check_button_get_active( check );
	gtk_widget_set_sensitive( csdialog.general.vis_mode_dropdown_w, sensitive );
	gtk_widget_set_sensitive( csdialog.general.color_mode_dropdown_w, sensitive );
	gtk_widget_set_sensitive( csdialog.general.scale_mode_dropdown_w, sensitive );
}


/* Helper: read the dropdown→FsvMode mapping (dropdown order differs from enum) */
static FsvMode
csdialog_get_vis_mode( void )
{
	guint sel = gtk_drop_down_get_selected( GTK_DROP_DOWN(csdialog.general.vis_mode_dropdown_w) );
	switch (sel) {
		case 0: return FSV_MAPV;
		case 1: return FSV_TREEV;
		case 2: return FSV_DISCV;
		default: return FSV_MAPV;
	}
}


/* Callback for the "OK" button */
static void
csdialog_ok_button_cb( G_GNUC_UNUSED GtkWidget *unused, GtkWidget *window_w )
{
	ColorMode mode;

	/* Commit new color configuration, and set color mode to match
	 * current color sub-notebook page */
	mode = (ColorMode)gtk_notebook_get_current_page( GTK_NOTEBOOK(csdialog.notebook_w) );
	color_set_config( &csdialog.color_config, mode );

	/* Update toolbar to reflect current color mode */
	window_set_color_mode( mode );

	/* Save General tab settings */
	{
		boolean remember = gtk_check_button_get_active(
			GTK_CHECK_BUTTON(csdialog.general.remember_check_w) );
		FsvMode vis = csdialog_get_vis_mode( );
		ColorMode col = (ColorMode)gtk_drop_down_get_selected(
			GTK_DROP_DOWN(csdialog.general.color_mode_dropdown_w) );
		boolean scale_log = gtk_drop_down_get_selected(
			GTK_DROP_DOWN(csdialog.general.scale_mode_dropdown_w) ) == 0;
		fsv_write_general_settings( remember, vis, col, scale_log );
	}

	/* Save color settings to config file */
	color_write_config( );

	gtk_window_destroy( GTK_WINDOW(window_w) );
}


/* Confirm dialog button callback for the X button close */
static void
csdialog_confirm_response_cb( GtkWidget *btn, gpointer user_data )
{
	GtkWidget *confirm_w = GTK_WIDGET(user_data);
	GtkWidget *prefs_w = g_object_get_data( G_OBJECT(confirm_w), "prefs-window" );
	int action = GPOINTER_TO_INT( g_object_get_data( G_OBJECT(btn), "action" ) );

	gtk_window_destroy( GTK_WINDOW(confirm_w) );

	if (action == 1) {
		/* Save — same as clicking OK */
		csdialog_ok_button_cb( NULL, prefs_w );
	}
	else if (action == 2) {
		/* Discard — just close without saving */
		gtk_window_destroy( GTK_WINDOW(prefs_w) );
	}
	/* action == 0 means Cancel — do nothing, go back to editing */
}


/* Intercept the X button close on Preferences window */
static gboolean
csdialog_close_request_cb( GtkWindow *prefs_w, G_GNUC_UNUSED gpointer data )
{
	GtkWidget *confirm_w, *vbox_w, *btn_box, *btn;

	confirm_w = gtk_window_new( );
	gtk_window_set_title( GTK_WINDOW(confirm_w), _("Unsaved Changes") );
	gtk_window_set_modal( GTK_WINDOW(confirm_w), TRUE );
	gtk_window_set_transient_for( GTK_WINDOW(confirm_w), prefs_w );
	gtk_window_set_resizable( GTK_WINDOW(confirm_w), FALSE );
	g_object_set_data( G_OBJECT(confirm_w), "prefs-window", prefs_w );

	vbox_w = gtk_box_new( GTK_ORIENTATION_VERTICAL, 12 );
	gtk_widget_set_margin_start( vbox_w, 18 );
	gtk_widget_set_margin_end( vbox_w, 18 );
	gtk_widget_set_margin_top( vbox_w, 18 );
	gtk_widget_set_margin_bottom( vbox_w, 12 );
	gtk_window_set_child( GTK_WINDOW(confirm_w), vbox_w );

	gtk_box_append( GTK_BOX(vbox_w),
		gtk_label_new( _("Save changes to preferences?") ) );

	btn_box = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 8 );
	gtk_widget_set_halign( btn_box, GTK_ALIGN_END );
	gtk_box_append( GTK_BOX(vbox_w), btn_box );

	btn = gtk_button_new_with_label( _("Cancel") );
	g_object_set_data( G_OBJECT(btn), "action", GINT_TO_POINTER(0) );
	g_signal_connect( btn, "clicked", G_CALLBACK(csdialog_confirm_response_cb), confirm_w );
	gtk_box_append( GTK_BOX(btn_box), btn );

	btn = gtk_button_new_with_label( _("Discard") );
	gtk_widget_add_css_class( btn, "destructive-action" );
	g_object_set_data( G_OBJECT(btn), "action", GINT_TO_POINTER(2) );
	g_signal_connect( btn, "clicked", G_CALLBACK(csdialog_confirm_response_cb), confirm_w );
	gtk_box_append( GTK_BOX(btn_box), btn );

	btn = gtk_button_new_with_label( _("Save") );
	gtk_widget_add_css_class( btn, "suggested-action" );
	g_object_set_data( G_OBJECT(btn), "action", GINT_TO_POINTER(1) );
	g_signal_connect( btn, "clicked", G_CALLBACK(csdialog_confirm_response_cb), confirm_w );
	gtk_box_append( GTK_BOX(btn_box), btn );

	gtk_window_present( GTK_WINDOW(confirm_w) );

	return TRUE; /* block the default close */
}


/* Callback for dialog window destruction */
static void
csdialog_destroy_cb( G_GNUC_UNUSED GObject *unused )
{
	/* We'd leak memory like crazy if we didn't do this */
	color_config_destroy( &csdialog.color_config );
}


void
dialog_color_setup( void )
{
	GtkWidget *window_w;
	GtkWidget *main_vbox_w;
	GtkWidget *vbox_w;
	GtkWidget *vbox2_w;
	GtkWidget *hbox_w;
	GtkWidget *hbox2_w;
	GtkWidget *frame_w;
	GtkWidget *table_w;
	GtkWidget *label_w;
	GtkWidget *optmenu_w;
	RGBcolor *color;
	ColorMode color_mode;
	int i;
	char strbuf[256];

	window_w = gui_dialog_window( _("Preferences"), NULL );
	gtk_window_set_resizable( GTK_WINDOW(window_w), TRUE );
	gtk_window_set_default_size( GTK_WINDOW(window_w), -1, 600 );
	gui_window_modalize( window_w, main_window_w );
	g_signal_connect( window_w, "close-request",
		G_CALLBACK(csdialog_close_request_cb), NULL );
	main_vbox_w = gui_vbox_add( window_w, 5 );

	/* Top-level notebook: General | Colors */
	csdialog.top_notebook_w = gui_notebook_add( main_vbox_w );

	/* ======== GENERAL TAB ======== */
	{
		GtkWidget *gen_vbox_w = gui_vbox_add( NULL, 12 );
		boolean cfg_remember = FALSE;
		int cfg_vis = FSV_MAPV;
		int cfg_color = COLOR_BY_NODETYPE;
		int cfg_scale_log = TRUE;

		/* Read current general settings from config */
		{
			GKeyFile *cfg_kf = g_key_file_new( );
			gchar *cfg_path = config_file_path( );
			if (g_key_file_load_from_file( cfg_kf, cfg_path, G_KEY_FILE_NONE, NULL )) {
				gchar *str;
				cfg_remember = g_key_file_get_boolean( cfg_kf, "Settings", "remember_session", NULL );
				str = g_key_file_get_string( cfg_kf, "Settings", "default_vis_mode", NULL );
				if (str != NULL) {
					if (!strcmp( str, "discv" )) cfg_vis = FSV_DISCV;
					else if (!strcmp( str, "mapv" )) cfg_vis = FSV_MAPV;
					else if (!strcmp( str, "treev" )) cfg_vis = FSV_TREEV;
					g_free( str );
				} else {
					/* Fallback: use legacy "mode" key or current runtime mode */
					str = g_key_file_get_string( cfg_kf, "Settings", "mode", NULL );
					if (str != NULL) {
						if (!strcmp( str, "discv" )) cfg_vis = FSV_DISCV;
						else if (!strcmp( str, "mapv" )) cfg_vis = FSV_MAPV;
						else if (!strcmp( str, "treev" )) cfg_vis = FSV_TREEV;
						g_free( str );
					} else {
						cfg_vis = globals.fsv_mode;
					}
				}
				str = g_key_file_get_string( cfg_kf, "Settings", "default_color_mode", NULL );
				if (str != NULL) {
					if (!strcmp( str, "wildcard" )) cfg_color = COLOR_BY_WPATTERN;
					else if (!strcmp( str, "nodetype" )) cfg_color = COLOR_BY_NODETYPE;
					else if (!strcmp( str, "time" )) cfg_color = COLOR_BY_TIMESTAMP;
					g_free( str );
				} else {
					cfg_color = color_get_mode( );
				}
				str = g_key_file_get_string( cfg_kf, "Settings", "default_scale_mode", NULL );
				if (str != NULL) {
					cfg_scale_log = !strcmp( str, "logarithmic" );
					g_free( str );
				} else {
					cfg_scale_log = geometry_treev_get_scale_logarithmic( );
				}
			}
			g_free( cfg_path );
			g_key_file_free( cfg_kf );
		}

		gtk_widget_set_margin_start( gen_vbox_w, 12 );
		gtk_widget_set_margin_end( gen_vbox_w, 12 );
		gtk_widget_set_margin_top( gen_vbox_w, 12 );
		gtk_widget_set_margin_bottom( gen_vbox_w, 12 );
		gui_notebook_page_add( csdialog.top_notebook_w, _("General"), gen_vbox_w );

		/* "Remember settings" checkbox */
		csdialog.general.remember_check_w = gtk_check_button_new_with_label(
			_("Remember settings from previous session") );
		gtk_check_button_set_active( GTK_CHECK_BUTTON(csdialog.general.remember_check_w), cfg_remember );
		gtk_box_append( GTK_BOX(gen_vbox_w), csdialog.general.remember_check_w );
		g_signal_connect( csdialog.general.remember_check_w, "toggled",
			G_CALLBACK(csdialog_remember_toggled), NULL );

		/* Default visualization mode */
		hbox_w = gui_hbox_add( gen_vbox_w, 8 );
		gui_label_add( hbox_w, _("Default visualization:") );
		{
			const char * const vis_items[] = { "MapV", "TreeV", "DiscV", NULL };
			GtkStringList *vis_list = gtk_string_list_new( vis_items );
			csdialog.general.vis_mode_dropdown_w = gtk_drop_down_new(
				G_LIST_MODEL(vis_list), NULL );
			/* Dropdown order: 0=MapV, 1=TreeV, 2=DiscV
			 * FsvMode enum: 0=DISCV, 1=MAPV, 2=TREEV */
			switch (cfg_vis) {
				case FSV_MAPV:  gtk_drop_down_set_selected( GTK_DROP_DOWN(csdialog.general.vis_mode_dropdown_w), 0 ); break;
				case FSV_TREEV: gtk_drop_down_set_selected( GTK_DROP_DOWN(csdialog.general.vis_mode_dropdown_w), 1 ); break;
				case FSV_DISCV: gtk_drop_down_set_selected( GTK_DROP_DOWN(csdialog.general.vis_mode_dropdown_w), 2 ); break;
				default: break;
			}
			gtk_box_append( GTK_BOX(hbox_w), csdialog.general.vis_mode_dropdown_w );
		}

		/* Default color mode */
		hbox_w = gui_hbox_add( gen_vbox_w, 8 );
		gui_label_add( hbox_w, _("Default color mode:") );
		{
			const char * const color_items[] = { "By wildcards", "By node type", "By timestamp", NULL };
			GtkStringList *color_list = gtk_string_list_new( color_items );
			csdialog.general.color_mode_dropdown_w = gtk_drop_down_new(
				G_LIST_MODEL(color_list), NULL );
			gtk_drop_down_set_selected( GTK_DROP_DOWN(csdialog.general.color_mode_dropdown_w),
				(guint)cfg_color );
			gtk_box_append( GTK_BOX(hbox_w), csdialog.general.color_mode_dropdown_w );
		}

		/* Default scale mode */
		hbox_w = gui_hbox_add( gen_vbox_w, 8 );
		gui_label_add( hbox_w, _("Default TreeV scale:") );
		{
			const char * const scale_items[] = { "Logarithmic", "Representative", NULL };
			GtkStringList *scale_list = gtk_string_list_new( scale_items );
			csdialog.general.scale_mode_dropdown_w = gtk_drop_down_new(
				G_LIST_MODEL(scale_list), NULL );
			gtk_drop_down_set_selected( GTK_DROP_DOWN(csdialog.general.scale_mode_dropdown_w),
				cfg_scale_log ? 0 : 1 );
			gtk_box_append( GTK_BOX(hbox_w), csdialog.general.scale_mode_dropdown_w );
		}

		/* Grey out dropdowns when "remember session" is active */
		if (cfg_remember) {
			gtk_widget_set_sensitive( csdialog.general.vis_mode_dropdown_w, FALSE );
			gtk_widget_set_sensitive( csdialog.general.color_mode_dropdown_w, FALSE );
			gtk_widget_set_sensitive( csdialog.general.scale_mode_dropdown_w, FALSE );
		}
	}

	/* ======== COLORS TAB ======== */
	{
		GtkWidget *colors_vbox_w = gui_vbox_add( NULL, 5 );
		gui_notebook_page_add( csdialog.top_notebook_w, _("Colors"), colors_vbox_w );
		csdialog.notebook_w = gui_notebook_add( colors_vbox_w );
	}

	/* Get current color mode/configuration */
	color_mode = color_get_mode( );
	color_get_config( &csdialog.color_config );


	/**** "By wildcards" page ****/

	vbox_w = gui_vbox_add( NULL, 6 );
	gtk_widget_set_margin_start( vbox_w, 8 );
	gtk_widget_set_margin_end( vbox_w, 8 );
	gtk_widget_set_margin_top( vbox_w, 8 );
	gtk_widget_set_margin_bottom( vbox_w, 8 );
	gui_notebook_page_add( csdialog.notebook_w, _("By wildcards"), vbox_w );

	/* Size groups to keep header labels aligned with data row widgets */
	csdialog.wpattern.name_sg = gtk_size_group_new( GTK_SIZE_GROUP_HORIZONTAL );
	csdialog.wpattern.color_sg = gtk_size_group_new( GTK_SIZE_GROUP_HORIZONTAL );

	/* Column headers */
	{
		GtkWidget *header_w = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 6 );
		GtkWidget *lbl;

		lbl = gtk_label_new( _("Name") );
		gtk_label_set_xalign( GTK_LABEL(lbl), 0.0 );
		gtk_size_group_add_widget( csdialog.wpattern.name_sg, lbl );
		gtk_box_append( GTK_BOX(header_w), lbl );

		lbl = gtk_label_new( _("Color") );
		gtk_size_group_add_widget( csdialog.wpattern.color_sg, lbl );
		gtk_box_append( GTK_BOX(header_w), lbl );

		lbl = gtk_label_new( _("Patterns (semicolon-separated)") );
		gtk_widget_set_hexpand( lbl, TRUE );
		gtk_label_set_xalign( GTK_LABEL(lbl), 0.0 );
		gtk_box_append( GTK_BOX(header_w), lbl );

		gtk_box_append( GTK_BOX(vbox_w), header_w );
	}

	/* Scrolled container for group rows */
	{
		csdialog.wpattern.scroll_w = gtk_scrolled_window_new( );
		gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(csdialog.wpattern.scroll_w),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
		gtk_widget_set_vexpand( csdialog.wpattern.scroll_w, TRUE );

		csdialog.wpattern.groups_box_w = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
		gtk_scrolled_window_set_child( GTK_SCROLLED_WINDOW(csdialog.wpattern.scroll_w),
			csdialog.wpattern.groups_box_w );
		gtk_box_append( GTK_BOX(vbox_w), csdialog.wpattern.scroll_w );
	}

	/* Populate group rows from config */
	csdialog_wpattern_rebuild_ui( );

	/* Bottom row: Add Group button + Default color */
	{
		GtkWidget *bottom_w = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 8 );
		GtkWidget *add_btn;

		add_btn = gtk_button_new_with_label( _("Add Group") );
		g_signal_connect( add_btn, "clicked",
			G_CALLBACK(csdialog_wpgroup_add_cb), NULL );
		gtk_box_append( GTK_BOX(bottom_w), add_btn );

		/* Spacer */
		{
			GtkWidget *spacer = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
			gtk_widget_set_hexpand( spacer, TRUE );
			gtk_box_append( GTK_BOX(bottom_w), spacer );
		}

		gui_label_add( bottom_w, _("Default color:") );
		color = &csdialog.color_config.by_wpattern.default_color;
		csdialog.wpattern.default_colorpicker_w = gui_colorpicker_add(
			bottom_w, color, _("Default Color"),
			G_CALLBACK(csdialog_wp_default_color_cb), color );

		gtk_box_append( GTK_BOX(vbox_w), bottom_w );
	}


	/**** "By node type" page ****/

	hbox_w = gui_hbox_add( NULL, 7 );
	gui_box_set_packing( hbox_w, EXPAND, NO_FILL, AT_START );
	gui_notebook_page_add( csdialog.notebook_w, _("By node type"), hbox_w );

	vbox_w = gui_vbox_add( hbox_w, 10 );
	gtk_widget_set_margin_start( vbox_w, 3 );
	gtk_widget_set_margin_end( vbox_w, 3 );
	gtk_widget_set_margin_top( vbox_w, 3 );
	gtk_widget_set_margin_bottom( vbox_w, 3 );
	gui_box_set_packing( vbox_w, EXPAND, NO_FILL, AT_START );
	vbox2_w = gui_vbox_add( hbox_w, 10 );
	gtk_widget_set_margin_start( vbox2_w, 3 );
	gtk_widget_set_margin_end( vbox2_w, 3 );
	gtk_widget_set_margin_top( vbox2_w, 3 );
	gtk_widget_set_margin_bottom( vbox2_w, 3 );
	gui_box_set_packing( vbox2_w, EXPAND, NO_FILL, AT_START );

	/* Create two-column listing of node type colors */
	for (i = 1; i < NUM_NODE_TYPES; i++) {
		if ((i % 2) == 1)
			frame_w = gui_frame_add( vbox_w, NULL );
		else
			frame_w = gui_frame_add( vbox2_w, NULL );

		hbox_w = gui_hbox_add( frame_w, 10 );

		/* Color picker button */
		sprintf( strbuf, _("Color: %s"), node_type_names[i] );
		color = &csdialog.color_config.by_nodetype.colors[i];
		gui_colorpicker_add( hbox_w, color, strbuf, G_CALLBACK(csdialog_node_type_color_picker_cb), color );

		/* Node type icon */
		gui_resource_image_add( hbox_w, node_type_icon_paths[i] );
		/* Node type label */
		gui_label_add( hbox_w, _(node_type_names[i]) );
	}


	/**** "By date/time" page ****/

	vbox_w = gui_vbox_add( NULL, 10 );
	gui_notebook_page_add( csdialog.notebook_w, _("By date/time"), vbox_w );

	/* Arrange the top part using a table */
	hbox_w = gui_hbox_add( vbox_w, 0 );
	table_w = gui_table_add( hbox_w, 3, 2, FALSE, 4 );
	gui_widget_packing( table_w, EXPAND, NO_FILL, AT_START );
        /* Old label */
	hbox2_w = gui_hbox_add( NULL, 0 );
	gui_table_attach( table_w, hbox2_w, 0, 1, 0, 1 );
	label_w = gui_label_add( hbox2_w, _("Oldest:") );
	gui_widget_packing( label_w, NO_EXPAND, NO_FILL, AT_END );
	/* New label */
	hbox2_w = gui_hbox_add( NULL, 0 );
	gui_table_attach( table_w, hbox2_w, 0, 1, 1, 2 );
	label_w = gui_label_add( hbox2_w, _("Newest:") );
	gui_widget_packing( label_w, NO_EXPAND, NO_FILL, AT_END );
	/* Timestamp selection label */
	hbox2_w = gui_hbox_add( NULL, 0 );
	gui_table_attach( table_w, hbox2_w, 0, 1, 2, 3 );
	label_w = gui_label_add( hbox2_w, _("Color by:") );
	gui_widget_packing( label_w, NO_EXPAND, NO_FILL, AT_END );
	/* Old date edit widget */
	csdialog.time.old_dateedit_w = gui_dateedit_add( NULL, csdialog.color_config.by_timestamp.old_time, G_CALLBACK(csdialog_time_edit_cb), NULL );
        gui_table_attach( table_w, csdialog.time.old_dateedit_w, 1, 2, 0, 1 );
	/* New date edit widget */
	csdialog.time.new_dateedit_w = gui_dateedit_add( NULL, csdialog.color_config.by_timestamp.new_time, G_CALLBACK(csdialog_time_edit_cb), NULL );
        gui_table_attach( table_w, csdialog.time.new_dateedit_w, 1, 2, 1, 2 );
	/* Timestamp selection combo box */
	gui_option_menu_item( _("Time of last access"), G_CALLBACK(csdialog_time_timestamp_option_menu_cb), NULL );
	gui_option_menu_item( _("Time of last modification"), G_CALLBACK(csdialog_time_timestamp_option_menu_cb), NULL );
	gui_option_menu_item( _("Time of last attribute change"), G_CALLBACK(csdialog_time_timestamp_option_menu_cb), NULL );
	optmenu_w = gui_option_menu_add( NULL, csdialog.color_config.by_timestamp.timestamp_type );
        gui_table_attach( table_w, optmenu_w, 1, 2, 2, 3 );

	/* Time spectrum */
	frame_w = gui_frame_add( vbox_w, NULL );
	/* GTK4: shadow type removed */
	csdialog.time.spectrum_preview_w = gui_preview_add( frame_w );
	gui_preview_spectrum( csdialog.time.spectrum_preview_w, csdialog_time_spectrum_func );

	/* Horizontal box for spectrum color pickers and menu */
	hbox_w = gui_hbox_add( vbox_w, 0 );

	/* Old end */
        color = &csdialog.color_config.by_timestamp.old_color;
	csdialog.time.old_colorpicker_w = gui_colorpicker_add( hbox_w, color, _("Older Color"), G_CALLBACK(csdialog_time_color_picker_cb), color );
	gui_hbox_add( hbox_w, 5 );
	gui_label_add( hbox_w, _("Older") );

	/* Spectrum type selection */
	gui_option_menu_item( _("Rainbow"), G_CALLBACK(csdialog_time_spectrum_option_menu_cb), NULL );
	gui_option_menu_item( _("Heat"), G_CALLBACK(csdialog_time_spectrum_option_menu_cb), NULL );
	gui_option_menu_item( _("Gradient"), G_CALLBACK(csdialog_time_spectrum_option_menu_cb), NULL );
	optmenu_w = gui_option_menu_add( hbox_w, csdialog.color_config.by_timestamp.spectrum_type );
	gui_widget_packing( optmenu_w, EXPAND, NO_FILL, AT_START );

	/* New end */
	gui_box_set_packing( hbox_w, NO_EXPAND, NO_FILL, AT_END );
        color = &csdialog.color_config.by_timestamp.new_color;
	csdialog.time.new_colorpicker_w = gui_colorpicker_add( hbox_w, color, _("Newer Color"), G_CALLBACK(csdialog_time_color_picker_cb), color );
	gui_hbox_add( hbox_w, 5 );
	gui_label_add( hbox_w, _("Newer") );

	/* Color pickers are accessible only for gradient spectrum */
	csdialog_time_color_picker_set_access( csdialog.color_config.by_timestamp.spectrum_type == SPECTRUM_GRADIENT );


	/* OK and Cancel buttons */
	{
		GtkWidget *btn_box = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 8 );
		GtkWidget *btn;

		gtk_widget_set_halign( btn_box, GTK_ALIGN_END );
		gtk_widget_set_margin_top( btn_box, 6 );
		gtk_widget_set_margin_bottom( btn_box, 6 );
		gtk_widget_set_margin_end( btn_box, 6 );

		btn = gtk_button_new_with_label( _("Cancel") );
		g_signal_connect( btn, "clicked", G_CALLBACK(close_cb), window_w );
		gtk_box_append( GTK_BOX(btn_box), btn );

		btn = gtk_button_new_with_label( _("OK") );
		gtk_widget_add_css_class( btn, "suggested-action" );
		g_signal_connect( btn, "clicked", G_CALLBACK(csdialog_ok_button_cb), window_w );
		gtk_box_append( GTK_BOX(btn_box), btn );

		gtk_box_append( GTK_BOX(main_vbox_w), btn_box );
	}

	/* Set page to current color mode */
	gtk_notebook_set_current_page( GTK_NOTEBOOK(csdialog.notebook_w), color_mode );

	/* Some cleanup will be required once the window goes away */
	g_signal_connect( G_OBJECT(window_w), "destroy", G_CALLBACK(csdialog_destroy_cb), NULL );

	gtk_window_present( GTK_WINDOW(window_w) );
}


/* Callback for the "Look at target node" button on the "Target" page
 * of the Properties dialog for symlinks */
static void
look_at_target_node_cb( G_GNUC_UNUSED GtkWidget *unused, GNode *node )
{
	/* Target node may be buried inside a collapsed tree--
	 * if it is, expand it out into the open */
	if (NODE_IS_DIR(node->parent))
		if (!dirtree_entry_expanded( node->parent ))
			colexp( node->parent, COLEXP_EXPAND_ANY );

	camera_look_at( node );
}


/* The Properties dialog */
static void
dialog_node_properties( GNode *node )
{
	const struct NodeInfo *node_info;
	GtkWidget *window_w;
	GtkWidget *main_vbox_w;
	GtkWidget *notebook_w;
	GtkWidget *vbox_w;
	GtkWidget *table_w;
	GtkWidget *pixmap_w;
	GtkWidget *hbox_w;
	GtkWidget *label_w;
	GtkWidget *separator_w;
	GtkWidget *button_w;
	GtkWidget *vbox2_w;
	GtkWidget *clist_w;
	GtkWidget *entry_w;
	GNode *target_node;
	char strbuf[1024];
	char *proptext;

	/* Get the lowdown on the node. get_node_info( ) may cause some
	 * disk activity, so change the cursor meanwhile (just in case) */
	gui_cursor( main_window_w, "wait" );
	gui_update( );
	node_info = get_node_info( node );
	gui_cursor( main_window_w, "not-allowed" );

	window_w = gui_dialog_window( _("Properties"), NULL );
	gui_window_modalize( window_w, main_window_w );
	main_vbox_w = gui_vbox_add( window_w, 5 );
	notebook_w = gui_notebook_add( main_vbox_w );

	/**** "General" page ****/

	vbox_w = gui_vbox_add( NULL, 10 );
	gui_notebook_page_add( notebook_w, _("General"), vbox_w );
	table_w = gui_table_add( vbox_w, 6, 2, FALSE, 0 );

	/* Node type icon */
	hbox_w = gui_hbox_add( NULL, 8 );
	gui_table_attach( table_w, hbox_w, 0, 1, 0, 1 );
	pixmap_w = gui_resource_image_add( hbox_w, node_type_icon_paths[NODE_DESC(node)->type] );
	gui_widget_packing( pixmap_w, NO_EXPAND, NO_FILL, AT_END );
	/* Name */
	hbox_w = gui_hbox_add( NULL, 8 );
	label_w = gui_label_add( hbox_w, node_info->name );
	gtk_label_set_justify( GTK_LABEL(label_w), GTK_JUSTIFY_LEFT );
	gui_table_attach( table_w, hbox_w, 1, 2, 0, 1 );

	separator_w = gui_separator_add( NULL );
	gui_table_attach( table_w, separator_w, 0, 2, 1, 2 );

	/* Labels: type, location, size, owner, group */
        strcpy( strbuf, "" );
	strcat( strbuf, _("Type:\n\n") );
	strcat( strbuf, _("Location:\n\n") );
	if (NODE_IS_DIR(node))
		strcat( strbuf, _("Total size:\n\n") );
	else {
		strcat( strbuf, _("Size:\n") );
		strcat( strbuf, _("Allocation:\n\n") );
	}
	strcat( strbuf, _("Owner:\n") );
	strcat( strbuf, _("Group:") );
	hbox_w = gui_hbox_add( NULL, 8 );
	label_w = gui_label_add( hbox_w, strbuf );
	gui_widget_packing( label_w, NO_EXPAND, NO_FILL, AT_END );
	gtk_label_set_justify( GTK_LABEL(label_w), GTK_JUSTIFY_RIGHT );
	gui_table_attach( table_w, hbox_w, 0, 1, 2, 3 );

	proptext = xstrdup( "" );
	/* Type */
	STRRECAT(proptext, _(node_type_names[NODE_DESC(node)->type]));
	STRRECAT(proptext, "\n\n");
	/* Location */
        STRRECAT(proptext, node_info->prefix);
	STRRECAT(proptext, "\n\n");
	if (NODE_IS_DIR(node)) {
		/* Total size */
		sprintf( strbuf, _("%s bytes"), node_info->subtree_size );
		STRRECAT(proptext, strbuf);
		if (DIR_NODE_DESC(node)->subtree.size >= 1024) {
			sprintf( strbuf, " (%s)", node_info->subtree_size_abbr );
			STRRECAT(proptext, strbuf);
		}
	}
	else {
		/* Size */
		sprintf( strbuf, _("%s bytes"), node_info->size );
		STRRECAT(proptext, strbuf);
		if (NODE_DESC(node)->size >= 1024) {
			sprintf( strbuf, " (%s)", node_info->size_abbr );
			STRRECAT(proptext, strbuf);
		}
		STRRECAT(proptext, "\n");
		/* Allocation */
		sprintf( strbuf, _("%s bytes"), node_info->size_alloc );
		STRRECAT(proptext, strbuf);
	}
	STRRECAT(proptext, "\n\n");
	/* Owner (user) */
	sprintf( strbuf, _("%s (uid %u)"), node_info->user_name, NODE_DESC(node)->user_id );
	STRRECAT(proptext, strbuf);
	STRRECAT(proptext, "\n");
	/* Group */
	sprintf( strbuf, _("%s (gid %u)"), node_info->group_name, NODE_DESC(node)->group_id );
	STRRECAT(proptext, strbuf);

	hbox_w = gui_hbox_add( NULL, 8 );
	label_w = gui_label_add( hbox_w, proptext );
	gtk_label_set_justify( GTK_LABEL(label_w), GTK_JUSTIFY_LEFT );
	gui_table_attach( table_w, hbox_w, 1, 2, 2, 3 );

	separator_w = gui_separator_add( NULL );
	gui_table_attach( table_w, separator_w, 0, 2, 3, 4 );

	/* Labels for date/time stamps */
	strcpy( strbuf, "" );
	strcat( strbuf, _("Modified:\n") );
	strcat( strbuf, _("AttribCh:\n") );
	strcat( strbuf, _("Accessed:") );
	hbox_w = gui_hbox_add( NULL, 8 );
	label_w = gui_label_add( hbox_w, strbuf );
	gui_widget_packing( label_w, NO_EXPAND, NO_FILL, AT_END );
	gtk_label_set_justify( GTK_LABEL(label_w), GTK_JUSTIFY_RIGHT );
	gui_table_attach( table_w, hbox_w, 0, 1, 4, 5 );

	/* Date/time stamps */
	strcpy( proptext, "" );
	/* Modified */
	STRRECAT(proptext, node_info->mtime);
	STRRECAT(proptext, "\n");
	/* Attributes changed */
	STRRECAT(proptext, node_info->ctime);
	STRRECAT(proptext, "\n");
	/* Accessed */
	STRRECAT(proptext, node_info->atime);

	hbox_w = gui_hbox_add( NULL, 8 );
	label_w = gui_label_add( hbox_w, proptext );
	gtk_label_set_justify( GTK_LABEL(label_w), GTK_JUSTIFY_LEFT );
	gui_table_attach( table_w, hbox_w, 1, 2, 4, 5 );

	separator_w = gui_separator_add( NULL );
	gui_table_attach( table_w, separator_w, 0, 2, 5, 6 );

	/* Node-type-specific information pages */

	switch (NODE_DESC(node)->type) {

		case NODE_DIRECTORY:
		/**** "Contents" page ****/

		vbox_w = gui_vbox_add( NULL, 10 );
		gui_notebook_page_add( notebook_w, _("Contents"), vbox_w );

		hbox_w = gui_hbox_add( vbox_w, 0 );
		gui_widget_packing( hbox_w, EXPAND, NO_FILL, AT_START );
		vbox2_w = gui_vbox_add( hbox_w, 10 );
		gui_widget_packing( vbox2_w, EXPAND, NO_FILL, AT_START );

		gui_label_add( vbox2_w, _("This directory contains:") );

		/* Directory contents listing */
		clist_w = dir_contents_list( node );
		gtk_box_append( GTK_BOX(vbox2_w), clist_w );

		gui_separator_add( vbox2_w );

		strcpy( proptext, "" );
		/* Total size readout */
		sprintf( strbuf, _("%s bytes"), node_info->subtree_size );
		STRRECAT(proptext, strbuf);
		if (DIR_NODE_DESC(node)->subtree.size >= 1024) {
			sprintf( strbuf, " (%s)", node_info->subtree_size_abbr );
			STRRECAT(proptext, strbuf);
		}
		gui_label_add( vbox2_w, proptext );
                break;


#ifdef HAVE_FILE_COMMAND
		case NODE_REGFILE:
		/**** "File type" page ****/

		vbox_w = gui_vbox_add( NULL, 10 );
		gui_notebook_page_add( notebook_w, _("File type"), vbox_w );

		gui_label_add( vbox_w, _("This file is recognized as:") );

		/* 'file' command output */
		gui_text_area_add( vbox_w, node_info->file_type_desc );
                break;
#endif /* HAVE_FILE_COMMAND */


		case NODE_SYMLINK:
		/**** "Target" page ****/

		vbox_w = gui_vbox_add( NULL, 10 );
		gui_notebook_page_add( notebook_w, _("Target"), vbox_w );

		/* (Relative) name of target */
		gui_label_add( vbox_w, _("This symlink points to:") );
		hbox_w = gui_hbox_add( vbox_w, 0 );
		entry_w = gui_entry_add( hbox_w, node_info->target, NULL, NULL );
		gtk_editable_set_editable( GTK_EDITABLE(entry_w), FALSE );

		hbox_w = gui_hbox_add( vbox_w, 0 ); /* spacer */

		/* Absolute name of target */
		gui_label_add( vbox_w, _("Absolute name of target:") );
		hbox_w = gui_hbox_add( vbox_w, 0 );
                if (!strcmp( node_info->target, node_info->abstarget ))
			entry_w = gui_entry_add( hbox_w, _("(same as above)"), NULL, NULL );
                else
			entry_w = gui_entry_add( hbox_w, node_info->abstarget, NULL, NULL );
		gtk_editable_set_editable( GTK_EDITABLE(entry_w), FALSE );

		/* This is NULL if target isn't in the filesystem tree */
		target_node = node_named( node_info->abstarget );

		/* The "Look at target node" feature does not work in TreeV
		 * mode if directories have to be expanded to see the
		 * target node, because unbuilt TreeV geometry does not
		 * have a definite location */
		if ((globals.fsv_mode == FSV_TREEV) && (target_node != NULL))
			if (NODE_IS_DIR(target_node->parent))
				if (!dirtree_entry_expanded( target_node->parent ))
					target_node = NULL;

		/* Button to point camera at target node (if present) */
		hbox_w = gui_hbox_add( vbox_w, 10 );
		button_w = gui_button_add( hbox_w, _("Look at target node"), G_CALLBACK(look_at_target_node_cb), target_node );
		gui_widget_packing( button_w, EXPAND, NO_FILL, AT_START );
		gtk_widget_set_sensitive( button_w, target_node != NULL );
		g_signal_connect( G_OBJECT(button_w), "clicked", G_CALLBACK(close_cb), window_w );
		break;


		default:
		/* No additional information for this node type */
		break;
	}

	/* Close button */
	button_w = gui_button_add( main_vbox_w, _("Close"), G_CALLBACK(close_cb), window_w );

	xfree( proptext );

	gtk_window_present( GTK_WINDOW(window_w) );
}


/**** Context-sensitive right-click menu ****/

/* (I know, it's not a dialog, but where else to put this? :-) */

/* GNode stored for context menu action callbacks */
static GNode *context_menu_node = NULL;

/* Action callbacks for context menu */
static void
ctx_collapse_cb( G_GNUC_UNUSED GSimpleAction *action, G_GNUC_UNUSED GVariant *parameter, G_GNUC_UNUSED gpointer user_data )
{
	if (context_menu_node != NULL)
		colexp( context_menu_node, COLEXP_COLLAPSE_RECURSIVE );
}

static void
ctx_expand_cb( G_GNUC_UNUSED GSimpleAction *action, G_GNUC_UNUSED GVariant *parameter, G_GNUC_UNUSED gpointer user_data )
{
	if (context_menu_node != NULL)
		colexp( context_menu_node, COLEXP_EXPAND );
}

static void
ctx_expand_recursive_cb( G_GNUC_UNUSED GSimpleAction *action, G_GNUC_UNUSED GVariant *parameter, G_GNUC_UNUSED gpointer user_data )
{
	if (context_menu_node != NULL)
		colexp( context_menu_node, COLEXP_EXPAND_RECURSIVE );
}

static void
ctx_look_at_cb( G_GNUC_UNUSED GSimpleAction *action, G_GNUC_UNUSED GVariant *parameter, G_GNUC_UNUSED gpointer user_data )
{
	if (context_menu_node != NULL)
		camera_look_at( context_menu_node );
}

static void
ctx_properties_cb( G_GNUC_UNUSED GSimpleAction *action, G_GNUC_UNUSED GVariant *parameter, G_GNUC_UNUSED gpointer user_data )
{
	if (context_menu_node != NULL)
		dialog_node_properties( context_menu_node );
}


void
context_menu( GNode *node, GtkWidget *parent_widget, double x, double y )
{
	static GtkWidget *popover_w = NULL;
	GMenu *menu;
	GMenu *section;
	GSimpleActionGroup *action_group;

	/* Recycle previous popover */
	if (popover_w != NULL) {
		gtk_widget_unparent( popover_w );
		popover_w = NULL;
	}

	/* Check for the special case in which the menu has only one item */
	if (!NODE_IS_DIR(node) && (node == globals.current_node)) {
		dialog_node_properties( node );
		return;
	}

	/* Save node for action callbacks */
	context_menu_node = node;

	/* Build action group */
	action_group = g_simple_action_group_new( );

	menu = g_menu_new( );
	section = g_menu_new( );

	if (NODE_IS_DIR(node)) {
		if (dirtree_entry_expanded( node )) {
			GSimpleAction *act = g_simple_action_new( "collapse", NULL );
			g_signal_connect( act, "activate", G_CALLBACK(ctx_collapse_cb), NULL );
			g_action_map_add_action( G_ACTION_MAP(action_group), G_ACTION(act) );
			g_menu_append( section, _("Collapse"), "ctx.collapse" );
		}
		else {
			GSimpleAction *act = g_simple_action_new( "expand", NULL );
			g_signal_connect( act, "activate", G_CALLBACK(ctx_expand_cb), NULL );
			g_action_map_add_action( G_ACTION_MAP(action_group), G_ACTION(act) );
			g_menu_append( section, _("Expand"), "ctx.expand" );
			if (DIR_NODE_DESC(node)->subtree.counts[NODE_DIRECTORY] > 0) {
				act = g_simple_action_new( "expand-all", NULL );
				g_signal_connect( act, "activate", G_CALLBACK(ctx_expand_recursive_cb), NULL );
				g_action_map_add_action( G_ACTION_MAP(action_group), G_ACTION(act) );
				g_menu_append( section, _("Expand all"), "ctx.expand-all" );
			}
		}
	}
	if (node != globals.current_node) {
		GSimpleAction *act = g_simple_action_new( "look-at", NULL );
		g_signal_connect( act, "activate", G_CALLBACK(ctx_look_at_cb), NULL );
		g_action_map_add_action( G_ACTION_MAP(action_group), G_ACTION(act) );
		g_menu_append( section, _("Look at"), "ctx.look-at" );
	}
	{
		GSimpleAction *act = g_simple_action_new( "properties", NULL );
		g_signal_connect( act, "activate", G_CALLBACK(ctx_properties_cb), NULL );
		g_action_map_add_action( G_ACTION_MAP(action_group), G_ACTION(act) );
		g_menu_append( section, _("Properties"), "ctx.properties" );
	}

	g_menu_append_section( menu, NULL, G_MENU_MODEL(section) );

	/* Insert action group into the parent widget */
	gtk_widget_insert_action_group( parent_widget, "ctx", G_ACTION_GROUP(action_group) );

	/* Create popover menu */
	popover_w = gtk_popover_menu_new_from_model( G_MENU_MODEL(menu) );
	gtk_widget_set_parent( popover_w, parent_widget );
	gtk_popover_set_has_arrow( GTK_POPOVER(popover_w), FALSE );

	/* Position at click location */
	{
		GdkRectangle rect = { (int)x, (int)y, 1, 1 };
		gtk_popover_set_pointing_to( GTK_POPOVER(popover_w), &rect );
	}

	gtk_popover_popup( GTK_POPOVER(popover_w) );

	g_object_unref( section );
	g_object_unref( menu );
}


/* end dialog.c */
