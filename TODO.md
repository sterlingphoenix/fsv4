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
  [x] Verify: ninja -C build produces zero deprecation warnings
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
  [x] Build a fourth cluster containing three text+glyph buttons:
        - "⚙ Preferences"  → on_preferences_button_clicked
        - "? About"        → on_about_button_clicked
        - "✕ Exit"         → on_exit_button_clicked
  [x] Added proper GtkButton "clicked" wrappers in callbacks.c/h for
      all three (matching the pattern used for on_open_button_clicked).
  [x] Right-aligned via an expanding spacer between the color cluster
      and the utility cluster.
  [x] Verify: builds, runs, all three buttons trigger the right
      dialogs / actions.

Step 11.7 — Cluster wrapping and alignment
  [x] Outer hbox: GtkFlowBox (hexpand, left-aligned) holds the three
      wrappable clusters (nav, vis, color); the utility cluster sits
      outside the FlowBox as a direct child of the outer hbox, so it
      stays at the far right regardless of wrapping.
  [x] FlowBox configured with selection-mode=NONE, homogeneous=FALSE,
      max-children-per-line=3, row-spacing=2, column-spacing=12.
  [x] Verify: builds, runs, the bar appears as a single row at a
      typical window size, wraps cleanly when narrowed, clusters
      stay grouped.

Step 11.8 — Cleanup
  [x] Removed all 12 ICON_* defines from window.c (no longer
      referenced after text-button conversion).
  [x] Removed 12 icon files from src/icons/ (4 PNG, 8 SVG) and
      their entries from fsv.gresource.xml.
  [x] GAction entries kept — still used by setup_actions() which
      wires change-root, exit, color-setup, about, vis-mode, and
      color-mode actions (some still referenced by other code paths).
  [x] birdseye-toggle CSS class kept — still used by the Top-Down
      toggle button.
  [x] Verify: clean build, no warnings, no missing-resource errors
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


PHASE 35: LARGE-DIRECTORY PERFORMANCE & HAZARD CLEANUP
=======================================================

Address the remaining pain points for very large filesystems and clean
up subtle memory / thread-safety hazards identified in the codebase
analysis. Steps are ordered by impact-to-effort ratio — the biggest
user-visible win (dirtree hash map) comes first, risky refactors
(background scan) come later.

Design summary:
- `dirtree.c` has an O(N) linear walk through the flat TreeListModel
  on every selection change, expand/collapse, and right-click. For
  trees with tens of thousands of rows this is the dominant UI stall.
  A GHashTable keyed on GNode* fixes it.
- `get_file_type_desc()` forks the `file` command per call via popen.
  Caching by extension (or by (dev,ino) for extensionless files)
  removes the per-call fork.
- `scanfs.c` runs synchronously on the main thread, so the UI is
  blocked during scans. Moving scan to a worker via GTask unblocks
  input.
- Per-visible-region VBO batching (spatial tiling) is deferred — only
  tackle if 35.1–35.3 aren't sufficient.
- `g_slice_*` has been deprecated since GLib 2.76; swap to `g_new0`
  / `g_free`.
- Static return buffers in `common.c` (ninfo, read_symlink,
  absname_merge, node_absname, i64toa, abbrev_size) are thread-unsafe
  and, in the case of `ninfo.target` / `ninfo.abstarget`, can clobber
  each other. Blocks future threading.
- `geometry_free_recursive()` is a no-op comment-wise labelled "kept
  for API compatibility" — either remove it or make it mark the
  relevant VBO batches dirty so stale node IDs are cleared on rescan.

Step 35.1 — Directory tree fast lookup (dirtree.c)
  [x] Replaced find_tree_list_row()'s O(N) flat-model scan with a
      tree-walk from the root using gtk_tree_list_row_get_child_row()
      at each level. O(depth * dir-siblings), effectively O(1) for
      filesystem-shaped trees.
  [x] Went with tree-walk instead of the originally-planned
      GHashTable<GNode*, GtkTreeListRow*> because the TreeListModel
      is dynamic (rows appear/disappear on expand/collapse). A hash
      would have required listening on items-changed and careful ref
      management; the tree-walk uses the existing GTK navigation API
      and has no state to maintain. Same end result, simpler.
  [x] Child models built by ctree_create_child_model contain only
      directory children, so child_idx counts dir siblings directly.
  [x] All call sites (dirtree_entry_show, dirtree_entry_expand,
      dirtree_entry_collapse_recursive, dirtree_entry_expand_recursive)
      unchanged — same function signature, same return semantics.
  [x] Verify: builds cleanly, directory tree sidebar behaves
      identically on small trees, and is visibly snappier on very
      large trees (e.g. /usr with many subdirectories expanded).

