/* gui.c */

/* Higher-level GTK interface */

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
#include <gtk/gtk.h>
#include "gui.h"

#include "animation.h"
#include "ogl.h" /* ogl_widget_new( ) */
#include "window.h" /* window_is_busy( ) */


/* Box packing flags (stored via g_object_set_data on box widgets) */
enum {
	GUI_PACK_EXPAND	= 1 << 0,
	GUI_PACK_FILL	= 1 << 1
};


/* Process pending GTK events */
void
gui_update( void )
{
	while (g_main_context_pending( NULL ))
		g_main_context_iteration( NULL, FALSE );
}


/* This checks if the widget associated with the given adjustment is
 * currently busy redrawing/reconfiguring itself, or is in steady state */
boolean
gui_adjustment_widget_busy( GtkAdjustment *adj )
{
	static const double threshold = (1.0 / 18.0);
	double t_prev;
	double t_now;
	double *tp;

	t_now = xgettime( );

	tp = g_object_get_data( G_OBJECT(adj), "t_prev" );
	if (tp == NULL) {
		tp = NEW(double);
		*tp = t_now;
		g_object_set_data_full( G_OBJECT(adj), "t_prev", tp, _xfree );
		return FALSE;
	}

	t_prev = *tp;

	if ((t_now - t_prev) > threshold) {
		*tp = t_now;
		return FALSE;
	}

	return TRUE;
}


/* Step/end callback used in animating a GtkAdjustment */
static void
adjustment_step_cb( Morph *morph )
{
	GtkAdjustment *adj;
	double anim_value;

	adj = (GtkAdjustment *)morph->data;
	g_return_if_fail( GTK_IS_ADJUSTMENT(adj) );
	anim_value = *(morph->var);
	if (!gui_adjustment_widget_busy( adj ) || (ABS(morph->end_value - anim_value) < EPSILON))
		gtk_adjustment_set_value( adj, anim_value );
}


/* Creates an integer-valued adjustment */
GtkAdjustment *
gui_int_adjustment( int value, int lower, int upper )
{
	return (GtkAdjustment *)gtk_adjustment_new( (float)value, (float)lower, (float)upper, 1.0, 1.0, 1.0 );
}


/* This places child_w into parent_w intelligently. In GTK4, box packing
 * is done with gtk_box_append. Expand/fill behavior is set via widget
 * properties (hexpand/vexpand/halign/valign). */
static void
parent_child_full( GtkWidget *parent_w, GtkWidget *child_w, boolean expand, boolean fill )
{
	bitfield *packing_flags;

	if (parent_w == NULL)
		return;

	if (GTK_IS_BOX(parent_w)) {
		packing_flags = g_object_get_data( G_OBJECT(parent_w), "packing_flags" );
		if (packing_flags != NULL) {
			expand = *packing_flags & GUI_PACK_EXPAND;
			fill = *packing_flags & GUI_PACK_FILL;
		}
		/* Set expand/fill via widget properties */
		if (GTK_IS_ORIENTABLE(parent_w)) {
			GtkOrientation orient = gtk_orientable_get_orientation( GTK_ORIENTABLE(parent_w) );
			if (orient == GTK_ORIENTATION_HORIZONTAL) {
				gtk_widget_set_hexpand( child_w, expand );
				if (expand && fill)
					gtk_widget_set_halign( child_w, GTK_ALIGN_FILL );
				else if (expand)
					gtk_widget_set_halign( child_w, GTK_ALIGN_CENTER );
			}
			else {
				gtk_widget_set_vexpand( child_w, expand );
				if (expand && fill)
					gtk_widget_set_valign( child_w, GTK_ALIGN_FILL );
				else if (expand)
					gtk_widget_set_valign( child_w, GTK_ALIGN_CENTER );
			}
		}
		gtk_box_append( GTK_BOX(parent_w), child_w );
	}
	else if (GTK_IS_FRAME(parent_w))
		gtk_frame_set_child( GTK_FRAME(parent_w), child_w );
	else if (GTK_IS_SCROLLED_WINDOW(parent_w))
		gtk_scrolled_window_set_child( GTK_SCROLLED_WINDOW(parent_w), child_w );
	else if (GTK_IS_WINDOW(parent_w))
		gtk_window_set_child( GTK_WINDOW(parent_w), child_w );
	else if (GTK_IS_BUTTON(parent_w))
		gtk_button_set_child( GTK_BUTTON(parent_w), child_w );
	else if (GTK_IS_PANED(parent_w)) {
		/* For paned widgets, append to the first empty slot */
		if (gtk_paned_get_start_child( GTK_PANED(parent_w) ) == NULL)
			gtk_paned_set_start_child( GTK_PANED(parent_w), child_w );
		else
			gtk_paned_set_end_child( GTK_PANED(parent_w), child_w );
	}
	else if (GTK_IS_NOTEBOOK(parent_w))
		gtk_notebook_append_page( GTK_NOTEBOOK(parent_w), child_w, NULL );
	/* No gtk_widget_show needed in GTK4 */
}


/* Calls parent_child_full( ) with defaults */
static void
parent_child( GtkWidget *parent_w, GtkWidget *child_w )
{
	parent_child_full( parent_w, child_w, NO_EXPAND, NO_FILL );
}


/* The horizontal box widget */
GtkWidget *
gui_hbox_add( GtkWidget *parent_w, int spacing )
{
	GtkWidget *hbox_w;

	hbox_w = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, spacing );
	if (spacing > 0) {
		gtk_widget_set_margin_start( hbox_w, spacing );
		gtk_widget_set_margin_end( hbox_w, spacing );
		gtk_widget_set_margin_top( hbox_w, spacing );
		gtk_widget_set_margin_bottom( hbox_w, spacing );
	}
	parent_child( parent_w, hbox_w );

	return hbox_w;
}


/* The vertical box widget */
GtkWidget *
gui_vbox_add( GtkWidget *parent_w, int spacing )
{
	GtkWidget *vbox_w;

	vbox_w = gtk_box_new( GTK_ORIENTATION_VERTICAL, spacing );
	if (spacing > 0) {
		gtk_widget_set_margin_start( vbox_w, spacing );
		gtk_widget_set_margin_end( vbox_w, spacing );
		gtk_widget_set_margin_top( vbox_w, spacing );
		gtk_widget_set_margin_bottom( vbox_w, spacing );
	}
	parent_child( parent_w, vbox_w );

	return vbox_w;
}


/* Changes a box widget's default packing flags */
void
gui_box_set_packing( GtkWidget *box_w, boolean expand, boolean fill, G_GNUC_UNUSED boolean start )
{
	static const char data_key[] = "packing_flags";
	bitfield *packing_flags;

	g_assert( GTK_IS_BOX(box_w) );
	g_assert( expand || !fill );

	packing_flags = g_object_get_data( G_OBJECT(box_w), data_key );
	if (packing_flags == NULL) {
		packing_flags = NEW(bitfield);
		g_object_set_data_full( G_OBJECT(box_w), data_key, packing_flags, _xfree );
	}

	*packing_flags = 0;
	*packing_flags |= (expand ? GUI_PACK_EXPAND : 0);
	*packing_flags |= (fill ? GUI_PACK_FILL : 0);
	/* start/end distinction removed in GTK4 — always appends */
}


