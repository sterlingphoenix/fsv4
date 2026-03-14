FSV Modern OpenGL Migration Plan
=================================

**Initial version:** 4.0.00
**Versioning scheme:** 4.{phase}.{step} (e.g., Phase 2, Step 3 = 4.2.03)

> After each step, the program MUST build and run. After each phase, the
> user tests before proceeding to the next phase.


Goal: Replace the legacy OpenGL fixed-function pipeline with modern
OpenGL 3.3 core profile using batched VBOs and shaders. Then migrate
from GTK 3 to GTK 4. Each step leaves the code compilable and runnable.

The baseline is the FSV GTK3 codebase with all prior optimizations
(frustum culling, label culling, keyboard panning, etc.) already in
place.


PHASE 1: MODERN GL INFRASTRUCTURE
==================================

Add the foundation for modern GL rendering alongside the existing legacy
pipeline. No rendering changes — the program looks and behaves
identically. Legacy GL still draws everything.

New dependency: libcglm-dev (C OpenGL mathematics library).

Step 1.1 - Add cglm dependency
  [x] meson.build: add dependency('cglm') (pkg-config name: cglm)
  [x] README.md: add libcglm-dev to the dependency list
  [x] Verify: meson setup / ninja builds cleanly

Step 1.2 - Matrix utilities
  [x] Create src/glmath.c and src/glmath.h: wrapper functions around
      cglm for the operations the codebase needs:
      - Projection matrix (perspective/frustum)
      - Modelview matrix (translate, rotate, scale, push/pop stack)
      - MVP computation
      - Normal matrix extraction
  [x] The matrix stack should mirror the legacy glPushMatrix/glPopMatrix
      behavior so the conversion can be done incrementally
  [x] Verify: builds cleanly, no functional changes

Step 1.3 - Shader compilation utilities
  [x] Create src/shader.c and src/shader.h:
      - Load, compile, and link vertex + fragment shader programs
      - Error reporting (compile/link errors printed to stderr)
      - Uniform location caching
  [x] Verify: builds cleanly, no functional changes

Step 1.4 - Write shaders
  [x] Create shaders (embedded as string constants or as separate files
      loaded at startup):
      - Vertex shader: MVP transform, basic directional lighting using
        per-vertex normals, pass-through of per-vertex colour
      - Fragment shader: output lit colour
      - Pick vertex shader: MVP transform only, pass-through of
        per-vertex node ID colour
      - Pick fragment shader: output node ID colour (no lighting)
  [x] Verify: builds cleanly, no functional changes

Step 1.5 - VBO/VAO management
  [x] Create src/vbobatch.c and src/vbobatch.h:
      - Vertex format: position (vec3), normal (vec3), colour (vec3),
        node_id (uint, for pick rendering)
      - Functions to begin building a batch, append vertices, upload
        to GPU (GL_STATIC_DRAW), draw, and free
      - Dirty-flag support: rebuild only when flagged stale
  [x] Verify: builds cleanly, no functional changes

Step 1.6 - Initialization
  [x] In ogl.c realize_cb (or equivalent): compile shaders, set up the
      shader programs, verify GL 3.3 availability
  [x] Run shaders alongside legacy pipeline (shaders compiled but not
      yet used for drawing)
  [x] Verify: program starts, prints no GL errors, renders identically
      to before

  Checkpoint: User tests that the program builds, runs, and renders
  identically. The new infrastructure is present but inactive.


PHASE 2: MAPV CONVERSION
=========================

Convert Map View rendering from legacy GL display lists to batched VBOs
with shaders. MapV is converted first because it has the simplest
geometry (axis-aligned boxes). DiscV and TreeV continue to use legacy GL.

Step 2.1 - MapV geometry to vertex batches
  [x] In geometry.c, create a function that walks the MapV tree and
      assembles all visible geometry (nodes, platforms) into a vertex
      batch (position, normal, colour, node_id per vertex)
  [x] Use the existing stale flags to determine when to rebuild
  [x] The batch is rebuilt only when geometry changes (expand/collapse,
      colour mode change), not every frame
  [x] Verify: builds cleanly, vertex batch is assembled but not yet drawn

