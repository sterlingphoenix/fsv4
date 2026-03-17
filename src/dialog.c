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
#include "gui.h"
#include "window.h"

/* OK/Cancel button XPM's */
#include "xmaps/button_ok.xpm"
#include "xmaps/button_cancel.xpm"


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

/* Types of rows in the wildcard pattern list */
enum {
	WPLIST_HEADER_ROW,
	WPLIST_WPATTERN_ROW,
	WPLIST_NEW_WPATTERN_ROW,
	WPLIST_DEFAULT_HEADER_ROW,
	WPLIST_DEFAULT_ROW
};

/* GObject item for the wildcard pattern list model */
#define FSV_TYPE_WP_LIST_ITEM (fsv_wp_list_item_get_type())
G_DECLARE_FINAL_TYPE(FsvWPListItem, fsv_wp_list_item, FSV, WP_LIST_ITEM, GObject)

struct _FsvWPListItem {
	GObject parent_instance;
	int row_type;
	struct WPatternGroup *wpgroup;
	char *wpattern;
	char *hex_color;
};

G_DEFINE_TYPE(FsvWPListItem, fsv_wp_list_item, G_TYPE_OBJECT)

static void
fsv_wp_list_item_finalize( GObject *obj )
{
	FsvWPListItem *item = FSV_WP_LIST_ITEM(obj);
	g_free( item->hex_color );
	G_OBJECT_CLASS(fsv_wp_list_item_parent_class)->finalize( obj );
}

static void
fsv_wp_list_item_class_init( FsvWPListItemClass *klass )
{
	G_OBJECT_CLASS(klass)->finalize = fsv_wp_list_item_finalize;
}

static void
fsv_wp_list_item_init( FsvWPListItem *self )
{
	self->row_type = 0;
	self->wpgroup = NULL;
	self->wpattern = NULL;
	self->hex_color = NULL;
}

static FsvWPListItem *
fsv_wp_list_item_new( int row_type, struct WPatternGroup *wpgroup, char *wpattern, const char *hex_color )
{
	FsvWPListItem *item = g_object_new( FSV_TYPE_WP_LIST_ITEM, NULL );
	item->row_type = row_type;
	item->wpgroup = wpgroup;
	item->wpattern = wpattern;
	item->hex_color = g_strdup( hex_color );
	return item;
}

/* Data passed to callbacks that need row context (same fields as old WPListRowData) */
struct WPListRowData {
	int row_type;
	struct WPatternGroup *wpgroup;
	char *wpattern;
	char *hex_color;
};

static struct ColorSetupDialog {
	/* Scratch copy of color configuration */
	struct ColorConfig color_config;

	/* Notebook widget (each page dedicated to a color mode) */
	GtkWidget *notebook_w;

	/* Node type configuration page */
	/* (doesn't have any widgets we need to keep) */

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
		/* Wildcard pattern list widget (GtkListView) */
		GtkWidget *clist_w;

		/* Action buttons */
		GtkWidget *new_color_button_w;
		GtkWidget *edit_pattern_button_w;
		GtkWidget *delete_button_w;
	} wpattern;
} csdialog;


/* Forward declarations */
static void csdialog_wpattern_clist_populate( void );


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


/* Helper function for csdialog_wpattern_clist_populate( ). This
 * generates a hex color string from an RGBcolor */
static char *
solid_color_hex( RGBcolor *color )
{
	char *hex = g_strdup_printf( "#%02X%02X%02X",
		(int)(255.0 * color->r),
		(int)(255.0 * color->g),
		(int)(255.0 * color->b) );
	return hex;
}


/* Helper: append a new item to the wildcard pattern list store */
static void
wplist_row( GListStore *store, int row_type, struct WPatternGroup *wpgroup,
            char *wpattern, const char *hex_color )
{
	FsvWPListItem *item = fsv_wp_list_item_new( row_type, wpgroup, wpattern, hex_color );
	g_list_store_append( store, item );
	g_object_unref( item );
}