/* The standard button widget */
GtkWidget *
gui_button_add( GtkWidget *parent_w, const char *label, GCallback callback, void *callback_data )
{
	GtkWidget *button_w;

	button_w = gtk_button_new( );
	if (label != NULL)
		gui_label_add( button_w, label );
	if (callback != NULL)
		g_signal_connect( G_OBJECT(button_w), "clicked", G_CALLBACK(callback), callback_data );
	parent_child( parent_w, button_w );

	return button_w;
}


/* Creates a button with an icon prepended to the label */
GtkWidget *
gui_button_with_icon_add( GtkWidget *parent_w, const char *resource_path, const char *label, GCallback callback, void *callback_data )
{
	GtkWidget *button_w;
	GtkWidget *hbox_w, *hbox2_w;

	button_w = gtk_button_new( );
	parent_child( parent_w, button_w );
	hbox_w = gui_hbox_add( button_w, 0 );
	hbox2_w = gui_hbox_add( hbox_w, 0 );
	gui_widget_packing( hbox2_w, EXPAND, NO_FILL, AT_START );
	gui_resource_image_add( hbox2_w, resource_path );
	if (label != NULL) {
		gui_vbox_add( hbox2_w, 2 ); /* spacer */
		gui_label_add( hbox2_w, label );
	}
	g_signal_connect( G_OBJECT(button_w), "clicked", G_CALLBACK(callback), callback_data );

	return button_w;
}


/* The toggle button widget */
GtkWidget *
gui_toggle_button_add( GtkWidget *parent_w, const char *label, boolean active, GCallback callback, void *callback_data )
{
	GtkWidget *tbutton_w;

	tbutton_w = gtk_toggle_button_new( );
	if (label != NULL)
		gui_label_add( tbutton_w, label );
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(tbutton_w), active );
	g_signal_connect( G_OBJECT(tbutton_w), "toggled", G_CALLBACK(callback), callback_data );
	parent_child( parent_w, tbutton_w );

	return tbutton_w;
}


/**** FsvListRow — GObject for GListStore-backed list views ****/

struct _FsvListRow {
	GObject parent;
	GdkTexture *icon;
	char *col_text[4];
	gpointer data;
};

#define FSV_TYPE_LIST_ROW (fsv_list_row_get_type( ))
G_DECLARE_FINAL_TYPE(FsvListRow, fsv_list_row, FSV, LIST_ROW, GObject)
G_DEFINE_TYPE(FsvListRow, fsv_list_row, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_ICON,
	PROP_TEXT0,
	PROP_TEXT1,
	PROP_TEXT2,
	PROP_TEXT3,
	N_PROPERTIES
};
static GParamSpec *list_row_properties[N_PROPERTIES];

static void
fsv_list_row_finalize( GObject *obj )
{
	FsvListRow *row = FSV_LIST_ROW(obj);
	int i;

	g_clear_object( &row->icon );
	for (i = 0; i < 4; i++)
		g_free( row->col_text[i] );
	G_OBJECT_CLASS(fsv_list_row_parent_class)->finalize( obj );
}

static void
fsv_list_row_set_property( GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec )
{
	FsvListRow *row = FSV_LIST_ROW(obj);
	int col;

	switch (prop_id) {
		case PROP_ICON:
		g_clear_object( &row->icon );
		row->icon = g_value_dup_object( value );
		break;

		case PROP_TEXT0: case PROP_TEXT1: case PROP_TEXT2: case PROP_TEXT3:
		col = prop_id - PROP_TEXT0;
		g_free( row->col_text[col] );
		row->col_text[col] = g_value_dup_string( value );
		break;

		default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID( obj, prop_id, pspec );
	}
}

static void
fsv_list_row_get_property( GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec )
{
	FsvListRow *row = FSV_LIST_ROW(obj);
	int col;

	switch (prop_id) {
		case PROP_ICON:
		g_value_set_object( value, row->icon );
		break;

		case PROP_TEXT0: case PROP_TEXT1: case PROP_TEXT2: case PROP_TEXT3:
		col = prop_id - PROP_TEXT0;
		g_value_set_string( value, row->col_text[col] );
		break;

		default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID( obj, prop_id, pspec );
	}
}

static void
fsv_list_row_class_init( FsvListRowClass *klass )
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

	gobject_class->finalize = fsv_list_row_finalize;
	gobject_class->set_property = fsv_list_row_set_property;
	gobject_class->get_property = fsv_list_row_get_property;

	list_row_properties[PROP_ICON] = g_param_spec_object( "icon", NULL, NULL, GDK_TYPE_TEXTURE, G_PARAM_READWRITE );
	list_row_properties[PROP_TEXT0] = g_param_spec_string( "text0", NULL, NULL, NULL, G_PARAM_READWRITE );
	list_row_properties[PROP_TEXT1] = g_param_spec_string( "text1", NULL, NULL, NULL, G_PARAM_READWRITE );
	list_row_properties[PROP_TEXT2] = g_param_spec_string( "text2", NULL, NULL, NULL, G_PARAM_READWRITE );
	list_row_properties[PROP_TEXT3] = g_param_spec_string( "text3", NULL, NULL, NULL, G_PARAM_READWRITE );
	g_object_class_install_properties( gobject_class, N_PROPERTIES, list_row_properties );
}

static void
fsv_list_row_init( G_GNUC_UNUSED FsvListRow *row )
{
}


/**** GtkColumnView factory callbacks ****/

/* Factory setup for column 0: icon + text */
static void
clist_col0_setup_cb( G_GNUC_UNUSED GtkListItemFactory *factory, GtkListItem *list_item, G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *box = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 4 );
	GtkWidget *image = gtk_image_new( );
	GtkWidget *label = gtk_label_new( NULL );

	gtk_label_set_xalign( GTK_LABEL(label), 0.0 );
	gtk_widget_set_hexpand( label, TRUE );
	gtk_box_append( GTK_BOX(box), image );
	gtk_box_append( GTK_BOX(box), label );
	gtk_list_item_set_child( list_item, box );
}

/* Factory bind for column 0: set icon from item (direct — icons do not
 * update in-place), and bind text0 via property so label refreshes on
 * row property notify. */
static void
clist_col0_bind_cb( G_GNUC_UNUSED GtkListItemFactory *factory, GtkListItem *list_item, G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *box = gtk_list_item_get_child( list_item );
	GtkWidget *image = gtk_widget_get_first_child( box );
	GtkWidget *label = gtk_widget_get_next_sibling( image );
	FsvListRow *row = FSV_LIST_ROW(gtk_list_item_get_item( list_item ));
	GBinding *binding;

	if (row->icon != NULL)
		gtk_image_set_from_paintable( GTK_IMAGE(image), GDK_PAINTABLE(row->icon) );
	else
		gtk_image_clear( GTK_IMAGE(image) );

	binding = g_object_bind_property( row, "text0", label, "label", G_BINDING_SYNC_CREATE );
	g_object_set_data( G_OBJECT(list_item), "text-binding", binding );
}