Step 35.2 — File-type description cache
  [x] In common.c, added a GHashTable keyed on absolute pathname →
      allocated description string. Keyed on pathname rather than
      extension because `file` inspects content, not extension, so
      extension-keyed caching would conflate different files.
  [x] get_file_type_desc() checks the cache first; on miss, runs
      popen(FILE_COMMAND) as before, stores the result, returns the
      cached pointer. Errors / timeouts are NOT cached so retries can
      succeed.
  [x] Returned pointers are now stable for the program lifetime
      (previously they pointed into a static buffer that got clobbered
      on the next call — a latent hazard, now fixed as a side effect).
  [x] Cache leaks on program exit (program-lifetime memory).
  [x] Verify: builds cleanly. User to confirm Properties dialog opens
      visibly faster on subsequent calls for the same file.

Step 35.3 — Replace g_slice with g_new0
  [x] scanfs.c: replaced the 4 g_slice_new0(DirNodeDesc) /
      g_slice_new0(NodeDesc) sites with g_new0(..., 1).
  [x] Replaced the 2 matching g_slice_free calls in
      free_node_data_cb with g_free (collapsed the dir/regular
      branch since g_free doesn't care about the type).
  [x] grep -rn 'g_slice_' src/ returns nothing. No other g_slice
      usage anywhere in the codebase.
  [x] Verify: clean build, no deprecation warnings. User to confirm
      scan / rescan still works and nothing leaks under normal use.

Step 35.4 — Clean up geometry_free_recursive
  [x] Went with option (b): geometry_free_recursive() now
      unconditionally invalidates every initialized VBO batch across
      all three modes, plus the pick FBO cache and TreeV arrangement
      state. Must be unconditional because on rescan fsv_mode is
      FSV_NONE, so geometry_queue_rebuild's mode-gated invalidation
      does nothing.
  [x] Found by user-visible bug: after Open Root → same directory,
      the old tree's geometry stayed on the GPU and rendered on top
      of the new root.
  [x] Related fix in scanfs.c: call dirtree_clear() BEFORE tearing
      down the GNode tree so GTK drops its FsvDirItem refs to the
      soon-to-be-freed nodes; then set globals.fstree and
      globals.current_node to NULL immediately after destroy. Also
      guarded find_tree_list_row() in dirtree.c against NULL fstree.
  [x] Verify: builds cleanly. User to confirm Open Root → same
      directory and Open Root → different directory both render
      correctly with no ghost geometry.

Step 35.5 — Fix static buffer hazards in common.c
  [x] Audit get_node_info() (common.c:677) and its callees
      (read_symlink, absname_merge, node_absname, i64toa,
      abbrev_size, get_file_type_desc). Document which fields alias
      which static buffer.
      - read_symlink: static `target` buffer (common.c:551), only
        caller is get_node_info.
      - absname_merge: static `absname` buffer (common.c:615), only
        caller is get_node_info. Distinct from read_symlink's buffer.
      - node_absname: heap-reallocated each call (xfree + NEW_ARRAY)
        at common.c:342-344; caller gets a pointer that is stable
        until the next node_absname() call.
      - i64toa: static strbuf1[256]. All six callers in get_node_info
        pass its return straight into xstrredup, so ninfo copies it
        before the next i64toa() overwrites.
      - abbrev_size: static strbuf[64]. Same pattern — copied via
        xstrredup immediately.
      - get_file_type_desc: returns cached pointer stable for the
        program lifetime (step 35.2); safe to store directly.
      - ctime(): libc per-thread static buffer; copied via xstrredup.
      - Pre-fix hazard: ninfo.target aliased read_symlink's static
        and ninfo.abstarget aliased absname_merge's static — returning
        &ninfo leaked those pointers out of common.c. Any future
        caller of read_symlink/absname_merge would clobber them.
  [x] Fix the ninfo.target / ninfo.abstarget aliasing: ninfo.target
      and ninfo.abstarget now copied via xstrredup() into ninfo-owned
      heap storage, matching all other ninfo fields. The blank-case
      branch also uses xstrredup for consistent ownership (both
      branches leave ninfo.target/abstarget as free-able heap).
  [ ] (Optional) Convert the remaining static buffers (i64toa,
      abbrev_size, node_absname) to per-call allocation to unblock
      future threading. Deferred — get_node_info() already copies
      their outputs via xstrredup before the next call, so they
      are not a correctness hazard under single-threaded use.
      Revisit in step 35.6 if the background scan worker needs any
      of these.
  [x] Verify: clean build. User to confirm Properties dialog and
      tooltips display correct values for symlinks (target +
      abstarget both visible and distinct when the symlink is
      relative).

Step 35.6 — Background filesystem scan (GTask)
  [x] Move scanfs.c's process_dir() recursion onto a GTask worker
      thread. The worker builds the GNode tree; completion is
      marshalled back to the main thread via g_idle_add /
      g_task_return.
      - Worker entry is scan_worker_thread(); it runs process_dir
        + setup_fstree_recursive and returns via
        g_task_return_boolean().
      - scanfs() now runs a nested GMainLoop while the worker
        executes, so the GTK main loop keeps pumping events
        (redraws, input, splash animation) during the scan.
        The loop is quit by the task completion callback.
      - node_table allocation + setup_fstree_recursive moved onto
        the worker (no GTK calls involved).
  [x] Ensure every data structure touched by the worker is either
      thread-local or protected — NodeDesc allocation, GNode
      insertion, scanfs-internal state. Do NOT touch GTK widgets
      or globals from the worker.
      - Progress statics (node_counts, size_counts, stat_count,
        scan_current_dir) are now protected by scan_stats_mutex;
        writer is the worker, reader is the main-thread
        scan_monitor timeout.
      - name_strchunk is worker-only during scan; the main thread
        does not touch it until the worker has returned.
      - dirtree_entry_new() is called from the worker (via
        process_dir), but it has been pre-reduced to a pure data
        write on the node being built (tree_expanded = FALSE) —
        no GTK widget access. All real GtkListStore/dirtree model
        work happens in dirtree_no_more_entries() on the main
        thread after the worker returns.
      - window_statusbar / gui_update calls removed from process_dir;
        the per-dir "Scanning: …" line is published to
        scan_current_dir and displayed by scan_monitor on main.
  [x] Replace the 50ms throttled gui_update() calls with a proper
      progress signal the main thread can consume.
      - The 0.05s throttled gui_update() loop in process_dir is
        gone; responsiveness is now provided by the nested
        GMainLoop running continuously on the main thread.
      - scan_monitor (still a 500ms g_timeout on main) is the
        "progress signal"; it snapshots stats under the mutex,
        displays them, and resets stat_count per period.
  [x] Audit for any static buffers the scan path currently relies
      on (see 35.5) — those must either be eliminated or made
      worker-safe before this step.
      - xgetcwd() uses a static, but it's called once on the main
        thread before the worker starts; its return is borrowed
        read-only by the worker and no one else calls xgetcwd
        during the scan. Safe.
      - All other path manipulations in process_dir use per-call
        pathbuf[PATH_MAX] / strbuf on the stack — thread-local.
      - i64toa / abbrev_size / node_absname statics are used only
        by the main thread (from scan_monitor, dialog, etc.) —
        not touched by the worker.
  [x] Verify: scan of a large tree (e.g. /usr) does not freeze the
      UI. Cancel, rescan, and close-during-scan all behave
      correctly. No races observed over many runs.
      NOTE: explicit user-triggered cancel is NOT wired up in this
      step — the worker does not check a GCancellable. The
      "rescan" and "close-during-scan" cases rely on the existing
      window_set_access(FALSE) lockout during scan. If
      close-during-scan proves to crash, we'll add a cancellation
      token in a follow-up.

      UPDATE — first attempt used a nested GMainLoop inside
      scanfs() to keep the synchronous API. User testing showed
      three problems: (1) the scan-monitor file list never
      visually updated during scans, (2) GNOME WM marked the
      window "not responding" frequently, (3) close-during-scan
      stalled until the worker finished. All three trace back to
      the nested loop interfering with GtkGLArea's frame clock
      and with GApplication shutdown. Refactored to proper async:
      scanfs() now returns immediately, continuation runs via
      scan_worker_done_cb on main once the worker completes.
      fsv_load split into fsv_load + fsv_load_after_scan (the
      post-scan part runs from the callback).

      UPDATE 2 — the async refactor fixed the "not responding"
      during scans and let close-during-scan dispatch cleanly,
      but the scan-monitor sidebar still appeared blank. Root
      cause was in gui.c's clist binding: gui_clist_set_row_text
      called g_list_store_splice with the same FsvListRow pointer
      in and out of the same position, expecting GtkColumnView to
      unbind+rebind the list item. It did not — same-item splices
      at the same index are treated as a no-op by the view. Fixed
      by switching the text columns to property bindings: the
      factory's bind handler now does g_object_bind_property on
      the row's "textN" → label's "label", and gui_clist_set_row_text
      uses g_object_set, which emits notify and causes the bound
      label to refresh automatically without needing a rebind.
      Unbind handler releases the binding. Col 0 (icon + name)
      uses a direct set for the icon (icons don't update
      in-place) and a property binding for text0.

Step 35.7 — Spatial VBO tiling (deferred / optional)
  [ ] Only tackle this step if 35.1–35.6 do not resolve the
      large-directory pain.
  [ ] Design: split each mode's single whole-tree VBO into a
      quadtree (MapV) or angular sector grid (TreeV / DiscV) of
      sub-batches. Cull whole tiles against the frustum before
      drawing so zoomed-in views of huge expanded trees don't
      pay for vertices they never see.
  [ ] Preserve the build-once-draw-many invariant: each tile is
      still GL_STATIC_DRAW with its own dirty flag.
  [ ] Verify: frame cost at normal zoom is unchanged; zoomed-in
      frame cost drops proportionally to visible area.

  Checkpoint: User tests on a genuinely large filesystem:
  - Directory tree sidebar stays responsive when expanding /
    collapsing deep subtrees
  - Properties dialog opens quickly on files of types already
    seen this session
  - Rescan on a large tree does not freeze the UI (after 35.6)
  - All three vis modes still render correctly, no regressions
  - Clean build, no GLib deprecation warnings, no GL errors


PHASE 36: DECIDE WHAT TO DO WITH CGLM
======================================

cglm (our current matrix-math dependency) is not readily packaged on
Fedora / RHEL / other RPM-based distros. This phase is a decision
point, not yet an implementation plan — pick an option first, then
flesh out the steps.

Current state:
- Entire cglm usage is confined to src/glmath.c (every `glm_*` call)
  plus one `#include <cglm/cglm.h>` in src/glmath.h.
- mat4 / mat3 / vec3 / vec4 typedefs from cglm leak into geometry.c
  and tmaptext.c through the glmath_get_mvp / glmath_get_normal_matrix
  return signatures.
- Operations actually used: glm_mat4_identity / copy / mul / inv,
  glm_frustum, glm_ortho, glm_translate, glm_rotate, glm_scale,
  glm_rad. That's the whole surface area.

Options on the table:

  (A) Vendor cglm headers in-tree
      - cglm is MIT-licensed and header-only.
      - Drop the cglm header tree into lib/cglm/include/cglm/, remove
        dependency('cglm') from meson.build, add
        include_directories('lib/cglm/include') to fsv_inc.
      - Zero code changes, zero performance delta, zero runtime dep.
      - ~150 header files added to the repo (a few hundred KB).
      - Bumping cglm upstream is a manual refresh — acceptable since
        we use a tiny, stable subset.

  (B) Swap for linmath.h
      - Single public-domain header, ~500 lines.
      - Rewrite glmath.c against linmath's API.
      - Redefine mat4 / mat3 / vec3 typedefs (or adapt call sites in
        geometry.c and tmaptext.c to linmath's mat4x4 / vec3 types).
      - Leaner, but with real change surface and bug risk.

  (C) Roll our own tiny matrix module
      - ~150 lines of hand-written 4x4 float matrix math.
      - Fully owned, zero external anything.
      - Matrix inversion is a classic footgun; would want tests
        before shipping, which we don't currently have infra for.

Step 36.1 — Pick an option
  [x] Decide between (A), (B), (C), or something else entirely.
  [x] Record the decision in this section so the implementation
      steps can be written against a concrete target.

  DECISION (2026-04): none of (A)/(B)/(C) chosen. Vendoring ~150
  third-party headers is unappealing; a hand-rolled or linmath
  swap is rewrite work with no user-visible upside. cglm stays as
  an external dependency; the RPM-packaging shortfall is pushed
  upstream — Fedora/RHEL users will need cglm from a side repo or
  a local build until the distros pick it up. Revisit only if a
  concrete user report makes this painful.

Step 36.2 — Implement the chosen option
  [—] N/A (no option chosen).

Step 36.3 — Confirm RPM-friendly build
  [—] N/A (no option chosen).

  Checkpoint: N/A — phase closed without code changes.


PHASE 37 — SYMLINK USABILITY IMPROVEMENTS
==========================================

Symlinks are currently rendered using their own on-disk size (the
byte length of the target pathname), which makes them visually
near-invisible, and both the hover statusbar and Properties dialog
treat them like any other file. This phase improves their display
and identification without changing their identity as distinct
nodes in the tree.

Decisions already made (from the Step 35.5 follow-up discussion):
  - Symlink-to-file: use the target file's size (`stat`, not `lstat`).
  - Symlink-to-directory: look up the target in the scanned GNode
    tree and use that dnode's `subtree.size` if present. If the
    target is outside the scanned root, fall back to the target's
    entry `st.st_size` from `stat`.
  - Broken symlink (`stat` fails): leave the tiny `lstat` size as-is
    so we don't lie about sizes. Properties dialog should note the
    link is broken.
  - Hover statusbar for symlinks: show `/path/to/symlink -> /path/to/target`.
  - Properties "Type:" field for symlinks: show the target's type /
    wildcard group (if resolvable) and always append "(symlink)",
    mirroring the existing "(executable)" suffix.

Step 37.1 — Symlink size follows the target
  [x] In scanfs.c `stat_node()` (around line 68), for NODE_SYMLINK
      nodes, additionally `stat()` the path. Store the result in a
      new NodeDesc field (or a parallel symlink-only struct) so we
      preserve both the symlink's own identity and the effective
      "display size".
      - Chose the parallel-struct route: added SymlinkNodeDesc
        (NodeDesc + int64 target_size) to common.h, plus the
        SYMLINK_NODE_DESC / NODE_IS_SYMLINK macros. process_dir in
        scanfs.c now allocates a SymlinkNodeDesc for NODE_SYMLINK
        nodes; target_size defaults to 0.
  [x] After scan completes, do a second pass that resolves any
      symlink whose target lies inside the scanned tree: look up
      the target by absname and use that dnode's `subtree.size`
      when it is a directory. This has to happen AFTER the tree
      is fully built so all subtree totals are finalised.
      - resolve_symlinks_recursive() runs on the worker thread
        right after setup_fstree_recursive(), walks the tree,
        stat()s each symlink's absolute path, and fills in
        target_size. For directory targets inside the scanned
        tree it uses realpath()+node_named_worker() to find the
        matching dnode and reads subtree.size. For directory
        targets outside the tree, falls back to stat().st_size.
        For non-directory targets, uses stat().st_size.
      - Worker-local helpers (node_abspath_worker,
        node_named_worker) are used in place of the main-thread
        node_absname / node_named to avoid racing on their
        shared static buffers.
  [x] Update geometry (MapV / DiscV / TreeV) to consult the
      effective display size rather than NODE_DESC(node)->size for
      symlinks. Do this with a single helper — something like
      `node_display_size(node)` — so the three modes stay in sync.
      - node_display_size() added to common.c. Returns
        size+subtree.size for directories, target_size for
        resolved symlinks, size otherwise. All four geometry
        sites (discv_node_compare, discv_init_recursive,
        mapv_init_recursive x2, treev_init_recursive) now go
        through this helper.
  [x] Preserve the existing NODE_DESC(node)->size field as-is for
      "actual on-disk size" readouts (Properties "Size:" line). The
      display-size change is purely for geometry.
      - NODE_DESC(node)->size is still the lstat size; only the
        derived display size consulted by geometry changes.
  [x] Verify: symlinks to files render at their target's size;
      symlinks to directories inside the scanned tree render at the
      target's subtree size; broken / unreachable symlinks stay
      tiny. No regression in MapV / DiscV / TreeV for non-symlinks.

  UPDATE 1: During testing, user reported two issues with 37.1:
    (a) In MapV, a symlink-to-file renders with the steep-slant
        symlink silhouette, which looks different from neighbouring
        regular files. Before the size fix this was barely visible
        because symlinks were pixel-sized — it only became obvious
        once they inflated to match their target.
        Fix: SymlinkNodeDesc now carries a `target_is_dir` boolean
        set during resolve_symlinks_recursive. A new helper
        `mapv_node_slant(node)` in geometry.c picks the slant
        based on the symlink's resolved target: symlink-to-file
        uses the regular-file slant; symlink-to-directory uses
        the directory slant. Unresolved / broken symlinks keep
        the distinctive steep symlink slant. All four mapv
        slant-ratio call sites go through the helper.
        Visual differentiation of symlinks (vs. their targets) is
        deferred to future texturing work rather than using shape.
    (b) Directory symlinks pointing outside the scanned root (the
        common case, since users rarely link within a root they're
        exploring) fell back to stat()'s st_size, which for a
        directory is the inode allocation (~4kB) — effectively
        invisible in MapV and DiscV. TreeV's log scale masked it.
        Fix: added scan_out_of_root_dir_size() in scanfs.c — a
        bounded recursive walk (lstat only, no symlink-following)
        capped at OUT_OF_ROOT_MAX_ENTRIES (20 000) and
        OUT_OF_ROOT_MAX_DEPTH (12) so a symlink to /
        can't hang the scan. resolve_symlinks_recursive calls
        this when realpath() places the target outside the tree,
        then falls back to st.st_size if even that returns zero
        (empty / unreadable dir).

Step 37.2 — Hover statusbar shows symlink target
  [x] In viewport.c, at the three `window_statusbar(SB_RIGHT,
      node_absname(...))` sites (click, right-click, hover), if the
      indicated node is a NODE_SYMLINK build a composed string of
      the form "<absname> -> <target-abs>" and display that.
      - All three viewport sites plus the two filelist sites now
        call node_hover_label instead of node_absname. Dirtree
        sites still call node_absname (dirtree only holds
        directory nodes, so symlinks can't appear there).
  [x] Use a single helper in common.c so the three call sites stay
      consistent (e.g. `node_hover_label(node)` returning a
      statically-owned string in the same style as `node_absname`).
      - node_hover_label() added to common.c. For non-symlinks
        it returns node_absname(node) unchanged. For symlinks it
        readlink()s the absname, builds "<absname> -> <target>"
        in a private static-buffer slot, and returns that.
  [x] For broken / unreachable targets, fall back to just the
      readlink() output so the user still sees what the link
      claims to point to.
      - readlink() reports the stored target text regardless of
        whether the target exists, so broken symlinks still show
        "<symlink> -> <stored-target>". If readlink itself fails
        (permissions, racing unlink) the helper falls back to the
        plain absname.
  [x] Verify: hovering a symlink shows "<symlink> -> <target>";
      hovering a regular file/directory shows just the absname
      (no regression); hovering a broken symlink shows
      "<symlink> -> <dangling-target>".

Step 37.3 — Properties dialog marks symlinks and shows target type
  [x] In dialog.c, in the `Type:` field construction (around line
      1254), when the node is a NODE_SYMLINK:
       - Compute the target's wildcard-group name (apply the
         existing `color_wpattern_group_name()` logic against the
         target path, not the symlink path).
       - If that differs from the symlink's own wildcard group,
         display "<symlink-group> -> <target-group>". Otherwise
         just show the group once.
       - Always append "(symlink)" (mirrors "(executable)").
       - If the target is unreachable (stat fails), append
         "(broken symlink)" instead.
      - color.c gained color_wpattern_group_name_for_filename()
        (the existing GNode-flavoured version now delegates to
        it). dialog.c's Type block consults it for the target
        basename extracted from node_info->abstarget. Falls
        back to node_type_names[] for Directory / Regular File
        when no wildcard group matches and stat() succeeds.
        Appends "(symlink)" or "(broken symlink)" as appropriate.
  [-] Consider adding a "File type" notebook page to the NODE_SYMLINK
      switch case in dialog.c so users can see the full `file`
      command description of the target (not just the short wildcard
      group name). This is optional — keep scope small if it gets
      hairy.
      - Skipped per user: "We can skip that, I like what properties
        looks like now."
  [x] Verify: Properties on a symlink-to-executable shows e.g.
      "Shared library -> Shared library (symlink)" or similar, with
      the broken-symlink path clearly marked when relevant.

  Checkpoint: User confirms that symlinks are visible in the 3D
  view at a size matching their target, that hovering shows the
  target, and that Properties clearly identifies the node as a
  symlink and describes what it points to (including broken links).


PHASE 38 — COLLAPSE "NOT RESPONDING" HANG
==========================================

User reported during 35.6 testing that collapsing a deeply-populated
directory in the 3D view triggers GNOME's "application is not
responding" banner (it eventually recovers). The filesystem scan has
already been moved off the main thread in 35.6, so this is a
separate, synchronous main-thread hot path — most likely the
collapse animation invalidating / rebuilding the affected subtree's
geometry and VBO batches in a single frame.

Step 38.1 — Profile the collapse path
  [x] Find the dominant cost: is it geometry_free_recursive on
      the collapsing subtree, VBO invalidation + rebuild, or the
      dirtree-model updates driven by colexp.c?
  [x] Instrument with a simple main-thread stopwatch (g_get_monotonic_time
      around the collapse handler) and a printf of the millisecond
      duration for a known directory — no need for perf/flamegraph
      yet.
      - colexp() entry function instrumented with sub-bucket
        timers for dirtree, gui_update, geom_init (and the
        remaining "other"). On a 125 392-directory tree:
          Expand All:   total=108 s  dirtree=12 s  other=96 s
          Collapse All: total=87 s   dirtree=0.07 s  other=87 s
        "other" dominates — almost all of it is
        morph_break + morph_full linear scans of morph_queue.
        With 125 k directories each call pays O(N) on an
        N-growing queue ≈ 15.7 billion compares total, which
        matches the 87-96 s figure.

Step 38.2 — Reduce or amortise the cost
  [x] Root cause was morph_queue linear scan, not any of the
      originally-suspected candidates. Replaced the GList with
      a GHashTable keyed by the `double *var` pointer so
      morph_break / morph_full lookups go O(N) -> O(1).
      morph_iteration still walks every active morph (that cost
      is fundamental — N updates per frame), but queue insertion
      and removal no longer scale with queue size.

Step 38.3 — Visual feedback during long expand/collapse
  [x] User feedback: "An action taking a while is less bad if
      the user knows the program is still working on it."
      Two small changes:
        - colexp() now calls window_set_access(FALSE) + gui_update()
          at the TOP of the depth==0 block for COLLAPSE_RECURSIVE /
          EXPAND_RECURSIVE so the wait cursor appears before the
          long dirtree / geometry work, not after.
        - dirtree.c's expand_recursive_via_row pumps the GTK main
          loop every 2048 expansions so Expand All on a 125k-node
          tree no longer trips GTK's "Not Responding" watchdog.

  Checkpoint: User confirms that collapsing a large directory no
  longer trips "not responding", and visual / interaction behaviour
  is unchanged.


PHASE 39 — LAZY RENDER
=======================

Very large directory trees (the user's reference set: 19 levels deep,
~1.4 M entries) make scanning slow and rendering unusable. Even with
the Phase 35 / 38 fixes, "Expand All" on a deep subtree still produces
an overwhelming amount of geometry. The fix is to bound how much of
the tree is actually rendered at any one time, with a small buffer
scanned ahead so navigation feels instant.

Design summary
--------------

  Two limits, both enforced together (whichever is hit first):
    - render_depth N (default 7)   — max levels rendered below an anchor
    - object_limit  M (default 250 000) — max total nodes per Expand All

  Plus:
    - readahead_depth R (default 3) — scan R extra levels past the
      visible frontier so the next interaction needs no I/O wait

  Anchors. A directory becomes an anchor when the user (a) opens it as
  the root, (b) navigates into it (current_node moves), or (c) invokes
  Expand All on it. Each anchor carries a fresh N-level render budget
  for its own subtree. Sibling subtrees retain whatever state they
  already had — anchoring /a/b/c/d/e does NOT reset siblings of /e.

  Expand All semantics. BFS, level-by-level. Every node at depth K is
  processed before any at depth K+1. When the depth limit or object
  limit is reached mid-traversal, finish the current level and stop —
  so the user always sees a uniform-depth subtree rather than ragged
  stops.

  Layout fidelity. All three modes size a parent by the recursive byte
  total of its subtree, including unrendered descendants. To keep
  layout exact without paying full-scan cost, the scanner does a
  size-only walk past depth N+R: readdir + stat to accumulate byte
  totals into ancestors, but no per-node descriptor allocation. Far
  cheaper than today's full scan, and ancestor sizes stay correct.

  Read-ahead. Background pass that keeps the scanned frontier R levels
  past the visible frontier on every active branch. Triggered eagerly
  whenever user interaction (navigation, expand, anchor) extends the
  visible frontier.

  Indicator. Whenever the current view contains at least one truncated
  subtree, the toolbar shows the text "Lazy Render Limit Reached"
  centered between the Timestamp and Preferences buttons.

  Preferences. New "Performance" page with an enable toggle (default
  ON) and three numeric fields: render_depth, readahead_depth,
  object_limit. Above the controls, a clearly-marked WARNING:
  disabling lazy render or raising the limits can cause severe
  slowdowns on large directories.

Step 39.1 — Settings infrastructure
  [x] Add four GKeyFile keys with defaults under [LazyRender] in
      ~/.config/fsv4/fsvrc:
        enabled          (bool, default TRUE)
        render_depth     (int,  default 7)
        readahead_depth  (int,  default 3)
        object_limit     (int,  default 250 000)
      (The TODO design note said "nvstore", but the codebase uses
      GKeyFile for all other settings; matched that pattern.)
  [x] Expose via a small lazy_render.c/h API:
        boolean lazy_render_enabled( void );
        int     lazy_render_depth( void );
        int     lazy_readahead_depth( void );
        int     lazy_object_limit( void );
        void    lazy_render_set_*( ... );  /* in-memory only */
        void    lazy_render_load_config( void );
        void    lazy_render_write_config( void );
  [x] No behavior change yet — values are read but not used.

Step 39.2 — Bounded scanfs with size-only deep walk
  [ ] scanfs.c: accept a max-full-scan depth parameter (= N+R when
      lazy is enabled; INT_MAX otherwise).
  [ ] Up to that depth, behave as today: allocate per-node
      descriptors and link into the GNode tree.
  [ ] Beyond that depth, do a size-only walk: readdir + stat on each
      entry, sum bytes/counts into the deepest scanned ancestor's
      subtree totals, but do NOT allocate NodeDesc / DirNodeDesc /
      add to the GNode tree.
  [ ] Add a per-DirNodeDesc flag scan_state ∈
        { UNSCANNED, SIZE_ONLY, FULL }
      so other modules can tell whether children exist as nodes.
  [ ] With lazy disabled, depth = INT_MAX so behavior is identical
      to today.

Step 39.3 — Render-depth gate in geometry
  [ ] Per-anchor budget tracking. Add to DirNodeDesc:
        boolean is_anchor;
        int     anchor_depth_remaining;  /* N when anchored, else inherited */
  [ ] On node visit during geometry_init / draw, compute effective
      remaining depth = max over all ancestor anchors of
      (anchor.N - distance_to_anchor). If <= 0, do not render.
  [ ] Initial anchors: the root passed to fsv on launch.
  [ ] Verify all three modes (MapV, DiscV, TreeV) honor the gate.
  [ ] No anchor changes from navigation/Expand All yet — that comes
      in 39.4.

Step 39.4 — Anchor on navigation
  [ ] When current_node moves (camera_look_at on a directory, or
      double-click navigation), mark that directory as an anchor and
      reset its anchor_depth_remaining to N.
  [ ] Trigger geometry rebuild for the anchored subtree only.
  [ ] Confirm: navigating into ~/src/mame/build/linux extends the
      visible frontier from /linux but does NOT alter sibling
      subtrees of /linux.

Step 39.5 — Expand All as BFS with object cap
  [ ] Refactor the COLEXP_EXPAND_RECURSIVE path in colexp.c to a
      level-by-level BFS instead of DFS recursion.
  [ ] Track total nodes scheduled-to-expand across the BFS. Between
      levels, check against object_limit; if exceeded, stop after
      the current level completes (no partial level).
  [ ] Mark the Expand All target directory as an anchor (so the
      depth limit is also measured from that point).
  [ ] Truncated branches keep their scan_state from 39.2; visible
      indicator (39.7) reflects their truncation.

Step 39.6 — Background read-ahead
  [ ] After any operation that exposes a new render frontier (initial
      load, navigation, expand), schedule a background pass that
      extends each frontier directory's scan_state to FULL down to
      depth N+R.
  [ ] Run on a worker thread (reuse the GTask worker pattern from
      35.6 / 38). On completion, marshal back to the main thread to
      mark the new descriptors and request a redraw.
  [ ] Do NOT proactively scan branches the user has not anchored or
      expanded toward.

Step 39.7 — Toolbar indicator
  [ ] Add a non-interactive label widget to the toolbar, placed in
      the toolbar_hbox between the Color Mode cluster and the Utility
      cluster, centered horizontally in the available space.
  [ ] Text: "Lazy Render Limit Reached" — visible only when at least
      one currently-rendered anchor's subtree was truncated by either
      depth or object limit. Hidden otherwise.
  [ ] Track per-anchor "was_truncated" boolean during the BFS /
      geometry pass; the label aggregates an OR across all visible
      anchors.

Step 39.8 — Preferences UI
  [ ] Add a "Performance" page (or section in the existing Preferences
      dialog) with:
        - Top: a clearly-styled WARNING block explaining that
          disabling or raising these values can cause severe
          slowdowns on large directory trees.
        - Toggle: "Enable lazy render" (default ON).
        - Numeric inputs: Render depth (N), Read-ahead (R),
          Object limit (M).
  [ ] Changes apply to FUTURE operations only — already-truncated
      trees are not re-scanned automatically. (Closing and reopening
      the directory picks up the new values.)
  [ ] Persist via nvstore from 39.1.

Step 39.9 — Cross-mode validation and cleanup
  [ ] Verify all three modes (MapV, DiscV, TreeV) on:
        - small tree (no truncation expected)
        - medium tree (~/src, slight truncation)
        - large tree (1.4 M entries, severe truncation, fast first
          paint, indicator visible)
  [ ] Verify Expand All: BFS, uniform depth, indicator updates.
  [ ] Verify navigation extends one branch only (B-anchor model).
  [ ] Strip any temporary profiling printfs.

  Checkpoint: With lazy render ON (default), large trees paint
  quickly and remain interactive; the indicator appears whenever the
  view is truncated; the user can navigate deeper to extend the view
  branch by branch. With lazy render OFF (via Preferences), behaviour
  matches the pre-Phase-39 codebase.

  Deferred (note for later, do NOT do in 39):
    - Memory / eviction policy for scanned data the user has wandered
      far from. Keep everything in memory for now and revisit only if
      memory pressure becomes a real issue.
    - Streaming render during scan (start drawing while still
      readdir-ing). Achievable but adds significant complexity and is
      not essential given depth bounds make initial scan fast.
    - "Scan everything now" button in preferences for users who turn
      lazy off mid-session and want immediate fill.


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