/* Helper: get the GListStore backing the wildcard pattern list */
static GListStore *
wplist_get_store( void )
{
	return g_object_get_data( G_OBJECT(csdialog.wpattern.clist_w), "list_store" );
}


/* Updates the wildcard pattern list with state in
 * csdialog.color_config.by_wpattern */
static void
csdialog_wpattern_clist_populate( void )
{
	struct WPatternGroup *wpgroup;
	const char *hex_color;
	GList *wpgroup_llink, *wp_llink;
	char *wpattern;
	GListStore *store;

	store = wplist_get_store( );
	g_list_store_remove_all( store );

	/* Iterate through all the wildcard pattern color groups */
	wpgroup_llink = csdialog.color_config.by_wpattern.wpgroup_list;
	while (wpgroup_llink != NULL) {
		wpgroup = (struct WPatternGroup *)wpgroup_llink->data;
		hex_color = solid_color_hex( &wpgroup->color );

		/* Add header row */
		wplist_row( store, WPLIST_HEADER_ROW, wpgroup, NULL, hex_color );

		/* Iterate through all the patterns in this group */
		wp_llink = wpgroup->wp_list;
		while (wp_llink != NULL) {
			wpattern = (char *)wp_llink->data;
			wplist_row( store, WPLIST_WPATTERN_ROW, wpgroup, wpattern, hex_color );
			wp_llink = wp_llink->next;
		}

		/* Add a "New pattern" row for adding new patterns
		 * to this color group */
		wplist_row( store, WPLIST_NEW_WPATTERN_ROW, wpgroup, NULL, hex_color );

		wpgroup_llink = wpgroup_llink->next;
	}

	/* Default color */
	hex_color = solid_color_hex( &csdialog.color_config.by_wpattern.default_color );
	wplist_row( store, WPLIST_DEFAULT_HEADER_ROW, NULL, NULL, hex_color );
	wplist_row( store, WPLIST_DEFAULT_ROW, NULL, NULL, hex_color );
}


/* Callback for the color selection dialog popped up by clicking on a
 * color bar in the wildcard pattern list */
static void
csdialog_wpattern_color_selection_cb( RGBcolor *selected_color, RGBcolor *wpattern_color )
{
	/* wpattern_color points to the appropriate color record
	 * somewhere inside csdialog.color_config.by_wpattern */
	wpattern_color->r = selected_color->r;
	wpattern_color->g = selected_color->g;
	wpattern_color->b = selected_color->b;

	/* Update the list */
	csdialog_wpattern_clist_populate( );
}


/* Callback for click on a color swatch in the wildcard pattern list.
 * The swatch widget is a GtkDrawingArea inside each row. For header rows,
 * the swatch fills the entire width. */
static void
wp_swatch_click_cb( GtkGestureClick *gesture, G_GNUC_UNUSED int n_press,
                    G_GNUC_UNUSED double x, G_GNUC_UNUSED double y,
                    G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *swatch = gtk_event_controller_get_widget( GTK_EVENT_CONTROLLER(gesture) );
	FsvWPListItem *item = g_object_get_data( G_OBJECT(swatch), "wp_item" );
	RGBcolor *color;
	const char *title;

	if (item == NULL)
		return;

	switch (item->row_type) {
		case WPLIST_HEADER_ROW:
		case WPLIST_WPATTERN_ROW:
		case WPLIST_NEW_WPATTERN_ROW:
		title = _("Group Color");
		color = &item->wpgroup->color;
		break;

		case WPLIST_DEFAULT_HEADER_ROW:
		case WPLIST_DEFAULT_ROW:
		title = _("Default Color");
		color = &csdialog.color_config.by_wpattern.default_color;
		break;

		SWITCH_FAIL
	}

	gui_colorsel_window( title, color, G_CALLBACK(csdialog_wpattern_color_selection_cb), color );
}