/* Factory unbind for column 0: release the text binding, clear icon */
static void
clist_col0_unbind_cb( G_GNUC_UNUSED GtkListItemFactory *factory, GtkListItem *list_item, G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *box = gtk_list_item_get_child( list_item );
	GtkWidget *image = gtk_widget_get_first_child( box );
	GBinding *binding;

	binding = g_object_get_data( G_OBJECT(list_item), "text-binding" );
	if (binding != NULL) g_binding_unbind( binding );
	g_object_set_data( G_OBJECT(list_item), "text-binding", NULL );

	gtk_image_clear( GTK_IMAGE(image) );
}

/* Factory setup for text-only columns */
static void
clist_text_setup_cb( G_GNUC_UNUSED GtkListItemFactory *factory, GtkListItem *list_item, G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *label = gtk_label_new( NULL );
	gtk_label_set_xalign( GTK_LABEL(label), 0.0 );
	gtk_list_item_set_child( list_item, label );
}

/* Factory bind for text-only columns — uses a property binding so that
 * updating the row's text property via g_object_set propagates to the
 * label without requiring items-changed to re-fire bind. */
static void
clist_text_bind_cb( GtkListItemFactory *factory, GtkListItem *list_item, G_GNUC_UNUSED gpointer user_data )
{
	GtkWidget *label = gtk_list_item_get_child( list_item );
	FsvListRow *row = FSV_LIST_ROW(gtk_list_item_get_item( list_item ));
	int col = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(factory), "col_index" ));
	char prop_name[16];
	GBinding *binding;

	if (col < 0 || col >= 4) {
		gtk_label_set_text( GTK_LABEL(label), "" );
		return;
	}

	g_snprintf( prop_name, sizeof(prop_name), "text%d", col );
	binding = g_object_bind_property( row, prop_name, label, "label", G_BINDING_SYNC_CREATE );
	g_object_set_data( G_OBJECT(list_item), "text-binding", binding );
}

/* Factory unbind for text-only columns: release the property binding */
static void
clist_text_unbind_cb( G_GNUC_UNUSED GtkListItemFactory *factory, GtkListItem *list_item, G_GNUC_UNUSED gpointer user_data )
{
	GBinding *binding = g_object_get_data( G_OBJECT(list_item), "text-binding" );
	if (binding != NULL) g_binding_unbind( binding );
	g_object_set_data( G_OBJECT(list_item), "text-binding", NULL );
}


/* The [multi-column] list widget (fitted into a scrolled window).
 * Returns a GtkColumnView backed by a GListStore of FsvListRow.
 * Column 0 shows icon + text; remaining columns show text only. */
