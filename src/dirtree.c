/* dirtree.c */

/* Directory tree control */

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
#include "dirtree.h"

#include <gtk/gtk.h>

#include "about.h"
#include "camera.h"
#include "colexp.h"
#include "dialog.h"
#include "filelist.h"
#include "geometry.h"
#include "gui.h"
#include "window.h"


/* Time for the directory tree to scroll to a given entry (in seconds) */
#define DIRTREE_SCROLL_TIME 0.5

/* Indentation per tree depth level (pixels) */
#define INDENT_PER_LEVEL 16


/* The directory tree widget (GtkListView) */
static GtkWidget *dir_tree_w;

/* Model references (retrieved from list view widget data) */
static GListStore *root_store;
static GtkTreeListModel *tree_list_model;
static GtkSingleSelection *selection_model;

/* Current directory */
static GNode *dirtree_current_dnode;

/* TRUE while programmatic expand/collapse is in progress —
 * the notify::expanded handler still fires (updating icons)
 * but arrow click handler ignores clicks */
static boolean colexp_blocked = FALSE;


/* Find the flat-model position and GtkTreeListRow for a given dnode.
 * Returns the row (caller must g_object_unref) or NULL.
 * If out_position is non-NULL, stores the flat position. */
static GtkTreeListRow *
find_tree_list_row( GNode *dnode, guint *out_position )
{
	guint n = g_list_model_get_n_items( G_LIST_MODEL(tree_list_model) );
	guint i;

	for (i = 0; i < n; i++) {
		GtkTreeListRow *row = gtk_tree_list_model_get_row( tree_list_model, i );
		if (row == NULL)
			continue;
		gpointer item = gtk_tree_list_row_get_item( row );
		if (item != NULL && FSV_IS_DIR_ITEM(item)) {
			GNode *row_dnode = fsv_dir_item_get_dnode( FSV_DIR_ITEM(item) );
			g_object_unref( item );
			if (row_dnode == dnode) {
				if (out_position != NULL)
					*out_position = i;
				return row; /* caller must unref */
			}
		} else if (item != NULL) {
			g_object_unref( item );
		}
		g_object_unref( row );
	}
	return NULL;
}


/* Callback for click on the expand/collapse arrow button */
static void
arrow_clicked_cb( GtkButton *button, G_GNUC_UNUSED gpointer user_data )
{
	GNode *dnode;

	if (colexp_blocked)
		return;

	if (globals.fsv_mode == FSV_SPLASH)
		return;

	dnode = g_object_get_data( G_OBJECT(button), "dnode" );
	if (dnode == NULL)
		return;

	/* Toggle: if expanded, collapse; if collapsed, expand */
	if (DIR_NODE_DESC(dnode)->tree_expanded)
		colexp( dnode, COLEXP_COLLAPSE_RECURSIVE );
	else
		colexp( dnode, COLEXP_EXPAND );
}


/* Factory setup: create the per-row widget structure */
static void
factory_setup_cb( G_GNUC_UNUSED GtkSignalListItemFactory *factory,
                  GtkListItem *list_item,
                  G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *box, *arrow_btn, *icon_img, *label;

	box = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 4 );

	arrow_btn = gtk_button_new_from_icon_name( "pan-end-symbolic" );
	gtk_button_set_has_frame( GTK_BUTTON(arrow_btn), FALSE );
	gtk_widget_set_focusable( arrow_btn, FALSE );
	gtk_widget_set_visible( arrow_btn, FALSE );
	gtk_box_append( GTK_BOX(box), arrow_btn );

	icon_img = gtk_image_new_from_icon_name( "folder" );
	gtk_image_set_pixel_size( GTK_IMAGE(icon_img), 16 );
	gtk_box_append( GTK_BOX(box), icon_img );

	label = gtk_label_new( "" );
	gtk_label_set_xalign( GTK_LABEL(label), 0.0f );
	gtk_widget_set_hexpand( label, TRUE );
	gtk_box_append( GTK_BOX(box), label );

	gtk_list_item_set_child( list_item, box );
}