/* Previous valid selection position (for header row rejection) */
static guint wp_prev_selection = GTK_INVALID_LIST_POSITION;
static boolean wp_selection_guard = FALSE;


/* Update button sensitivity based on current selection */
static void
csdialog_wpattern_update_buttons( boolean row_selected, FsvWPListItem *item )
{
	boolean newwp_row = FALSE;
	boolean defcolor_row = FALSE;
	boolean empty_wpgroup = FALSE;
	boolean new_color_allow;
	boolean edit_pattern_allow;
	boolean delete_allow;

	if (row_selected && item != NULL) {
		newwp_row = item->row_type == WPLIST_NEW_WPATTERN_ROW;
		defcolor_row = (item->row_type == WPLIST_DEFAULT_ROW) || (item->row_type == WPLIST_DEFAULT_HEADER_ROW);
		if (!defcolor_row && item->wpgroup != NULL)
			empty_wpgroup = item->wpgroup->wp_list == NULL;
	}

	new_color_allow = !row_selected || !defcolor_row;
	edit_pattern_allow = row_selected && !defcolor_row;
	delete_allow = row_selected && !defcolor_row && (!newwp_row || empty_wpgroup);

	gtk_widget_set_sensitive( csdialog.wpattern.new_color_button_w, new_color_allow );
	gtk_widget_set_sensitive( csdialog.wpattern.edit_pattern_button_w, edit_pattern_allow );
	gtk_widget_set_sensitive( csdialog.wpattern.delete_button_w, delete_allow );
}


/* Callback for selection change in the wildcard pattern list */
static void
csdialog_wpattern_selection_changed_cb( GtkSingleSelection *sel,
                                        G_GNUC_UNUSED GParamSpec *pspec,
                                        G_GNUC_UNUSED gpointer user_data )
{
	guint pos;
	gpointer obj;
	FsvWPListItem *item;

	if (wp_selection_guard)
		return;

	pos = gtk_single_selection_get_selected( sel );
	if (pos == GTK_INVALID_LIST_POSITION) {
		csdialog_wpattern_update_buttons( FALSE, NULL );
		return;
	}

	obj = g_list_model_get_item( gtk_single_selection_get_model( sel ), pos );
	if (obj == NULL) {
		csdialog_wpattern_update_buttons( FALSE, NULL );
		return;
	}

	item = FSV_WP_LIST_ITEM(obj);

	/* Reject selection of header rows */
	if (item->row_type == WPLIST_HEADER_ROW || item->row_type == WPLIST_DEFAULT_HEADER_ROW) {
		g_object_unref( obj );
		wp_selection_guard = TRUE;
		gtk_single_selection_set_selected( sel, wp_prev_selection );
		wp_selection_guard = FALSE;
		return;
	}

	wp_prev_selection = pos;
	csdialog_wpattern_update_buttons( TRUE, item );
	g_object_unref( obj );
}


/* Callback for the color selection dialog popped up by the "New color"
 * button */
static void
csdialog_wpattern_new_color_selection_cb( RGBcolor *selected_color, struct WPListRowData *row_data )
{
	struct WPatternGroup *wpgroup;
	boolean place_before_existing_group = FALSE;

	/* Create new group */
	wpgroup = NEW(struct WPatternGroup);
        wpgroup->color.r = selected_color->r;
        wpgroup->color.g = selected_color->g;
	wpgroup->color.b = selected_color->b;
	wpgroup->wp_list = NULL;

	/* If a row in an existing group was selected, we add the new group
	 * immediately before the existing group */
        if (row_data != NULL)
		if (row_data->wpgroup != NULL)
			place_before_existing_group = TRUE;

#define WPGROUP_LIST csdialog.color_config.by_wpattern.wpgroup_list
	if (place_before_existing_group) {
		GList *sibling = g_list_find( WPGROUP_LIST, row_data->wpgroup );
		WPGROUP_LIST = g_list_insert_before( WPGROUP_LIST, sibling, wpgroup );
	}
	else {
		G_LIST_APPEND(WPGROUP_LIST, wpgroup);
	}
#undef WPGROUP_LIST

	/* Update the list */
	csdialog_wpattern_clist_populate( );
}