GtkWidget *
gui_clist_add( GtkWidget *parent_w, int num_cols, char *col_titles[] )
{
	GtkWidget *scrollwin_w;
	GtkWidget *column_view_w;
	GListStore *store;
	GtkSingleSelection *sel_model;
	GtkListItemFactory *factory;
	GtkColumnViewColumn *column;
	int i;

	scrollwin_w = gtk_scrolled_window_new( );
	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(scrollwin_w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
	gtk_widget_add_css_class( scrollwin_w, "view" );
	parent_child_full( parent_w, scrollwin_w, EXPAND, FILL );

	store = g_list_store_new( FSV_TYPE_LIST_ROW );
	sel_model = gtk_single_selection_new( G_LIST_MODEL(store) );
	gtk_single_selection_set_autoselect( sel_model, FALSE );
	gtk_single_selection_set_can_unselect( sel_model, TRUE );

	column_view_w = gtk_column_view_new( GTK_SELECTION_MODEL(sel_model) );
	gtk_column_view_set_show_column_separators( GTK_COLUMN_VIEW(column_view_w), FALSE );
	gtk_column_view_set_show_row_separators( GTK_COLUMN_VIEW(column_view_w), FALSE );

	/* Column 0: icon + text */
	factory = gtk_signal_list_item_factory_new( );
	g_signal_connect( factory, "setup", G_CALLBACK(clist_col0_setup_cb), NULL );
	g_signal_connect( factory, "bind", G_CALLBACK(clist_col0_bind_cb), NULL );
	g_signal_connect( factory, "unbind", G_CALLBACK(clist_col0_unbind_cb), NULL );
	column = gtk_column_view_column_new( (col_titles != NULL) ? col_titles[0] : NULL, factory );
	gtk_column_view_column_set_resizable( column, TRUE );
	gtk_column_view_column_set_expand( column, TRUE );
	gtk_column_view_append_column( GTK_COLUMN_VIEW(column_view_w), column );
	g_object_unref( column );

	/* Additional text columns */
	for (i = 1; i < num_cols; i++) {
		factory = gtk_signal_list_item_factory_new( );
		g_object_set_data( G_OBJECT(factory), "col_index", GINT_TO_POINTER(i) );
		g_signal_connect( factory, "setup", G_CALLBACK(clist_text_setup_cb), NULL );
		g_signal_connect( factory, "bind", G_CALLBACK(clist_text_bind_cb), NULL );
		g_signal_connect( factory, "unbind", G_CALLBACK(clist_text_unbind_cb), NULL );
		column = gtk_column_view_column_new( (col_titles != NULL) ? col_titles[i] : NULL, factory );
		gtk_column_view_column_set_resizable( column, TRUE );
		gtk_column_view_append_column( GTK_COLUMN_VIEW(column_view_w), column );
		g_object_unref( column );
	}

	/* Show headers only if titles were provided */
	gtk_column_view_set_show_column_separators( GTK_COLUMN_VIEW(column_view_w), col_titles != NULL );

	/* Store references for callers */
	g_object_set_data( G_OBJECT(column_view_w), "list_store", store );
	g_object_set_data( G_OBJECT(column_view_w), "selection_model", sel_model );

	gtk_scrolled_window_set_child( GTK_SCROLLED_WINDOW(scrollwin_w), column_view_w );

	return column_view_w;
}


/* Scrolls a list widget to a given row (-1 indicates last row). */
void
gui_clist_moveto_row( GtkWidget *widget, int row, double moveto_time )
{
	GtkAdjustment *vadj;
	GtkWidget *scrollwin_w;
	double *anim_value_var;
	float k, new_value;
	int n_rows;
	GListStore *store;

	store = g_object_get_data( G_OBJECT(widget), "list_store" );
	g_return_if_fail( store != NULL );
	n_rows = (int)g_list_model_get_n_items( G_LIST_MODEL(store) );

	if (n_rows == 0)
		return;
	if (row < 0)
		row = n_rows - 1;
	if (row >= n_rows)
		row = n_rows - 1;

	scrollwin_w = gtk_widget_get_parent( widget );
	if (!GTK_IS_SCROLLED_WINDOW(scrollwin_w))
		return;
	vadj = gtk_scrolled_window_get_vadjustment( GTK_SCROLLED_WINDOW(scrollwin_w) );

	k = (double)row / (double)n_rows;
	k = k * gtk_adjustment_get_upper( vadj ) - 0.5 * gtk_adjustment_get_page_size( vadj );
	new_value = CLAMP(k, 0.0, gtk_adjustment_get_upper( vadj ) - gtk_adjustment_get_page_size( vadj ));

	if (moveto_time <= 0.0) {
		gtk_adjustment_set_value( vadj, new_value );
		return;
	}

	anim_value_var = g_object_get_data( G_OBJECT(vadj), "anim_value_var" );
	if (anim_value_var == NULL) {
		anim_value_var = NEW(double);
		g_object_set_data_full( G_OBJECT(vadj), "anim_value_var", anim_value_var, _xfree );
	}

	morph_break( anim_value_var );
	*anim_value_var = gtk_adjustment_get_value( vadj );
	morph_full( anim_value_var, MORPH_SIGMOID, new_value, moveto_time, adjustment_step_cb, adjustment_step_cb, vadj );
}


/**** gui_clist helper functions ****/

/* Removes all rows from a clist */
void
gui_clist_clear( GtkWidget *clist_w )
{
	GListStore *store = g_object_get_data( G_OBJECT(clist_w), "list_store" );
	g_list_store_remove_all( store );
}


/* Appends a row to a clist. text[] must have at least num_cols entries. */
void
gui_clist_append( GtkWidget *clist_w, GdkTexture *icon, const char *text[], int num_text, gpointer data )
{
	GListStore *store = g_object_get_data( G_OBJECT(clist_w), "list_store" );
	FsvListRow *row = g_object_new( FSV_TYPE_LIST_ROW, NULL );
	int i;

	if (icon != NULL)
		row->icon = g_object_ref( icon );
	for (i = 0; i < num_text && i < 4; i++)
		row->col_text[i] = g_strdup( text[i] );
	row->data = data;
	g_list_store_append( store, row );
	g_object_unref( row );
}


/* Returns the number of rows in a clist */
int
gui_clist_get_n_rows( GtkWidget *clist_w )
{
	GListStore *store = g_object_get_data( G_OBJECT(clist_w), "list_store" );
	return (int)g_list_model_get_n_items( G_LIST_MODEL(store) );
}


/* Returns the data pointer from row at position */
gpointer
gui_clist_get_row_data( GtkWidget *clist_w, int position )
{
	GListStore *store = g_object_get_data( G_OBJECT(clist_w), "list_store" );
	FsvListRow *row = g_list_model_get_item( G_LIST_MODEL(store), (guint)position );
	gpointer data;

	if (row == NULL)
		return NULL;
	data = row->data;
	g_object_unref( row );
	return data;
}


/* Sets a text column value on an existing row. Uses g_object_set so the
 * "textN" property notification fires, which the factory's property
 * binding picks up to refresh the label automatically. */
void
gui_clist_set_row_text( GtkWidget *clist_w, int position, int col, const char *text )
{
	GListStore *store = g_object_get_data( G_OBJECT(clist_w), "list_store" );
	FsvListRow *row;
	char prop_name[16];

	if (col < 0 || col >= 4)
		return;
	row = g_list_model_get_item( G_LIST_MODEL(store), (guint)position );
	if (row == NULL)
		return;
	g_snprintf( prop_name, sizeof(prop_name), "text%d", col );
	g_object_set( G_OBJECT(row), prop_name, text, NULL );
	g_object_unref( row );
}


/* Finds a row by its data pointer, returns position or -1 */
int
gui_clist_find_by_data( GtkWidget *clist_w, gpointer data )
{
	GListStore *store = g_object_get_data( G_OBJECT(clist_w), "list_store" );
	guint n = g_list_model_get_n_items( G_LIST_MODEL(store) );
	guint i;

	for (i = 0; i < n; i++) {
		FsvListRow *row = g_list_model_get_item( G_LIST_MODEL(store), i );
		gpointer row_data = row->data;
		g_object_unref( row );
		if (row_data == data)
			return (int)i;
	}
	return -1;
}


/* Selects a row by position (-1 to unselect all) */
void
gui_clist_select_row( GtkWidget *clist_w, int position )
{
	GtkSingleSelection *sel = g_object_get_data( G_OBJECT(clist_w), "selection_model" );
	if (position < 0)
		gtk_single_selection_set_selected( sel, GTK_INVALID_LIST_POSITION );
	else
		gtk_single_selection_set_selected( sel, (guint)position );
}


/* Returns the selected row position, or -1 if none */
int
gui_clist_get_selected( GtkWidget *clist_w )
{
	GtkSingleSelection *sel = g_object_get_data( G_OBJECT(clist_w), "selection_model" );
	guint pos = gtk_single_selection_get_selected( sel );
	if (pos == GTK_INVALID_LIST_POSITION)
		return -1;
	return (int)pos;
}


/* Internal callback for the color dialog button "notify::rgba" signal */
static void
color_picker_cb( GObject *object, G_GNUC_UNUSED GParamSpec *pspec, gpointer data )
{
	void (*user_callback)( RGBcolor *, void * );
	RGBcolor color;
	const GdkRGBA *rgba;

	rgba = gtk_color_dialog_button_get_rgba( GTK_COLOR_DIALOG_BUTTON(object) );
	color.r = (float)rgba->red;
	color.g = (float)rgba->green;
	color.b = (float)rgba->blue;

	user_callback = (void (*)( RGBcolor *, void * ))g_object_get_data( object, "user_callback" );
	(user_callback)( &color, data );
}


/* The color picker widget */
GtkWidget *
gui_colorpicker_add( GtkWidget *parent_w, RGBcolor *init_color, const char *title, GCallback callback, void *callback_data )
{
	GtkWidget *colorbutton_w;
	GtkColorDialog *dialog;

	dialog = gtk_color_dialog_new( );
	gtk_color_dialog_set_title( dialog, title );
	colorbutton_w = gtk_color_dialog_button_new( dialog );
	gui_colorpicker_set_color( colorbutton_w, init_color );
	g_signal_connect( G_OBJECT(colorbutton_w), "notify::rgba", G_CALLBACK(color_picker_cb), callback_data );
	g_object_set_data( G_OBJECT(colorbutton_w), "user_callback", (void *)callback );
	parent_child( parent_w, colorbutton_w );

	return colorbutton_w;
}


/* Sets the color on a color picker widget */
void
gui_colorpicker_set_color( GtkWidget *colorbutton_w, RGBcolor *color )
{
	GdkRGBA rgba = {
		.red	= color->r,
		.green	= color->g,
		.blue	= color->b,
		.alpha	= 1.0,
	};

	gtk_color_dialog_button_set_rgba( GTK_COLOR_DIALOG_BUTTON(colorbutton_w), &rgba );
}


/* FsvDirItem — GObject wrapping a GNode* for the directory tree model */

struct _FsvDirItem {
	GObject parent_instance;
	GNode *dnode;
};

G_DEFINE_TYPE(FsvDirItem, fsv_dir_item, G_TYPE_OBJECT)

static void
fsv_dir_item_class_init( G_GNUC_UNUSED FsvDirItemClass *klass )
{
}

static void
fsv_dir_item_init( FsvDirItem *self )
{
	self->dnode = NULL;
}

FsvDirItem *
fsv_dir_item_new( GNode *dnode )
{
	FsvDirItem *item = g_object_new( FSV_TYPE_DIR_ITEM, NULL );
	item->dnode = dnode;
	return item;
}

GNode *
fsv_dir_item_get_dnode( FsvDirItem *item )
{
	g_return_val_if_fail( FSV_IS_DIR_ITEM(item), NULL );
	return item->dnode;
}


/* Callback for GtkTreeListModel: create child model for a directory item */
static GListModel *
ctree_create_child_model( gpointer item_ptr, G_GNUC_UNUSED gpointer user_data )
{
	FsvDirItem *item = FSV_DIR_ITEM(item_ptr);
	GNode *dnode = fsv_dir_item_get_dnode( item );
	GNode *child;
	GListStore *store;
	boolean has_dir_children = FALSE;

	if (dnode == NULL)
		return NULL;

	/* Check if this directory has any directory children */
	child = dnode->children;
	while (child != NULL) {
		if (NODE_IS_DIR(child)) {
			has_dir_children = TRUE;
			break;
		}
		child = child->next;
	}

	if (!has_dir_children)
		return NULL;

	/* Create a GListStore of FsvDirItem for each directory child */
	store = g_list_store_new( FSV_TYPE_DIR_ITEM );
	child = dnode->children;
	while (child != NULL) {
		if (NODE_IS_DIR(child)) {
			FsvDirItem *child_item = fsv_dir_item_new( child );
			g_list_store_append( store, child_item );
			g_object_unref( child_item );
		}
		child = child->next;
	}

	return G_LIST_MODEL(store);
}


/* The directory tree widget (fitted into a scrolled window).
 * Returns a GtkListView backed by GtkTreeListModel + GtkSingleSelection. */
GtkWidget *
gui_ctree_add( GtkWidget *parent_w )
{
	GtkWidget *scrollwin_w;
	GtkWidget *list_view_w;
	GListStore *root_store;
	GtkTreeListModel *tree_model;
	GtkSingleSelection *sel;
	GtkListItemFactory *factory;

	scrollwin_w = gtk_scrolled_window_new( );
	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(scrollwin_w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
	gtk_widget_add_css_class( scrollwin_w, "view" );
	parent_child_full( parent_w, scrollwin_w, EXPAND, FILL );

	root_store = g_list_store_new( FSV_TYPE_DIR_ITEM );
	tree_model = gtk_tree_list_model_new(
		G_LIST_MODEL(root_store),
		FALSE,		/* passthrough = FALSE (wrap in GtkTreeListRow) */
		FALSE,		/* autoexpand = FALSE */
		ctree_create_child_model,
		NULL, NULL );

	sel = gtk_single_selection_new( G_LIST_MODEL(tree_model) );
	gtk_single_selection_set_autoselect( sel, FALSE );
	gtk_single_selection_set_can_unselect( sel, TRUE );

	factory = gtk_signal_list_item_factory_new( );
	/* Factory setup/bind signals are connected in dirtree_pass_widget */

	list_view_w = gtk_list_view_new( GTK_SELECTION_MODEL(sel), factory );
	gtk_scrolled_window_set_child( GTK_SCROLLED_WINDOW(scrollwin_w), list_view_w );

	/* Store references for dirtree.c to retrieve */
	g_object_set_data( G_OBJECT(list_view_w), "root_store", root_store );
	g_object_set_data( G_OBJECT(list_view_w), "tree_list_model", tree_model );
	g_object_set_data( G_OBJECT(list_view_w), "selection_model", sel );

	return list_view_w;
}


/* Changes the mouse cursor associated with the given widget.
 * In GTK4, uses gtk_widget_set_cursor with gdk_cursor_new_from_name.
 * While the window is in a "busy" state (long operation in progress),
 * every cursor request is forced to "wait" so motion/click handlers
 * on various widgets can't reset the cursor back to the pointer. */
void
gui_cursor( GtkWidget *widget, const char *name )
{
	if (window_is_busy( ))
		name = "wait";

	if (name != NULL) {
		GdkCursor *cursor = gdk_cursor_new_from_name( name, NULL );
		gtk_widget_set_cursor( widget, cursor );
		if (cursor != NULL)
			g_object_unref( cursor );
	}
	else {
		gtk_widget_set_cursor( widget, NULL );
	}
}


/* The date edit widget (placeholder) */
GtkWidget *
gui_dateedit_add( GtkWidget *parent_w, G_GNUC_UNUSED time_t the_time, G_GNUC_UNUSED GCallback callback, G_GNUC_UNUSED void *callback_data )
{
	GtkWidget *dateedit_w;

	dateedit_w = gtk_label_new( "N/A" );
	parent_child( parent_w, dateedit_w );

	return dateedit_w;
}


/* Reads current time from a date edit widget */
time_t
gui_dateedit_get_time( G_GNUC_UNUSED GtkWidget *dateedit_w )
{
	return 0;
}


/* Sets the time on a date edit widget */
void
gui_dateedit_set_time( G_GNUC_UNUSED GtkWidget *dateedit_w, G_GNUC_UNUSED time_t the_time )
{
}


/* The entry (text input) widget */
GtkWidget *
gui_entry_add( GtkWidget *parent_w, const char *init_text, GCallback callback, void *callback_data )
{
	GtkWidget *entry_w;

	entry_w = gtk_entry_new( );
	if (init_text != NULL)
		gtk_editable_set_text( GTK_EDITABLE(entry_w), init_text );
	if (callback != NULL )
		g_signal_connect( G_OBJECT(entry_w), "activate", G_CALLBACK(callback), callback_data );
	parent_child_full( parent_w, entry_w, EXPAND, FILL );

	return entry_w;
}


/* Sets the text in an entry */
void
gui_entry_set_text( GtkWidget *entry_w, const char *entry_text )
{
	gtk_editable_set_text( GTK_EDITABLE(entry_w), entry_text );
}


/* Returns the text currently in an entry */
char *
gui_entry_get_text( GtkWidget *entry_w )
{
	return (char *)gtk_editable_get_text( GTK_EDITABLE(entry_w) );
}


/* Highlights the text in an entry */
void
gui_entry_highlight( GtkWidget *entry_w )
{
	gtk_editable_select_region( GTK_EDITABLE(entry_w), 0, gtk_entry_get_text_length( GTK_ENTRY(entry_w) ) );
}


/* The frame widget (with optional title) */
GtkWidget *
gui_frame_add( GtkWidget *parent_w, const char *title )
{
	GtkWidget *frame_w;

	frame_w = gtk_frame_new( title );
	parent_child_full( parent_w, frame_w, EXPAND, FILL );

	return frame_w;
}


/* The OpenGL area widget */
GtkWidget *
gui_gl_area_add( GtkWidget *parent_w )
{
	GtkWidget *gl_area_w;

	gl_area_w = ogl_widget_new( );
	/* In GTK4, event handling is via controllers, not event masks */
	gtk_widget_set_can_focus( GTK_WIDGET(gl_area_w), TRUE );
	gtk_widget_set_focusable( GTK_WIDGET(gl_area_w), TRUE );
	parent_child_full( parent_w, gl_area_w, EXPAND, FILL );

	return gl_area_w;
}


/* The label widget */
GtkWidget *
gui_label_add( GtkWidget *parent_w, const char *label_text )
{
	GtkWidget *label_w;
	GtkWidget *hbox_w;

	label_w = gtk_label_new( label_text );
	if (parent_w != NULL) {
		if (GTK_IS_BUTTON(parent_w)) {
			/* Labels are often too snug inside buttons */
			hbox_w = gui_hbox_add( parent_w, 0 );
			gtk_widget_set_hexpand( label_w, TRUE );
			gtk_widget_set_margin_start( label_w, 5 );
			gtk_widget_set_margin_end( label_w, 5 );
			gtk_box_append( GTK_BOX(hbox_w), label_w );
		}
		else
			parent_child( parent_w, label_w );
	}

	return label_w;
}


/* Storage for option menu items being built */
struct OptionMenuItem {
	const char *label;
	GCallback callback;
	void *callback_data;
};
static struct OptionMenuItem optmenu_items[16];
static int optmenu_item_count = 0;


/* Callback dispatcher for GtkDropDown "notify::selected" signal */
static void
option_menu_selected_cb( GObject *object, G_GNUC_UNUSED GParamSpec *pspec, G_GNUC_UNUSED gpointer data )
{
	struct OptionMenuItem *items;
	int count;
	guint active;

	active = gtk_drop_down_get_selected( GTK_DROP_DOWN(object) );
	items = (struct OptionMenuItem *)g_object_get_data( object, "optmenu_items" );
	count = GPOINTER_TO_INT(g_object_get_data( object, "optmenu_item_count" ));
	if (active < (guint)count && items[active].callback != NULL) {
		void (*cb)( GtkWidget *, void * ) = (void (*)( GtkWidget *, void * ))items[active].callback;
		cb( GTK_WIDGET(object), items[active].callback_data );
	}
}


/* The drop-down selection widget */
GtkWidget *
gui_option_menu_add( GtkWidget *parent_w, int init_selected )
{
	GtkWidget *dropdown_w;
	GtkStringList *string_list;
	struct OptionMenuItem *items_copy;
	int i;

	/* Build string list from accumulated items */
	string_list = gtk_string_list_new( NULL );
	for (i = 0; i < optmenu_item_count; i++)
		gtk_string_list_append( string_list, optmenu_items[i].label );

	dropdown_w = gtk_drop_down_new( G_LIST_MODEL(string_list), NULL );

	items_copy = g_new( struct OptionMenuItem, optmenu_item_count );
	memcpy( items_copy, optmenu_items, optmenu_item_count * sizeof(struct OptionMenuItem) );
	g_object_set_data_full( G_OBJECT(dropdown_w), "optmenu_items", items_copy, g_free );
	g_object_set_data( G_OBJECT(dropdown_w), "optmenu_item_count", GINT_TO_POINTER(optmenu_item_count) );

	gtk_drop_down_set_selected( GTK_DROP_DOWN(dropdown_w), init_selected );
	g_signal_connect( G_OBJECT(dropdown_w), "notify::selected", G_CALLBACK(option_menu_selected_cb), NULL );

	parent_child( parent_w, dropdown_w );
	optmenu_item_count = 0;

	return dropdown_w;
}


/* Combo box item definition */
GtkWidget *
gui_option_menu_item( const char *label, GCallback callback, void *callback_data )
{
	g_assert( optmenu_item_count < 16 );
	optmenu_items[optmenu_item_count].label = label;
	optmenu_items[optmenu_item_count].callback = callback;
	optmenu_items[optmenu_item_count].callback_data = callback_data;
	optmenu_item_count++;

	return NULL;
}


/* The notebook widget */
GtkWidget *
gui_notebook_add( GtkWidget *parent_w )
{
	GtkWidget *notebook_w;

	notebook_w = gtk_notebook_new( );
	parent_child_full( parent_w, notebook_w, EXPAND, FILL );

	return notebook_w;
}


/* Adds a new page to a notebook */
void
gui_notebook_page_add( GtkWidget *notebook_w, const char *tab_label, GtkWidget *content_w )
{
	GtkWidget *tab_label_w;

	tab_label_w = gtk_label_new( tab_label );
	gtk_notebook_append_page( GTK_NOTEBOOK(notebook_w), content_w, tab_label_w );
}


/* Horizontal paned window widget */
GtkWidget *
gui_hpaned_add( GtkWidget *parent_w, int divider_x_pos )
{
	GtkWidget *hpaned_w;

	hpaned_w = gtk_paned_new( GTK_ORIENTATION_HORIZONTAL );
	gtk_paned_set_position( GTK_PANED(hpaned_w), divider_x_pos );
	parent_child_full( parent_w, hpaned_w, EXPAND, FILL );

	return hpaned_w;
}


/* Vertical paned window widget */
GtkWidget *
gui_vpaned_add( GtkWidget *parent_w, int divider_y_pos )
{
	GtkWidget *vpaned_w;

	vpaned_w = gtk_paned_new( GTK_ORIENTATION_VERTICAL );
	gtk_paned_set_position( GTK_PANED(vpaned_w), divider_y_pos );
	parent_child_full( parent_w, vpaned_w, EXPAND, FILL );

	return vpaned_w;
}


/* The image widget (created from a GResource path) */
GtkWidget *
gui_resource_image_add( GtkWidget *parent_w, const char *resource_path )
{
	GtkWidget *image_w;

	image_w = gtk_image_new_from_resource( resource_path );
	parent_child( parent_w, image_w );

	return image_w;
}


/* The color preview widget (GtkDrawingArea) */
GtkWidget *
gui_preview_add( GtkWidget *parent_w )
{
	GtkWidget *drawing_w;

	drawing_w = gtk_drawing_area_new( );
	gtk_widget_set_size_request( drawing_w, 64, 16 );
	parent_child_full( parent_w, drawing_w, EXPAND, FILL );

	return drawing_w;
}


/* Draw function for the spectrum drawing area (GTK4 style) */
static void
preview_spectrum_draw_func( GtkDrawingArea *drawing_w, cairo_t *cr, int width, int height, G_GNUC_UNUSED gpointer data )
{
	RGBcolor (*spectrum_func)( double x );
	RGBcolor color;
	int i;

	spectrum_func = (RGBcolor (*)( double x ))g_object_get_data( G_OBJECT(drawing_w), "spectrum_func" );
	if (spectrum_func == NULL)
		return;

	if (width <= 0 || height <= 0)
		return;

	for (i = 0; i < width; i++) {
		color = (spectrum_func)( (double)i / (double)(width - 1) );
		cairo_set_source_rgb( cr, color.r, color.g, color.b );
		cairo_rectangle( cr, i, 0, 1, height );
		cairo_fill( cr );
	}
}


/* Fills a preview widget with an arbitrary spectrum */
void
gui_preview_spectrum( GtkWidget *preview_w, RGBcolor (*spectrum_func)( double x ) )
{
	static const char data_key[] = "spectrum_func";
	boolean first_time;

	first_time = g_object_get_data( G_OBJECT(preview_w), data_key ) == NULL;

	g_object_set_data( G_OBJECT(preview_w), data_key, (void *)spectrum_func );

	if (first_time) {
		gtk_drawing_area_set_draw_func( GTK_DRAWING_AREA(preview_w),
			preview_spectrum_draw_func, NULL, NULL );
	}

	gtk_widget_queue_draw( preview_w );
}


/* The horizontal scrollbar widget */
GtkWidget *
gui_hscrollbar_add( GtkWidget *parent_w, GtkAdjustment *adjustment )
{
	GtkWidget *frame_w;
	GtkWidget *hscrollbar_w;

	frame_w = gui_frame_add( NULL, NULL );
	parent_child( parent_w, frame_w );

	hscrollbar_w = gtk_scrollbar_new( GTK_ORIENTATION_HORIZONTAL, adjustment );
	gtk_frame_set_child( GTK_FRAME(frame_w), hscrollbar_w );

	return hscrollbar_w;
}


/* The vertical scrollbar widget */
GtkWidget *
gui_vscrollbar_add( GtkWidget *parent_w, GtkAdjustment *adjustment )
{
	GtkWidget *frame_w;
	GtkWidget *vscrollbar_w;

	frame_w = gui_frame_add( NULL, NULL );
	parent_child( parent_w, frame_w );

	vscrollbar_w = gtk_scrollbar_new( GTK_ORIENTATION_VERTICAL, adjustment );
	gtk_frame_set_child( GTK_FRAME(frame_w), vscrollbar_w );

	return vscrollbar_w;
}


/* The separator widget */
GtkWidget *
gui_separator_add( GtkWidget *parent_w )
{
	GtkWidget *separator_w;

	separator_w = gtk_separator_new( GTK_ORIENTATION_HORIZONTAL );
	if (parent_w != NULL) {
		gtk_widget_set_margin_top( separator_w, 10 );
		gtk_widget_set_margin_bottom( separator_w, 10 );
		parent_child( parent_w, separator_w );
	}

	return separator_w;
}


/* The statusbar widget (implemented as a GtkLabel in GTK4,
 * since GtkStatusbar was removed) */
GtkWidget *
gui_statusbar_add( GtkWidget *parent_w )
{
	GtkWidget *label_w;

	label_w = gtk_label_new( "" );
	gtk_label_set_xalign( GTK_LABEL(label_w), 0.0 );
	gtk_widget_set_margin_start( label_w, 4 );
	parent_child( parent_w, label_w );

	return label_w;
}


/* Displays the given message in a statusbar (GtkLabel) */
void
gui_statusbar_message( GtkWidget *statusbar_w, const char *message )
{
	char strbuf[1024];

	snprintf( strbuf, sizeof(strbuf), " %s", message );
	gtk_label_set_text( GTK_LABEL(statusbar_w), strbuf );
}


/* The grid (layout) widget */
GtkWidget *
gui_table_add( GtkWidget *parent_w, G_GNUC_UNUSED int num_rows, G_GNUC_UNUSED int num_cols, boolean homog, int cell_padding )
{
	GtkWidget *grid_w;

	grid_w = gtk_grid_new( );
	gtk_grid_set_row_homogeneous( GTK_GRID(grid_w), homog );
	gtk_grid_set_column_homogeneous( GTK_GRID(grid_w), homog );
	gtk_grid_set_row_spacing( GTK_GRID(grid_w), cell_padding );
	gtk_grid_set_column_spacing( GTK_GRID(grid_w), cell_padding );
	parent_child_full( parent_w, grid_w, EXPAND, FILL );

	return grid_w;
}


/* Attaches a widget to a grid */
void
gui_table_attach( GtkWidget *grid_w, GtkWidget *widget, int left, int right, int top, int bottom )
{
	gtk_grid_attach( GTK_GRID(grid_w), widget, left, top, right - left, bottom - top );
}


/* The text (area) widget */
GtkWidget *
gui_text_area_add( GtkWidget *parent_w, const char *init_text )
{
	GtkWidget *text_area_w;

	text_area_w = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text_area_w), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_area_w), GTK_WRAP_WORD);
	if (init_text != NULL) {
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_area_w));
		gtk_text_buffer_set_text(buffer, init_text, -1);
	}
	parent_child( parent_w, text_area_w );

	return text_area_w;
}


