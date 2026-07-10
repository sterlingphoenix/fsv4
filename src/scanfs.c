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
#include <fcntl.h> /* AT_SYMLINK_NOFOLLOW */
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <gtk/gtk.h>

#include "color.h" /* color_mark_dirty( ) */
#include "dirtree.h"
#include "filelist.h"
#include "geometry.h" /* geometry_free( ) */
#include "gui.h" /* gui_update( ) */
#include "viewport.h" /* viewport_pass_node_table( ) */
#include "window.h"


#ifndef HAVE_SCANDIR
int scandir( const char *dir, struct dirent ***namelist, int (*selector)( const struct dirent * ), int (*cmp)( const void *, const void * ) );
int alphasort( const void *a, const void *b );
#endif


/* On-the-fly progress display is updated at intervals this far apart
 * (integer value in milliseconds) */
#define SCAN_MONITOR_PERIOD 500


/* Name strings are stored here (main-thread inserts: root/metanode
 * names; scan threads use per-thread chunks, see scan_strchunks) */
static GStringChunk *name_strchunk = NULL;

/* Node ID counter. Plain increments outside the scan; atomic
 * fetch-add from the parallel scan tasks (IDs are unique but not
 * DFS-ordered — nothing depends on the order: node_table is indexed
 * by ID and children are sorted post-scan) */
static gint node_id;


/* --- Parallel scan machinery ------------------------------------ */

/* Upper bound on scan threads (storage stops scaling before CPUs do) */
#define SCANFS_MAX_SCAN_THREADS 8

/* A unit of scan work: one directory */
typedef struct {
	char *path;	/* owned */
	GNode *dnode;
} DirTask;

static GThreadPool *scan_pool = NULL;
/* Tasks queued or running; incremented before every push */
static gint scan_pending = 0;
static GMutex scan_done_mutex;
static GCond scan_done_cond;

/* GStringChunk is not thread-safe: each scan thread lazily creates
 * its own chunk. All chunks are kept in scan_strchunks (names must
 * outlive the scan) and freed at the start of the next scan. The
 * pool is exclusive, so every scan gets fresh threads and thus
 * fresh per-thread chunk pointers. */
static GPrivate scan_chunk_key = G_PRIVATE_INIT( NULL );
static GSList *scan_strchunks = NULL;
static GMutex scan_strchunks_mutex;


static GStringChunk *
scan_thread_strchunk( void )
{
	GStringChunk *chunk = g_private_get( &scan_chunk_key );

	if (chunk == NULL) {
		chunk = g_string_chunk_new( 8192 );
		g_private_set( &scan_chunk_key, chunk );
		g_mutex_lock( &scan_strchunks_mutex );
		scan_strchunks = g_slist_prepend( scan_strchunks, chunk );
		g_mutex_unlock( &scan_strchunks_mutex );
	}

	return chunk;
}

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


/* Fills a node descriptor from stat results */
static void
node_desc_fill_from_stat( NodeDesc *ndesc, const struct stat *st )
{
	/* Determine node type */
	if (S_ISDIR(st->st_mode))
		ndesc->type = NODE_DIRECTORY;
	else if (S_ISREG(st->st_mode))
		ndesc->type = NODE_REGFILE;
	else if (S_ISLNK(st->st_mode))
		ndesc->type = NODE_SYMLINK;
	else if (S_ISFIFO(st->st_mode))
		ndesc->type = NODE_FIFO;
	else if (S_ISSOCK(st->st_mode))
		ndesc->type = NODE_SOCKET;
	else if (S_ISCHR(st->st_mode))
		ndesc->type = NODE_CHARDEV;
	else if (S_ISBLK(st->st_mode))
		ndesc->type = NODE_BLOCKDEV;
	else
		ndesc->type = NODE_UNKNOWN;

	/* A corrupted DOS filesystem once gave me st_size = -4GB */
	g_assert( st->st_size >= 0 );

	ndesc->size = st->st_size;
	ndesc->size_alloc = 512 * st->st_blocks;
	ndesc->user_id = st->st_uid;
	ndesc->group_id = st->st_gid;
	ndesc->perms = st->st_mode & 0777;
	ndesc->atime = st->st_atime;
	ndesc->mtime = st->st_mtime;
	ndesc->ctime = st->st_ctime;
}


/* Official stat function. Returns 0 on success, -1 on error.
 * The path argument is the absolute path to stat. */
