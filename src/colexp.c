/* colexp.c */

/* Collapse/expansion engine */

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
#include "colexp.h"

#include "animation.h"
#include "camera.h"
#include "dirtree.h"
#include "filelist.h"
#include "geometry.h"
#include "gui.h" /* gui_update( ) */
#include "ogl.h" /* ogl_frame_serial( ) */
#include "tmaptext.h" /* text_cache_invalidate( ) */
#include "window.h"


/* Duration of a single collapse/expansion (in seconds) */
#define DISCV_COLEXP_TIME	0.5
#define MAPV_COLEXP_TIME	0.375
#define TREEV_COLEXP_TIME	0.5


/* TRUE whenever the collapse/expand process affects the viewport's
 * scrollable area */
static boolean scrollbars_colexp_adjust;

/* Reentrancy guard: TRUE while colexp() is executing at depth 0.
 * gui_update() can process events that trigger tree expand/collapse
 * signals, which would re-enter colexp() */
static boolean colexp_in_progress = FALSE;

/* (The TreeV expand-all camera lock that used to live here is gone:
 * since the row-wrapped layout, expansion geometry is stable and its
 * end-state extents are known up front, so the camera simply glides
 * to the final framing via camera_treev_frame_expansion) */

/* Recursive expand/collapse on subtrees at or above this node count
 * drops the per-depth stagger and deploys everything simultaneously
 * (Phase 47.3). Rationale: each deployment wave forces a full-scene
 * content rebuild whose GPU upload is PCIe-bound (~1 s at 1.5M
 * nodes / 2.6 GB of vertices), and on trees this size frames outlast
 * the waves anyway — the cascade has never been visible. Without the
 * stagger there are exactly TWO content crossings (start and end)
 * bracketing a pure-transform animation that plays at full frame
 * rate. Smaller trees keep their cascade. */
#define COLEXP_NO_STAGGER_NODES 50000

/* Bloom duration for no-stagger operations. The morphs are deferred
 * until after the (multi-second) content-rebuild frame has rendered
 * — morph clocks are wall-time, so starting them before that frame
 * would consume the whole animation inside it — and then play at
 * full frame rate over static content, so give the bloom a duration
 * worth watching */
#define COLEXP_NO_STAGGER_TIME 2.5

/* TRUE for the duration of one no-stagger recursive operation */
static boolean colexp_no_stagger = FALSE;

/* Pending deferred-morph request (no-stagger operations) */
static GNode *colexp_deferred_dnode = NULL;
static boolean colexp_deferred_expanding = FALSE;
/* Frame serial at nudge time: the morphs may only start once a LATER
 * frame has fully rendered (the content-rebuild frame) */
static guint64 colexp_deferred_serial = 0;



/* This returns the number of collapsed directory levels above the
 * given directory */
static int
collapsed_depth( GNode *dnode )
{
	GNode *up_node;
        int depth = 0;

	up_node = dnode->parent;
	while (NODE_IS_DIR(up_node)) {
		if (!DIR_COLLAPSED(up_node))
			break;
		++depth;
		up_node = up_node->parent;
	}

	return depth;
}



/* This returns the maximum depth to which a certain directory's
 * subtree has been expanded. A return value of 0 is typical; 1 or
 * more means a recursive collapse would be necessary if collapsing
 * the given directory */
static int
max_expanded_depth( GNode *dnode )
{
	GNode *node;
	int max_depth = 0;
	int subtree_depth;

	node = dnode->children;
	while (node != NULL) {
		if (NODE_IS_DIR(node)) {
			if (DIR_EXPANDED(node))
				subtree_depth = 1 + max_expanded_depth( node );
			else
				subtree_depth = 0;
			max_depth = MAX(max_depth, subtree_depth);
		}
		else
			break;
		node = node->next;
	}

	return max_depth;
}


/* Step/end callback for collapses/expands */
static void
colexp_progress_cb( Morph *morph )
{
	GNode *dnode;

	dnode = (GNode *)morph->data;
	g_assert( NODE_IS_DIR(dnode) );

        /* Keep geometry module appraised of collapse/expand progress */
	geometry_colexp_in_progress( dnode );

	/* Label-vertex cache (Phase 39.4 follow-up): a deployment morph
	 * just stepped, so layout positions changed → cached labels are
	 * stale. Soft-stale only (Phase 46.C): the stale labels keep
	 * drawing and the cache refreshes on its throttle, instead of a
	 * full rebuild + re-upload every morph frame. */
	text_cache_touch( );

	/* Keep viewport refreshed */
	globals.need_redraw = TRUE;

	if (scrollbars_colexp_adjust)
		camera_update_scrollbars( ABS(*(morph->var) - morph->end_value) < EPSILON );
}