/* This changes the packing properties of a widget inside a box.
 * In GTK4, expand/fill are set via hexpand/vexpand/halign/valign. */
void
gui_widget_packing( GtkWidget *widget, boolean expand, boolean fill, G_GNUC_UNUSED boolean start )
{
	GtkWidget *parent_box_w;
	GtkOrientation orient;

	parent_box_w = gtk_widget_get_parent( widget );
	g_assert( GTK_IS_BOX(parent_box_w) );

	orient = gtk_orientable_get_orientation( GTK_ORIENTABLE(parent_box_w) );
	if (orient == GTK_ORIENTATION_HORIZONTAL) {
		gtk_widget_set_hexpand( widget, expand );
		if (expand && fill)
			gtk_widget_set_halign( widget, GTK_ALIGN_FILL );
		else if (expand)
			gtk_widget_set_halign( widget, GTK_ALIGN_CENTER );
	}
	else {
		gtk_widget_set_vexpand( widget, expand );
		if (expand && fill)
			gtk_widget_set_valign( widget, GTK_ALIGN_FILL );
		else if (expand)
			gtk_widget_set_valign( widget, GTK_ALIGN_CENTER );
	}
}


/* Data passed to the async color dialog completion callback */
struct ColorSelData {
	GCallback callback;
	void *callback_data;
};

