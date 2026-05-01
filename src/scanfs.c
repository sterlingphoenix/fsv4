/* scanfs.c */

/* Filesystem scanner */

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
#include "scanfs.h"

#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <gtk/gtk.h>

#include "color.h" /* color_mark_dirty( ) */
#include "dirtree.h"
#include "filelist.h"
#include "geometry.h" /* geometry_free( ) */
#include "gui.h" /* gui_update( ) */
#include "lazy_render.h"
#include "viewport.h" /* viewport_pass_node_table( ) */
#include "window.h"


#ifndef HAVE_SCANDIR
int scandir( const char *dir, struct dirent ***namelist, int (*selector)( const struct dirent * ), int (*cmp)( const void *, const void * ) );
int alphasort( const void *a, const void *b );
#endif


/* On-the-fly progress display is updated at intervals this far apart
 * (integer value in milliseconds) */
#define SCAN_MONITOR_PERIOD 500


/* Name strings are stored here */
static GStringChunk *name_strchunk = NULL;

/* Node ID counter (worker-thread only during scan) */
static unsigned int node_id;

/* Scan progress — all of the fields below are written by the worker
 * thread during a scan and read by the main thread's scan_monitor
 * timeout. They are protected by scan_stats_mutex. */
static GMutex scan_stats_mutex;
static int node_counts[NUM_NODE_TYPES];
static int64 size_counts[NUM_NODE_TYPES];
static int stat_count = 0;
static char scan_current_dir[PATH_MAX];
/* Set to TRUE by the worker once the disk-scan phase is done and the
 * post-scan geometry_init / color_assign is running. scan_monitor
 * uses this to change the statusbar readout to "Preparing...". */
static boolean scan_preparing = FALSE;


/* Official stat function. Returns 0 on success, -1 on error.
 * The path argument is the absolute path to stat. */
static int
stat_node( GNode *node, const char *path )
{
	struct stat st;

	if (lstat( path, &st ))
		return -1;

	/* Determine node type */
	if (S_ISDIR(st.st_mode))
		NODE_DESC(node)->type = NODE_DIRECTORY;
	else if (S_ISREG(st.st_mode))
		NODE_DESC(node)->type = NODE_REGFILE;
	else if (S_ISLNK(st.st_mode))
		NODE_DESC(node)->type = NODE_SYMLINK;
	else if (S_ISFIFO(st.st_mode))
		NODE_DESC(node)->type = NODE_FIFO;
	else if (S_ISSOCK(st.st_mode))
		NODE_DESC(node)->type = NODE_SOCKET;
	else if (S_ISCHR(st.st_mode))
		NODE_DESC(node)->type = NODE_CHARDEV;
	else if (S_ISBLK(st.st_mode))
		NODE_DESC(node)->type = NODE_BLOCKDEV;
	else
		NODE_DESC(node)->type = NODE_UNKNOWN;

	/* A corrupted DOS filesystem once gave me st_size = -4GB */
	g_assert( st.st_size >= 0 );

	NODE_DESC(node)->size = st.st_size;
	NODE_DESC(node)->size_alloc = 512 * st.st_blocks;
	NODE_DESC(node)->user_id = st.st_uid;
	NODE_DESC(node)->group_id = st.st_gid;
	NODE_DESC(node)->perms = st.st_mode & 0777;
	NODE_DESC(node)->atime = st.st_atime;
	NODE_DESC(node)->mtime = st.st_mtime;
	NODE_DESC(node)->ctime = st.st_ctime;

	return 0;
}


/* Selector function for use with scandir( ). This lets through all
 * directory entries except for "." and ".." */
static int
de_select( const struct dirent *de )
{
	if (de->d_name[0] != '.')
		return 1; /* Allow "whatever" */
	if (de->d_name[1] == '\0')
		return 0; /* Disallow "." */
	if (de->d_name[1] != '.')
		return 1; /* Allow ".whatever" */
	if (de->d_name[2] == '\0')
		return 0; /* Disallow ".." */

	/* Allow "..whatever", "...whatever", etc. */
	return 1;
}


/* Determine NodeType from a stat-mode bitset. Used by the size-only
 * walk past the lazy-render depth limit, where we don't allocate
 * NodeDescs but still want to bucket counts/sizes by type. */