/* Callback for the wildcard pattern edit subdialog */
static void
csdialog_wpattern_edit_cb( const char *input_text, struct WPListRowData *row_data )
{
	char *wpattern;

	/* Trim leading/trailing whitespace in input */
	wpattern = xstrstrip( xstrdup( input_text ) );

	if (strlen( wpattern ) == 0) {
		/* Ignore empty input */
		xfree( wpattern );
		return;
	}

	/* Check for duplicate pattern in group */
	/* (This doesn't prevent the possibility of duplicate patterns
	 * across groups, but hey, this is better than nothing) */
	if (g_list_find_custom( row_data->wpgroup->wp_list, wpattern, (GCompareFunc)strcmp ) != NULL) {
		/* Yep, there's a duplicate */
		xfree( wpattern );
		return;
	}

	switch (row_data->row_type) {
		case WPLIST_WPATTERN_ROW:
		/* Update existing pattern */
		g_assert( g_list_replace( row_data->wpgroup->wp_list, row_data->wpattern, wpattern ) != NULL );
		xfree( row_data->wpattern );
		break;

		case WPLIST_NEW_WPATTERN_ROW:
		/* Add new pattern */
		G_LIST_APPEND(row_data->wpgroup->wp_list, wpattern);
		break;

		SWITCH_FAIL
	}

	/* Update the list */
	csdialog_wpattern_clist_populate( );
}


/* Helper: get the selected row's data from the wildcard pattern list.
 * Returns TRUE if a row is selected, filling out the provided fields. */
static boolean
wplist_get_selected( int *row_type_out, struct WPatternGroup **wpgroup_out, char **wpattern_out )
{
	GtkSingleSelection *sel;
	guint pos;
	gpointer obj;
	FsvWPListItem *item;

	sel = g_object_get_data( G_OBJECT(csdialog.wpattern.clist_w), "selection_model" );
	pos = gtk_single_selection_get_selected( sel );
	if (pos == GTK_INVALID_LIST_POSITION)
		return FALSE;

	obj = g_list_model_get_item( gtk_single_selection_get_model( sel ), pos );
	if (obj == NULL)
		return FALSE;

	item = FSV_WP_LIST_ITEM(obj);
	if (row_type_out)
		*row_type_out = item->row_type;
	if (wpgroup_out)
		*wpgroup_out = item->wpgroup;
	if (wpattern_out)
		*wpattern_out = item->wpattern;
	g_object_unref( obj );

	return TRUE;
}