Step 2.2 - MapV solid geometry rendering
  [x] Replace MapV's MAPV_DRAW_GEOMETRY tree walk + display list calls
      with a single VBO draw call using the lit shader
  [x] Set up projection and modelview matrices using glmath utilities,
      pass as uniforms to the shader
  [x] Verify: MapV renders solid geometry correctly via modern GL.
      Colours, lighting, and depth sorting match the legacy rendering.

Step 2.3 - MapV wireframe outline rendering
  [x] Replace MapV's outline pass (second tree walk in GL_LINE mode)
      with a draw of the same VBO using glPolygonMode(GL_LINE) or a
      separate edge VBO
  [x] Verify: MapV outlines render correctly

Step 2.4 - MapV label rendering
  [x] Convert MapV label rendering to work with the modern pipeline
  [x] tmaptext.c may need updates to use shaders for textured quads
  [x] Verify: MapV labels render correctly

Step 2.5 - MapV pick rendering
  [x] Replace MapV's pick rendering with a draw of the VBO using the
      pick shader (node_id as colour output, no lighting)
  [x] Verify: hovering and clicking correctly identifies nodes in MapV

Step 2.6 - MapV animation integration
  [x] Verify that expand/collapse animations work correctly:
      - The VBO must be rebuilt when geometry changes during animation
      - The deployment scaling must be applied correctly
      - Frustum and size culling must still work
  [x] Verify: MapV expand/collapse is smooth, camera panning works,
      keyboard pan works

  Checkpoint: User tests MapV thoroughly — rendering, interaction,
  expand/collapse, zoom, orbit, keyboard pan, colour modes, node
  selection. DiscV and TreeV still work via legacy GL.


PHASE 3: DISCV CONVERSION
==========================

Convert Disc View rendering to batched VBOs with shaders. Same pattern
as Phase 2.

Step 3.1 - DiscV geometry to vertex batches
  [x] Walk the DiscV tree and assemble visible geometry into a vertex
      batch
  [x] Rebuild only when stale flags fire
  [x] Verify: builds cleanly

Step 3.2 - DiscV solid geometry rendering
  [x] Replace DiscV display list drawing with VBO draw calls
  [x] Verify: DiscV renders correctly

Step 3.3 - DiscV label rendering
  [x] Convert DiscV label rendering to the modern pipeline
  [x] Verify: DiscV labels render correctly

Step 3.4 - DiscV pick rendering
  [x] Replace DiscV pick rendering with VBO-based pick
  [x] Verify: node selection works correctly in DiscV

Step 3.5 - DiscV animation and interaction
  [x] Verify expand/collapse, zoom, pan, keyboard pan all work
  [x] Verify: DiscV is fully functional

  Checkpoint: User tests DiscV thoroughly. MapV and DiscV both use
  modern GL. TreeV still uses legacy GL.


PHASE 4: TREEV CONVERSION
==========================

Convert Tree View rendering to batched VBOs with shaders. TreeV is the
most complex mode (platforms, leaves, branches, radial layout) so it is
done last.

Step 4.1 - TreeV geometry to vertex batches
  [x] Walk the TreeV tree and assemble visible geometry (platforms,
      leaves, branches) into vertex batches
  [x] Rebuild only when stale flags fire
  [x] Verify: builds cleanly

Step 4.2 - TreeV solid geometry and branch rendering
  [x] Replace TreeV display list drawing with VBO draw calls
  [x] Branches may use a separate batch or be included in the main one
  [x] Verify: TreeV renders correctly (platforms, leaves, branches)

Step 4.3 - TreeV wireframe outline rendering
  [x] Replace TreeV outline pass with modern GL equivalent
  [x] Verify: TreeV outlines render correctly

Step 4.4 - TreeV label rendering
  [x] Convert TreeV label rendering to the modern pipeline
  [x] Verify: TreeV labels render correctly

Step 4.5 - TreeV pick rendering
  [x] Replace TreeV pick rendering with VBO-based pick
  [x] Verify: node selection works correctly in TreeV

Step 4.6 - TreeV animation and interaction
  [x] Verify expand/collapse, treev_arrange, zoom, orbit, keyboard pan
  [x] Verify: TreeV is fully functional

  Checkpoint: User tests TreeV thoroughly. All three modes now use
  modern GL.