/* Timeout callback to re-enable access after collapse animation */
static gboolean
colexp_reenable_access( G_GNUC_UNUSED gpointer data )
{
	window_set_access( TRUE );
	return G_SOURCE_REMOVE;
}


/* Starts the deferred deployment morphs for a no-stagger operation
 * (every directory in the subtree that isn't already at its target) */
static void
colexp_deferred_recursive( GNode *dnode, boolean expanding )
{
	GNode *node;
	double d = DIR_NODE_DESC(dnode)->deployment;

	/* Sigmoid: slow start (the camera glide is still settling in
	 * the bloom's first beats), visible growth through the middle */
	if (expanding ? (d < 1.0 - EPSILON) : (d > EPSILON))
		morph_full( &DIR_NODE_DESC(dnode)->deployment,
		            MORPH_SIGMOID,
		            expanding ? 1.0 : 0.0,
		            COLEXP_NO_STAGGER_TIME,
		            colexp_progress_cb, colexp_progress_cb, dnode );

	node = dnode->children;
	while (node != NULL) {
		if (!NODE_IS_DIR(node))
			break;
		colexp_deferred_recursive( node, expanding );
		node = node->next;
	}
}


/* Timeout gating the deferred morph start on an actually COMPLETED
 * frame (ogl_frame_serial advances at the end of ogl_draw). A mere
 * queued redraw is not enough: the animation loop queues the render
 * and returns, so anything keyed to loop ticks starts the wall-time
 * morph clocks BEFORE the multi-second rebuild frame draws — and
 * they expire inside it (the v4.47.06 "instant change" bug). The
 * main thread is busy during the rebuild render, so this timeout
 * fires right after it completes — exactly the intended moment */
static gboolean
colexp_deferred_check( G_GNUC_UNUSED gpointer data )
{
	if (colexp_deferred_dnode == NULL)
		return G_SOURCE_REMOVE;

	if (ogl_frame_serial( ) <= colexp_deferred_serial)
		return G_SOURCE_CONTINUE; /* rebuild frame not drawn yet */

	colexp_deferred_recursive( colexp_deferred_dnode,
	                           colexp_deferred_expanding );
	colexp_deferred_dnode = NULL;
	redraw( );
	return G_SOURCE_REMOVE;
}




/* This keeps the directory tree and the map geometry in sync
 * (expansion state vs. "deployment" value) */
