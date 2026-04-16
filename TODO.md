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
  [x] Replace GtkFileChooserDialog with GtkFileDialog +
      gtk_file_dialog_open (async)
  [x] Update gui.c (gui_filesel_window) and dialog.c callers
  [x] Verify: builds with no GtkFileChooser deprecation warnings,
      Change Root dialog works correctly

Step 7.4 - File list (GtkTreeView → GtkColumnView)
  [x] Replace gui_clist_add with GtkColumnView + GtkColumnViewColumn
      + GtkSignalListItemFactory backed by a GListStore
  [x] Replace GtkListStore row operations (append, set, clear) with
      GListStore equivalents in filelist.c
  [x] Replace GtkTreeSelection usage with GtkSingleSelection
  [x] Replace GtkCellRenderer (pixbuf + text) with factory-created
      widgets (GtkImage + GtkLabel in a GtkBox)
  [x] Update gui_clist_moveto_row for the new model
  [x] Verify: builds with no GtkListStore/GtkCellRenderer deprecation
      warnings in filelist.c, file list displays and selects correctly

  Checkpoint: User tests file list — columns display correctly,
  clicking selects nodes, double-click navigates, right-click context
  menu works.

Step 7.5 - Directory tree (GtkTreeView → GtkListView + GtkTreeListModel)
  [x] Replace gui_ctree_add with GtkListView + GtkTreeListModel +
      GtkSignalListItemFactory backed by a GListStore-per-level
  [x] Replace GtkTreeStore operations (append, set, clear) with
      GListStore equivalents in dirtree.c
  [x] Replace GtkTreeSelection with GtkSingleSelection
  [x] Implement expand/collapse via GtkTreeListRow
  [x] Replace gui_ctree_node_add with GListStore item insertion
  [x] Update dirtree_entry_expand, dirtree_entry_collapse,
      dirtree_entry_expand_recursive, dirtree_select for new model
  [x] Verify: builds with no GtkTreeView/GtkTreeStore deprecation
      warnings in dirtree.c, directory tree displays, expands,
      collapses, and selects correctly

  Checkpoint: User tests directory tree — expand/collapse works,
  clicking selects and navigates, tree stays in sync with 3D view.

Step 7.6 - Dialog lists (remaining GtkTreeView usage)
  [x] Replace any remaining GtkTreeView/GtkListStore usage in
      dialog.c (color setup wildcard pattern list, node properties)
  [x] Verify: builds with no remaining deprecation warnings,
      all dialogs work correctly

Step 7.7 - Final cleanup
  [x] Remove any remaining GdkPixbuf usage where GdkTexture/
      GdkPaintable is the GTK 4 replacement
  [x] Remove G_GNUC_BEGIN_IGNORE_DEPRECATIONS guards
  [x] Verify: ninja -C builddir produces zero deprecation warnings
  [x] Verify: program runs, all features work

  Checkpoint: User tests everything. Build is warning-free.

Step 8 - Bug fixes and Minor Enhancements
  [x] Complete bug fixes and minor enhancements.


PHASE 9: UI MODERNISATION
==========================

Modernise the menu bar, toolbar, and preferences dialog. Simplify the
menu to a single File menu, move visualization and color mode selection
to the toolbar, add a comprehensive Preferences window, and rework the
wildcard color editor.

Step 9.1 — Simplify the menu bar
  [x] Replace the current multi-menu bar (File, Visualisation, Colors)
      with a single "File" menu containing:
        - Open Root...
        - Preferences...
        - About
        - (separator)
        - Exit
  [x] Remove the Visualisation and Colors menus and their GAction
      handlers (vis-mode, color-mode radio actions in window.c)
  [x] Verify: builds, runs, File menu works, old menus are gone