/* Callback for the buttons to the right of the wildcard pattern list */
static void
csdialog_wpattern_button_cb( GtkWidget *button_w )
{
	struct WPListRowData *row_data = NULL;
	RGBcolor default_new_color = { 0.0, 0.0, 0.75 }; /* I like blue */
	RGBcolor *color;
	const char *title = NULL;
	int row_type = -1;
	struct WPatternGroup *wpgroup = NULL;
	char *wpattern = NULL;
	boolean has_sel;

	has_sel = wplist_get_selected( &row_type, &wpgroup, &wpattern );

	if (button_w == csdialog.wpattern.new_color_button_w) {
		/* Bring up color selection dialog for new color group */
		title = _("New Color Group");
		if (!has_sel || wpgroup == NULL)
			color = &default_new_color;
		else
			color = &wpgroup->color;
		/* Build row_data for callback context */
		if (has_sel) {
			row_data = NEW(struct WPListRowData);
			row_data->row_type = row_type;
			row_data->wpgroup = wpgroup;
			row_data->wpattern = wpattern;
			row_data->hex_color = NULL;
		}
		gui_colorsel_window( title, color, G_CALLBACK(csdialog_wpattern_new_color_selection_cb), row_data );
	}
	else if (button_w == csdialog.wpattern.edit_pattern_button_w) {
		/* Bring up pattern edit subdialog */
		g_assert( has_sel );
		row_data = NEW(struct WPListRowData);
		row_data->row_type = row_type;
		row_data->wpgroup = wpgroup;
		row_data->wpattern = wpattern;
		row_data->hex_color = NULL;
		switch (row_type) {
			case WPLIST_WPATTERN_ROW:
			title = _("Edit Wildcard Pattern");
			break;

			case WPLIST_NEW_WPATTERN_ROW:
			title = _("New Wildcard Pattern");
			break;

			SWITCH_FAIL
		}
		gui_entry_window( title, wpattern, G_CALLBACK(csdialog_wpattern_edit_cb), row_data );
	}
	else if (button_w == csdialog.wpattern.delete_button_w) {
		/* Delete a pattern or color group */
		g_assert( has_sel );
		switch (row_type) {
			case WPLIST_WPATTERN_ROW:
			/* Delete pattern */
			G_LIST_REMOVE(wpgroup->wp_list, wpattern);
			xfree( wpattern );
			break;

			case WPLIST_NEW_WPATTERN_ROW:
			/* Delete color group ONLY if group is empty */
			if (wpgroup->wp_list != NULL)
				return;
			G_LIST_REMOVE(csdialog.color_config.by_wpattern.wpgroup_list, wpgroup);
			xfree( wpgroup );
			break;

			SWITCH_FAIL
		}
		/* Repopulate the list */
		csdialog_wpattern_clist_populate( );
	}
}


/* Callback for the "OK" button */
static void
csdialog_ok_button_cb( G_GNUC_UNUSED GtkWidget *unused, GtkWidget *window_w )
{
	ColorMode mode;

	/* Commit new color configuration, and set color mode to match
	 * current notebook page */
	mode = (ColorMode)gtk_notebook_get_current_page( GTK_NOTEBOOK(csdialog.notebook_w) );
	color_set_config( &csdialog.color_config, mode );

        /* Update option menu to reflect current color mode */
	window_set_color_mode( mode );

	gtk_window_destroy( GTK_WINDOW(window_w) );
}


/* Callback for dialog window destruction */
static void
csdialog_destroy_cb( G_GNUC_UNUSED GObject *unused )
{
	/* We'd leak memory like crazy if we didn't do this */
	color_config_destroy( &csdialog.color_config );
}


/* Draw function for GtkDrawingArea color swatches in the wildcard pattern list */
static void
wp_swatch_draw_func( GtkDrawingArea *area, cairo_t *cr,
                     int width, int height, G_GNUC_UNUSED gpointer user_data )
{
	const char *hex = g_object_get_data( G_OBJECT(area), "hex_color" );
	GdkRGBA rgba = { 0.5, 0.5, 0.5, 1.0 };

	if (hex != NULL)
		gdk_rgba_parse( &rgba, hex );

	gdk_cairo_set_source_rgba( cr, &rgba );
	cairo_rectangle( cr, 0, 0, width, height );
	cairo_fill( cr );
}


/* Factory setup: create the row widget structure */
static void
wp_factory_setup_cb( G_GNUC_UNUSED GtkSignalListItemFactory *factory,
                     GtkListItem *list_item,
                     G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *box_w, *swatch_w, *label_w;
	GtkGesture *click;

	box_w = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 4 );

	/* Color swatch (GtkDrawingArea) */
	swatch_w = gtk_drawing_area_new( );
	gtk_drawing_area_set_draw_func( GTK_DRAWING_AREA(swatch_w), wp_swatch_draw_func, NULL, NULL );
	gtk_widget_set_size_request( swatch_w, 40, 20 );

	/* Click gesture on swatch to change color */
	click = gtk_gesture_click_new( );
	gtk_gesture_single_set_button( GTK_GESTURE_SINGLE(click), 1 );
	g_signal_connect( click, "released", G_CALLBACK(wp_swatch_click_cb), NULL );
	gtk_widget_add_controller( swatch_w, GTK_EVENT_CONTROLLER(click) );

	gtk_box_append( GTK_BOX(box_w), swatch_w );

	/* Pattern label */
	label_w = gtk_label_new( "" );
	gtk_label_set_xalign( GTK_LABEL(label_w), 0.0 );
	gtk_widget_set_hexpand( label_w, TRUE );
	gtk_box_append( GTK_BOX(box_w), label_w );

	gtk_list_item_set_child( list_item, box_w );
}