PHASE 5: LEGACY GL REMOVAL
===========================

Remove all legacy OpenGL code now that all three modes use the modern
pipeline.

Step 5.1 - Remove legacy drawing code
  [x] Remove all glBegin/glEnd/glVertex/glNormal/glColor immediate-mode
      calls from geometry.c
  [x] Remove display list management (glNewList, glEndList, glCallList,
      glGenLists, glDeleteLists) and the a_dlist/b_dlist/c_dlist fields
  [x] Remove the per-directory display list stale flags (replaced by
      VBO batch dirty flags)
  [x] Verify: grep confirms no legacy draw calls remain (comments
      excluded)

Step 5.2 - Remove legacy matrix stack
  [x] Remove all glMatrixMode, glPushMatrix, glPopMatrix, glLoadIdentity,
      glTranslated, glRotated, glScaled calls
  [x] All matrix operations now go through the glmath module
  [x] Remove the base modelview matrix setup in ogl.c realize_cb
  [x] Verify: grep confirms no legacy matrix calls remain

Step 5.3 - Remove legacy state management
  [x] Remove glEnable/glDisable for fixed-function features that are now
      handled by shaders (GL_LIGHTING, GL_LIGHT0, GL_COLOR_MATERIAL,
      GL_NORMALIZE)
  [x] Remove glShadeModel, glLightfv, glMaterialfv, glColorMaterial
  [x] Keep GL state that is still relevant in core profile (GL_DEPTH_TEST,
      GL_CULL_FACE, GL_BLEND, GL_POLYGON_OFFSET_FILL, glPolygonMode,
      glLineWidth, glViewport, glClear)
  [x] Verify: program runs with no GL errors

Step 5.4 - Switch to core profile
  [x] In ogl.c create-context signal handler, request a GL 3.3 core
      profile context instead of compatibility
  [x] Remove gdk_gl_context_set_forward_compatible(FALSE)
  [x] Verify: program runs in core profile with no GL errors

Step 5.5 - Clean up
  [x] Remove any dead code, unused includes, unused variables left over
      from the conversion
  [x] Update comments that reference display lists or the old pipeline
  [x] Verify: clean build with no warnings

  Checkpoint: User tests all three modes, all interactions, all features.
  Confirm with: grep -rn 'glBegin\|glEnd\|glVertex\|glMatrixMode\|
  glNewList\|glCallList\|glPushMatrix\|glPopMatrix' src/
  (should return nothing outside of comments).


PHASE 6: GTK 4 MIGRATION
=========================

With all rendering on modern GL core profile, migrate from GTK 3 to
GTK 4. The rendering pipeline does not change — only the GTK widget
and API layer.

Step 6.1 - Build system and dependencies
  [x] meson.build: change gtk+-3.0 to gtk4 >= 4.0
  [x] README.md: update dependency list
  [x] Verify: configure step finds GTK 4

Step 6.2 - Widget API updates
  [x] Replace deprecated GTK 3 APIs with GTK 4 equivalents
      (this will be a detailed list once Phase 5 is complete — the
      specific changes depend on what GTK 3 APIs remain in use)
  [x] GtkGLArea API changes (if any) between GTK 3 and 4
  [x] Event handling migration (GdkEvent changes in GTK 4)
  [x] Verify: builds and runs on GTK 4

Step 6.3 - Final verification
  [x] Test all three visualization modes
  [x] Test all interactions (mouse, keyboard, menus)
  [x] Test on Wayland and X11
  [x] Verify: fully functional on GTK 4

  Checkpoint: User tests everything. The migration is complete.


PHASE 7: REMOVE DEPRECATED GTK 4 APIs
=======================================

Replace all deprecated GTK 4 widget APIs so the build produces zero
deprecation warnings. Each step replaces one category of deprecated
API, leaving the program buildable and runnable.

Step 7.1 - GtkComboBoxText → GtkDropDown
  [x] Replace gui_option_menu_add / gui_option_menu_item with
      GtkDropDown + GtkStringList
  [x] Update dialog.c callers (color setup uses option menus)
  [x] Verify: builds with no GtkComboBox deprecation warnings,
      color setup dialog works correctly