/* Async completion callback for the color dialog */
static void
colorsel_dialog_done_cb( GObject *source, GAsyncResult *result, gpointer user_data )
{
	GtkColorDialog *dialog = GTK_COLOR_DIALOG(source);
	struct ColorSelData *csd = (struct ColorSelData *)user_data;
	GdkRGBA *rgba;
	RGBcolor color;
	void (*user_callback)( const RGBcolor *, void * );

	rgba = gtk_color_dialog_choose_rgba_finish( dialog, result, NULL );
	if (rgba != NULL) {
		color.r = (float)rgba->red;
		color.g = (float)rgba->green;
		color.b = (float)rgba->blue;
		user_callback = (void (*)( const RGBcolor *, void * ))csd->callback;
		(user_callback)( &color, csd->callback_data );
		gdk_rgba_free( rgba );
	}
	g_free( csd );
}


/* Opens a color chooser dialog (async) */
GtkWidget *
gui_colorsel_window( const char *title, RGBcolor *init_color, GCallback ok_callback, void *ok_callback_data )
{
	GtkColorDialog *dialog;
	GdkRGBA rgba;
	struct ColorSelData *csd;

	dialog = gtk_color_dialog_new( );
	gtk_color_dialog_set_title( dialog, title );
	rgba.red = init_color->r;
	rgba.green = init_color->g;
	rgba.blue = init_color->b;
	rgba.alpha = 1.0;

	csd = g_new( struct ColorSelData, 1 );
	csd->callback = ok_callback;
	csd->callback_data = ok_callback_data;

	gtk_color_dialog_choose_rgba( dialog,
		GTK_WINDOW(gtk_application_get_active_window( GTK_APPLICATION(g_application_get_default( )) )),
		&rgba, NULL, colorsel_dialog_done_cb, csd );

	return NULL;
}