Step 9.2 — Toolbar layout: two-row button bar
  [x] Restructure the toolbar area into two rows:
      - Row 1 (navigation): [Root] [Back] [Up] [Bird's Eye]
      - Row 2 (modes): [MapV] [TreeV] [DiscV]  ···  [W*] [N] [D/T]
        ···  [Log/Rep]
  [x] Visualization mode buttons are radio-style (GtkToggleButton
      group, exactly one active at a time). Larger than nav buttons.
  [x] Color mode buttons are radio-style (same pattern, one active).
  [x] Log/Rep scale toggle: GtkToggleButton, greyed out when vis mode
      is not TreeV
  [x] All buttons have tooltips
  [x] Verify: builds, runs, both rows display correctly, buttons
      reflect current state

Step 9.3 — Generate placeholder SVG icons
  [x] Create SVG icons for each new button:
      - MapV: bold letter "M"
      - TreeV: bold letter "T"
      - DiscV: bold letter "D"
      - Wildcard color mode: "W*"
      - Node type color mode: "N"
      - Date/Time color mode: "D/T"
      - Log scale: "LOG" (or similar)
      - Representative scale: "REP" (or similar)
  [x] Add SVGs to src/icons/ and the GResource manifest
  [x] Verify: icons display correctly in the toolbar

Step 9.4 — Wire up toolbar mode buttons
  [x] Visualization buttons switch the active visualization mode
      (call the same code path as the old menu radio actions)
  [x] Color mode buttons switch the active color mode
  [x] Log/Rep toggle switches TreeV between logarithmic and
      representative (linear) height scaling. Grey out when not TreeV.
  [x] Switching vis mode to/from TreeV enables/disables the Log/Rep
      button
  [x] Verify: switching vis modes, color modes, and scale mode all work
      from the toolbar. 3D view updates correctly.

Step 9.5 — Add representative scale mode to TreeV
  [x] Add a global or per-mode flag for TreeV scale mode (logarithmic
      vs representative). Store in globals or a new settings struct.
  [x] In geometry.c TreeV leaf height calculation, branch on the flag:
      - Logarithmic: current log² × multiplier formula
      - Representative: original sqrt(size) × multiplier formula
  [x] When the flag changes at runtime, invalidate TreeV geometry
      (mark VBO batches dirty) and trigger a redraw
  [x] Verify: toggling scale mode at runtime visually changes bar
      heights. Collapse/expand still works.

  Checkpoint: User tests toolbar — vis mode switching, color mode
  switching, scale toggle, navigation buttons, Bird's Eye toggle.
  All modes render correctly.

Step 9.6 — Preferences window: General tab
  [x] Create a new Preferences dialog (reuse or replace the existing
      color setup dialog in dialog.c)
  [x] Use a GtkNotebook with tabs: "General" and "Colors"
  [x] General tab contains:
      - "Remember settings from previous session" checkbox
      - Default Visualization Mode dropdown (greyed if remember is on)
      - Default Color Mode dropdown (greyed if remember is on)
      - Default TreeV Scale Mode dropdown (greyed if remember is on)
  [x] Verify: builds, Preferences opens from File menu, General tab
      displays correctly

Step 9.7 — Preferences window: Colors tab (Node Type and Date/Time)
  [x] Colors tab uses a sub-notebook or expandable sections for each
      color mode: "By Wildcard", "By Node Type", "By Date/Time"
  [x] "By Node Type" page: keep existing layout (icon + label + color
      picker per node type)
  [x] "By Date/Time" page: keep existing layout (spectrum type,
      timestamp type, time range, gradient colours, spectrum preview)
  [x] Verify: builds, Node Type and Date/Time color pages work as
      before

Step 9.8 — Preferences window: Wildcard color editor rework
  [x] Replace the current wildcard list UI with a new design:
      - Each wildcard group is a row showing:
        - Name (editable text field, e.g. "Source Code")
        - Color picker (GtkColorDialogButton)
        - Patterns field (editable text field, e.g. "*.c *.h *.cpp")
      - "Add Group" button to add a new wildcard group
      - "Remove" button (or per-row delete button) to remove a group
      - Default color picker at the bottom for non-matching files
  [x] Pattern field accepts spaces, commas, or semicolons as
      delimiters between patterns. Normalize to semicolons when saving.
  [x] Add a "name" field to WPatternGroup (currently has only color
      and pattern list)
  [x] Update color_read_config / color_write_config to persist group
      names (add a "name" key to each [Wildcard:N] section)
  [x] Verify: builds, wildcard editor displays, adding/removing/editing
      groups works, patterns are saved and loaded correctly

Step 9.9 — Settings persistence
  [x] Add new settings to the config file (fsvrc):
      - [Settings] remember_session = true/false
      - [Settings] default_vis_mode = mapv/treev/discv
      - [Settings] default_color_mode = wildcard/nodetype/time
      - [Settings] default_scale_mode = logarithmic/representative
      - [Settings] last_vis_mode, last_color_mode, last_scale_mode
        (saved on exit when remember_session is true)
  [x] Settings are saved when the Preferences window is closed:
      - If OK/Cancel buttons: save on OK, discard on Cancel
      - If the user closes the window via the X button, warn and
        offer Save/Discard/Cancel
  [x] On startup, read settings and apply:
      - If remember_session: use last_* values
      - Otherwise: use default_* values
  [x] Verify: settings persist across sessions. Changing defaults
      and restarting uses them. "Remember session" overrides defaults.

Step 9.10 — Cleanup and polish
  [x] Remove dead code from the old menu system (unused GAction
      handlers, old color setup dialog if fully replaced)
      — Audit complete: no dead code found. All callbacks, actions,
        and dialog functions are actively referenced.
  [x] Ensure all new UI elements follow GTK4 conventions (use system
      widgets where possible: GtkColorDialogButton, GtkDropDown, etc.)
      — All menus use GMenu/GSimpleAction, dialogs use GtkColorDialog,
        GtkDropDown, GtkPopoverMenu. No deprecated GTK3 patterns remain.
  [x] Verify: clean build, no warnings, all features work

  Checkpoint: User tests the full UI modernisation:
  - File menu works (Open Root, Preferences, About, Exit)
  - Toolbar: vis mode, color mode, scale toggle all work
  - Preferences: General settings, all three color modes
  - Wildcard editor: add/remove/edit groups with names and patterns
  - Settings persist correctly across sessions
  - All three vis modes render and interact correctly


PHASE 10: EXECUTABLE FILE VISUALISATION
========================================

Detect files with any executable permission bit set and surface that
in the wildcard colour mode. An "Executable Color" and an
"Executable overrides type" toggle are added to Preferences. On top
of that, each visualisation mode shows executable files with a
two-tone effect so both the type and exec-ness are visible at a
glance.

Design summary:
- scanfs stores the file mode so NodeDesc can report executable-ness.
- ColorConfig gains two fields: executable_color (RGB) and
  executable_overrides (bool). Both live in the [Wildcard] section
  of fsvrc.
- wpattern_color() returns the exec colour when override is on and
  the file is executable; otherwise it returns the normal pattern
  match.
- The geometry builders apply a two-tone effect to executable files:
  top face uses executable_color, side faces use wpattern_color().
  When override is on these are the same colour (looks solid), when
  off the top face highlights the exec bit (looks two-tone).
- Status bar shows both the type label and "(executable)" when both
  apply, regardless of colour mode.

Step 10.1 — Capture executable bit in the scanner
  [x] scanfs.c: uncomment the perms assignment. Store st.st_mode &
      0777 to fit the existing 10-bit perms field in NodeDesc
      (setuid/setgid/sticky bits are not needed for this feature).
  [x] common.c/h: add `node_is_executable(GNode *)` returning TRUE
      iff the node is a regular file with any of the X bits set
      (0111). Directories are excluded.
  [x] Verify: builds cleanly, runs, no visible change.

Step 10.2 — ColorConfig: executable colour and override flag
  [x] color.h: added executable_color and executable_overrides to
      struct ColorByWPattern.
  [x] color.c: defaults are "#00FF00" (matches the current
      Executables and Scripts group in fsvrc.sample) and TRUE.
  [x] color.c color_read_config: reads "exec_color" and
      "exec_overrides" keys from [Wildcard] with fallback defaults.
      Uses g_key_file_has_key for the boolean so a missing key
      gets the default instead of FALSE.
  [x] color.c color_write_config: writes both keys.
  [x] Verify: builds cleanly, existing configs still load.

Step 10.3 — Apply exec colour in wpattern_color()
  [x] color.c wpattern_color(): after the directory-override branch,
      if executable_overrides is on AND node_is_executable(node),
      return &executable_color immediately, bypassing pattern match.
  [x] Verify: with override on (default), executable files show
      the exec colour in wildcard mode regardless of filename.

Step 10.4 — Preferences UI: exec colour and override toggle
  [x] dialog.c Preferences > Colors > By Wildcard page: new row
      below the Default Color row holds the "Executable overrides
      type" checkbox (left) and "Executable color:" picker (right).
  [x] Checkbox has the full tooltip explaining both states.
  [x] Save path is automatic: the existing csdialog_ok_button_cb
      passes csdialog.color_config to color_set_config(), which
      triggers color_assign_recursive() → geometry_queue_rebuild(),
      so the new fields flow through without extra plumbing.
  [x] Verify: builds cleanly. User to test interactively.

Step 10.5 — Status bar shows both type and executable
  [-] DEFERRED: the status bar currently shows only the file name
      and has no type information at all. A proper overhaul of the
      status bar (adding type, size, etc.) is planned as a separate
      future task, at which point the executable marker will be
      included naturally. Skipping for now.

  Checkpoint (mid-phase): User confirms the basic behaviour —
  override toggle works, exec colour is configurable, all three
  modes still render correctly. The two-tone effect is not yet
  implemented; executables show as flat exec colour (override on)
  or flat type colour (override off).

Step 10.6 — Executable visualisation (ls-style override)
  [x] Final semantics (after two iterations):
       - Unmatched file + executable → solid exec colour
         (typical /usr/bin case: "other"-type files that are
         actual Unix binaries — almost always what you want)
       - Matched file + executable + override flag on →
         solid exec colour (ls-style: exec trumps type)
       - Matched file + executable + override flag off →
         plain match colour (type trumps exec)
       - Non-executable → unchanged
  [x] Flag renamed to override_typed_exec, config key
      "override_typed_exec", default ON.
  [x] color.h: removed the brightness cache fields from the
      previous iteration (bright_color on WPatternGroup,
      bright_default_color on ColorByWPattern).
  [x] color.c: removed brighten_color() and regen_bright_colors()
      helpers. wpattern_color() now short-circuits to the exec
      colour at the top when override is on, falling through to
      the normal pattern search otherwise. Unmatched executables
      still get the exec colour at the end.
  [x] dialog.c: checkbox relabelled to "Override type colour for
      executables" with a simpler tooltip.
  [x] geometry.c: MapV top-face split already reverted.
      Brightness-based two-tone is gone. All three mode builders
      untouched — the override lives entirely in the colour path.
  [x] Verify: clean build. User to test interactively.

Step 10.7 — TreeV (subsumed by 10.6)
  [x] Works automatically via the new wpattern_color() path.
      TreeV leaves already read NODE_DESC(node)->color, which now
      carries the correct brightness-adjusted or exec colour.

Step 10.8 — DiscV (subsumed by 10.6)
  [x] Same as TreeV — DiscV geometry reads NODE_DESC(node)->color
      and automatically gets the new behaviour.

Step 10.9 — Cleanup and polish
  [x] Double-check that scanfs never leaks permission bits beyond
      what NodeDesc.perms can store.
      — scanfs.c:100 stores st.st_mode & 0777 (max value 511);
        NodeDesc.perms is 10 bits (max 1023). No truncation possible.
  [x] Verify the override toggle, the exec colour picker, and the
      status bar all behave correctly when switching colour modes
      (wildcard → nodetype → time and back).
      — color_set_mode() calls color_assign_recursive() which
        re-derives every node's colour from the active mode.
        Switching away from wildcard and back picks up the latest
        override flag and exec colour without staleness.
  [x] Confirm no GL errors, no warnings, no frame-rate regression.
      — Clean rebuild (29 targets) produces zero warnings. The
        feature lives in the colour-assignment path; geometry
        builders are unchanged so per-frame cost is identical.
  [x] Verify: clean build, all features work.

  Checkpoint: User tests the full feature:
  - Executable files detected correctly (try /usr/bin)
  - Override toggle works in all three modes
  - Two-tone effect visible on executables in MapV, TreeV, DiscV
  - Status bar shows "(executable)" alongside the type label
  - Changing the exec colour updates the view immediately
  - Settings persist across restarts
  - Non-wildcard colour modes (nodetype, time) still work normally


PHASE 11: TEXT-BASED TOP TOOLBAR
=================================

Replace the icon-based two-row toolbar (currently nested inside the
left pane) with a single text-based button bar that spans the full
width of the window at the top, and remove the menu bar. Buttons are
grouped into four labelled clusters that wrap as units when the window
is narrow.

Final layout (left-aligned, then a flexible spacer, then right-aligned):

  Navigation: [Root] [Back] [Up] [Top-Down]   [Open...]
  Visualisation: [MapV]   [TreeV] [x] Log   [DiscV]
  Color Mode: [Wildcard] [Node Type] [Timestamp]
                                    [⚙ Preferences] [? About] [✕ Exit]

Conventions:
- Each cluster is a labelled GtkBox; clusters are children of a
  GtkFlowBox so they stay together when wrapped.
- Buttons use plain text labels (no icons). Utility cluster uses
  glyph + word.
- Vis and color buttons remain radio toggles (existing logic
  preserved). The Log checkbox is a sibling of the TreeV button,
  separated from MapV and DiscV by extra spacing so the visual
  association is obvious.
- Top-Down (was Bird's Eye) keeps its existing toggle behaviour and
  its accent CSS class.

Step 11.1 — Move toolbar out of the left pane
  [x] In window.c window_init(), build the toolbar as a child of
      main_vbox_w directly (above the hpaned), not inside left_vbox_w.
  [x] The existing two-row hbox setup stays in place for now (still
      icon-based). The only change is that the parent is the main
      vbox so it spans the full window width.
  [x] Remove the "left pane minimum width = 350px for the toolbar"
      constraint since the toolbar no longer lives there.
  [x] Verify: builds, runs, toolbar appears at the top of the window
      above the hpaned, all existing buttons still work.

Step 11.2 — Drop the menu bar
  [x] Remove build_menu_model(), the gtk_popover_menu_bar_new_from_model
      call, and the popover-min-height workaround block.
  [x] Keep the underlying GActions (change-root, color-setup, about,
      exit) and the setup_actions() registration — the new toolbar
      buttons will trigger the same callbacks directly.
  [x] Verify: builds, runs, no menu bar visible, window opens with
      just the toolbar at the top.

Step 11.3 — Replace icon buttons with text-labelled buttons
  [x] Replace each gui_resource_image_add(...) icon inside the existing
      toolbar buttons with a plain text label. Specifically:
        - Nav: "Root", "Back", "Up", "Top-Down"
        - Vis: "MapV", "TreeV", "DiscV"
        - Color: "Wildcard", "Node Type", "Timestamp"
        - Scale: "Log" (will become a checkbox in step 11.5)
  [x] Keep all signal handlers and radio grouping unchanged.
  [x] Keep the CSS accent class on the Top-Down toggle.
  [x] Update tooltips to match the new labels:
        - Top-Down: "Toggle top-down camera"
        - Wildcard: "Color by wildcard pattern"
        - Node Type: "Color by node type"
        - Timestamp: "Sorted by modification time"
        - Log: "Logarithmic vs representative TreeV scale"
  [x] Verify: builds, runs, all buttons display text, all functions
      still work.

Step 11.4 — Add cluster labels and an "Open..." button
  [x] Wrap each cluster in a horizontal box that begins with a
      GtkLabel ("Navigation:", "Visualisation:", "Color Mode:").
      The utility cluster has no label.
  [x] Add a fifth nav button at the end of the navigation cluster:
      "Open..." with extra left margin so it visually separates from
      the in-tree nav buttons. Uses a proper GtkButton "clicked"
      callback (on_open_button_clicked) that calls dialog_change_root().
  [x] Merged the two toolbar rows into one single row with clusters
      and vertical separators between them. Scale toggle moved into
      the visualisation cluster next to TreeV.
  [x] Verify: builds, runs, labels appear, Open... opens the file
      dialog.

Step 11.5 — Convert Log toggle into a checkbox next to TreeV
  [x] Replace the standalone Log GtkToggleButton with a GtkCheckButton
      placed inside the visualisation cluster between TreeV and DiscV.
  [x] Add visible spacing (margin-start 2px, margin-end 8px) on the
      checkbox so the layout reads as
        [MapV] [TreeV] [x] Log   [DiscV]
      and the checkbox clearly belongs to TreeV.
  [x] Wire the "toggled" signal to on_scale_mode_toggled (updated to
      accept GtkCheckButton). Grey-out-when-not-TreeV preserved via
      gtk_widget_set_sensitive on the stored scale_tbutton_w.
  [x] Verify: builds, runs, checkbox toggles TreeV scale mode,
      grey-out follows the active vis mode.

Step 11.6 — Add the right-aligned utility cluster
  [ ] Build a fourth cluster containing three text+glyph buttons:
        - "⚙ Preferences"  → on_color_setup_activate
        - "? About"        → on_help_about_fsv_activate
        - "✕ Exit"         → on_file_exit_activate
  [ ] Connect each button's "clicked" signal directly to the existing
      callback (no GAction indirection needed since the menu is gone,
      but the actions can stay registered for completeness).
  [ ] Verify: builds, runs, all three buttons trigger the right
      dialogs / actions.

Step 11.7 — Cluster wrapping and alignment
  [ ] Place all four clusters as children of a GtkFlowBox configured
      with:
        - orientation = GTK_ORIENTATION_HORIZONTAL
        - homogeneous = FALSE
        - selection-mode = GTK_SELECTION_NONE
        - max-children-per-line = 4
        - row-spacing / column-spacing tuned for a tight bar
      Each FlowBox child contains one cluster box; clusters never
      split across rows.
  [ ] Right-align the utility cluster: set hexpand on a spacer or
      on the utility cluster's halign so it sits flush right while
      the other clusters stay flush left.
  [ ] Verify: builds, runs, the bar appears as a single row at a
      typical window size, wraps cleanly when narrowed, clusters
      stay grouped.

Step 11.8 — Cleanup
  [ ] Remove unused icon resources from the GResource manifest:
      ICON_CD_ROOT, ICON_BACK, ICON_CD_UP, ICON_BIRDSEYE_VIEW,
      ICON_VIS_MAPV, ICON_VIS_TREEV, ICON_VIS_DISCV,
      ICON_COLOR_WILDCARD, ICON_COLOR_NODETYPE,
      ICON_COLOR_TIMESTAMP, ICON_SCALE_LOG (only the ones the new
      toolbar no longer references — keep any used elsewhere).
  [ ] Delete the corresponding SVG files under src/icons/ if nothing
      else references them.
  [ ] Remove the GAction entries that the new toolbar no longer
      uses indirectly (only if truly dead — verify nothing else
      references them).
  [ ] Remove the birdseye-toggle CSS rule if no longer needed (or
      keep, if Top-Down still uses it).
  [ ] Verify: clean build, no warnings, no missing-resource errors
      at runtime.

  Checkpoint: User tests the new toolbar:
  - Single row at a typical window size
  - Cluster labels visible
  - Wraps cluster-by-cluster when window is narrowed
  - Navigation: Root, Back, Up, Top-Down, Open... all work
  - Visualisation: MapV/TreeV/DiscV switch correctly, Log checkbox
    sits next to TreeV and toggles the scale mode
  - Color Mode: Wildcard/Node Type/Timestamp switch correctly
  - Utility cluster is right-aligned and Preferences/About/Exit work
  - Menu bar is gone


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
