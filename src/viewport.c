/* viewport.c */

/* Viewport routines */

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
#include "viewport.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "about.h"
#include "camera.h"
#include "dialog.h" /* context_menu( ) */
#include "filelist.h" /* filelist_show_entry( ) */
#include "geometry.h"
#include "gui.h"
#include "ogl.h"
#include "window.h"


/* Sensitivity factor used for manual camera control */
#define MOUSE_SENSITIVITY 0.5

/* Step size for keyboard panning (arrow keys / WASD) */
#define KEY_PAN_STEP 3.0


/* The node table, used to find a node by its ID number */
static GNode **node_table = NULL;
static unsigned int node_table_size = 0;

/* The currently highlighted (indicated) node */
static GNode *indicated_node = NULL;

/* Previous mouse pointer coordinates */
static int prev_x = 0, prev_y = 0;

/* Drag detection state */
#define DRAG_THRESHOLD 4
static boolean btn1_pressed = FALSE;
static boolean btn1_is_dragging = FALSE;
static int btn1_press_x = 0, btn1_press_y = 0;

/* Pick throttle: minimum interval between color picks (seconds) */
#define PICK_MIN_INTERVAL (1.0 / 60.0)
static double last_pick_time = 0.0;

/* Keyboard pan state: TRUE while the key is physically held */
static boolean pan_key_left = FALSE;
static boolean pan_key_right = FALSE;
static boolean pan_key_up = FALSE;
static boolean pan_key_down = FALSE;
static guint pan_idle_id = 0;
static double pan_last_time = 0.0;

/* Saved reference to the GL area widget */
static GtkWidget *viewport_gl_area = NULL;


/* Receives a newly created node table from scanfs( ) */
void
viewport_pass_node_table( GNode **new_node_table, unsigned int table_size )
{
	if (node_table != NULL)
		xfree( node_table );

	node_table = new_node_table;
	node_table_size = table_size;
}


/* This returns the node (if any) that is visible at viewport location
 * (x,y). Uses color-buffer picking. */
static GNode *
node_at_location( int x, int y, unsigned int *face_id )
{
	unsigned int node_id;

	node_id = ogl_color_pick( x, y, face_id );
	if (node_id != 0 && node_id < node_table_size)
		return node_table[node_id];

	return NULL;
}


/* Per-frame keyboard pan callback */
static gboolean
keyboard_pan_tick( G_GNUC_UNUSED gpointer user_data )
{
	double kx = 0.0, ky = 0.0;
	double t_now, dt;

	if (pan_key_left)  kx -= KEY_PAN_STEP;
	if (pan_key_right) kx += KEY_PAN_STEP;
	if (pan_key_up)    ky += KEY_PAN_STEP;
	if (pan_key_down)  ky -= KEY_PAN_STEP;

	if (kx == 0.0 && ky == 0.0) {
		pan_idle_id = 0;
		return G_SOURCE_REMOVE;
	}

	t_now = xgettime( );
	dt = t_now - pan_last_time;
	pan_last_time = t_now;

	if (dt > 0.1)
		dt = 0.1;

	if (!camera_moving( ))
		camera_pan( kx * dt * 60.0, ky * dt * 60.0 );

	return G_SOURCE_CONTINUE;
}


/* Starts the pan idle if not already running */
static void
keyboard_pan_ensure_running( void )
{
	if (pan_idle_id == 0) {
		pan_last_time = xgettime( );
		pan_idle_id = g_idle_add_full( G_PRIORITY_DEFAULT_IDLE,
		                               keyboard_pan_tick, NULL, NULL );
	}
}


/* Clears all held key state (e.g. on focus loss) */
static void
keyboard_pan_reset( void )
{
	pan_key_left = FALSE;
	pan_key_right = FALSE;
	pan_key_up = FALSE;
	pan_key_down = FALSE;
}


/* GtkGestureClick "pressed" callback — handles button press and double-click */
static void
viewport_click_pressed_cb( GtkGestureClick *gesture, int n_press, double x, double y, G_GNUC_UNUSED gpointer user_data )
{
	unsigned int face_id;
	int button;
	int ix = (int)x, iy = (int)y;

	button = gtk_gesture_single_get_current_button( GTK_GESTURE_SINGLE(gesture) );

	if (n_press == 1) {
		/* Exit the About presentation if it is up */
		if (about( ABOUT_END )) {
			indicated_node = NULL;
			return;
		}
	}

	if (globals.fsv_mode == FSV_SPLASH)
		return;

	/* Grab focus so the GL area receives key events */
	if (viewport_gl_area != NULL && !gtk_widget_has_focus( viewport_gl_area ))
		gtk_widget_grab_focus( viewport_gl_area );

	if (n_press == 1) {
		if (camera_moving( )) {
			camera_pan_finish( );
			indicated_node = NULL;
		}
		else if (button == 1) {
			btn1_pressed = TRUE;
			btn1_is_dragging = FALSE;
			btn1_press_x = ix;
			btn1_press_y = iy;
			indicated_node = node_at_location( ix, iy, &face_id );
			if (indicated_node == NULL) {
				geometry_highlight_node( NULL, FALSE );
				window_statusbar( SB_RIGHT, "" );
			}
			else {
				geometry_highlight_node( indicated_node, TRUE );
				window_statusbar( SB_RIGHT, node_absname( indicated_node ) );
			}
		}
		else if (button == 2) {
			indicated_node = NULL;
			geometry_highlight_node( NULL, FALSE );
			window_statusbar( SB_RIGHT, "" );
		}
		else if (button == 3) {
			GNode *menu_node = node_at_location( ix, iy, &face_id );
			indicated_node = menu_node;
			if (menu_node != NULL) {
				geometry_highlight_node( menu_node, FALSE );
				window_statusbar( SB_RIGHT, node_absname( menu_node ) );
				context_menu( menu_node, viewport_gl_area, x, y );
				filelist_show_entry( menu_node );
			}
		}
		prev_x = ix;
		prev_y = iy;
	}
	else if (n_press == 2) {
		if (button == 1 && !camera_moving( ) && indicated_node != NULL) {
			camera_look_at( indicated_node );
			btn1_pressed = FALSE;
			btn1_is_dragging = FALSE;
		}
	}
}


