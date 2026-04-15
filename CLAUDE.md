# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

FSV (File System Visualizer) is a 3D file system visualizer written in C that renders directory hierarchies in three dimensions. Inspired by SGI's `fsn` from Jurassic Park.

This codebase starts as a GTK3 + legacy OpenGL (fixed-function pipeline) application. The goal is to migrate it to modern OpenGL 3.3 core profile with batched VBOs and shaders, then to GTK4. The migration plan is defined in `TODO.md`.

## Mandatory Rules

These rules are **non-negotiable**. Violating any of them will result in broken, untestable, or degraded software. Read and follow them exactly.

### 1. The program MUST build and run after every TODO item

After completing ANY item in `TODO.md`:
1. Run `ninja -C build` — it must compile with zero errors.
2. The user must run `./build/fsv ~/src` (or another directory) — it must launch, display the 3D visualization, and be interactive. Ask the user to verify functionality.
3. If the build fails or the program crashes, or the user reports issues, **the item is not done**. Fix it before moving on.

Do NOT mark an item complete if the program does not build and run. Do NOT skip ahead to later items. Do NOT batch multiple items and test at the end.

### 2. Follow TODO.md in strict order

The phases in `TODO.md` are ordered deliberately. Each phase builds on the previous one. **Do not skip phases or reorder items.** Specifically:
- All OpenGL modernization (Phases 1-5) MUST be completed and tested BEFORE switching from GTK3 to GTK4 (Phase 6).
- Do not start a new phase until the current phase's checkpoint passes.

### 3. Mark items complete in TODO.md as you go

When an item is done (builds, runs, tested), change its checkbox in `TODO.md` from `[ ]` to `[x]`. This is how progress is tracked. If you don't update TODO.md, there is no record of what was actually done.

### 4. Stop at phase checkpoints and ask the user to test

At the end of each phase in `TODO.md` there is a **Checkpoint** block. When you reach it:
1. Tell the user the phase is complete.
2. Tell them exactly what to test (the checkpoint describes this).
3. **Stop and wait for the user to confirm** that the program works before starting the next phase.

Do NOT proceed past a checkpoint without user confirmation.

### 5. Do not degrade performance

The rendering pipeline must use a build-once, draw-many pattern. Geometry must be cached and only rebuilt when it actually changes. Specifically:
- Do NOT rebuild or re-upload vertex data every frame.
- Do NOT replace cached drawing with per-frame CPU vertex assembly.
- VBO batches must use `GL_STATIC_DRAW` with dirty-flag invalidation (build once, draw many, rebuild only when stale).
- The goal is to reduce draw calls: batch all visible geometry into a small number of large VBOs (ideally 1-3 per frame) rather than thousands of individual draw calls.
- If you are unsure whether a change impacts performance, say so and ask.

### 6. Do not claim work is done that isn't done

If a function still contains `glBegin`/`glEnd`, it has not been converted to VBOs. If `glMatrixMode` is still called at runtime, legacy GL has not been removed. If the program crashes on launch, the phase is not complete.

Verify your own work:
- After converting GL code, `grep` for the legacy calls you claim to have removed. If they're still there, you're not done.
- After a phase that removes legacy GL, confirm with `grep -rn 'glBegin\|glEnd\|glVertex\|glMatrixMode\|glNewList\|glCallList' src/` that they are actually gone (comments excluded).

### 7. Preserve existing functionality and visuals

The program must look and behave the same after each change. All three visualization modes (DiscV, MapV, TreeV) must render correctly. Mouse interaction (click, double-click, right-click context menu, drag to orbit, scroll to zoom), keyboard panning (arrow keys/WASD), directory tree sidebar, file list, search, color modes, and About screen must all work.

If something breaks, fix it before proceeding.

## Build Commands

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install libgtk-3-dev libepoxy-dev libcglm-dev meson ninja-build

# Configure and build
meson setup build
ninja -C build

# Run without installing
./build/fsv [directory]