/* Creates a base dialog window */
GtkWidget *
gui_dialog_window( const char *title, GCallback close_callback )
{
	GtkWidget *window_w;

	window_w = gtk_window_new( );
	gtk_window_set_resizable( GTK_WINDOW(window_w), FALSE );
	gtk_window_set_title( GTK_WINDOW(window_w), title );
	if (close_callback != NULL)
		g_signal_connect( G_OBJECT(window_w), "destroy", G_CALLBACK(close_callback), NULL );

	return window_w;
}


/* Internal callback for the text-entry window */
static void
entry_window_cb( G_GNUC_UNUSED GtkWidget *unused, GtkWidget *entry_window_w )
{
	GtkWidget *entry_w;
	char *entry_text;
	void (*user_callback)( const char *, void * );
	void *user_callback_data;

	entry_w = g_object_get_data( G_OBJECT(entry_window_w), "entry_w" );
	entry_text = xstrdup( gtk_editable_get_text( GTK_EDITABLE(entry_w) ) );

	user_callback = (void (*)( const char *, void * ))g_object_get_data( G_OBJECT(entry_window_w), "user_callback" );
	user_callback_data = g_object_get_data( G_OBJECT(entry_window_w), "user_callback_data" );
	gtk_window_destroy( GTK_WINDOW(entry_window_w) );

	(user_callback)( entry_text, user_callback_data );
	xfree( entry_text );
}