static int
stat_node( GNode *node, const char *path )
{
	struct stat st;

	if (lstat( path, &st ))
		return -1;

	node_desc_fill_from_stat( NODE_DESC(node), &st );
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


/* Number of scanned entries to accumulate locally before flushing
 * progress stats to the mutex-protected globals */
#define SCAN_STATS_BATCH 256


/* Flushes locally batched progress stats to the shared counters
 * read by the main thread's scan_monitor */
static void
flush_scan_stats( int *counts, int64 *sizes, int n )
{
	int i;

	if (n == 0)
		return;

	g_mutex_lock( &scan_stats_mutex );
	stat_count += n;
	for (i = 0; i < NUM_NODE_TYPES; i++) {
		node_counts[i] += counts[i];
		size_counts[i] += sizes[i];
		counts[i] = 0;
		sizes[i] = 0;
	}
	g_mutex_unlock( &scan_stats_mutex );
}


/* Recursive directory scanner.
 *
 * Runs on the scan worker thread (see scan_worker_thread). MUST NOT
 * touch GTK widgets, globals.fsv_mode, or any other main-thread-owned
 * state — only the tree being built, the name_strchunk, and the
 * progress statics under scan_stats_mutex.
 *
 * Entries are stat'ed relative to the open directory fd (fstatat),
 * so the kernel resolves a single path component per entry instead
 * of re-walking the full path. Sibling order is irrelevant here:
 * setup_fstree_recursive sorts every directory's children after the
 * scan. */
static int
process_dir( const char *dir, GNode *dnode )
{
	union AnyNodeDesc *andesc;
	DIR *dirp;
	struct dirent *de;
	struct stat st;
	GNode *node;
	int dfd;
	int dir_len;
	int batch_n = 0;
	int batch_counts[NUM_NODE_TYPES] = { 0 };
	int64 batch_sizes[NUM_NODE_TYPES] = { 0 };
	char pathbuf[PATH_MAX];

	dirp = opendir( dir );
	if (dirp == NULL)
		return -1;
	dfd = dirfd( dirp );

	/* Publish current directory for the main-thread scan_monitor
	 * timeout to display. */
	g_mutex_lock( &scan_stats_mutex );
	g_strlcpy( scan_current_dir, dir, sizeof(scan_current_dir) );
	g_mutex_unlock( &scan_stats_mutex );

	/* Prepare path buffer: "dir/" prefix, entry name appended when
	 * recursing into a subdirectory */
	dir_len = strlen( dir );
	memcpy( pathbuf, dir, dir_len );
	if (dir_len > 0 && pathbuf[dir_len - 1] != '/')
		pathbuf[dir_len++] = '/';

	/* Process directory entries */
	while ((de = readdir( dirp )) != NULL) {
		if (!de_select( de ))
			continue;

		if (fstatat( dfd, de->d_name, &st, AT_SYMLINK_NOFOLLOW ) != 0)
			continue;

		/* Allocate the right-sized descriptor up front */
		if (S_ISDIR(st.st_mode))
			andesc = (union AnyNodeDesc *)g_new0( DirNodeDesc, 1 );
		else if (S_ISLNK(st.st_mode)) {
			/* Symlinks get an extended descriptor so the
			 * post-scan resolve_symlinks pass can record the
			 * target's display size. target_size defaults to
			 * 0 (unresolved). */
			andesc = (union AnyNodeDesc *)g_new0( SymlinkNodeDesc, 1 );
		}
		else
			andesc = (union AnyNodeDesc *)g_new0( NodeDesc, 1 );

		node_desc_fill_from_stat( &andesc->node_desc, &st );
		andesc->node_desc.id = (unsigned int)g_atomic_int_add( &node_id, 1 );
		andesc->node_desc.name = g_string_chunk_insert( scan_thread_strchunk( ), de->d_name );

		node = g_node_prepend_data( dnode, andesc );

		if (NODE_IS_DIR(node)) {
			/* Create corresponding directory tree entry */
			dirtree_entry_new( node );

			/* Build the full child path */
			strncpy( &pathbuf[dir_len], de->d_name, PATH_MAX - dir_len - 1 );
			pathbuf[PATH_MAX - 1] = '\0';

			if (scan_pool != NULL) {
				/* Fan out: hand the subdirectory to the
				 * pool. The child node is fully constructed
				 * before the push; only its task will ever
				 * touch its subtree */
				DirTask *task = g_new( DirTask, 1 );
				task->path = g_strdup( pathbuf );
				task->dnode = node;
				g_atomic_int_inc( &scan_pending );
				g_thread_pool_push( scan_pool, task, NULL );
			}
			else {
				/* Serial fallback (pool creation failed) */
				process_dir( pathbuf, node );
			}

			pathbuf[dir_len] = '\0';
		}

		/* Batched progress for the main-thread monitor */
		++batch_counts[NODE_DESC(node)->type];
		batch_sizes[NODE_DESC(node)->type] += NODE_DESC(node)->size;
		if (++batch_n >= SCAN_STATS_BATCH) {
			flush_scan_stats( batch_counts, batch_sizes, batch_n );
			batch_n = 0;
		}
	}

	closedir( dirp );
	flush_scan_stats( batch_counts, batch_sizes, batch_n );

	return 0;
}


/* Thread-pool wrapper around process_dir. The last task to finish
 * wakes the scan worker waiting in scan_disk_phase */
static void
scan_dir_task( gpointer data, G_GNUC_UNUSED gpointer user_data )
{
	DirTask *task = data;

	process_dir( task->path, task->dnode );
	g_free( task->path );
	g_free( task );

	if (g_atomic_int_dec_and_test( &scan_pending )) {
		g_mutex_lock( &scan_done_mutex );
		g_cond_broadcast( &scan_done_cond );
		g_mutex_unlock( &scan_done_mutex );
	}
}


/* Runs the disk phase: parallel fan-out over a thread pool, falling
 * back to the serial recursive scan if the pool can't be created */
static void
scan_disk_phase( const char *root_dir )
{
	GError *err = NULL;
	int nthreads;
	DirTask *task;

	nthreads = CLAMP( (int)g_get_num_processors( ), 2, SCANFS_MAX_SCAN_THREADS );
	scan_pool = g_thread_pool_new( scan_dir_task, NULL, nthreads, TRUE, &err );
	if (err != NULL) {
		g_warning( "scanfs: thread pool unavailable (%s), scanning serially", err->message );
		g_clear_error( &err );
		scan_pool = NULL;
		process_dir( root_dir, root_dnode );
		return;
	}

	task = g_new( DirTask, 1 );
	task->path = g_strdup( root_dir );
	task->dnode = root_dnode;
	g_atomic_int_set( &scan_pending, 1 );
	g_thread_pool_push( scan_pool, task, NULL );

	/* Wait for the whole traversal to drain. The predicate is
	 * checked under the mutex, and finishing tasks take the mutex
	 * to broadcast, so the wakeup cannot be lost */
	g_mutex_lock( &scan_done_mutex );
	while (g_atomic_int_get( &scan_pending ) > 0)
		g_cond_wait( &scan_done_cond, &scan_done_mutex );
	g_mutex_unlock( &scan_done_mutex );

	g_thread_pool_free( scan_pool, FALSE, TRUE );
	scan_pool = NULL;
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
	gint64 scan_t0;

	/* Disk thrashing phase */
	scan_t0 = g_get_monotonic_time( );
	scan_disk_phase( ctx->root_dir );
	g_message( "scanfs: disk phase %.2f s (%u nodes)",
	           (g_get_monotonic_time( ) - scan_t0) / 1.0e6,
	           (unsigned int)g_atomic_int_get( &node_id ) );

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

	if (scan_monitor_source_id != 0) {
		g_source_remove( scan_monitor_source_id );
		scan_monitor_source_id = 0;
	}
	window_statusbar( SB_RIGHT, "" );

	dirtree_no_more_entries( );

	/* Hand node table ownership off to the viewport */
	viewport_pass_node_table( ctx->node_table, ctx->node_count );
	ctx->node_table = NULL;

	/* Fire the user's continuation (e.g. fsv_load_after_scan) */
	if (ctx->done_cb != NULL)
		ctx->done_cb( ctx->user_data );

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
		/* Initialize subtree quantities */
		DIR_NODE_DESC(node)->subtree.size = 0;
		for (i = 0; i < NUM_NODE_TYPES; i++)
			DIR_NODE_DESC(node)->subtree.counts[i] = 0;

		/* Recurse down */
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

	/* New tree → fresh colour assignment needed. Without this, the
	 * dirty flag (see color.c) would be stale FALSE from the
	 * previous scan, and color_assign_recursive on the worker would
	 * skip the work, leaving every node->color NULL. */
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

	/* ...and string chunks to hold name strings (both the main
	 * thread's chunk and the previous scan's per-thread chunks) */
	if (name_strchunk != NULL)
		g_string_chunk_free( name_strchunk );
	name_strchunk = g_string_chunk_new( 8192 );
	g_mutex_lock( &scan_strchunks_mutex );
	g_slist_free_full( scan_strchunks, (GDestroyNotify)g_string_chunk_free );
	scan_strchunks = NULL;
	g_mutex_unlock( &scan_strchunks_mutex );

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