/* GtkGestureClick "released" callback */
static void
viewport_click_released_cb( G_GNUC_UNUSED GtkGestureClick *gesture, G_GNUC_UNUSED int n_press,
                            G_GNUC_UNUSED double x, G_GNUC_UNUSED double y, G_GNUC_UNUSED gpointer user_data )
{
	int button = gtk_gesture_single_get_current_button( GTK_GESTURE_SINGLE(gesture) );

	if (button == 1) {
		btn1_pressed = FALSE;
		btn1_is_dragging = FALSE;
	}
	if (viewport_gl_area != NULL)
		gui_cursor( viewport_gl_area, NULL );
}


/* GtkEventControllerMotion "motion" callback */
static void
viewport_motion_cb( G_GNUC_UNUSED GtkEventControllerMotion *controller,
                    double x, double y, G_GNUC_UNUSED gpointer user_data )
{
	GdkModifierType state;
	double dx, dy;
	unsigned int face_id;
	int ix = (int)x, iy = (int)y;
	boolean btn1, btn2;

	if (globals.fsv_mode == FSV_SPLASH)
		return;

	/* Get current modifier state for button detection during drag */
	state = gtk_event_controller_get_current_event_state( GTK_EVENT_CONTROLLER(controller) );
	btn1 = (state & GDK_BUTTON1_MASK) != 0;
	btn2 = (state & GDK_BUTTON2_MASK) != 0;

	if (!camera_moving( )) {
		if (btn2) {
			gui_cursor( viewport_gl_area, "ns-resize" );
			dy = MOUSE_SENSITIVITY * (iy - prev_y);
			camera_dolly( - dy );
			indicated_node = NULL;
		}
		else if (btn1) {
			if (!btn1_is_dragging) {
				int total_dx = ix - btn1_press_x;
				int total_dy = iy - btn1_press_y;
				if (abs( total_dx ) > DRAG_THRESHOLD || abs( total_dy ) > DRAG_THRESHOLD) {
					btn1_is_dragging = TRUE;
					indicated_node = NULL;
					geometry_highlight_node( NULL, FALSE );
					window_statusbar( SB_RIGHT, "" );
				}
			}
			if (btn1_is_dragging) {
				gui_cursor( viewport_gl_area, "move" );
				dx = MOUSE_SENSITIVITY * (ix - prev_x);
				dy = MOUSE_SENSITIVITY * (iy - prev_y);
				camera_revolve( dx, dy );
			}
		}
		else {
			/* No button: hover highlight (throttled) */
			double t_now = xgettime( );
			if (t_now - last_pick_time >= PICK_MIN_INTERVAL) {
				last_pick_time = t_now;
				indicated_node = node_at_location( ix, iy, &face_id );
				if (indicated_node == NULL) {
					geometry_highlight_node( NULL, FALSE );
					window_statusbar( SB_RIGHT, "" );
				}
				else {
					if (geometry_should_highlight( indicated_node, face_id ))
						geometry_highlight_node( indicated_node, FALSE );
					else
						geometry_highlight_node( NULL, FALSE );
					window_statusbar( SB_RIGHT, node_absname( indicated_node ) );
				}
			}
		}
		prev_x = ix;
		prev_y = iy;
	}
}


/* GtkEventControllerMotion "leave" callback */
static void
viewport_leave_cb( G_GNUC_UNUSED GtkEventControllerMotion *controller, G_GNUC_UNUSED gpointer user_data )
{
	geometry_highlight_node( NULL, FALSE );
	window_statusbar( SB_RIGHT, "" );
	if (viewport_gl_area != NULL)
		gui_cursor( viewport_gl_area, NULL );
	indicated_node = NULL;
	btn1_pressed = FALSE;
	btn1_is_dragging = FALSE;
}