void
colexp( GNode *dnode, ColExpMesg mesg )
{
	static double colexp_time;
	static int depth = 0;
	static int max_depth;
	GNode *node;
	double wait_time;
	double pan_time;
	int wait_count = 0;
	boolean curnode_is_ancestor, curnode_is_descendant, curnode_is_equal;

	g_assert( NODE_IS_DIR(dnode) );

	if (depth == 0) {
		/* Guard against reentrancy: gui_update() below processes
		 * pending GTK events, which can fire tree expand/collapse
		 * signals that would re-enter colexp() */
		if (colexp_in_progress)
			return;
		colexp_in_progress = TRUE;

		/* Recursive operations can take several seconds on large
		 * trees. Lock down the interface and show a wait cursor
		 * up-front, then pump the GUI once so the cursor is
		 * actually painted before we start the long work. */
		if (mesg == COLEXP_COLLAPSE_RECURSIVE
		    || mesg == COLEXP_EXPAND_RECURSIVE) {
			window_set_access( FALSE );
			gui_update( );
		}

#ifdef DEBUG
		if (mesg != COLEXP_EXPAND_ANY) {
			/* All ancestor directories must be expanded */
			node = dnode->parent;
			while (NODE_IS_DIR(node)) {
				g_assert( DIR_NODE_DESC(node)->deployment > (1.0 - EPSILON) );
				node = node->parent;
			}
		}
#endif

		/* Stagger policy: huge recursive operations deploy all at
		 * once (see COLEXP_NO_STAGGER_NODES above). Decided BEFORE
		 * the dirtree update, which also branches on it */
		colexp_no_stagger = FALSE;
		if (mesg == COLEXP_EXPAND_RECURSIVE
		    || mesg == COLEXP_COLLAPSE_RECURSIVE) {
			int i, n = 0;
			for (i = 0; i < NUM_NODE_TYPES; i++)
				n += (int)DIR_NODE_DESC(dnode)->subtree.counts[i];
			colexp_no_stagger = (n >= COLEXP_NO_STAGGER_NODES);
		}

		/* Update ctree and determine maximum recursion depth */
		switch (mesg) {
			case COLEXP_COLLAPSE_RECURSIVE:
			dirtree_entry_collapse_recursive( dnode );
			max_depth = max_expanded_depth( dnode );
			break;

			case COLEXP_EXPAND:
			dirtree_entry_expand( dnode );
			max_depth = 0;
			break;

			case COLEXP_EXPAND_ANY:
			dirtree_entry_expand( dnode );
			max_depth = collapsed_depth( dnode );
			break;

			case COLEXP_EXPAND_RECURSIVE:
			/* Huge trees: flags only — expanding ~100K sidebar
			 * rows costs seconds of GTK model splices (and worse;
			 * see Phase 47 notes), and a 100K-row sidebar isn't
			 * navigable anyway. Rows expand normally on user
			 * interaction against the already-set flags */
			if (colexp_no_stagger)
				dirtree_entry_expand_flags_only( dnode );
			else
				dirtree_entry_expand_recursive( dnode );
			/* max_depth will be used as a high-water mark */
			max_depth = 0;
			break;

			SWITCH_FAIL
		}

		/* Make file list appropriately (in)accessible */
		filelist_reset_access( );

		gui_update( );

		/* Collapse/expand time for current visualization mode */
		switch (globals.fsv_mode) {
			case FSV_DISCV:
			colexp_time = DISCV_COLEXP_TIME;
			break;

			case FSV_MAPV:
			colexp_time = MAPV_COLEXP_TIME;
			break;

			case FSV_TREEV:
			colexp_time = TREEV_COLEXP_TIME;
			break;

                        SWITCH_FAIL
		}

	}

	morph_break( &DIR_NODE_DESC(dnode)->deployment );

	/* Determine time to wait before collapsing/expanding directory */
	switch (mesg) {
		case COLEXP_COLLAPSE_RECURSIVE:
		wait_count = max_depth - depth;
		break;

		case COLEXP_EXPAND_RECURSIVE:
		case COLEXP_EXPAND:
		wait_count = depth;
		break;

		case COLEXP_EXPAND_ANY:
		wait_count = max_depth - depth;
		break;

		SWITCH_FAIL
	}
	if (colexp_no_stagger)
		wait_count = 0;
	if (wait_count > 0) {
		wait_time = (double)wait_count * colexp_time;
		morph( &DIR_NODE_DESC(dnode)->deployment, MORPH_LINEAR, DIR_NODE_DESC(dnode)->deployment, wait_time );
	}

	/* Initiate collapse/expand */
	if (colexp_no_stagger) {
		/* Nudge across the content boundary now — the single big
		 * rebuild happens on the next frame — but leave the actual
		 * morph to colexp_deferred_event (see its comment). Only
		 * directories actually changing state get nudged */
		if (mesg == COLEXP_COLLAPSE_RECURSIVE) {
			if (DIR_NODE_DESC(dnode)->deployment > 1.0 - EPSILON)
				DIR_NODE_DESC(dnode)->deployment = 1.0 - 1.0e-4;
		}
		else {
			if (DIR_NODE_DESC(dnode)->deployment < EPSILON)
				DIR_NODE_DESC(dnode)->deployment = 1.0e-4;
		}
		geometry_colexp_in_progress( dnode );
	}
	else switch (mesg) {
		case COLEXP_COLLAPSE_RECURSIVE:
		morph_full( &DIR_NODE_DESC(dnode)->deployment, MORPH_QUADRATIC, 0.0, colexp_time, colexp_progress_cb, colexp_progress_cb, dnode );
		break;

		case COLEXP_EXPAND:
		case COLEXP_EXPAND_ANY:
		case COLEXP_EXPAND_RECURSIVE:
		morph_full( &DIR_NODE_DESC(dnode)->deployment, MORPH_INV_QUADRATIC, 1.0, colexp_time, colexp_progress_cb, colexp_progress_cb, dnode );
		break;

		SWITCH_FAIL
	}

	/* Recursion */
	/* geometry_colexp_initiated( ) is called at differing points below
	 * because (at least in TreeV mode) notification must always
	 * proceed from parent to children, and not the other way around */
	switch (mesg) {
		case COLEXP_EXPAND:
		/* Initial collapse/expand notify */
		geometry_colexp_initiated( dnode );
		/* EXPAND does not walk the tree */
		break;

		case COLEXP_EXPAND_ANY:
		/* Ensure that all parent directories are expanded */
		if (NODE_IS_DIR(dnode->parent)) {
			++depth;
			colexp( dnode->parent, COLEXP_EXPAND_ANY );
			--depth;
		}
		/* Initial collapse/expand notify */
		geometry_colexp_initiated( dnode );
		break;

		case COLEXP_COLLAPSE_RECURSIVE:
		case COLEXP_EXPAND_RECURSIVE:
		/* Initial collapse/expand notify */
		geometry_colexp_initiated( dnode );
		/* Perform action on subdirectories */
		++depth;
		node = dnode->children;
		while (node != NULL) {
			if (NODE_IS_DIR(node))
				colexp( node, mesg );
			else
				break;
			node = node->next;
		}
		--depth;
		break;

		SWITCH_FAIL
	}

	if (mesg == COLEXP_EXPAND_RECURSIVE) {
		/* Update high-water mark */
		max_depth = MAX(max_depth, depth);
	}

	if (depth == 0) {
		/* Effective animation depth: a no-stagger operation runs
		 * everything in one colexp_time regardless of tree depth */
		int eff_depth = colexp_no_stagger ? 0 : max_depth;

		/* Determine position of current node w.r.t. the
		 * collapsing/expanding directory node */
		curnode_is_ancestor = g_node_is_ancestor( globals.current_node, dnode );
		curnode_is_equal = globals.current_node == dnode;
		curnode_is_descendant = g_node_is_ancestor( dnode, globals.current_node );

		/* Show busy cursor during the animation */
		window_set_access( FALSE );

		/* Handle the camera semi-intelligently if it is not under
		 * manual control.  camera_look_at_full() will re-enable
		 * access when the pan finishes. */
		boolean camera_panning = FALSE;
		if (!camera->manual_control) {
			switch (mesg) {
				case COLEXP_COLLAPSE_RECURSIVE:
				pan_time = (double)(eff_depth + 1) * colexp_time;
				if (curnode_is_ancestor || curnode_is_equal) {
					camera_look_at_full( globals.current_node, MORPH_LINEAR, pan_time );
					camera_panning = TRUE;
				}
				else if (curnode_is_descendant) {
					camera_look_at_full( dnode, MORPH_LINEAR, pan_time );
					camera_panning = TRUE;
				}
				break;

				case COLEXP_EXPAND:
				case COLEXP_EXPAND_RECURSIVE:
				if (curnode_is_ancestor || curnode_is_equal) {
					pan_time = (double)(eff_depth + 1) * colexp_time;
					if (globals.fsv_mode == FSV_TREEV) {
						/* Glide to frame the expanding
						 * subtree while it deploys; access
						 * is re-enabled by the generic
						 * timer below */
						camera_treev_frame_expansion( dnode, pan_time );
					}
					else {
						camera_look_at_full( globals.current_node, MORPH_LINEAR, pan_time );
						camera_panning = TRUE;
					}
				}
				break;

				case COLEXP_EXPAND_ANY:
				/* Don't do anything. Something else
				 * should already be doing something
				 * with the camera */
				break;

				SWITCH_FAIL
			}
		}

		/* No-stagger: kick off the single content rebuild and
		 * start the deferred morphs once it has actually drawn */
		if (colexp_no_stagger) {
			colexp_deferred_dnode = dnode;
			colexp_deferred_expanding =
				(mesg != COLEXP_COLLAPSE_RECURSIVE);
			colexp_deferred_serial = ogl_frame_serial( );
			g_timeout_add( 30, colexp_deferred_check, NULL );
			redraw( );
		}

		/* If no camera pan was started, re-enable access after
		 * the collapse/expand animation completes */
		if (!camera_panning) {
			double anim_time = colexp_no_stagger
				? (COLEXP_NO_STAGGER_TIME + 2.0)
				: (double)(eff_depth + 1) * colexp_time;
			g_timeout_add( (guint)(anim_time * 1000.0),
				colexp_reenable_access, NULL );
		}

		/* If, in TreeV mode, the current node is an ancestor of
		 * a collapsing/expanding directory, the scrollbars may
		 * need updating to reflect a new scroll range */
		scrollbars_colexp_adjust = FALSE;
		if (curnode_is_ancestor && (globals.fsv_mode == FSV_TREEV))
			scrollbars_colexp_adjust = TRUE;

		colexp_in_progress = FALSE;
	}
}


/* end colexp.c */