/* Update arrow and folder icons based on expansion state */
static void
update_row_icons( GtkWidget *box, boolean expanded, boolean expandable )
{
	GtkWidget *arrow_btn, *icon_img;

	arrow_btn = gtk_widget_get_first_child( box );
	icon_img = gtk_widget_get_next_sibling( arrow_btn );

	if (expandable) {
		gtk_widget_set_visible( arrow_btn, TRUE );
		gtk_button_set_icon_name( GTK_BUTTON(arrow_btn),
			expanded ? "pan-down-symbolic" : "pan-end-symbolic" );
	} else {
		gtk_widget_set_visible( arrow_btn, FALSE );
	}

	gtk_image_set_from_icon_name( GTK_IMAGE(icon_img),
		expanded ? "folder-open" : "folder" );
}


/* Callback for notify::expanded on a GtkTreeListRow — icon updates only */
static void
row_expanded_notify_cb( GtkTreeListRow *row,
                        G_GNUC_UNUSED GParamSpec *pspec,
                        GtkWidget *box )
{
	boolean expanded = gtk_tree_list_row_get_expanded( row );
	boolean expandable = gtk_tree_list_row_is_expandable( row );
	gpointer item = gtk_tree_list_row_get_item( row );

	/* Update the tree_expanded flag on the node */
	if (item != NULL && FSV_IS_DIR_ITEM(item)) {
		GNode *dnode = fsv_dir_item_get_dnode( FSV_DIR_ITEM(item) );
		if (dnode != NULL)
			DIR_NODE_DESC(dnode)->tree_expanded = expanded;
	}
	if (item != NULL)
		g_object_unref( item );

	update_row_icons( box, expanded, expandable );
}


/* Factory bind: populate widgets with data from the item */
static void
factory_bind_cb( G_GNUC_UNUSED GtkSignalListItemFactory *factory,
                 GtkListItem *list_item,
                 G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *box, *arrow_btn, *icon_img, *label;
	GtkTreeListRow *row;
	gpointer item;
	GNode *dnode;
	const char *name;
	guint depth;
	boolean expanded, expandable;

	box = gtk_list_item_get_child( list_item );
	row = GTK_TREE_LIST_ROW(gtk_list_item_get_item( list_item ));

	item = gtk_tree_list_row_get_item( row );
	if (item == NULL || !FSV_IS_DIR_ITEM(item)) {
		if (item != NULL)
			g_object_unref( item );
		return;
	}

	dnode = fsv_dir_item_get_dnode( FSV_DIR_ITEM(item) );
	g_object_unref( item );
	if (dnode == NULL)
		return;

	/* Set indentation based on tree depth */
	depth = gtk_tree_list_row_get_depth( row );
	gtk_widget_set_margin_start( box, depth * INDENT_PER_LEVEL );

	/* Set name */
	if (strlen( NODE_DESC(dnode)->name ) > 0)
		name = NODE_DESC(dnode)->name;
	else
		name = _("/. (root)");

	arrow_btn = gtk_widget_get_first_child( box );
	icon_img = gtk_widget_get_next_sibling( arrow_btn );
	label = gtk_widget_get_next_sibling( icon_img );
	gtk_label_set_text( GTK_LABEL(label), name );

	/* Set arrow and icon state */
	expanded = gtk_tree_list_row_get_expanded( row );
	expandable = gtk_tree_list_row_is_expandable( row );
	update_row_icons( box, expanded, expandable );

	/* Connect notify::expanded for icon updates */
	g_signal_connect( row, "notify::expanded",
		G_CALLBACK(row_expanded_notify_cb), box );

	/* Store dnode on the arrow button and connect click handler */
	g_object_set_data( G_OBJECT(arrow_btn), "dnode", dnode );
	g_signal_connect( arrow_btn, "clicked",
		G_CALLBACK(arrow_clicked_cb), NULL );
}