/* GtkEventControllerScroll "scroll" callback */
static gboolean
viewport_scroll_cb( G_GNUC_UNUSED GtkEventControllerScroll *controller,
                    G_GNUC_UNUSED double dx, double dy, G_GNUC_UNUSED gpointer user_data )
{
	if (globals.fsv_mode == FSV_SPLASH)
		return FALSE;

	if (!camera_moving( )) {
		if (dy != 0.0)
			camera_dolly( dy * 16.0 );
		indicated_node = NULL;
		return TRUE;
	}

	return FALSE;
}


/* GtkEventControllerKey "key-pressed" callback */
static gboolean
viewport_key_pressed_cb( G_GNUC_UNUSED GtkEventControllerKey *controller,
                         guint keyval, G_GNUC_UNUSED guint keycode,
                         G_GNUC_UNUSED GdkModifierType state, G_GNUC_UNUSED gpointer user_data )
{
	if (globals.fsv_mode == FSV_SPLASH)
		return FALSE;

	switch (keyval) {
		case GDK_KEY_Left:  case GDK_KEY_a: case GDK_KEY_A:
		pan_key_left = TRUE;  break;
		case GDK_KEY_Right: case GDK_KEY_d: case GDK_KEY_D:
		pan_key_right = TRUE; break;
		case GDK_KEY_Up:    case GDK_KEY_w: case GDK_KEY_W:
		pan_key_up = TRUE;    break;
		case GDK_KEY_Down:  case GDK_KEY_s: case GDK_KEY_S:
		pan_key_down = TRUE;  break;
		case GDK_KEY_t: case GDK_KEY_T:
		geometry_toggle_labels( );
		return TRUE;
		case GDK_KEY_b: case GDK_KEY_B:
		ogl_cycle_background( );
		return TRUE;
		default:
		return FALSE;
	}

	indicated_node = NULL;
	keyboard_pan_ensure_running( );
	return TRUE;
}


/* GtkEventControllerKey "key-released" callback */
static void
viewport_key_released_cb( G_GNUC_UNUSED GtkEventControllerKey *controller,
                          guint keyval, G_GNUC_UNUSED guint keycode,
                          G_GNUC_UNUSED GdkModifierType state, G_GNUC_UNUSED gpointer user_data )
{
	switch (keyval) {
		case GDK_KEY_Left:  case GDK_KEY_a: case GDK_KEY_A:
		pan_key_left = FALSE;  break;
		case GDK_KEY_Right: case GDK_KEY_d: case GDK_KEY_D:
		pan_key_right = FALSE; break;
		case GDK_KEY_Up:    case GDK_KEY_w: case GDK_KEY_W:
		pan_key_up = FALSE;    break;
		case GDK_KEY_Down:  case GDK_KEY_s: case GDK_KEY_S:
		pan_key_down = FALSE;  break;
		default:
		break;
	}
}


/* GtkEventControllerFocus "leave" callback */
static void
viewport_focus_leave_cb( G_GNUC_UNUSED GtkEventControllerFocus *controller, G_GNUC_UNUSED gpointer user_data )
{
	keyboard_pan_reset( );
}


/* Sets up GTK4 event controllers on the GL area widget.
 * Called from window_init( ) instead of the old g_signal_connect("event",...) */
void
viewport_setup_controllers( GtkWidget *gl_area_w )
{
	GtkGesture *click;
	GtkEventController *motion, *scroll, *key, *focus;

	viewport_gl_area = gl_area_w;

	/* Click gesture (handles press, release, double-click for all buttons) */
	click = gtk_gesture_click_new( );
	gtk_gesture_single_set_button( GTK_GESTURE_SINGLE(click), 0 ); /* all buttons */
	g_signal_connect( click, "pressed", G_CALLBACK(viewport_click_pressed_cb), NULL );
	g_signal_connect( click, "released", G_CALLBACK(viewport_click_released_cb), NULL );
	gtk_widget_add_controller( gl_area_w, GTK_EVENT_CONTROLLER(click) );

	/* Motion controller */
	motion = gtk_event_controller_motion_new( );
	g_signal_connect( motion, "motion", G_CALLBACK(viewport_motion_cb), NULL );
	g_signal_connect( motion, "leave", G_CALLBACK(viewport_leave_cb), NULL );
	gtk_widget_add_controller( gl_area_w, motion );

	/* Scroll controller */
	scroll = gtk_event_controller_scroll_new( GTK_EVENT_CONTROLLER_SCROLL_VERTICAL |
	                                          GTK_EVENT_CONTROLLER_SCROLL_DISCRETE );
	g_signal_connect( scroll, "scroll", G_CALLBACK(viewport_scroll_cb), NULL );
	gtk_widget_add_controller( gl_area_w, scroll );

	/* Key controller */
	key = gtk_event_controller_key_new( );
	g_signal_connect( key, "key-pressed", G_CALLBACK(viewport_key_pressed_cb), NULL );
	g_signal_connect( key, "key-released", G_CALLBACK(viewport_key_released_cb), NULL );
	gtk_widget_add_controller( gl_area_w, key );

	/* Focus controller */
	focus = gtk_event_controller_focus_new( );
	g_signal_connect( focus, "leave", G_CALLBACK(viewport_focus_leave_cb), NULL );
	gtk_widget_add_controller( gl_area_w, focus );
}


/* end viewport.c */