/* Factory bind: populate row with data from FsvWPListItem */
static void
wp_factory_bind_cb( G_GNUC_UNUSED GtkSignalListItemFactory *factory,
                    GtkListItem *list_item,
                    G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *box_w = gtk_list_item_get_child( list_item );
	GtkWidget *swatch_w = gtk_widget_get_first_child( box_w );
	GtkWidget *label_w = gtk_widget_get_next_sibling( swatch_w );
	FsvWPListItem *item = gtk_list_item_get_item( list_item );
	const char *text = "";

	/* Store item reference on swatch for click handler */
	g_object_set_data( G_OBJECT(swatch_w), "wp_item", item );

	/* Store hex color string on swatch for draw function */
	g_object_set_data_full( G_OBJECT(swatch_w), "hex_color",
	                        g_strdup( item->hex_color ), g_free );

	/* Set label text and style based on row type */
	switch (item->row_type) {
		case WPLIST_HEADER_ROW:
		text = _("(color group)");
		break;

		case WPLIST_WPATTERN_ROW:
		text = item->wpattern;
		break;

		case WPLIST_NEW_WPATTERN_ROW:
		text = _("(new pattern)");
		break;

		case WPLIST_DEFAULT_HEADER_ROW:
		text = _("Default color");
		break;

		case WPLIST_DEFAULT_ROW:
		text = _("(all other files)");
		break;
	}

	gtk_label_set_text( GTK_LABEL(label_w), text );

	/* Make header rows bold */
	if (item->row_type == WPLIST_HEADER_ROW || item->row_type == WPLIST_DEFAULT_HEADER_ROW) {
		PangoAttrList *attrs = pango_attr_list_new( );
		pango_attr_list_insert( attrs, pango_attr_weight_new( PANGO_WEIGHT_BOLD ) );
		gtk_label_set_attributes( GTK_LABEL(label_w), attrs );
		pango_attr_list_unref( attrs );
	}
	else {
		gtk_label_set_attributes( GTK_LABEL(label_w), NULL );
	}

	/* Redraw swatch */
	gtk_widget_queue_draw( swatch_w );
}


