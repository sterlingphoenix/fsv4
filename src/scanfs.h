/* scanfs.h */

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


#ifdef FSV_SCANFS_H
	#error
#endif
#define FSV_SCANFS_H


/* Callback invoked on the main thread when scanfs() has finished.
 * user_data is the same pointer passed to scanfs(). */
typedef void (*ScanDoneCallback)( gpointer user_data );

/* Begins a filesystem scan of `dir`. Returns immediately — the scan
 * runs on a worker thread. When the scan is complete, `done_cb` is
 * invoked on the main thread (may be NULL).
 *
 * initial_mode selects which visualization the worker should pre-lay
 * out + color immediately after the scan, so the main thread doesn't
 * have to do that work synchronously after scan completion. */
void scanfs( const char *dir, FsvMode initial_mode,
             ScanDoneCallback done_cb, gpointer user_data );


/* end scanfs.h */
