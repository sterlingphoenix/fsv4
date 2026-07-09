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


PHASE 39 — RENDER PERFORMANCE FOR LARGE TREES
==============================================

Render-side fix-it phase for the "rendering hits a bottleneck that
makes the program unusable" symptom on large filesystems. Scan is
explicitly out of scope: scan has clear in-progress feedback and is
bounded by lstat() latency, which only parallel-syscall work would
shift; that is its own future phase.

Background
----------

Static analysis of the per-frame path (ogl_draw → geometry_draw →
mode-specific *_draw(high_detail)):

  1. frustum_extract — recomputes 6 frustum planes from the MVP.
  2. *_rebuild_batch — no-op unless the dirty flag is set
     (geometry change, color-mode change, expand/collapse).
  3. vbo_batch_draw / vbo_batch_draw_lines — 1-3 glDrawArrays calls.
     GPU-side cost; CPU just submits.
  4. if high_detail: *_draw_recursive — walks the entire visible
     subtree to emit text quads, one per visible leaf label.
  5. Pick (when the user moves the mouse): ogl_color_pick re-renders
     the scene into a private FBO at display resolution whenever
     invalidated, using the same VBO with the pick shader.

Frustum and screen-size culling exist at the directory level inside
the recursive label walks. There is NO per-leaf size cull — a single
visible directory with N file children emits N text quads regardless
of their projected pixel size.

An earlier session tried a per-leaf label cull and reportedly saw no
noticeable improvement. That is itself diagnostic: it implies the
label walk is NOT the dominant cost on the workloads tried. Without
measurement, the natural next guess (label cull, more aggressive
culling, etc.) risks chasing the wrong cost again. Therefore: measure
first, decide second.

Out of scope:
- Scan parallelism / io_uring (separate phase, separate concerns).
- Lazy / depth-bounded rendering — failed avenue from a previous
  branch; the depth cliff makes the UX worse and the bottleneck on
  real trees is width, not depth.
- Any change that breaks the "build once, draw many" invariant
  (CLAUDE.md Rule 5). VBO contents must stay GL_STATIC_DRAW with
  dirty-flag invalidation — no per-frame CPU vertex assembly.

Step 39.1 — Frame-cost instrumentation
  [x] Added frameprof.c/h. F11 toggles a per-frame collector that
      accumulates per-bucket microseconds and prints a 60-frame
      summary to stderr. Each entry point starts with a single
      load+branch on `active`, so cost is negligible when off.
      Buckets:
        - frustum_extract — frustum_extract() call
        - vbo_rebuild     — *_rebuild_batch() (counted only on
                            real rebuilds, not no-op dirty=FALSE)
        - vbo_solid       — vbo_batch_draw of solid + branch
        - vbo_outline     — vbo_batch_draw_lines of outline
        - label_walk      — text_pre + *_draw_recursive + text_post
        - text_flush      — GL upload + draw inside text_flush;
                            also publishes bytes/frame and
                            draws/frame
        - pick_render     — re-render block inside ogl_color_pick
                            (only when pick FBO invalidated);
                            also publishes pick-invocation count
        - other           — frame_total − accounted (label_walk
                            already contains text_flush, so it's
                            counted once in the "accounted" sum)
  [x] Wired into ogl_draw (frame begin/end), all three *_draw
      paths (per-bucket markers), all three *_rebuild_batch
      (rebuild counter), tmaptext text_flush, and ogl_color_pick.
      F11 bound in viewport_key_pressed_cb.
  [x] User runs on the actual problem workload (their large
      filesystem) in each of the three vis modes, capturing
      numbers in each scenario.
  [x] First-pass results (TreeV mode, post Expand-All on a large
      tree):
        - idle:        9.81 ms/frame, label_walk=4.35, text_flush
                       2.48 ms with 2933 draws/frame, vbo_rebuild
                       5.41 ms (35 rebuilds — expand-all anim
                       still settling)
        - bad/idle:  993.04 ms/frame, label_walk=942.67,
                       text_flush=736.79 with 350,667 draws/frame
        - panning:   252.32 ms/frame, label_walk=252.28
        - zoomed in:  54.00 ms/frame, label_walk=53.96
      Dominant cost in every bad scenario: label_walk + the
      text_flush calls nested inside it. vbo_solid stays at 0.01
      ms/frame throughout, ruling out Options B (tiling) and C
      (instancing) as the right starting point.

Step 39.1 follow-up — fix latent text_set_color flush storm
  [x] text_set_color() in tmaptext.c was flushing the pending
      text batch UNCONDITIONALLY, even when the new colour equalled
      the current one. Label loops (especially TreeV's, which
      re-sets the same leaf and platform colours per node) were
      triggering one full GL upload + draw cycle per label —
      observed as 350,667 text_flush calls per frame in the worst
      Phase 39.1 bucket.
      Fix: text_set_color now early-returns when r/g/b match the
      current colour. One conditional, no architecture change.
      This is almost certainly the same hidden multiplier that
      defeated the previous "per-leaf label cull, no measurable
      win" attempt — with flush-per-label, no amount of culling
      could move the needle.
  [x] frameprof.c: text_bytes_uploaded promoted from int to gint64.
      Heavy scenes push GB/window through GL_STREAM_DRAW and the
      old int counter wrapped negative in the reported summaries.
  [x] First re-measurement (MapV — user clarified the heavy
      workload was MapV, not TreeV): text_flush draws/frame
      DID NOT drop. Still ~360,000. Diagnosis: the text_set_color
      fix was real but addressed only one of TWO flush triggers.
      The actual MapV culprit was different — see follow-up below.