# Build with debug enabled
meson setup build -Dfsv_debug=true
ninja -C build

# Reconfigure existing build
meson configure build -Dfsv_debug=true
ninja -C build

# Install
sudo ninja -C build install
```

Build system is Meson (>= 0.55). There is no test suite or linter configured.

## Dependencies

- **gtk+-3.0** (>= 3.16) — GUI framework (will become gtk4 in Phase 6)
- **libepoxy** — OpenGL function loading
- **cglm** — C OpenGL mathematics library (added in Phase 1)
- **libm** — math library
- **file** command (optional) — MIME type detection

## Architecture

### Visualization Modes

Three 3D layout modes defined in `geometry.c`:
- **DISCV** (Disc View) — radial/pie layout
- **MAPV** (Map View) — 2D rectangular tilemap
- **TREEV** (Tree View) — 3D tree with platforms

### Core Data Flow

1. **`fsv.c`** — Entry point, initializes GTK and starts filesystem scan
2. **`scanfs.c`** — Scans filesystem into a GLib GNode tree
3. **`geometry.c`** — Computes 3D positions for each node based on active visualization mode
4. **`ogl.c`** — OpenGL rendering via GtkGLArea
5. **`animation.c`** — Morphing system: time-based interpolation of variables with easing and scheduled callbacks
6. **`viewport.c`** — Input handling (mouse interaction, keyboard panning, node selection)
7. **`camera.c`** — Camera positioning and movement

### UI Layer

- **`gui.c`** / **`window.c`** — GTK main window and widget construction
- **`dirtree.c`** — Directory tree sidebar widget
- **`filelist.c`** — File list panel
- **`callbacks.c`** — GTK signal handlers
- **`dialog.c`** / **`search.c`** / **`about.c`** — Dialog windows

### Key Subsystems

- **`common.c/h`** — Global state (`globals` struct: fstree, current_node, history), shared types, and node data structures
- **`color.c`** — Per-node RGB color management with config storage
- **`colexp.c`** — Collapse/expand animation for directory nodes
- **`tmaptext.c`** — Texture-mapped text rendering in OpenGL
- **`lib/nvstore.c`** — Name-value pair storage for user configuration (`~/.config/fsv/fsvrc`)

### OpenGL Rendering Pipeline

The codebase starts with a legacy fixed-function pipeline using per-directory display lists with stale flags. The migration (Phases 1-5) replaces this with:
1. **Batched VBOs** — all visible geometry packed into a small number of large vertex buffers, rebuilt only when geometry changes (dirty-flag invalidation)
2. **Shaders** — vertex and fragment shaders for lit rendering and color-pick rendering
3. **Matrix utilities** (via cglm) — replace the legacy `glPushMatrix`/`glPopMatrix` stack
4. **Pick FBO cache** — color-pick rendering cached until camera/scene changes

The key performance principle: geometry is built once and drawn many times. Rebuilds happen only when the scene changes (expand/collapse, color mode change), not every frame.

### Animation System

`redraw()` triggers `animation_loop()` as an idle callback, which each frame calls:
- `morph_iteration()` — updates dynamically changing variables
- `ogl_draw()` → `geometry_draw()` — dispatches to mode-specific drawing functions
- `framerate_iteration()` — tracks framerate
- `scheduled_event_iteration()` — fires registered timed callbacks

## New Modules (added during migration)

These files are created during Phase 1 and used throughout:
- **`src/glmath.c/h`** — Matrix stack utilities wrapping cglm (projection, modelview, MVP, normal matrix)
- **`src/shader.c/h`** — Shader compilation, linking, uniform location caching
- **`src/vbobatch.c/h`** — VBO/VAO management (vertex format, batch building, upload, draw, dirty flags)

## Conventions

- GLib/GTK naming: `g_` prefix for GLib functions, snake_case for all functions
- Header guards: `FSV_<MODULE>_H`
- Filesystem tree represented as GLib GNode structures throughout
- Vertex data cached in VBO batches with dirty flags for invalidation
