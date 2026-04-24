/* filelist.c */

/* File list control */

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
#include "filelist.h"

#include <gtk/gtk.h>

#include "about.h"
#include "camera.h"
#include "dialog.h"
#include "dirtree.h"
#include "geometry.h"
#include "gui.h"
#include "window.h"


/* Time for the filelist to scroll to a given entry (in seconds) */
#define FILELIST_SCROLL_TIME 0.5


/* The file list widget (GtkColumnView) */
static GtkWidget *file_tree_w;

/* Directory currently listed */
static GNode *filelist_current_dnode;

/* Mini node type icons (GdkTexture) */
static Icon node_type_mini_icons[NUM_NODE_TYPES];


/* Loads the mini node type icons from GResource */
static void
filelist_icons_init( void )
{
	int i;

	for (i = 1; i < NUM_NODE_TYPES; i++) {
		node_type_mini_icons[i].texture = gdk_texture_new_from_resource( node_type_mini_icon_paths[i] );
	}
}


/* Correspondence from window_init( ) */
void
filelist_pass_widget( GtkWidget *tree_w )
{
	file_tree_w = tree_w;
	filelist_icons_init( );
}


/* Called from window_set_access to push a wait cursor onto the file
 * list pane when the window is busy. The non-busy state is restored
 * separately by filelist_reset_access when colexp reshuffles things. */
void
filelist_refresh_cursor( void )
{
	if (file_tree_w == NULL)
		return;
	if (window_is_busy( ))
		gui_cursor( file_tree_w, "wait" );
}


/* This makes entries in the file list selectable or unselectable,
 * depending on whether the directory they are in is expanded or not */
void
filelist_reset_access( void )
{
	boolean enabled;

	enabled = dirtree_entry_expanded( filelist_current_dnode );
	gtk_widget_set_sensitive( file_tree_w, enabled );

	/* Extra fluff for interface niceness */
	if (enabled)
		gui_cursor( file_tree_w, NULL );
	else {
		gui_clist_select_row( file_tree_w, -1 );
		gui_cursor( file_tree_w, "not-allowed" );
	}
}


/* Compare function for sorting nodes alphabetically */
static int
compare_node( GNode *a, GNode *b )
{
	return strcmp( NODE_DESC(a)->name, NODE_DESC(b)->name );
}


/* Displays contents of a directory in the file list */
void
filelist_populate( GNode *dnode )
{
	GNode *node;
	GList *node_list = NULL, *node_llink;
	Icon *icon;
	int count = 0;
	char strbuf[64];

	g_assert( NODE_IS_DIR(dnode) );

	/* Get an alphabetized list of directory's immediate children */
	node = dnode->children;
	while (node != NULL) {
		G_LIST_PREPEND(node_list, node);
		node = node->next;
	}
	G_LIST_SORT(node_list, compare_node);

	/* Update file list */
	gui_clist_clear( file_tree_w );
	node_llink = node_list;
	while (node_llink != NULL) {
		node = (GNode *)node_llink->data;
		icon = &node_type_mini_icons[NODE_DESC(node)->type];

		{
			const char *text[] = { NODE_DESC(node)->name };
			gui_clist_append( file_tree_w, icon->texture, text, 1, node );
		}

		++count;
		node_llink = node_llink->next;
	}

	g_list_free( node_list );

	/* Set node count message in the left statusbar */
	switch (count) {
		case 0:
		strcpy( strbuf, "" );
		break;

		case 1:
		strcpy( strbuf, _("1 node") );
		break;

		default:
		sprintf( strbuf, _("%d nodes"), count );
		break;
	}
	window_statusbar( SB_LEFT, strbuf );

	filelist_current_dnode = dnode;
	filelist_reset_access( );
}


/* This updates the file list to show (and select) a particular node
 * entry. The directory tree is also updated appropriately */
void
filelist_show_entry( GNode *node )
{
	GNode *dnode;
	int pos;

	/* Corresponding directory */
	if (NODE_IS_DIR(node))
		dnode = node;
	else
		dnode = node->parent;

	if (dnode != filelist_current_dnode) {
		/* Scroll directory tree to proper entry */
		dirtree_entry_show( dnode );
	}

	/* Scroll file list to proper entry */
	pos = gui_clist_find_by_data( file_tree_w, node );
	if (pos >= 0) {
		gui_clist_select_row( file_tree_w, pos );
		gui_clist_moveto_row( file_tree_w, pos, 0.0 );
	}
	else {
		gui_clist_select_row( file_tree_w, -1 );
	}
}


/* Callback for selection change in the file list */
static void
filelist_selection_changed_cb( GtkSingleSelection *sel, G_GNUC_UNUSED GParamSpec *pspec, G_GNUC_UNUSED gpointer user_data )
{
	guint position;
	GNode *node;

	/* If About presentation is up, end it */
	about( ABOUT_END );

	if (globals.fsv_mode == FSV_SPLASH)
		return;

	position = gtk_single_selection_get_selected( sel );
	if (position == GTK_INVALID_LIST_POSITION)
		return;

	node = (GNode *)gui_clist_get_row_data( file_tree_w, (int)position );
	if (node == NULL)
		return;

	geometry_highlight_node( node, FALSE );
	window_statusbar( SB_RIGHT, node_hover_label( node ) );
}


/* Callback for double-click (activate) in the file list */
static void
filelist_activate_cb( GtkColumnView *column_view, guint position, G_GNUC_UNUSED gpointer user_data )
{
	GNode *node;

	if (globals.fsv_mode == FSV_SPLASH)
		return;

	node = (GNode *)gui_clist_get_row_data( GTK_WIDGET(column_view), (int)position );
	if (node != NULL)
		camera_look_at( node );
}