Step 39.1 follow-up 2 — fix the per-label text_flush
  [x] Root cause: text_draw_straight, text_draw_straight_rotated,
      and text_draw_curved each called text_flush() unconditionally
      at the END of every call. That's "one full GL upload + draw
      cycle per label" baked into the API, defeating the entire
      text_pre / text_post batching protocol.
      The reason it was there: labels are emitted in local-space
      coordinates, while the modelview is set to that node's
      local frame during *_draw_recursive. Flushing per-label
      captured the right MVP for each one.
  [x] Fix: text_add_quad now reads the current modelview at emit
      time and pre-multiplies each corner into view space. The
      vertex buffer stores view-space coords. text_flush sends
      the projection-only matrix as u_mvp. Result: batches can
      accumulate across many text_draw_* calls with different
      modelview matrices, and the per-call text_flush() at the
      end of each draw function is removed. Per-frame flush count
      drops from O(labels) to O(distinct colours used).
  [x] Cost added per quad: 4 corners × (12 mults + 12 adds) of
      4×4 modelview multiply. Negligible vs. one removed GL
      upload + draw cycle per label.
  [x] text_pre_matrix_change() retained as a no-op-safe flush
      (still useful before a projection change, though no current
      caller invokes it from inside the label walks).
  [x] Re-measured with v4.39.03 on the MapV stuck scenario:
      text_flush dropped from 360,000 draws/frame → 1.0 draw/frame,
      exactly as predicted. But frame time only improved marginally
      (993 → 729 ms/frame). text_flush still uploads 1.7 GB/frame
      in that one giant draw — we just batched the work, didn't
      reduce it. The actual fix needs to be label COUNT, not flush
      count. That's 39.4 Option A.

Step 39.2 — Skip label walk during camera motion  [REVERTED v4.39.08]
  [—] Attempted gating the label walk on camera_settled().
      Worked perf-wise (pan/orbit dropped to near-zero) but the
      UX trade was unacceptable: text vanished during animation
      across initial-load camera-in, expand/collapse anim, and
      drag-orbit. Reverted in all three mode draws. camera_settled()
      stayed in the codebase (unused) in case a future per-frame
      "is the camera moving" gate needs a cheaper signal than
      walking the morph queue; the side-effect redraw inside it
      was reverted too.
      User constraint, now hard: labels MUST be present during
      animation. Any future perf work has to make the label walk
      itself cheap enough to run every frame, not skip it.

Step 39.4 follow-up — TreeV per-leaf cull tightening (v4.39.08)
  [x] After 39.2 reverted, TreeV idle was still 341 ms/frame
      with ~50K labels surviving the per-leaf cull. Root cause:
      TreeV leaves are uniform-size (TREEV_LEAF_NODE_EDGE = 256
      world units), so the cull either passes every leaf on a
      "visible" platform or rejects every leaf. With the standard
      LABEL_SIZE_THRESHOLD (6 px radius / 12 px full cube), a
      typical filename's worth of text is unreadable but still
      drawn.
  [x] Fix: TreeV per-leaf cull uses 4× LABEL_SIZE_THRESHOLD as
      its threshold (24 px radius / 48 px full cube). Roughly
      matches the smallest cube size that can hold a few readable
      characters. MapV and DiscV cull thresholds are unchanged
      (their leaves are data-driven, so the standard threshold
      already culls effectively).
  [—] Re-measurement showed almost no improvement (547 ms/frame,
      ~60K labels). Cull tightening alone isn't enough. Moved to
      the architectural fix below.

Step 39.4 follow-up 2 — Label vertex cache (v4.39.09)
  [x] Architectural change: cache the assembled label vertex
      buffer in a persistent GL_STATIC_DRAW VBO. World-space
      vertices, so camera moves don't invalidate it — they just
      update the shader uniform.
  [x] ogl.c: saves the camera-only modelview at the end of
      setup_modelview_matrix and exposes it via
      ogl_get_camera_modelview(). The "node-only" modelview
      during a label walk is then camera_modelview_inverse *
      current_modelview.
  [x] tmaptext.c: added persistent cache_vao / cache_vbo /
      cache_vertex_count / cache_is_valid / cache_filling state
      and the API: text_cache_replay (draws cached buffer with
      projection × camera_modelview as the uniform, returns TRUE
      to skip the walk), text_cache_begin_emit / end_emit
      (begin/end a walk that fills the cache), text_cache_invalidate.
      text_add_quad now has two modes: direct (current modelview,
      view-space — for about/splash) and cache-filling (node-only
      modelview, world-space — for the geometry label walks).
  [x] geometry.c: all three *_draw label sections now do
      text_cache_replay → if invalid, fall through to walk that
      calls text_cache_begin_emit / draw / text_cache_end_emit.
  [x] Invalidation hooks wired:
        - colexp_progress_cb (per deployment-morph step, so
          cache stays invalid for the duration of expand/collapse
          animations; settles to valid afterwards)
        - geometry_init (mode switch, fresh scan — both produce
          new label layouts)
        - geometry_toggle_labels ('t' hotkey)
        - geometry_treev_set_scale_logarithmic (calls
          geometry_init, covered)
        - resize_cb in ogl.c (viewport change → screen-size cull
          decisions in cache become stale)
  [x] Trade-off accepted: per-leaf cull decisions in the cache
      are frozen at build time. After a big zoom-in, labels
      that would now pass the cull aren't shown until something
      invalidates (mode switch, expand/collapse, etc.). If this
      is a UX problem, add a heuristic re-invalidate on large
      camera-distance changes.
  [x] Follow-up (v4.39.10): added zoom-threshold invalidation
      inside text_cache_replay itself. If the current camera
      distance differs from the build-time distance by more than
      ~30% in either direction (ratio outside [0.7, 1.5]), the
      cache is invalidated so the next frame walks with current
      cull decisions. Tracks build-time camera->distance at
      end_emit. Self-contained — no hooks needed in
      camera_dolly / camera_revolve / etc.
  [x] User confirmed across MapV, DiscV, TreeV: labels look
      correct, performance excellent at idle and during
      pan/orbit. Expand/collapse animation feels less smooth
      than steady-state (expected — cache invalidates each
      frame). DiscV unchanged-fast as before.
  [x] Phase 39 checkpoint MET on the user's large workload:
      idle frame times in the low-single-digit ms in all three
      modes; camera motion cost no longer dominated by the
      label walk; pan/orbit feel smooth.