/* Factory unbind: disconnect signals */
static void
factory_unbind_cb( G_GNUC_UNUSED GtkSignalListItemFactory *factory,
                   GtkListItem *list_item,
                   G_GNUC_UNUSED gpointer user_data )
{
	GtkTreeListRow *row;

	row = GTK_TREE_LIST_ROW(gtk_list_item_get_item( list_item ));
	if (row != NULL) {
		GtkWidget *box = gtk_list_item_get_child( list_item );
		GtkWidget *arrow_btn = gtk_widget_get_first_child( box );

		g_signal_handlers_disconnect_by_func( row,
			G_CALLBACK(row_expanded_notify_cb), box );
		g_signal_handlers_disconnect_by_func( arrow_btn,
			G_CALLBACK(arrow_clicked_cb), NULL );
		g_object_set_data( G_OBJECT(arrow_btn), "dnode", NULL );
	}
}


/* Correspondence from window_init( ) */
void
dirtree_pass_widget( GtkWidget *tree_w )
{
	GtkListItemFactory *factory;

	dir_tree_w = tree_w;

	/* Retrieve model references stored by gui_ctree_add */
	root_store = g_object_get_data( G_OBJECT(dir_tree_w), "root_store" );
	tree_list_model = g_object_get_data( G_OBJECT(dir_tree_w), "tree_list_model" );
	selection_model = g_object_get_data( G_OBJECT(dir_tree_w), "selection_model" );

	/* Connect factory signals */
	factory = gtk_list_view_get_factory( GTK_LIST_VIEW(dir_tree_w) );
	g_signal_connect( factory, "setup", G_CALLBACK(factory_setup_cb), NULL );
	g_signal_connect( factory, "bind", G_CALLBACK(factory_bind_cb), NULL );
	g_signal_connect( factory, "unbind", G_CALLBACK(factory_unbind_cb), NULL );
}


/* Clears out all entries from the directory tree */
void
dirtree_clear( void )
{
	g_list_store_remove_all( root_store );
	dirtree_current_dnode = NULL;
}


/* Adds a new entry to the directory tree.
 * During scan, we just track the expansion state — the model is
 * populated in dirtree_no_more_entries after the scan completes. */
void
dirtree_entry_new( GNode *dnode )
{
	g_assert( NODE_IS_DIR(dnode) );

	DIR_NODE_DESC(dnode)->tree_expanded = FALSE;
}


/* Call this after the last call to dirtree_entry_new( ).
 * Populates the tree model from the GNode tree. */
void
dirtree_no_more_entries( void )
{
	FsvDirItem *root_item;

	/* Add root directory to the model — starts collapsed */
	root_item = fsv_dir_item_new( root_dnode );
	g_list_store_append( root_store, root_item );
	g_object_unref( root_item );
}


/* This updates the directory tree to show (and select) a particular
 * directory entry, repopulating the file list with the contents of the
 * directory if not already listed */
void
dirtree_entry_show( GNode *dnode )
{
	guint position;
	GtkTreeListRow *row;

	g_assert( NODE_IS_DIR(dnode) );

	/* Repopulate file list if directory is different */
	if (dnode != dirtree_current_dnode) {
		filelist_populate( dnode );
		gui_update( );
	}

	/* Find the row and select it */
	row = find_tree_list_row( dnode, &position );
	if (row != NULL) {
		gtk_single_selection_set_selected( selection_model, position );
		/* Scroll to the row */
		gtk_widget_activate_action( dir_tree_w, "list.scroll-to-item", "u", position );
		g_object_unref( row );
	}
	else {
		gtk_single_selection_set_selected( selection_model, GTK_INVALID_LIST_POSITION );
	}

	dirtree_current_dnode = dnode;
}


/* Returns TRUE if the entry for the given directory is expanded */
boolean
dirtree_entry_expanded( GNode *dnode )
{
	g_assert( NODE_IS_DIR(dnode) );
	return DIR_NODE_DESC(dnode)->tree_expanded;
}