/* Callback for right-click in the file list */
static void
filelist_right_click_cb( GtkGestureClick *gesture, G_GNUC_UNUSED int n_press,
                         double x, double y, G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *cv_w = gtk_event_controller_get_widget( GTK_EVENT_CONTROLLER(gesture) );
	int pos;
	GNode *node;

	if (globals.fsv_mode == FSV_SPLASH)
		return;

	pos = gui_clist_get_selected( cv_w );
	if (pos < 0)
		return;

	node = (GNode *)gui_clist_get_row_data( cv_w, pos );
	if (node == NULL)
		return;

	geometry_highlight_node( node, FALSE );
	window_statusbar( SB_RIGHT, node_hover_label( node ) );
	context_menu( node, cv_w, x, y );
}


/* Creates/initializes the file list widget */
void
filelist_init( void )
{
	GtkWidget *parent_w;
	GtkSingleSelection *sel;
	GtkGesture *click;

	/* Replace current widget with a single-column one */
	{
		GtkWidget *old_scroll_w = gtk_widget_get_parent( file_tree_w );
		parent_w = gtk_widget_get_parent( old_scroll_w );
		gtk_paned_set_end_child( GTK_PANED(parent_w), NULL );
	}
	file_tree_w = gui_clist_add( parent_w, 1, NULL );

	/* Connect selection-changed for single-click highlight */
	sel = g_object_get_data( G_OBJECT(file_tree_w), "selection_model" );
	g_signal_connect( sel, "notify::selected", G_CALLBACK(filelist_selection_changed_cb), NULL );

	/* Connect activate for double-click */
	g_signal_connect( file_tree_w, "activate", G_CALLBACK(filelist_activate_cb), NULL );

	/* Connect right-click gesture for context menu */
	click = gtk_gesture_click_new( );
	gtk_gesture_single_set_button( GTK_GESTURE_SINGLE(click), 3 );
	g_signal_connect( click, "pressed", G_CALLBACK(filelist_right_click_cb), NULL );
	gtk_widget_add_controller( file_tree_w, GTK_EVENT_CONTROLLER(click) );

	filelist_populate( root_dnode );

	/* Do this so that directory tree gets scrolled to the top at
	 * end of initial camera pan (right after filesystem scan) */
	filelist_current_dnode = NULL;
}


/* This replaces the file list widget with another one made specifically
 * to monitor the progress of an impending scan */
void
filelist_scan_monitor_init( void )
{
	char *col_titles[3];
	GtkWidget *parent_w;
	Icon *icon;
	int i;

	col_titles[0] = _("Type");
	col_titles[1] = _("Found");
	col_titles[2] = _("Bytes");

	/* Replace current widget with a 3-column one */
	{
		GtkWidget *old_scroll_w = gtk_widget_get_parent( file_tree_w );
		parent_w = gtk_widget_get_parent( old_scroll_w );
		gtk_paned_set_end_child( GTK_PANED(parent_w), NULL );
	}
	file_tree_w = gui_clist_add( parent_w, 3, col_titles );

	/* Place icons and static text */
	for (i = 1; i <= NUM_NODE_TYPES; i++) {
		if (i < NUM_NODE_TYPES) {
			icon = &node_type_mini_icons[i];
			const char *text[] = { _(node_type_plural_names[i]), "", "" };
			gui_clist_append( file_tree_w, icon->texture, text, 3, NULL );
		}
		else {
			const char *text[] = { _("TOTAL"), "", "" };
			gui_clist_append( file_tree_w, NULL, text, 3, NULL );
		}
	}
}


/* Updates the scan-monitoring file list with the given values */
void
filelist_scan_monitor( int *node_counts, int64 *size_counts )
{
	const char *str;
	int64 size_total = 0;
	int node_total = 0;
	int i;

	for (i = 1; i <= NUM_NODE_TYPES; i++) {
		int row = i - 1;

		/* Column 1: Found count */
		if (i < NUM_NODE_TYPES) {
			str = i64toa( node_counts[i] );
			node_total += node_counts[i];
		}
		else
			str = i64toa( node_total );
		gui_clist_set_row_text( file_tree_w, row, 1, str );

		/* Column 2: Bytes */
		if (i < NUM_NODE_TYPES) {
			str = i64toa( size_counts[i] );
			size_total += size_counts[i];
		}
		else
			str = i64toa( size_total );
		gui_clist_set_row_text( file_tree_w, row, 2, str );
	}
}


/* Creates the list widget used in the "Contents" page of the Properties
 * dialog for a directory. Returns a scrolled window containing the list. */
GtkWidget *
dir_contents_list( GNode *dnode )
{
	char *col_titles[2];
	GtkWidget *clist_w;
	Icon *icon;
	int i;

	g_assert( NODE_IS_DIR(dnode) );

	col_titles[0] = _("Node type");
	col_titles[1] = _("Quantity");

	/* Create a 2-column list (NULL parent — caller will parent it) */
	clist_w = gui_clist_add( NULL, 2, col_titles );

	/* Populate */
	for (i = 1; i < NUM_NODE_TYPES; i++) {
		icon = &node_type_mini_icons[i];
		const char *text[] = {
			_(node_type_plural_names[i]),
			(char *)i64toa( DIR_NODE_DESC(dnode)->subtree.counts[i] )
		};
		gui_clist_append( clist_w, icon->texture, text, 2, NULL );
	}

	/* Return the scrolled window (parent of the column view) */
	return gtk_widget_get_parent( clist_w );
}


/* end filelist.c */