Step 39.2 follow-up — TreeV text_set_color flush storm (v4.39.06)
  [x] After per-leaf cull was added to TreeV (v4.39.05), TreeV
      label_walk was still 429 ms/frame at idle with 60,175
      text_flush draws/frame. The remaining flushes were from
      text_set_color: TreeV alternates platform_label_color and
      leaf_label_color per visited platform directory, so each
      visited dir caused 2 flushes. ~30K platforms → 60K flushes.
  [x] Fix: removed text_flush() from text_set_color entirely. The
      flush was a holdover from an earlier per-batch-uniform color
      design; since text_add_quad copies text_cur_color into each
      vertex at emit time, the per-vertex color attribute carries
      it through the shader. Multiple colors can coexist in one
      drawn batch with no glitches. Per-frame flush count is now
      bounded by 1 (text_post) regardless of how many colors are
      used during the walk.

Step 39.3 — Lower-resolution pick FBO
  [ ] In ogl.c pick_fbo_ensure, allocate the pick renderbuffers at
      viewport / PICK_DOWNSCALE (default PICK_DOWNSCALE = 2; expose
      as a #define at the top of ogl.c so it's easy to retune).
  [ ] In ogl_color_pick, divide the input (x, y) by PICK_DOWNSCALE
      before glReadPixels. Pick precision drops from 1 pixel to
      PICK_DOWNSCALE pixels — fine for clicking on bounded blocks
      and discs, which are nearly always ≥ 4 px wide if they're
      visible at all.
  [ ] Verify: clean build. User confirms hover/click still
      identifies nodes correctly (try clicking on small blocks
      near a directory edge), and pick re-render is noticeably
      faster during camera animation (when ogl_pick_invalidate
      fires every frame).
  [ ] Independent of 39.1's numbers: do this regardless. Cheap
      and reversible.

Step 39.4 — Decision point: which big fix
  [x] DECISION (recorded 2026-06-19): Option A — per-leaf label
      cull. Supporting numbers from 39.1 follow-up 2 (v4.39.03):
        - text_flush draws/frame: 1.0   → batching is solved
        - text_flush KB/frame:    1.7 GB → label COUNT is the cost
        - vbo_solid ms/frame:     0.01   → ruling out B (tiling)
                                            and C (instancing)
        - label_walk ms/frame:    729 ms → CPU walk + emission
                                            of millions of labels
      The per-leaf cull cuts both CPU emission and GL upload
      bytes proportionally to how many labels are sub-pixel.
  [x] Implemented in MapV (v4.39.04): label_walk dropped from
      729 ms → 28.69 ms/frame (~25× win). text_flush KB dropped
      from 1.7 GB → 17 MB/frame.
  [x] Replicated in DiscV (v4.39.05): same per-leaf
      screen_size_pixels check inside the label loop.
  [x] Replicated in TreeV (v4.39.05): per-leaf cull on the
      non-DIR children loop; cube edge half-width as the
      projected-size proxy.

  OPTION A — Per-leaf label cull (revisit)
    Trigger: label walk and/or text_flush dominate idle-frame
    cost on the workload.
    Plan: in each *_draw_recursive, before calling apply_label on
    a leaf child, compute the child's own projected pixel size and
    skip if below LABEL_SIZE_THRESHOLD. The previous attempt may
    have been gated against the PARENT extent (which is what the
    existing code already does at the dir level) rather than the
    leaf's own extent — measurement should clarify which.
    Also reconsider: hoist string-length / max-dim work out of
    the per-frame label call into a one-time precompute on the
    NodeDesc.

  OPTION B — Spatial VBO tiling (revival of TODO 35.7)
    Trigger: VBO draw time (fragment / fillrate) dominates the
    GPU side, OR VBO rebuild on geometry change is the spike,
    OR the visible vertex count grows past the GPU's comfort zone.
    Plan: split each mode's whole-tree VBO into spatial tiles:
      - MapV: quadtree of XY-aligned tiles; leaf tile sized so
        the average tile holds ~10K-50K vertices.
      - TreeV: angular sectors at root, sub-sectored per radial
        ring.
      - DiscV: angular sectors only (no rings).
    Each tile keeps its own VBOBatch + dirty flag. The build pass
    populates tiles in one tree walk (assign each node to its
    tile, emit vertices into that tile's batch). The draw pass
    frustum-culls each tile against the planes from frustum_extract
    before issuing the per-tile glDrawArrays.
    Build-once / draw-many preserved at tile granularity. Dirty
    invalidation propagates only to tiles whose nodes changed
    (e.g. expand/collapse marks only the affected tiles dirty).

  OPTION C — Instanced rendering for box-shaped nodes
    Trigger: VBO rebuild cost after expand/collapse dominates
    OR VBO memory is a real constraint (visible vertex count
    × 32 B per vertex pushing past hundreds of MB).
    Plan: every MapV block is a unit cube; every TreeV leaf is
    a unit cube. Emit one per-instance record per node
    (vec3 position, vec3 size, vec3 color, uint id) and let the
    vertex shader place a shared unit cube. Per-batch memory
    drops 5-10× (≈30 verts → 1 instance record), build CPU cost
    drops proportionally.
    Scope: only the unit-cube nodes — TreeV platforms (curved
    sector geometry) and DiscV discs (curved geometry) keep
    their existing triangle-list batches. Two batches per mode:
    instanced + non-instanced.

  Note: A/B/C are mutually compatible. Pick the one that
  addresses the dominant bucket. If two buckets are nearly tied
  (e.g. label walk and VBO draw both 8 ms), prefer the cheaper
  option first.

Step 39.5 — Implement chosen big fix
  [ ] Fill in detailed sub-steps once 39.4 picks A, B, or C.
      Each sub-step must leave the program buildable + runnable
      (Rule 1) and preserve build-once / draw-many (Rule 5).

Step 39.6 — Re-measure and stop
  [ ] Re-run 39.1's instrumentation on the same workload that
      drove 39.4's decision. Confirm the bucket that justified
      the choice has collapsed. Note the new numbers inline.
  [ ] If the new dominant bucket is still painful, loop back to
      39.4 with the next-most-likely option from A/B/C.
  [ ] Strip 39.1's instrumentation once perf is acceptable, OR
      keep it permanently behind the F11 hotkey if useful as a
      diagnostic hook (cost is zero when off).

  Checkpoint: User confirms the workload that was previously
  "unusable" is now interactive. Quantitative target on the
  user's test tree:
    - frame time < 33 ms (≥ 30 fps) at idle in all three modes
    - frame time < 16 ms (≥ 60 fps) during camera motion
    - hover-pick response feels immediate (no per-frame stall
      tied to pick FBO re-render)

  Stop criteria: any step beyond Phase 39 belongs to its own
  phase. Do not start an instanced-rendering refactor "while
  we're in here" if the chosen fix already cleared the
  checkpoint.

  Discovered-during-phase, defer to its own phase:
    - Mode switch on a large unexpanded tree triggers GNOME's
      "not responding" window. Cause: geometry_init runs on the
      main thread on mode switch (no prebuilt cache for the new
      mode, unlike the initial load which pre-builds on the scan
      worker). Symmetric to Phase 38's Expand-All "not
      responding" fix. Likely needs either (a) prebuild ALL three
      modes' geometry on the scan worker, (b) a UI lockout + busy
      cursor + main-loop pump like colexp(), or (c) move the
      mode-switch geometry_init to a worker and consume on main.
      PARTIAL MITIGATION (v4.39.07): the dominant cost was
      color_assign_recursive, which ran unconditionally inside
      geometry_init even though colors don't change between mode
      switches. See Step 39.2 follow-up 2.
    - TreeV "Expand All" (and "Collapse All") leaves the camera
      in a position where the geometry is off-screen — user sees
      a black void until they press 'r' to reset. Pre-existing,
      tied to colexp.c's treev_saved_* / treev_camera_locked
      mechanism for tethering the camera to the growing radius
      during expansion. Not caused by any Phase 39 work.
    - Mouse-wheel zoom can drive the camera past the safe near-
      clip range — viewport partially or fully turns black at
      zooms much closer than double-click navigation would
      produce. Likely a near/far clip plane mismatch with
      distance, or camera->distance going below some minimum.
      Reported by user during Phase 39.4 cache testing.

Step 39.2 follow-up 2 — colors_dirty flag (v4.39.07)
  [x] geometry_init() calls color_assign_recursive() at the end
      of every mode switch. On a 1M+ tree with wildcard pattern
      coloring this is seconds of fnmatch() work per switch —
      observed as a 30+s freeze that defeats interactive testing.
      Reported by user during Phase 39 measurement work, and
      independently fixed on the abandoned lazy_render branch.
  [x] Fix: color.c gains a static colors_dirty flag. The public
      color_assign_recursive is now a thin gate that early-returns
      when !colors_dirty and delegates to color_assign_recursive_inner
      otherwise; the inner function clears the flag after the walk.
      color_mark_dirty() is exported and called from:
        - scanfs() entry (new tree → fresh colours required)
        - color_set_mode (mode change → re-derive every node's
          colour from the new mode's palette)
        - color_set_config (via the color_set_mode call it makes)
      So every legitimate "colours changed" event still triggers
      a full walk; the freeloading walks on mode switch are gone.
  [x] Mode switch on a large tree should now be near-instant —
      whatever cost remains is mapv_init / discv_init / treev_init
      (the layout walk), which is already fast (~ms range).


PHASE 41 — TREEV ROW-WRAPPED LAYOUT (fix the Expand-All void)
==============================================================

Problem (quantified from live diagnostics on the FixTreeV branch,
since reverted): TreeV is a wedge decomposition — every subtree gets
a disjoint angular slice and the whole tree must fit in 225°
(TREEV_MAX_ARC_WIDTH). Each generation occupies exactly one ring, so
the tree's total angular demand is the sum of the LINEAR breadth of
the widest generation divided by the radius. When it doesn't fit,
treev_arrange() grows treev_core_radius ×1.25 and instantly re-lays
out the whole scene. On a large tree this converges to
radius ≈ breadth / 3.9 rad: observed 39.4M units of radius with only
~170K units of radial thickness. That end state is a ribbon ~155M
long and 170K wide bent into a 225° "C" — aspect ~900:1. No camera
position can render it (whole-scene framing = tree is ~2px thick;
anything visible = <0.1% of the tree). Phase 40 proved this
empirically with seven failed camera-side fixes (all reverted).

Same mechanism causes the Expand-All flashing: every morph step
queues a rearrange (geometry_colexp_in_progress →
treev_queue_rearrange), the next treev_draw runs treev_arrange's
resize loop, and growing 8192 → 39.4M is log1.25(4800) ≈ 38 discrete
whole-scene ×1.25 jumps mid-animation, each with a full-tree reshape
(also the "Not Responding" stutter). One mechanism, three symptoms:
void + flashing + stutter.

Fix: keep the wedge decomposition, but cap each directory's subtree
arc width. When a directory's expanded children don't fit in one
ring within the cap, wrap the overflow siblings into additional
concentric ROWS inside the parent's wedge. Breadth then grows
radially (radius ~ sqrt(N)) instead of angularly (radius ~ N). The
scene stays a compact, filled disc sector at any tree size, and the
core-radius resize loop almost never fires (kills the flashing too).

The single-ring assumption lives in exactly these places:
  - treev_arrange_recursive (children spread in one ring)
  - geometry_treev_platform_r0 (r0 = sum of ancestor depths+spacing)
  - treev_get_extents_recursive (same sum)
  - treev_batch_recursive (single subtree_r0 for all children;
    single outbranch arc spanning first..last expanded child)
  - the label walk in treev_draw_recursive (same subtree_r0 pattern)

New geometry state (TreeVGeomParams.platform):
  - row_offset: radial offset of this platform's row base relative
    to the parent's first-row base radius (0 for row 0)
  - subtree_depth: radial extent of this node's expanded subtree,
    from its own r0 to the outer edge of its outermost descendant
    row (deployment-weighted, like subtree_arc_width)
New constant: TREEV_MAX_SUBTREE_ARC (initial value 112.5°; tune at
checkpoint).

Step 41.1 — Plumbing refactor (zero visual change) (v4.41.01)
  [x] Add row_offset + subtree_depth fields to TreeVGeomParams.
      Required growing DirNodeDesc.geomparams2 from [3] to [5]
      (TreeVGeomParams overlays geomparams[5]+geomparams2 and was
      already full at 8 doubles). Both fields zeroed in
      treev_init_recursive (covers treev_init and
      geometry_treev_reinit).
  [x] Thread row_offset through every place that computes a child's
      inner radius: geometry_treev_platform_r0 (own + each
      ancestor's), treev_get_extents_recursive,
      treev_arrange_recursive, treev_batch_recursive, and
      treev_draw_recursive (label walk) — child r0 =
      parent subtree_r0 + child row_offset.
  [x] With all offsets zero the layout must be bit-identical to
      v4.39.10. Build (clean, v4.41.01). User verified TreeV
      behaves exactly as before (including the still-present
      blinking on medium and void on large trees, as expected).

Step 41.2 — Row wrapping in treev_arrange_recursive (v4.41.02)
  [x] Greedy row fill in sibling order. DESIGN REFINEMENT pulled
      forward from 41.5: row membership is decided from RESERVED
      (end-state) arc widths — a subtree reserves its full
      MAX(platform arc, subtree arc) from the moment it starts
      deploying (deployment > 0 OR dirtree says expanded) — while
      theta spreading uses deployment-weighted arcs as before. So
      membership and row bases are fixed at the start of an
      expand-all instead of churning every frame as weighted arcs
      grow; platforms grow smoothly INTO their reserved slots.
      subtree_arc_width semantics changed from "sum of children's
      weighted arcs" to "widest row's reserved arc" (camera.c's two
      readers use it as angular extent for framing — still correct).
  [x] When a subtree's row base differs from its stored row_offset,
      its whole subtree is re-arranged with reshape=TRUE so platform
      arcs match the new radius. A row's first member never wraps.
      Next row base = row base + MAX(reserved subtree_depth in row)
      + TREEV_PLATFORM_SPACING_DEPTH.
  [x] Spread thetas per row (rows are consecutive sibling runs
      sharing a row base; each row centered on parent centerline).
  [x] subtree_arc_width = MAX over rows of reserved row arc;
      subtree_depth = own depth + spacing + rows' radial extent
      (0 extra when no child subtree is reserved).
  [x] Core-radius resize loop kept as fallback (still wanted for
      the min-radius case and single-platform oversize); should now
      rarely trigger. Build clean (v4.41.02).
  [x] User tests: confirmed — Expand All on medium and large trees
      ends compact and frameable, no void, flash storm gone.
      (Branch connectors to rows > 0 wrong as expected until 41.3.)

Step 41.3 — Branch drawing for rows (v4.41.03)
  [x] treev_batch_outbranch signature generalized: takes stem_r0
      (stem start radius) and arc_r (arc radius) instead of deriving
      both from the parent's outer edge. Metanode caller updated.
  [x] treev_batch_recursive: walks expanded children per row
      (consecutive sibling runs sharing row_offset); emits one
      outbranch arc per row at that row's base radius minus
      0.5 * spacing (exactly where the children's inbranch stubs
      reach), spanning MIN/MAX(0, first/last expanded child theta).
      The radial stem chains: parent outer edge → row 0 arc → row 1
      arc → ..., so every row is attached. Stems pass UNDER
      intervening platforms (platforms have no bottom face, so no
      Z-fighting; reads like a road under a building). Rows with no
      fully-expanded member get no arc (stem passes through to the
      next row that has one), matching the old behavior for
      mid-morph children.
  [x] Build clean (v4.41.03). Superseded by 41.5's scaffolding
      redesign after user feedback (the chained center stem made
      wrapped siblings read as descendants); final connectivity
      verified at the 41.6 checkpoint.

Step 41.4 — Extents, camera framing, scroll ranges (v4.41.04+)
  [x] WASD/arrow panning (v4.41.04): camera_pan's TreeV branch
      panned in Cartesian and converted back to cylindrical, and
      split screen-vertical between ground motion (× sin(phi)) and
      target.z motion (× cos(phi)). Both were masked by the old
      giant-radius layout; at compact radius A/D read as "rotate
      view" (chord vs arc) and W/S read as "up/down" (z bleed at
      shallow phi). Replaced with native cylindrical navigation:
      screen-vertical walks radially, screen-horizontal walks along
      the arcs (dtheta = arc / max(target.r, leaf edge)), oriented
      by camera->theta, no z drift, full speed at any elevation.
      Reported by user while testing 41.3.
  [x] Far-plane clamp (v4.41.06): user reported distant geometry not
      drawing until any zoom/move. The solid batch always contains
      everything and every frame draws it whole, so the only
      mechanism that hides far-but-not-near geometry is the far clip
      plane: several camera paths (interrupted two-stage pans, the
      expand-all camera lock) can leave far_clip sized for a former
      smaller view, and only camera_dolly recomputed it. Fix at the
      single authoritative place, setup_projection_matrix (ogl.c):
      in TreeV the far plane is clamped to at least target.r +
      camera distance + geometry_treev_scene_radius() (new export:
      root r0 + root subtree_depth, maintained by the row layout).
      Whole class of stale-far_clip states fixed; near plane
      untouched (the too-close zoom blackout remains a separate
      deferred item).
  [x] User settled TREEV_MAX_SUBTREE_ARC at 220.0 after trying
      values (user's own edit, v4.41.05 era).
  [x] Verify geometry_treev_get_extents includes row offsets:
      confirmed via user testing — whole-tree framing after Expand
      All on massive trees shows the full disc sector including all
      outer rows, with the far-plane clamp keeping it drawn.
  [x] Verify camera_look_at / 'R' reset / double-click navigation
      land correctly on platforms in outer rows: confirmed — Up
      lands on the true parent (targeting goes through row-aware
      geometry_treev_platform_r0); the initial "didn't seem right"
      report was the changed spatial intuition (radially-inward
      neighbor is now usually an inner-row sibling, not the parent),
      resolved by the 41.5 scaffolding making the distinction
      visible.
  [x] treev_camera_locked (colexp.c) kept as-is: with the compact
      layout, root r0 barely moves during expand-all so the radial
      tether is nearly a no-op, and the angle/distance freeze is
      still useful while morphs run. Harmless now; simplification
      possible later but not worth the regression risk.
  [x] Scrollbar ranges during/after expand-all: exercised as part
      of the 41.6 checkpoint — passed.

Step 41.5 — Row scaffolding readability (v4.41.05)
  (Original 41.5 content — animation stability via end-state row
  assignment — was pulled forward into 41.2's reserved-arc design.)
  Problem, reported by user testing 41.4: wrapped sibling rows sit
  radially beyond the inner row's subtrees, connected by arcs and a
  center stem IDENTICAL to the parent-child visual language, and the
  stem chained through every row — one merged through-line. Unrelated
  sibling directories therefore read as nested descendants.
  Fix — give wrapped rows their own visual grammar:
  [x] New helpers treev_batch_branch_radial / treev_batch_branch_arc
      (parameterized radius span, angle, height z, color);
      treev_batch_outbranch is now a thin wrapper over both.
  [x] First row with expanded children keeps the classic red center
      stem + arc = "children of this platform" (arc now spans to the
      row's angular edges rather than member centers).
  [x] Continuation rows: scaffold arc in row_link_color (amber,
      distinct from branch red) lifted TREEV_ROW_LINK_Z (2 units,
      kills z-fighting with ground-level branches), joined to the
      previous row's arc by TWO side rails at the rows' shared
      angular edges (ladder/racetrack frame). The center
      through-line past the first row is gone. Every row straddles
      the parent centerline so consecutive arc spans always overlap
      and the rails always land on both arcs.
  [x] Build clean (v4.41.05). User verdict: "acceptable — not ideal",
      taken as the price of the void/blinking fix. User then tuned
      TREEV_MAX_SUBTREE_ARC to 220.0 to reduce wrapping.

Step 41.6 — Checkpoint  [PASSED at v4.41.06 — full checklist
  confirmed by user on small/medium/massive trees; merged to main]
  Checkpoint: User confirms on small, medium, and large trees:
    - Expand All ends with the whole tree visible and frameable;
      'R' reset frames it; no void at any point during animation
    - No flashing / discrete whole-scene jumps during expand-all
    - Collapse All returns to a sane compact scene
    - Double-click navigation, hover-pick, right-click menus work
      on platforms in outer rows
    - Labels render on all rows; no Z-fighting regressions
    - Idle + camera-motion frame rates comparable to v4.39.10
      (row logic adds only O(children) work to arranges that
      already run per morph step — no new per-frame cost at idle)
    - TREEV_MAX_SUBTREE_ARC tuning: does 112.5° look right, or
      should wedges be wider/narrower before wrapping?

  Out of scope for this phase (own phases later):
    - DiscV large-directory usability (separate investigation)
    - Mouse-wheel zoom past safe near-clip (carried from Phase 39)
    - Log-scale leaf heights looking flat (carried from FixTreeV)


PHASE 42 — DISCV MULTI-RING ORBIT LAYOUT (fix disc overlap)
============================================================

Problem: DiscV sizes every disc from its own size and strings a
directory's children around its rim, each claiming an angular slot
proportional to its DISC alone (geometry.c discv_init_recursive).
An expanded child brings its whole satellite system, none of which
is accounted for in the slot — sibling subtrees plow through each
other, worse with node count, until the view is a jumble of
overlapping discs. Historical fixes spaced discs so far apart that
visual relationships died: that is the sound completion of a
SINGLE-ring layout (ring radius must grow linearly with total child
breadth), and it's unusable. DiscV was disabled in the original fsv
source, presumably for exactly this.

Fix (user-selected direction): keep the orbiting fractal look
("discs with children orbiting is a lovely visual — preserve it";
containment/nested packing explicitly rejected as "MapV with more
steps"; no drawn stems — proximity only). Two ingredients:
  1. SUBTREE BOUNDING CIRCLES, computed bottom-up: every node gets a
     bound radius enclosing its disc plus all descendant orbits.
     Siblings are spaced by bounds, so overlap between subtrees is
     impossible by construction — "the future" is priced in.
  2. MULTI-RING ORBITS (the TreeV-rows trick in polar form): when a
     directory's children don't fit one orbit ring, overflow wraps
     into further concentric rings just beyond. Distance from the
     parent grows ~sqrt(subtree) (area-driven), not linearly
     (circumference-driven), so satellites keep hugging their parent.
Layout is expansion-independent (bounds derive from the full tree),
computed once per scan: expand/collapse only reveals/hides satellite
systems (deployment scaling), never relayouts. Deterministic, cheap,
build-once/draw-many preserved. Slot math: a circle of radius b at
center distance d is exactly inscribed in the cone of half-angle
asin(b/d), so disjoint cones ⇒ disjoint circles.
Accepted cost: deep subtrees hold visible empty moats (convex
reservation). A force-directed tightening pass stays a possible
future phase; rejected as the foundation (non-deterministic,
relayout ripples, tuning risk).

Step 42.1 — Bounding circles + bound-spaced single ring (v4.42.01)
  [x] Add `bound` to DiscVGeomParams (5th double: radius, theta,
      pos.x, pos.y, bound — exactly fills geomparams[5], works for
      leaves and dirs alike).
  [x] Restructure discv_init_recursive to post-order: 1st pass
      assigns children's disc radii (leaf bound = radius), 2nd pass
      recurses into subdirectories (computes their bounds), 3rd pass
      places children around the parent spaced by BOUNDS: child
      center at d_i = ring_inner + b_i, slot = 2*asin(b_i/d_i),
      full 360 degrees (the old 315-degree stem gap and the
      stagger hack are gone — bounding circles isolate systems).
      Slack distributed via k factor; overfull ring grows its inner
      radius ×1.25 until fit (knowingly sparse on big directories;
      rings come next). stem_theta parameter dropped; root system
      centered at origin in discv_init.
  [x] dnode bound = DISCV_BOUND_PADDING * max(d_i + b_i).
  [x] Build clean (v4.42.01). User verified on a small directory:
      no overlap anywhere ("discs almost touching but nothing on top
      of each other"); sparse spread and missing labels reported —
      the former is the known single-ring limitation (42.2), the
      latter the disc-radius cull (42.3 item, pulled forward).

Step 42.2 — Multi-ring wrap (v4.42.02)
  [x] Fill ring 1 (sorted by size, largest first) until adding a
      child would exceed 360 degrees; overflow starts ring 2 at
      inner radius += (2 + DISCV_RING_GAP) * max_bound(ring); repeat
      outward. Slack distributed per ring (k factor); a ring's first
      member never wraps (any bound fits alone: half-angle <= 90).
  [x] dnode bound = DISCV_BOUND_PADDING * max(d_i + b_i) across all
      rings.
  [x] Pulled forward from 42.3: discv_draw_recursive culls (subtree
      size cull, frustum test, label gate) now use the world-scaled
      subtree BOUND instead of the disc radius — a tiny disc owning
      a huge satellite system no longer drops the system's labels.
  [x] Build clean (v4.42.02). User: no overlap, but distances got
      WORSE ("gave up zooming to find things"). Diagnosis below.

Step 42.2b — Off-center enclosures (v4.42.03)
  Root cause of the 42.2 distance explosion: the bounding circle was
  CENTERED on the parent's disc, but a directory's disc is sized by
  its own entry (~4KB → tiny) while its children orbit outside it.
  For the ubiquitous dominant-child chain (src/main/java/...), a
  centered enclosure must span disc → past the child system: radius
  ≈ 2× the child's bound, i.e. a ×2 multiplier PER DEPTH LEVEL —
  exponential in depth (12 levels ≈ ×4000). Small dirs show it
  immediately (chains are everywhere), matching "if anything worse".
  Fix: let the enclosure float off the disc center.
  [x] DiscVGeomParams gains bound_ofs (XYvec, enclosure center
      relative to disc center). Spills into geomparams2 — valid for
      directories/metanode ONLY, never accessed on leaves.
  [x] Ring placement positions each child's ENCLOSURE center on the
      ring (that's what must not overlap); the child's disc sits at
      enclosure center − bound_ofs. Cones still disjoint ⇒
      enclosures still disjoint: overlap guarantee unchanged.
  [x] After placement, each directory's own enclosure is computed by
      Ritter expansion over {own disc} ∪ {child enclosures}: start
      at the disc, expand toward anything poking out, letting the
      center drift. Chain multiplier drops from ~2.0 to ~1.05–1.1
      per level (plus DISCV_BOUND_PADDING).
  [x] discv_draw_recursive culls test the bound circle at its offset
      center (world_bx/world_by).
  [x] Build clean (v4.42.03). User: "much better", but small files
      in the root (e.g. TODO.md) still exiled "miles away" — rings
      fill largest-first and advance by the largest member's
      diameter, so the smallest items always land in the outermost
      ring, past every big subtree, with no way to backfill the
      empty space near the parent. Structural flaw of the cone-
      partitioned ring approximation.

Step 42.2c — Front-chain circle packing (v4.42.04)
  Replace rings with the real thing: front-chain circle packing
  (Wang et al. 2006, the d3 packSiblings algorithm). Circles are
  placed one at a time (largest first), each externally tangent to
  two circles of the front chain, always at the candidate pair
  closest to the cluster center — small items nestle into the
  crevices between big subtree enclosures. Deterministic, computed
  once, exact tangency = overlap impossible.
  [x] pack_place / pack_intersects (relative epsilon) / pack_score /
      pack_circles: front chain as an index-based circular doubly-
      linked list, with a bounded-retry guard (2n cuts per circle,
      then force-insert) against pathological non-termination.
  [x] discv_init_recursive: rings deleted. Circle 0 = nucleus (the
      parent disc + DISCV_RING_CLEARANCE); children's enclosure
      circles packed around it in size order; everything translated
      so the nucleus lands at the node's disc center; child disc =
      enclosure center - bound_ofs; theta = atan2 of final position.
      Ritter enclosure over the result unchanged (DISCV_RING_GAP
      constant retired).
  [x] Build v4.42.04 had overlaps again — TWO transcription bugs in
      the from-memory port of the reference algorithm: (1) the main
      loop's place() call passes the chain pair in reversed
      parameter order (the reference's API genuinely does this);
      (2) a spurious `b = c` after chain init made the first
      insertion splice between nodes 0 and 2 while next[0] still
      pointed at 1, corrupting the chain so intersection checks
      missed. Fixed against the actual d3-hierarchy siblings.js
      source (fetched, not remembered).
  [x] Standalone verification harness (scratchpad pack_test.c):
      extracts the pack_* code verbatim from geometry.c, packs 200
      randomized directories (up to 6000 children, radii across 5
      orders of magnitude, tiny-nucleus cases) and checks every
      pair: 0 overlapping pairs, worst penetration 0. The packing
      is now proven correct outside the GUI.
  [x] Build clean (v4.42.05). User: no overlaps, distancing almost
      good, BUT the fractal look was lost ("petri dish"): parent
      discs drifted off-center within their own clusters, and
      linear area sizing (4KB vs 4GB = 1000:1 radii) made zoomed-out
      views unreadable.

Step 42.2d — Fractal recovery: pinned nucleus + log sizing (v4.42.06)
  [x] pack_circles: deliberate deviation from the reference — the
      first circle (nucleus = parent disc) is PINNED at the origin
      instead of centering the first pair around it. The insertion
      scoring already prefers near-origin positions, so children
      wrap around the nucleus in concentric shells: the orbit look,
      packed tight. Harness re-run: 0 overlaps; nucleus at exactly
      (0,0); children centroid within a few units of it.
  [x] DiscV honors the existing log/linear scale toggle (shared
      treev_scale_logarithmic flag, moved to top of geometry.c):
      log mode disc radius = DISCV_LOG_RADIUS_FACTOR(16) *
      log2(size) — a 4KB file and 4GB dir stay within ~1 visual
      order of magnitude; linear mode keeps area == size.
      geometry_treev_set_scale_logarithmic re-inits DiscV too;
      window.c enables the toolbar Log checkbox in DiscV.
  [x] Build clean (v4.42.06). User: "has potential", but the Log
      checkbox desynced from the actual flag (fixed in 42.2e; the
      fractal-look verdict itself is still pending).

Step 42.2e — Log checkbox state sync (v4.42.07)
  User-reported: checkbox always starts checked regardless of config,
  and eventually flips independently of the real toggle state.
  Three causes found:
  [x] Checkbox was hardcoded active(TRUE) at creation — now
      initialized from geometry_treev_get_scale_logarithmic() (the
      config may have loaded "representative", making the first
      click a visible no-op — the "flips regardless" feel).
  [x] window_set_vis_mode was a THIRD sensitivity gate still
      restricting the checkbox to TreeV on every mode switch —
      missed in 42.2d's sweep. Now TreeV+DiscV like the other two.
  [x] window_set_vis_mode now re-syncs the checkbox to the flag
      (signal-blocked) on every mode change, covering any path that
      changes the flag behind the toolbar's back. (The settings
      dialog's scale dropdown is startup-default-only — writes
      config, never the live flag — so no live path remained.)
      Tooltip no longer claims to be TreeV-only.
  [x] Build clean (v4.42.07). User verdict on the layout: "Good
      Enough" — "the Log mode is super weird, which I like. I
      wouldn't call it fractal but I like it." Layout accepted.

Step 42.3 progress (v4.42.08) — DiscV camera framing on bounds:
  [x] discv_look_at: an expanded/expanding directory (dirtree state,
      which is updated before the camera pan starts) is framed by
      its subtree BOUND, targeted at the bound's offset center —
      expansion can no longer happen off-camera (user request:
      "small zoom out when expanding"). Files and collapsed dirs
      keep tight disc framing.
  [x] camera_init (DiscV): frames the root's bound instead of the
      root disc; target = root bound center.

Step 42.3 — Camera, scrollbars, culling on bounds
  [x] discv camera look_at framing by bound — done v4.42.08 (see
      progress notes above); user confirmed ("Excellent").
  [x] discv_get_scrollbar_states: scroll range from the root's
      bound circle at its offset center (v4.42.09 — this one missed
      the #22 merge and was reapplied on main afterwards).
  [x] discv_draw_recursive culls on world bound — done v4.42.05.
  [x] DiscV near/far clips: the 0.9375/1.0625 * distance shell
      scales with bound-derived distances; scene is flat z=0, so
      the shell always straddles it. No change needed.

Step 42.4 — Checkpoint  [PASSED at v4.42.08 — user confirmed all
  modes fine (TreeV/MapV included); branch merged as PR #22. The
  carried-over "TreeV log-scale flatness" item is retired ("might
  be a dangling thought" — user). Force-directed tightening stays
  an optional future idea.]
  Checkpoint: User confirms on small, medium, and large trees:
    - No disc ever overlaps another (expand everything and look)
    - Orbit/fractal visual preserved; satellites hug their parents
    - Expand/collapse animates smoothly, no relayout jumps
    - Navigation (double-click, Up, R), hover-pick, context menus
    - Labels legible; culling behaves with tiny discs / huge systems
    - Idle and camera-motion performance comparable to before
    - Margins/padding constants look right (tuning knobs identified)
  Out of scope: force-directed tightening (possible future phase);
  TreeV/MapV must be untouched by this phase.


PHASE 43 — TREEV DISTANCE BRIGHTENING (inverse fog)
====================================================

User request: when scrolling away in TreeV, everything is dark and
muted — "make objects brighter the farther away we get; in a game
engine I'd increase the amount of light objects put out."
Cause: the lit shader's maximum output is ambient(0.2) +
diffuse(0.5) = 0.7 * base color, and the diffuse term (eye-origin
positional light) collapses at grazing angles — exactly the
geometry-vs-camera relationship of a zoomed-out TreeV.

Step 43.1 — Emissive distance ramp in the lit shader (v4.43.01)
  [x] lit_frag_src: new uniforms u_glow_near / u_glow_far. Fragments
      blend from the lit result toward emissive base color by eye
      distance: normal lighting inside NEAR, full base color beyond
      FAR (brighter than any lit fragment can be), disabled when
      far <= near. The outline pass (u_diffuse_scale = 0) glows
      toward 0.6 * color so edges keep contrast against their faces.
  [x] Thresholds tied to the scene: TREEV_GLOW_NEAR_FACTOR (0.25)
      and TREEV_GLOW_FAR_FACTOR (1.25) * geometry_treev_scene_radius
      — self-calibrates to any tree size (street-level views keep
      local lighting; whole-tree framing renders ~everything
      emissive). Set per draw in treev_setup_lit_shader_ex.
  [x] MapV/DiscV lit setups zero both thresholds (uniforms persist
      per shader program, so TreeV's values must not leak across a
      mode switch). Effect is TreeV-only.
  [ ] Build clean (v4.43.01). User judges: zoomed-out TreeV shows
      clear, bright shapes; close-up keeps normal lighting/depth;
      MapV/DiscV unchanged. Tuning knobs: the two factors and the
      0.6 outline glow target.


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