Step 7.2 - GtkColorChooser → GtkColorDialog
  [x] Replace GtkColorChooserDialog with GtkColorDialog +
      gtk_color_dialog_choose_rgba (async)
  [x] Replace GtkColorButton with GtkColorDialogButton
  [x] Update gui.c (gui_colorsel_window, gui_colorpicker_add,
      gui_colorpicker_set_color) and dialog.c callers
  [x] Verify: builds with no GtkColorChooser deprecation warnings,
      color picker and color setup dialog work correctly

Step 7.3 - GtkFileChooserDialog → GtkFileDialog
  [ ] Replace GtkFileChooserDialog with GtkFileDialog +
      gtk_file_dialog_open (async)
  [ ] Update gui.c (gui_filesel_window) and dialog.c callers
  [ ] Verify: builds with no GtkFileChooser deprecation warnings,
      Change Root dialog works correctly

Step 7.4 - File list (GtkTreeView → GtkColumnView)
  [ ] Replace gui_clist_add with GtkColumnView + GtkColumnViewColumn
      + GtkSignalListItemFactory backed by a GListStore
  [ ] Replace GtkListStore row operations (append, set, clear) with
      GListStore equivalents in filelist.c
  [ ] Replace GtkTreeSelection usage with GtkSingleSelection
  [ ] Replace GtkCellRenderer (pixbuf + text) with factory-created
      widgets (GtkImage + GtkLabel in a GtkBox)
  [ ] Update gui_clist_moveto_row for the new model
  [ ] Verify: builds with no GtkListStore/GtkCellRenderer deprecation
      warnings in filelist.c, file list displays and selects correctly

  Checkpoint: User tests file list — columns display correctly,
  clicking selects nodes, double-click navigates, right-click context
  menu works.

Step 7.5 - Directory tree (GtkTreeView → GtkListView + GtkTreeListModel)
  [ ] Replace gui_ctree_add with GtkListView + GtkTreeListModel +
      GtkSignalListItemFactory backed by a GListStore-per-level
  [ ] Replace GtkTreeStore operations (append, set, clear) with
      GListStore equivalents in dirtree.c
  [ ] Replace GtkTreeSelection with GtkSingleSelection
  [ ] Implement expand/collapse via GtkTreeListRow
  [ ] Replace gui_ctree_node_add with GListStore item insertion
  [ ] Update dirtree_entry_expand, dirtree_entry_collapse,
      dirtree_entry_expand_recursive, dirtree_select for new model
  [ ] Verify: builds with no GtkTreeView/GtkTreeStore deprecation
      warnings in dirtree.c, directory tree displays, expands,
      collapses, and selects correctly

  Checkpoint: User tests directory tree — expand/collapse works,
  clicking selects and navigates, tree stays in sync with 3D view.

Step 7.6 - Dialog lists (remaining GtkTreeView usage)
  [ ] Replace any remaining GtkTreeView/GtkListStore usage in
      dialog.c (color setup wildcard pattern list, node properties)
  [ ] Verify: builds with no remaining deprecation warnings,
      all dialogs work correctly

Step 7.7 - Final cleanup
  [ ] Remove any remaining GdkPixbuf usage where GdkTexture/
      GdkPaintable is the GTK 4 replacement
  [ ] Remove G_GNUC_BEGIN_IGNORE_DEPRECATIONS guards
  [ ] Verify: ninja -C builddir produces zero deprecation warnings
  [ ] Verify: program runs, all features work

  Checkpoint: User tests everything. Build is warning-free.


NOTES
=====
- Each step should leave the code compilable and runnable
- User tests after each phase completion
- The cglm library is header-only capable but also available as a
  shared library via pkg-config (libcglm-dev on Debian/Ubuntu)
- The About screen (about.c) has minimal GL usage and can be converted
  as part of whichever phase is convenient
- The splash screen (fsv.c) has one glBegin/glEnd block — convert
  alongside the first mode or during cleanup
- lib/getopt.c, lib/getopt1.c, lib/scandir.c, lib/fileblocks.c are
  POSIX polyfills and need no changes