/* Creates a one-line text-entry window */
GtkWidget *
gui_entry_window( const char *title, const char *init_text, GCallback ok_callback, void *ok_callback_data )
{
	GtkWidget *entry_window_w;
	GtkWidget *frame_w;
	GtkWidget *vbox_w;
	GtkWidget *entry_w;
	GtkWidget *hbox_w;
	GtkWidget *button_w;
	int width;

	entry_window_w = gui_dialog_window( title, NULL );
	gtk_widget_set_margin_start( entry_window_w, 5 );
	gtk_widget_set_margin_end( entry_window_w, 5 );
	gtk_widget_set_margin_top( entry_window_w, 5 );
	gtk_widget_set_margin_bottom( entry_window_w, 5 );
	{
		GdkDisplay *display = gdk_display_get_default( );
		GListModel *monitors = gdk_display_get_monitors( display );
		GdkMonitor *monitor = g_list_model_get_item( monitors, 0 );
		GdkRectangle geom;
		gdk_monitor_get_geometry( monitor, &geom );
		width = geom.width / 2;
		g_object_unref( monitor );
	}
	gtk_widget_set_size_request( entry_window_w, width, -1 );
	g_object_set_data( G_OBJECT(entry_window_w), "user_callback", (void *)ok_callback );
	g_object_set_data( G_OBJECT(entry_window_w), "user_callback_data", ok_callback_data );

	frame_w = gui_frame_add( entry_window_w, NULL );
	vbox_w = gui_vbox_add( frame_w, 10 );

	entry_w = gui_entry_add( vbox_w, init_text, G_CALLBACK(entry_window_cb), entry_window_w );
	g_object_set_data( G_OBJECT(entry_window_w), "entry_w", entry_w );

	hbox_w = gui_hbox_add( vbox_w, 0 );
	gtk_box_set_homogeneous( GTK_BOX(hbox_w), TRUE );
	gui_box_set_packing( hbox_w, EXPAND, FILL, AT_START );

	gui_button_add( hbox_w, _("OK"), G_CALLBACK(entry_window_cb), entry_window_w );
	vbox_w = gui_vbox_add( hbox_w, 0 ); /* spacer */
	button_w = gui_button_add( hbox_w, _("Cancel"), NULL, NULL );
	g_signal_connect_swapped( G_OBJECT(button_w), "clicked", G_CALLBACK(gtk_window_destroy), G_OBJECT(entry_window_w) );

	gtk_window_set_modal( GTK_WINDOW(entry_window_w), TRUE );
	gtk_window_present( GTK_WINDOW(entry_window_w) );
	gtk_widget_grab_focus( entry_w );

	return entry_window_w;
}


/* Data passed to the async file dialog completion callback */
struct FileSelData {
	GCallback callback;
	void *callback_data;
};

/* Async completion callback for the file/folder dialog */
static void
filesel_dialog_done_cb( GObject *source, GAsyncResult *result, gpointer user_data )
{
	GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
	struct FileSelData *fsd = (struct FileSelData *)user_data;
	GFile *file;
	char *filename;
	void (*user_callback)( const char *, void * );

	/* Try select_folder_finish first, then open_finish */
	file = gtk_file_dialog_select_folder_finish( dialog, result, NULL );
	if (file == NULL)
		file = gtk_file_dialog_open_finish( dialog, result, NULL );
	if (file != NULL) {
		filename = g_file_get_path( file );
		g_object_unref( file );
		user_callback = (void (*)( const char *, void * ))fsd->callback;
		(user_callback)( filename, fsd->callback_data );
		g_free( filename );
	}
	g_free( fsd );
}


/* Opens a file/folder chooser dialog (async).
 * If select_folder is TRUE, opens a folder chooser instead. */
void
gui_filesel_window( const char *title, const char *init_filename, GCallback ok_callback, void *ok_callback_data, boolean select_folder )
{
	GtkFileDialog *dialog;
	GtkWindow *parent;
	struct FileSelData *fsd;
	GFile *init_folder = NULL;

	dialog = gtk_file_dialog_new( );
	gtk_file_dialog_set_title( dialog, title );
	gtk_file_dialog_set_modal( dialog, TRUE );

	if (init_filename != NULL) {
		init_folder = g_file_new_for_path( init_filename );
		gtk_file_dialog_set_initial_folder( dialog, init_folder );
	}

	parent = GTK_WINDOW(gtk_application_get_active_window( GTK_APPLICATION(g_application_get_default( )) ));

	fsd = g_new( struct FileSelData, 1 );
	fsd->callback = ok_callback;
	fsd->callback_data = ok_callback_data;

	if (select_folder)
		gtk_file_dialog_select_folder( dialog, parent, NULL, filesel_dialog_done_cb, fsd );
	else
		gtk_file_dialog_open( dialog, parent, NULL, filesel_dialog_done_cb, fsd );

	if (init_folder != NULL)
		g_object_unref( init_folder );
}


/* Helper function for gui_window_modalize( ) */
static void
window_unmodalize( G_GNUC_UNUSED GObject *unused, GtkWidget *parent_window_w )
{
	gtk_widget_set_sensitive( parent_window_w, TRUE );
	gui_cursor( parent_window_w, NULL );
}


/* Makes a window modal w.r.t its parent window */
void
gui_window_modalize( GtkWidget *window_w, GtkWidget *parent_window_w )
{
	gtk_window_set_transient_for( GTK_WINDOW(window_w), GTK_WINDOW(parent_window_w) );
	gtk_window_set_modal( GTK_WINDOW(window_w), TRUE );
	gtk_widget_set_sensitive( parent_window_w, FALSE );
	gui_cursor( parent_window_w, "not-allowed" );

	g_signal_connect( G_OBJECT(window_w), "destroy", G_CALLBACK(window_unmodalize), parent_window_w );
}


/* end gui.c */