/* Helper: recursively set tree_expanded = FALSE on a GNode subtree */
static void
clear_tree_expanded_recursive( GNode *dnode )
{
	GNode *child;

	DIR_NODE_DESC(dnode)->tree_expanded = FALSE;
	child = dnode->children;
	while (child != NULL) {
		if (NODE_IS_DIR(child))
			clear_tree_expanded_recursive( child );
		else
			break;
		child = child->next;
	}
}


/* Recursively collapses the directory tree entry of the given directory.
 * Collapsing the root of the subtree automatically removes all descendant
 * rows from the flat model, so we only need one set_expanded(FALSE) call
 * plus a GNode-tree walk to clear the flags. */
void
dirtree_entry_collapse_recursive( GNode *dnode )
{
	GtkTreeListRow *row;

	g_assert( NODE_IS_DIR(dnode) );

	colexp_blocked = TRUE;

	/* Clear all tree_expanded flags in the subtree */
	clear_tree_expanded_recursive( dnode );

	/* Collapse the root — all descendants disappear from the model */
	row = find_tree_list_row( dnode, NULL );
	if (row != NULL) {
		gtk_tree_list_row_set_expanded( row, FALSE );
		g_object_unref( row );
	}

	colexp_blocked = FALSE;
}


/* Expands the directory tree entry of the given directory. If any of its
 * ancestor directory entries are not expanded, then they are expanded
 * as well */
void
dirtree_entry_expand( GNode *dnode )
{
	GNode *ancestors[128];
	int n = 0;
	GNode *up_node;
	int i;

	g_assert( NODE_IS_DIR(dnode) );

	colexp_blocked = TRUE;

	/* Build list of ancestors (bottom-up) that need expanding */
	up_node = dnode;
	while (NODE_IS_DIR(up_node)) {
		ancestors[n++] = up_node;
		up_node = up_node->parent;
		if (n >= 128)
			break;
	}

	/* Expand top-down (reverse order) so that child rows become visible */
	for (i = n - 1; i >= 0; i--) {
		if (!DIR_NODE_DESC(ancestors[i])->tree_expanded) {
			GtkTreeListRow *row = find_tree_list_row( ancestors[i], NULL );
			if (row != NULL) {
				gtk_tree_list_row_set_expanded( row, TRUE );
				g_object_unref( row );
			}
			DIR_NODE_DESC(ancestors[i])->tree_expanded = TRUE;
		}
	}

	colexp_blocked = FALSE;
}


/* Helper: recursively expand a row and all its children.
 * Uses gtk_tree_list_row_get_child_row for O(1) child lookup
 * instead of scanning the flat model. */
static void
expand_recursive_via_row( GtkTreeListRow *row, GNode *dnode )
{
	GNode *child;
	guint child_idx;

	/* Expand this row first (making children visible in the model) */
	gtk_tree_list_row_set_expanded( row, TRUE );
	DIR_NODE_DESC(dnode)->tree_expanded = TRUE;

	/* Then expand each directory child via O(1) child_row lookup */
	child = dnode->children;
	child_idx = 0;
	while (child != NULL) {
		if (NODE_IS_DIR(child)) {
			GtkTreeListRow *child_row = gtk_tree_list_row_get_child_row( row, child_idx );
			if (child_row != NULL) {
				expand_recursive_via_row( child_row, child );
				g_object_unref( child_row );
			}
			child_idx++;
		}
		else
			break;
		child = child->next;
	}
}


/* Recursively expands the entire directory tree subtree of the given
 * directory */
void
dirtree_entry_expand_recursive( GNode *dnode )
{
	GtkTreeListRow *row;

	g_assert( NODE_IS_DIR(dnode) );

#if DEBUG
	/* Guard against expansions inside collapsed subtrees */
	if (NODE_IS_DIR(dnode->parent))
		g_assert( dirtree_entry_expanded( dnode->parent ) );
#endif

	colexp_blocked = TRUE;
	row = find_tree_list_row( dnode, NULL );
	if (row != NULL) {
		expand_recursive_via_row( row, dnode );
		g_object_unref( row );
	}
	colexp_blocked = FALSE;
}


/* end dirtree.c */