static NodeType
node_type_from_stmode( mode_t mode )
{
	if (S_ISDIR(mode))   return NODE_DIRECTORY;
	if (S_ISREG(mode))   return NODE_REGFILE;
	if (S_ISLNK(mode))   return NODE_SYMLINK;
	if (S_ISFIFO(mode))  return NODE_FIFO;
	if (S_ISSOCK(mode))  return NODE_SOCKET;
	if (S_ISCHR(mode))   return NODE_CHARDEV;
	if (S_ISBLK(mode))   return NODE_BLOCKDEV;
	return NODE_UNKNOWN;
}


/* Size-only directory walk used past the lazy-render depth limit.
 * Recursively readdir()/lstat()s every entry below `path`, accumulating
 * total bytes and per-type counts into the caller's running totals,
 * but allocates NO NodeDesc / DirNodeDesc / GNode. Progress is also
 * published to the scan_monitor statics so the statusbar keeps moving.
 *
 * Runs on the scan worker thread. */
static void
size_only_walk( const char *path, int64 *total_size,
                unsigned int counts[NUM_NODE_TYPES] )
{
	DIR *d;
	struct dirent *ent;
	char child[PATH_MAX];
	size_t path_len;

	d = opendir( path );
	if (d == NULL)
		return;

	/* Update worker-thread "current dir" readout */
	g_mutex_lock( &scan_stats_mutex );
	g_strlcpy( scan_current_dir, path, sizeof(scan_current_dir) );
	g_mutex_unlock( &scan_stats_mutex );

	path_len = strlen( path );
	if (path_len >= PATH_MAX - 2) {
		closedir( d );
		return;
	}
	memcpy( child, path, path_len );
	if (path_len > 0 && child[path_len - 1] != '/')
		child[path_len++] = '/';

	while ((ent = readdir( d )) != NULL) {
		struct stat st;
		NodeType type;
		size_t name_len;

		/* Skip "." and ".." */
		if (ent->d_name[0] == '.'
		    && (ent->d_name[1] == '\0'
		        || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue;

		name_len = strlen( ent->d_name );
		if (path_len + name_len >= PATH_MAX)
			continue;
		memcpy( &child[path_len], ent->d_name, name_len + 1 );

		if (lstat( child, &st ) != 0)
			continue;

		type = node_type_from_stmode( st.st_mode );

		*total_size += (int64)st.st_size;
		counts[type]++;

		/* Publish progress so scan_monitor's stats/sec readout keeps
		 * moving during deep size-only sweeps. */
		g_mutex_lock( &scan_stats_mutex );
		++stat_count;
		++node_counts[type];
		size_counts[type] += (int64)st.st_size;
		g_mutex_unlock( &scan_stats_mutex );

		if (S_ISDIR(st.st_mode))
			size_only_walk( child, total_size, counts );
	}

	closedir( d );
}


/* Recursive directory scanner.
 *
 * `depth_remaining` is the number of additional levels below `dnode`
 * for which we will allocate per-node descriptors. When it reaches
 * zero, child directories are walked size-only (their bytes/counts
 * accumulate into the child's subtree but their grandchildren are not
 * allocated as GNodes) and marked SCAN_SIZE_ONLY. INT_MAX disables
 * the limit (used when lazy render is OFF).
 *
 * Runs on the scan worker thread (see scan_worker_thread). MUST NOT
 * touch GTK widgets, globals.fsv_mode, or any other main-thread-owned
 * state — only the tree being built, the name_strchunk, and the
 * progress statics under scan_stats_mutex. */
static int
process_dir( const char *dir, GNode *dnode, int depth_remaining )
{
	union AnyNodeDesc any_node_desc, *andesc;
	struct dirent **dir_entries;
	GNode *node;
	int num_entries, i;
	int dir_len;
	char pathbuf[PATH_MAX];

	/* Scan in directory entries */
	num_entries = scandir( dir, &dir_entries, de_select, alphasort );
	if (num_entries < 0)
		return -1;

	/* Publish current directory for the main-thread scan_monitor
	 * timeout to display. */
	g_mutex_lock( &scan_stats_mutex );
	g_strlcpy( scan_current_dir, dir, sizeof(scan_current_dir) );
	g_mutex_unlock( &scan_stats_mutex );

	/* Prepare path buffer: "dir/" prefix, entry name appended per iteration */
	dir_len = strlen( dir );
	memcpy( pathbuf, dir, dir_len );
	if (dir_len > 0 && pathbuf[dir_len - 1] != '/')
		pathbuf[dir_len++] = '/';

	/* Process directory entries */
	for (i = 0; i < num_entries; i++) {
		/* Build full path for this entry */
		strncpy( &pathbuf[dir_len], dir_entries[i]->d_name, PATH_MAX - dir_len - 1 );
		pathbuf[PATH_MAX - 1] = '\0';

		/* Create new node */
		node = g_node_prepend_data( dnode, &any_node_desc );
		NODE_DESC(node)->id = node_id;
		NODE_DESC(node)->name = g_string_chunk_insert( name_strchunk, dir_entries[i]->d_name );
		if (stat_node( node, pathbuf )) {
			/* Stat failed */
			g_node_unlink( node );
			g_node_destroy( node );
			continue;
		}
		++node_id;

		if (NODE_IS_DIR(node)) {
			/* Default scan state — explicitly set so any stack
			 * garbage in the bitfield doesn't carry through the
			 * memcpy below. */
			DIR_NODE_DESC(node)->scan_state = SCAN_FULL;

			/* Create corresponding directory tree entry */
			dirtree_entry_new( node );

			if (depth_remaining > 0) {
				/* Recurse down using the already-built path */
				process_dir( pathbuf, node, depth_remaining - 1 );
			}
			else {
				/* Past the lazy-render depth limit: do a
				 * size-only walk that aggregates this dir's
				 * subtree totals without allocating any
				 * children. setup_fstree_recursive() honors
				 * SCAN_SIZE_ONLY by skipping the subtree
				 * zero-init so these totals survive. */
				DIR_NODE_DESC(node)->scan_state = SCAN_SIZE_ONLY;
				DIR_NODE_DESC(node)->subtree.size = 0;
				memset( DIR_NODE_DESC(node)->subtree.counts,
				        0,
				        sizeof(DIR_NODE_DESC(node)->subtree.counts) );
				size_only_walk( pathbuf,
				                &DIR_NODE_DESC(node)->subtree.size,
				                DIR_NODE_DESC(node)->subtree.counts );
			}

			/* Move new descriptor into working memory */
			andesc = (union AnyNodeDesc *)g_new0( DirNodeDesc, 1 );
			memcpy( andesc, DIR_NODE_DESC(node), sizeof(DirNodeDesc) );
			node->data = andesc;
		}
		else if (NODE_DESC(node)->type == NODE_SYMLINK) {
			/* Symlinks get an extended descriptor so the
			 * post-scan resolve_symlinks pass can record the
			 * target's display size. target_size defaults to 0
			 * (unresolved). */
			andesc = (union AnyNodeDesc *)g_new0( SymlinkNodeDesc, 1 );
			memcpy( andesc, NODE_DESC(node), sizeof(NodeDesc) );
			node->data = andesc;
		}
		else {
			/* Move new descriptor into working memory */
			andesc = (union AnyNodeDesc *)g_new0( NodeDesc, 1 );
			memcpy( andesc, NODE_DESC(node), sizeof(NodeDesc) );
			node->data = andesc;
		}

		/* Publish progress for the main-thread monitor */
		g_mutex_lock( &scan_stats_mutex );
		++stat_count;
		++node_counts[NODE_DESC(node)->type];
		size_counts[NODE_DESC(node)->type] += NODE_DESC(node)->size;
		g_mutex_unlock( &scan_stats_mutex );

		free( dir_entries[i] ); /* !xfree */
	}

	free( dir_entries ); /* !xfree */

	return 0;
}


/* Dynamic scan progress readout. Runs on the main thread while the
 * scan worker is populating the counts under scan_stats_mutex. */
static gboolean
scan_monitor( G_GNUC_UNUSED gpointer user_data )
{
	int local_node_counts[NUM_NODE_TYPES];
	int64 local_size_counts[NUM_NODE_TYPES];
	int local_stat_count;
	char local_dir[PATH_MAX];
	boolean local_preparing;
	char strbuf[PATH_MAX + 64];

	/* Snapshot worker-thread stats under the mutex. Reset stat_count
	 * so the per-period rate stays sane. */
	g_mutex_lock( &scan_stats_mutex );
	memcpy( local_node_counts, node_counts, sizeof(local_node_counts) );
	memcpy( local_size_counts, size_counts, sizeof(local_size_counts) );
	local_stat_count = stat_count;
	stat_count = 0;
	g_strlcpy( local_dir, scan_current_dir, sizeof(local_dir) );
	local_preparing = scan_preparing;
	g_mutex_unlock( &scan_stats_mutex );

	/* Running totals in file list area */
	filelist_scan_monitor( local_node_counts, local_size_counts );

	if (local_preparing) {
		/* Post-scan layout/color pass is running on the worker. */
		window_statusbar( SB_LEFT, "" );
		window_statusbar( SB_RIGHT, _("Preparing visualisation...") );
	}
	else {
		/* Stats-per-second readout in left statusbar */
		sprintf( strbuf, _("%d stats/sec"), 1000 * local_stat_count / SCAN_MONITOR_PERIOD );
		window_statusbar( SB_LEFT, strbuf );

		/* Currently scanning directory in right statusbar */
		if (local_dir[0] != '\0') {
			snprintf( strbuf, sizeof(strbuf), _("Scanning: %s"), local_dir );
			window_statusbar( SB_RIGHT, strbuf );
		}
	}

	return TRUE;
}


/* Forward declaration — definition is below with the other
 * setup-once-scan-is-done helpers. */
static void setup_fstree_recursive( GNode *node, GNode **node_table );
static void resolve_symlinks_recursive( GNode *node, const char *root_abs );
static int node_abspath_worker( GNode *node, char *buf, size_t buflen );
static int64 scan_out_of_root_dir_size( const char *abs_path );


/* Context for an in-flight async scan. Allocated on the heap, owned
 * by the GTask, freed in scan_worker_done_cb (main thread) after the
 * post-scan GTK work and user callback have run. */
typedef struct {
	char *root_dir;		/* owned */
	GNode **node_table;	/* output, allocated on worker; ownership
				 * transferred to viewport in done_cb */
	unsigned int node_count; /* output */
	FsvMode initial_mode;	/* mode to pre-initialise on the worker */
	ScanDoneCallback done_cb;
	gpointer user_data;
} ScanContext;


static void
scan_ctx_free( gpointer data )
{
	ScanContext *ctx = data;
	g_free( ctx->root_dir );
	g_free( ctx );
}


/* Main-thread source id for the periodic scan_monitor timeout; owned
 * by the async scan, removed in scan_worker_done_cb. */
static guint scan_monitor_source_id = 0;


static void
scan_worker_thread(
	GTask *task,
	G_GNUC_UNUSED gpointer source_object,
	gpointer task_data,
	G_GNUC_UNUSED GCancellable *cancellable )
{
	ScanContext *ctx = task_data;
	int max_full_depth;

	/* Determine how deep to go before switching to size-only walks.
	 * Lazy-render setting: render_depth + readahead_depth. When the
	 * feature is disabled we use INT_MAX so behavior matches the
	 * original full-scan implementation exactly. */
	if (lazy_render_enabled( ))
		max_full_depth = lazy_render_depth( ) + lazy_readahead_depth( );
	else
		max_full_depth = INT_MAX;

	/* Disk thrashing phase */
	process_dir( ctx->root_dir, root_dnode, max_full_depth );

	/* Post-scan tree finalisation. No GTK calls — safe on worker. */
	ctx->node_count = node_id;
	ctx->node_table = NEW_ARRAY(GNode *, ctx->node_count);
	setup_fstree_recursive( globals.fstree, ctx->node_table );

	/* Resolve symlink target sizes now that subtree totals exist.
	 * Pure tree walk + stat() calls; no GTK access. Precompute the
	 * root's absolute path so the recursive pass doesn't touch
	 * node_absname's main-thread-owned static buffer. */
	{
		char root_abs[PATH_MAX];
		if (node_abspath_worker( root_dnode, root_abs, sizeof(root_abs) ) == 0)
			resolve_symlinks_recursive( globals.fstree, root_abs );
	}

	/* Pre-lay out + color the initial visualization on the worker
	 * thread so the main thread doesn't freeze for seconds after
	 * the scan completes. geometry_init() is pure data over the
	 * GNode tree — no GTK widgets, no GL calls (vbo_batch_invalidate
	 * and ogl_pick_invalidate only flip bool flags). Main thread
	 * consumes the prebuilt layout via geometry_consume_prebuilt().
	 *
	 * Flip scan_preparing so the next scan_monitor tick on the main
	 * thread swaps the statusbar readout to "Preparing...". */
	g_mutex_lock( &scan_stats_mutex );
	scan_preparing = TRUE;
	g_mutex_unlock( &scan_stats_mutex );

	geometry_init( ctx->initial_mode );

	g_task_return_boolean( task, TRUE );
}


/* Main-thread callback fired when the scan worker finishes. Does all
 * remaining GTK work (dirtree population, viewport handoff, statusbar
 * clear) and then invokes the caller-supplied done_cb. */
static void
scan_worker_done_cb(
	G_GNUC_UNUSED GObject *source,
	GAsyncResult *res,
	G_GNUC_UNUSED gpointer user_data )
{
	ScanContext *ctx = g_task_get_task_data( G_TASK(res) );
	gint64 t_start, t0, t_dirtree, t_viewport, t_donecb;

	t_start = g_get_monotonic_time( );

	if (scan_monitor_source_id != 0) {
		g_source_remove( scan_monitor_source_id );
		scan_monitor_source_id = 0;
	}
	window_statusbar( SB_RIGHT, "" );

	t0 = g_get_monotonic_time( );
	dirtree_no_more_entries( );
	t_dirtree = g_get_monotonic_time( ) - t0;

	/* Hand node table ownership off to the viewport */
	t0 = g_get_monotonic_time( );
	viewport_pass_node_table( ctx->node_table, ctx->node_count );
	ctx->node_table = NULL;
	t_viewport = g_get_monotonic_time( ) - t0;

	/* Fire the user's continuation (e.g. fsv_load_after_scan) */
	t0 = g_get_monotonic_time( );
	if (ctx->done_cb != NULL)
		ctx->done_cb( ctx->user_data );
	t_donecb = g_get_monotonic_time( ) - t0;

	g_printerr(
		"[scan done_cb] total=%.1fms dirtree_no_more_entries=%.1fms "
		"viewport_pass_node_table=%.1fms done_cb=%.1fms\n",
		(double)(g_get_monotonic_time( ) - t_start) / 1000.0,
		(double)t_dirtree / 1000.0,
		(double)t_viewport / 1000.0,
		(double)t_donecb / 1000.0 );

	/* GTask will be unref'd after this returns, which frees ctx via
	 * scan_ctx_free. */
}


/* Compare function for sorting nodes
 * (directories first, then larger to smaller, then alphabetically A-Z)
 * Note: Directories must *always* go before leafs-- this speeds up
 * recursion, as it allows iteration to stop at the first leaf */
static int
compare_node( NodeDesc *a, NodeDesc *b )
{
	int64 a_size, b_size;
	int s = 0;

	a_size = a->size;
	if (a->type == NODE_DIRECTORY) {
		a_size += ((DirNodeDesc *)a)->subtree.size;
		s -= 2;
	}

	b_size = b->size;
	if (b->type == NODE_DIRECTORY) {
		b_size += ((DirNodeDesc *)b)->subtree.size;
		s += 2;
	}

	if (a_size > b_size)
		--s;
	if (a_size < b_size)
		++s;
	if (!s)
		return strcmp( a->name, b->name );

	return s;
}


/* This does major post-scan housekeeping on the filesystem tree. It
 * sorts everything, assigns subtree size/count information to directory
 * nodes, sets up the node table, etc. */
static void
setup_fstree_recursive( GNode *node, GNode **node_table )
{
	GNode *child_node;
	int i;

	/* Assign entry in the node table */
	node_table[NODE_DESC(node)->id] = node;

	if (NODE_IS_DIR(node) || NODE_IS_METANODE(node)) {
		/* Initialize subtree quantities — except for SIZE_ONLY
		 * dirs, whose subtree was already populated by the
		 * size-only walk in process_dir() and has no children to
		 * walk here. */
		boolean is_size_only = NODE_IS_DIR(node)
			&& DIR_NODE_DESC(node)->scan_state == SCAN_SIZE_ONLY;
		if (!is_size_only) {
			DIR_NODE_DESC(node)->subtree.size = 0;
			for (i = 0; i < NUM_NODE_TYPES; i++)
				DIR_NODE_DESC(node)->subtree.counts[i] = 0;
		}

		/* Recurse down (no-op when SIZE_ONLY: no children) */
		child_node = node->children;
		while (child_node != NULL) {
			setup_fstree_recursive( child_node, node_table );
			child_node = child_node->next;
		}
	}

	if (!NODE_IS_METANODE(node)) {
		/* Increment subtree quantities of parent */
		DIR_NODE_DESC(node->parent)->subtree.size += NODE_DESC(node)->size;
		++DIR_NODE_DESC(node->parent)->subtree.counts[NODE_DESC(node)->type];
	}

	if (NODE_IS_DIR(node)) {
		/* Sort directory contents */
		node->children = (GNode *)g_list_sort( (GList *)node->children, (GCompareFunc)compare_node );
		/* Propagate subtree size/counts upward */
		DIR_NODE_DESC(node->parent)->subtree.size += DIR_NODE_DESC(node)->subtree.size;
		for (i = 0; i < NUM_NODE_TYPES; i++)
			DIR_NODE_DESC(node->parent)->subtree.counts[i] += DIR_NODE_DESC(node)->subtree.counts[i];
	}
}


/* Build the absolute pathname of `node` into `buf` without touching
 * the static buffer inside node_absname() — that static is shared
 * with main-thread callers (dirtree / filelist selection handlers)
 * and cannot be assumed race-free during the post-scan symlink pass
 * on the worker thread. Returns 0 on success, -1 if the path would
 * overflow buflen or the tree is pathologically deep. */
static int
node_abspath_worker( GNode *node, char *buf, size_t buflen )
{
	GNode *chain[256];
	int depth = 0;
	size_t pos = 0;
	int i;

	while (node != NULL) {
		if (depth >= (int)G_N_ELEMENTS(chain))
			return -1;
		chain[depth++] = node;
		node = node->parent;
	}

	for (i = depth - 1; i >= 0; i--) {
		const char *name = NODE_DESC(chain[i])->name;
		size_t nlen = strlen( name );
		if (pos > 0) {
			if (pos + 1 >= buflen) return -1;
			buf[pos++] = '/';
		}
		if (pos + nlen >= buflen) return -1;
		memcpy( &buf[pos], name, nlen );
		pos += nlen;
	}
	if (pos >= buflen) return -1;
	buf[pos] = '\0';

	/* node_absname() special-cases a root of "/" by stripping a
	 * leading double-slash; do the same here for consistency. */
	if (pos >= 2 && buf[0] == '/' && buf[1] == '/')
		memmove( buf, buf + 1, pos );

	return 0;
}


/* Worker-thread variant of node_named() that takes a pre-computed
 * root absolute path (so it doesn't call node_absname() and race
 * with the main thread's static buffer). Otherwise identical to
 * the component-by-component match in node_named(). */
static GNode *
node_named_worker( const char *absname, const char *root_abs )
{
	GNode *node;
	size_t root_len;
	const char *tail;
	char *tail_copy, *name;

	root_len = strlen( root_abs );
	if (strncmp( root_abs, absname, root_len ) != 0)
		return NULL;

	/* Handle root == "/" so tail starts at absname itself */
	if (root_len == 1 && root_abs[0] == '/')
		root_len = 0;

	tail = &absname[root_len];
	if (tail[0] == '\0' || (tail[0] == '/' && tail[1] == '\0'))
		return root_dnode;

	tail_copy = xstrdup( tail );
	name = strtok( tail_copy, "/" );
	node = root_dnode->children;
	while (node != NULL && name != NULL) {
		if (strcmp( name, NODE_DESC(node)->name ) == 0) {
			name = strtok( NULL, "/" );
			if (name == NULL)
				break;
			node = node->children;
			continue;
		}
		node = node->next;
	}

	xfree( tail_copy );
	return node;
}


/* Bounded recursive directory size calculator for out-of-root
 * symlink targets. Walks the target directory summing file sizes,
 * recursing into subdirectories but NOT following symlinks (uses
 * lstat). Capped at a few thousand entries so a symlink pointing
 * outside the scanned root (e.g. /usr, /home) returns a meaningful
 * number without the scan hanging on pathological targets. */
#define OUT_OF_ROOT_MAX_ENTRIES 20000
#define OUT_OF_ROOT_MAX_DEPTH   12

static void
scan_out_of_root_walk( const char *path, int depth, int64 *total, int *budget )
{
	DIR *dir;
	struct dirent *ent;

	if (depth >= OUT_OF_ROOT_MAX_DEPTH)
		return;
	if (*budget <= 0)
		return;

	dir = opendir( path );
	if (dir == NULL)
		return;

	while ((ent = readdir( dir )) != NULL) {
		char child[PATH_MAX];
		struct stat st;

		if (*budget <= 0)
			break;
		if (ent->d_name[0] == '.'
		    && (ent->d_name[1] == '\0'
		        || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue;

		if ((int)(strlen( path ) + 1 + strlen( ent->d_name )) >= PATH_MAX)
			continue;
		snprintf( child, sizeof(child), "%s/%s", path, ent->d_name );

		if (lstat( child, &st ) != 0)
			continue;
		(*budget)--;

		if (S_ISDIR(st.st_mode))
			scan_out_of_root_walk( child, depth + 1, total, budget );
		else if (S_ISREG(st.st_mode))
			*total += (int64)st.st_size;
	}

	closedir( dir );
}

static int64
scan_out_of_root_dir_size( const char *abs_path )
{
	int64 total = 0;
	int budget = OUT_OF_ROOT_MAX_ENTRIES;
	scan_out_of_root_walk( abs_path, 0, &total, &budget );
	return total;
}


/* Post-scan pass: for every symlink, resolve what it points at and
 * record the target's "display size" so MapV / DiscV / TreeV can lay
 * the symlink out at something closer to its effective weight. The
 * symlink keeps its own NODE_DESC(node)->size (the lstat byte count
 * of the pathname) — that's what Properties shows.
 *
 * Strategy:
 *   - stat() (follows symlinks) the absolute pathname of the symlink.
 *   - If stat fails → broken link, leave target_size = 0.
 *   - If stat succeeds and S_ISDIR: try to find the target inside
 *     the scanned tree via node_named() + realpath resolution. If
 *     found, use that dnode's subtree.size + own size. If target
 *     is outside the scanned root, fall back to st.st_size.
 *   - Otherwise (regular file / fifo / etc.), use st.st_size.
 *
 * Runs on the scan worker thread between setup_fstree_recursive()
 * and g_task_return_boolean(). No GTK access. node_absname() is not
 * called here — see node_abspath_worker(). */
static void
resolve_symlinks_recursive( GNode *node, const char *root_abs )
{
	GNode *child;

	if (NODE_IS_SYMLINK(node)) {
		char symlink_abs[PATH_MAX];
		struct stat st;

		if (node_abspath_worker( node, symlink_abs, sizeof(symlink_abs) ) == 0
		    && stat( symlink_abs, &st ) == 0) {
			int64 target_size = 0;
			boolean target_is_dir = FALSE;

			if (S_ISDIR(st.st_mode)) {
				target_is_dir = TRUE;
				char resolved[PATH_MAX];
				if (realpath( symlink_abs, resolved ) != NULL) {
					GNode *target_node = node_named_worker( resolved, root_abs );
					if (target_node != NULL && NODE_IS_DIR(target_node)) {
						target_size = NODE_DESC(target_node)->size
							    + DIR_NODE_DESC(target_node)->subtree.size;
					}
					else {
						/* Out-of-root (or unscanned) directory:
						 * walk the target ourselves to get a
						 * meaningful size — bounded so a symlink
						 * to / doesn't hang the scan. */
						target_size = scan_out_of_root_dir_size( resolved );
					}
				}
				if (target_size == 0)
					target_size = st.st_size;
			}
			else {
				target_size = st.st_size;
			}

			SYMLINK_NODE_DESC(node)->target_size = target_size;
			SYMLINK_NODE_DESC(node)->target_is_dir = target_is_dir;
		}
		/* else: broken link — target_size stays 0 */
	}

	/* Recurse */
	child = node->children;
	while (child != NULL) {
		resolve_symlinks_recursive( child, root_abs );
		child = child->next;
	}
}


/* Callback for g_node_traverse to free node descriptor data */
static gboolean
free_node_data_cb( GNode *node, G_GNUC_UNUSED gpointer data )
{
	if (node->data != NULL)
		g_free( node->data );
	return FALSE;
}


/* Top-level call to recursively scan a filesystem.
 *
 * Returns immediately — the scan runs on a worker thread so the GTK
 * main loop stays live (widgets paint, events dispatch, window
 * close/minimise work normally). When the scan completes, done_cb is
 * invoked on the main thread with user_data; that's where the caller
 * (fsv_load) runs its own post-scan logic. */
void
scanfs( const char *dir, FsvMode initial_mode,
        ScanDoneCallback done_cb, gpointer user_data )
{
	const char *root_dir;
	ScanContext *ctx;
	GTask *task;
	char *name;

	/* The new tree's nodes will need fresh colours assigned. */
	color_mark_dirty( );

	if (globals.fstree != NULL) {
		/* Tear down the UI first so GTK drops its FsvDirItem refs to
		 * the GNode pointers before we free them. Otherwise stale
		 * dnode pointers can be dereferenced during teardown. */
		dirtree_clear( );

		geometry_free_recursive( globals.fstree );
		/* Free node descriptors */
		g_node_traverse( globals.fstree, G_PRE_ORDER, G_TRAVERSE_ALL, -1, free_node_data_cb, NULL );
		g_node_destroy( globals.fstree );
		globals.fstree = NULL;
		globals.current_node = NULL;
	}
	else {
		dirtree_clear( );
	}

	/* ...and string chunks to hold name strings */
	if (name_strchunk != NULL)
		g_string_chunk_free( name_strchunk );
	name_strchunk = g_string_chunk_new( 8192 );

	/* Reset node numbering */
	node_id = 0;

	/* Get absolute path of desired root (top-level) directory */
	if (chdir( dir ) != 0) {
		/* Nothing to scan — still fire the callback so the caller
		 * doesn't deadlock waiting for a scan that never happened. */
		if (done_cb != NULL)
			done_cb( user_data );
		return;
	}
	root_dir = xgetcwd( );

	/* Set up fstree metanode */
	globals.fstree = g_node_new( g_new0( DirNodeDesc, 1 ) );
	NODE_DESC(globals.fstree)->type = NODE_METANODE;
	NODE_DESC(globals.fstree)->id = node_id++;
	name = g_path_get_dirname( root_dir );
	NODE_DESC(globals.fstree)->name = g_string_chunk_insert( name_strchunk, name );
	g_free( name );
	DIR_NODE_DESC(globals.fstree)->tree_expanded = FALSE;

	/* Set up root directory node */
	g_node_append_data( globals.fstree, g_new0( DirNodeDesc, 1 ) );
	/* Note: We can now use root_dnode to refer to the node just
	 * created (it is an alias for globals.fstree->children) */
	NODE_DESC(root_dnode)->id = node_id++;
	name = g_path_get_basename( root_dir );
	NODE_DESC(root_dnode)->name = g_string_chunk_insert( name_strchunk, name );
	g_free( name );
	stat_node( root_dnode, root_dir );
	dirtree_entry_new( root_dnode );

	/* Reset progress counters for the new scan */
	g_mutex_lock( &scan_stats_mutex );
	memset( node_counts, 0, sizeof(node_counts) );
	memset( size_counts, 0, sizeof(size_counts) );
	stat_count = 0;
	scan_current_dir[0] = '\0';
	scan_preparing = FALSE;
	g_mutex_unlock( &scan_stats_mutex );

	/* GUI stuff */
	filelist_scan_monitor_init( );
	scan_monitor_source_id = g_timeout_add( SCAN_MONITOR_PERIOD, scan_monitor, NULL );

	/* Dispatch the disk-thrashing phase to a worker thread. When the
	 * worker finishes, scan_worker_done_cb fires on the main thread
	 * and does all remaining GTK work + invokes done_cb. */
	ctx = g_new0( ScanContext, 1 );
	ctx->root_dir = g_strdup( root_dir );
	ctx->initial_mode = initial_mode;
	ctx->done_cb = done_cb;
	ctx->user_data = user_data;

	task = g_task_new( NULL, NULL, scan_worker_done_cb, NULL );
	g_task_set_task_data( task, ctx, scan_ctx_free );
	g_task_run_in_thread( task, scan_worker_thread );
	g_object_unref( task );	/* GTask holds its own ref during the
				 * in-thread run; drop ours */
}


/* end scanfs.c */