/* Factory unbind: clear data from row */
static void
wp_factory_unbind_cb( G_GNUC_UNUSED GtkSignalListItemFactory *factory,
                      GtkListItem *list_item,
                      G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *box_w = gtk_list_item_get_child( list_item );
	GtkWidget *swatch_w = gtk_widget_get_first_child( box_w );

	g_object_set_data( G_OBJECT(swatch_w), "wp_item", NULL );
	g_object_set_data( G_OBJECT(swatch_w), "hex_color", NULL );
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

	window_w = gui_dialog_window( _("Color Setup"), NULL );
	gui_window_modalize( window_w, main_window_w );
	main_vbox_w = gui_vbox_add( window_w, 5 );
	csdialog.notebook_w = gui_notebook_add( main_vbox_w );

	/* Get current color mode/configuration */
        color_mode = color_get_mode( );
	color_get_config( &csdialog.color_config );


	/**** "By wildcards" page ****/

	hbox_w = gui_hbox_add( NULL, 10 );
	gui_notebook_page_add( csdialog.notebook_w, _("By wildcards"), hbox_w );

	/* Create a scrolled window with a GtkListView for the pattern list */
	{
		GtkWidget *scroll_w;
		GListStore *wp_store;
		GtkSingleSelection *sel;
		GtkListItemFactory *factory;
		GtkWidget *list_view_w;

		wp_store = g_list_store_new( FSV_TYPE_WP_LIST_ITEM );

		sel = gtk_single_selection_new( G_LIST_MODEL(wp_store) );
		gtk_single_selection_set_autoselect( sel, FALSE );
		gtk_single_selection_set_can_unselect( sel, TRUE );
		g_signal_connect( sel, "notify::selected", G_CALLBACK(csdialog_wpattern_selection_changed_cb), NULL );

		factory = gtk_signal_list_item_factory_new( );
		g_signal_connect( factory, "setup", G_CALLBACK(wp_factory_setup_cb), NULL );
		g_signal_connect( factory, "bind", G_CALLBACK(wp_factory_bind_cb), NULL );
		g_signal_connect( factory, "unbind", G_CALLBACK(wp_factory_unbind_cb), NULL );

		list_view_w = gtk_list_view_new( GTK_SELECTION_MODEL(sel), factory );
		gtk_list_view_set_show_separators( GTK_LIST_VIEW(list_view_w), TRUE );

		/* Store references for later access */
		g_object_set_data( G_OBJECT(list_view_w), "list_store", wp_store );
		g_object_set_data( G_OBJECT(list_view_w), "selection_model", sel );

		/* Put inside a scrolled window */
		scroll_w = gtk_scrolled_window_new( );
		gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(scroll_w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
		gtk_scrolled_window_set_child( GTK_SCROLLED_WINDOW(scroll_w), list_view_w );
		gtk_widget_set_hexpand( scroll_w, TRUE );
		gtk_widget_set_vexpand( scroll_w, TRUE );
		gtk_box_append( GTK_BOX(hbox_w), scroll_w );

		csdialog.wpattern.clist_w = list_view_w;
	}

	/* Action buttons */
	vbox_w = gui_vbox_add( hbox_w, 0 );
	csdialog.wpattern.new_color_button_w = gui_button_add( vbox_w, _("New color"), G_CALLBACK(csdialog_wpattern_button_cb), NULL );
	gui_separator_add( vbox_w );
	csdialog.wpattern.edit_pattern_button_w = gui_button_add( vbox_w, _("Edit pattern"), G_CALLBACK(csdialog_wpattern_button_cb), NULL );
	gtk_widget_set_sensitive( csdialog.wpattern.edit_pattern_button_w, FALSE );
	gui_separator_add( vbox_w );
	csdialog.wpattern.delete_button_w = gui_button_add( vbox_w, _("Delete"), G_CALLBACK(csdialog_wpattern_button_cb), NULL );
	gtk_widget_set_sensitive( csdialog.wpattern.delete_button_w, FALSE );

	csdialog_wpattern_clist_populate( );


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
		gui_pixmap_xpm_add( hbox_w, node_type_xpms[i] );
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


	/* Horizontal box for OK and Cancel buttons */
	hbox_w = gui_hbox_add( main_vbox_w, 0 );
	gtk_box_set_homogeneous( GTK_BOX(hbox_w), TRUE );
	gui_box_set_packing( hbox_w, EXPAND, FILL, AT_START );

	/* OK and Cancel buttons */
	gui_button_with_pixmap_xpm_add( hbox_w, button_ok_xpm, _("OK"), G_CALLBACK(csdialog_ok_button_cb), window_w );
	gui_hbox_add( hbox_w, 0 ); /* spacer */
	gui_button_with_pixmap_xpm_add( hbox_w, button_cancel_xpm, _("Cancel"), G_CALLBACK(close_cb), window_w );

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
	pixmap_w = gui_pixmap_xpm_add( hbox_w, node_type_xpms[NODE_DESC(node)->type] );
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
