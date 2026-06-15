# PixelMapper: Architecture Critique & 1-Week Sprint Plan

> **Context:** 12 × vertical Line fixtures, 36px × 4ch RGBW each → **1,728 bytes / ~4 Art-Net universes**,
> multiple Art-Net nodes, GLSL shaders as the primary effects engine, visual canvas-first workflow.
> Written: 2026-06-15.

---

## Part 1: Architecture Critique

### What the design gets right

| Strength | Detail |
|---|---|
| **Clean ECS-driven pipeline** | Flecs observers/systems form a correct dirty-flag cascade. Changing a fixture's DMX address automatically propagates all the way to recompile — elegant. |
| **Lock-free hot-swap via `PatchProgram`** | Compiling a new program and atomically pointer-swapping it is the right model. The RT thread never touches ECS. |
| **VFB + bilinear sampling** | Decoupling the "effect domain" (a 2D grid) from the "physical domain" (arbitrary pixel positions) is a genuinely smart design that handles non-grid layouts. |
| **Three render modes in one architecture** | Sharing the same VFB → encode path for C++, Lua, and GLSL is elegant code reuse. |
| **Isolated Lua VM per PatchProgram** | Correct approach — avoids all cross-thread Lua state corruption. |

### Critical gaps & design issues

#### 🔴 No patch persistence (save/load)
TinyXML2 is a dep but **completely unused**. With 12 carefully-placed fixtures this is a **show-stopper** — you cannot lose your patch data each session.

#### 🔴 GLSL renders on the RT thread — with a shared OpenGL context
The GLSL path (`glBindFramebuffer`, `glDrawArrays`, `glReadPixels`) runs inside `runPatch()` using `sharedContextWindow`. This is the most dangerous part of the codebase:
- `glReadPixels` is a GPU pipeline stall — it blocks until the GPU finishes. On a 40 Hz cycle this introduces significant jitter.
- The shared GL context means the RT thread competes with the main thread's ImGui rendering. On macOS with a Core Profile context, this is particularly fragile.
- **The GLSL FBO is created lazily on the RT thread** (in `render()`), but VAO/VBO setup also happens there. GL object creation on a non-main thread is legal but risky without careful sync.

**Recommended fix:**
- Move shader *compilation* (compile, link, FBO/VAO setup) to the **main thread** — do it inside `PatchProgramCompile` before pushing the program.
- Keep only `glDrawArrays` + `glReadPixels` on the RT thread (needs the shared context).
- Add a `shaderReadyToRender` flag the RT thread checks before using the FBO.
- Replace `glReadPixels` with a **PBO (Pixel Buffer Object)** async readback to eliminate RT jitter.

#### 🟡 No file-watch / hot-reload from disk
The "Compile & Save" button saves to disk and recompiles, but **editing the file externally does nothing** — the app doesn't watch the file for changes.

**Recommended fix:** Poll `std::filesystem::last_write_time` on script paths once per second from the main thread. On change, reload source into `ScriptData` and set `ProgramDirty`. Also add a "Reload from Disk" button as explicit fallback.

#### 🟡 Single fixture selection only
With 12 vertical fixtures, the canvas has no multi-select, no marquee selection, no group-drag. This limits batch repositioning and patching.

#### 🟡 `main.cpp` hardcodes a test patch
The startup creates two hardcoded patches. On first run the user sees garbage data. Should be replaced by: load-from-disk on launch, or an empty patch on first run.

#### 🟡 ArtnetSender binds source port 6454
This is the standard Art-Net *receive* port. Binding *from* port 6454 may conflict if any Art-Net listener is also running on the same machine. Standard practice is to send from an ephemeral port.

#### 🟡 `PatchProgram` stores GLSL GPU objects on the RT thread
When a program is replaced by `pushNewProgram`, `~PatchProgram` calls `glDeleteProgram` etc. on the main thread (since deletion happens after the pointer swap). This is actually correct, but it means GL object lifetimes are split across threads — a subtle invariant that's easy to break.

#### ⚪ `SyncRealtimeDataToECS` holds the lock during nested iteration
The sync system (ECS PreStore) holds `patchProgramLock` while doing two nested fixture iterations. If the RT thread is also trying to acquire the lock at 40 Hz and the main thread is slow (e.g. layout update), lock contention will cause RT jitter. A double-buffer or ring-buffer approach would be better long-term.

#### ⚪ Canvas drag handles only work for the *selected* fixture
Shape handles are rendered only for the selected fixture. You have to click-to-select in the list panel, then drag. This makes repositioning 48 fixtures slow.

---

## Part 2: Development State Assessment

```
Feature                            Status
────────────────────────────────────────────────────────────
ECS pipeline / dirty flags          ✅  Complete & well-designed
Fixture CRUD (Line / Circle)        ✅  Working
DMX mapping / universe alloc        ✅  Working
PatchProgram compile                ✅  Working
RT thread + ArtnetSender            ✅  Working (functional)
C++ sine-wave render                ✅  Working (placeholder)
Lua VM + Canvas API                 ✅  Working
GLSL FBO render path                ✅  Working (but fragile on RT thread)
VFB → bilinear → DMX encode         ✅  Working, well-designed
VFB preview in canvas editor        ✅  Working
Script editor (TextEdit)            ✅  Working (Lua + GLSL tabs)
Error markers in editor             ✅  Working
Fixtures window (merged list+props) ✅  Working
ArtNet Devices window               ✅  Working
Hex universe viewer                 ✅  Working
Save / Load patch (XML)             ❌  Not implemented
File-watch hot-reload               ❌  Not implemented
Multi-select / group drag           ❌  Not implemented
Canvas click-to-select fixture      ❌  Not implemented (only drag handles on selected)
Effect preset library               ❌  Not implemented
Fixture duplication / copy-paste    ❌  Not implemented
```

---

## Part 3: 1-Week Sprint Plan

> **Primary objective:** Ship a stable, usable tool for the 48-fixture installation.
> **Guiding principle:** Visual effect quality first, then workflow ergonomics, then persistence.

### Priority Stack

| Level | Meaning |
|---|---|
| **P0** | Show-stopper — must ship Day 1–2 |
| **P1** | Critical for workflow — must ship by Day 4 |
| **P2** | Very useful — target Day 5–6 |
| **P3** | Polish / nice-to-have — Day 7 if time allows |

---

### P0: Save / Load Patch (Day 1)

**Why critical:** 48 manually-placed fixtures with pixel counts, addresses, and device configs cannot be re-entered every session.

**Approach:** XML serialization using TinyXML2 (already a dep).
- **Serialize:** Patch settings, each Fixture (name, shape type, shape params, layout, dmxAddress), each ArtNet Device (ip, startUniverse, count), ScriptData (luaSource, glslSource, scriptPaths).
- **Deserialize:** Recreate entities from XML, trigger dirty flags to recompile.
- **UI:** File menu → Save Patch / Load Patch. Start with a hardcoded default path (e.g. `patches/default.xml`) to keep it simple.
- On launch: try to load `patches/default.xml`; if it doesn't exist, create one empty patch.

**Files to create/modify:** `src/PatchSerializer.h`, `src/PatchSerializer.cpp`, `src/main.cpp`, `src/gui/PixelMapperGui.cpp` (menu items).

---

### P0: Fix GLSL Threading — Move GL Setup to Main Thread (Day 1–2)

**Why critical:** GLSL is the primary effects engine. It currently compiles shaders and does FBO setup lazily inside `render()` on the RT thread. The `glReadPixels` call stalls the GPU pipeline, introducing frame-time jitter on the live show.

**Approach:**
1. In `PatchProgram::compile()` (main thread), if `renderMode == GLSL`, compile the shader, link the program, create the VAO/VBO and FBO/texture immediately.
2. Set `shaderCompiled = true` before pushing the program.
3. In `render()` on the RT thread, skip the compile block entirely — just render if `glslProgram != 0`.
4. (Stretch) Replace `glReadPixels` with a PBO async readback: map a PBO in one frame, read it the next to avoid the stall.

---

### P1: File-Watch Hot-Reload (Day 2–3)

**Why important:** Edit shaders externally in VS Code, see them update in the app without touching the GUI.

**Approach:**
- Add a lightweight ECS system (runs in `PreStore`, once per second via a timer accumulator) that checks `std::filesystem::last_write_time` on `settings.luaScriptPath` and `settings.shaderPath`.
- If the file timestamp changed since last check, reload the file into `ScriptData`, set `ProgramDirty`.
- Add a **"↺ Reload from Disk"** button in the Script Editor toolbar as an explicit fallback.

**Files:** New `src/FileWatcher.h` (thin utility), `src/App.cpp` (new ECS system).

---

### P1: Canvas Click-to-Select Fixture (Day 2)

**Why important:** With 48 fixtures on a canvas, clicking directly on a fixture to select it is essential. Currently selection is only possible from the list panel.

**Approach:**
- In `WindowPatchEditor`, after rendering all shapes, do a hit test on `ImGui::IsMouseClicked(ImGuiMouseButton_Left)` (when not dragging a handle).
- **Line:** point-to-segment distance test (`< 8px screen threshold`).
- **Circle:** point-in-circle test.
- On hit: call `Fixture::select()`. If no hit on any fixture: clear selection.

**Files:** `src/gui/PixelMapperGui.cpp`.

---

### P1: Multi-Select + Group Drag (Day 3–4)

**Why important:** Placing 48 similar line fixtures requires selecting groups and moving them together.

**Approach:**
- Change the selection model from the exclusive `SelectedFixture` pair to a `std::unordered_set<flecs::id_t>` stored as a component on the patch entity.
- **Shift+click:** Add/remove fixture from selection set.
- **Marquee select:** On drag start on empty canvas → rubber-band rect → select all fixtures whose bounding box intersects the rect on release.
- **Group drag:** When multiple fixtures are selected, dragging any one of them moves all by the same delta (applied via `get_mut` + `PixelPositionsDirty`).
- Properties panel only shows editable properties when exactly 1 fixture is selected; otherwise shows a count summary.

**Files:** `src/Fixture.h`, `src/Fixture.cpp`, `src/gui/PixelMapperGui.cpp`.

> **Simplified fallback:** If time is short, implement Shift+click multi-select + group drag only (skip rubber-band marquee). That alone solves the main pain point.

---

### P1: Clean Startup — Empty Patch on First Run (Day 1)

Remove the two hardcoded test patches from `main.cpp`. On launch:
1. Try to load `patches/default.xml`.
2. If it doesn't exist (first run), create one empty patch with a sensible default name.

---

### P2: Effect Preset Library (Day 4–5)

A small library of ready-to-use GLSL shaders selectable from a dropdown, so you can pull up effects instantly at the installation without writing code.

**Proposed presets:**

| Name | Description |
|---|---|
| `plasma` | Classic sine-based plasma (already the defaultGLSL) |
| `horizontal_sweep` | Bright bar sweeping left → right |
| `vertical_chase` | Bright bar sweeping top → bottom |
| `rainbow_wave` | HSV color wheel scrolling across the X axis |
| `fire` | Upward-moving Perlin fire noise |
| `sparkle` | Random white sparkle/flash |
| `color_solid` | Uniform fill with time-animating hue |
| `breathing` | Full-canvas fade in/out on a sinusoidal cycle |
| `strobe` | Hard on/off at configurable Hz |

**Implementation:** Embed as `const char*` strings in `src/Presets.h`. UI: an `ImGui::Combo` labelled "Load Preset…" in the Script Editor header bar that populates the GLSL editor and triggers a compile.

**Files:** New `src/Presets.h`, `src/gui/PixelMapperGui.cpp`.

---

### P2: Fixture Duplication (Day 5)

Add a **"Duplicate"** button (and `Cmd+D` shortcut) that clones the selected fixture with a small positional offset. This allows placing one line fixture correctly, duplicating it 11 times, and adjusting addresses sequentially.

Bonus: **DMX address auto-pack** button — re-packs all fixture addresses sequentially starting from universe 0 / address 0, sorted by current fixture order.

---

### P2: Effect Sequencer — Multi-Effect Triggering (Day 5–6)

**Why important:** You want to curate a set of effects and switch between them (manually or on a schedule) during the installation, without editing scripts on the fly.

**Core concept:** A **Cue List** lives on the patch. Each cue is a named snapshot of:
- A GLSL shader source (or a named preset)
- Optional parameters (speed, color tint, intensity)
- Optional transition duration (crossfade to next)

**Three triggering modes:**

| Mode | Behaviour |
|---|---|
| **Manual** | Click a cue in the list to activate it immediately |
| **Timed sequence** | Cues advance automatically after a configurable hold time |
| **Crossfade** | Output lerps between two rendered VFBs over a transition time |

**Implementation approach:**

1. Add a `Patch::CueList` component (a `std::vector<Cue>`) and a `Patch::ActiveCue` index.
2. Each `Cue` stores: `name`, `glslSource`, `holdSeconds`, `fadeSeconds`.
3. The RT thread renders the *active* cue's shader into VFB A. During a transition, it also renders the *next* cue into VFB B, then blends A and B pixel-by-pixel using the fade progress `t ∈ [0,1]` before the bilinear sampling step.
4. A new ECS system (`CueAdvancer`) on the main thread ticks the hold timer and triggers cue transitions by setting dirty flags.
5. **UI — "Cues" window:**
   - List of cues (click to trigger, drag to reorder).
   - Add/remove cue buttons.
   - Per-cue: name field, shader editor shortcut (opens Script Editor with that cue's source loaded), hold time slider, fade time slider.
   - Transport controls: `▶ Auto-advance`, `⏮ Prev`, `⏭ Next`, loop toggle.

**Files:** New `src/CueList.h`, `src/CueList.cpp`, `src/gui/PixelMapperGui.cpp` (new `WindowCues` system).

> **Simplified MVP (if time is short):** No crossfade, no blend. Just store multiple GLSL source strings, and switching cues recompiles the `PatchProgram` with the new shader. The existing compile pipeline handles this instantly.

---

### P3: Polish Items (Day 6–7)

- **Fixture rename:** Double-click on a fixture name in the list to edit it in-place.
- **Canvas "Zoom to Fit":** Button/shortcut that auto-scales and pans the view to fit all fixtures.
- **Status bar:** Persistent bar at the bottom of the window showing current FPS, RT refresh rate, universe count, total bytes/frame, ArtNet send status (✓ / ✗).
- **ArtNet sequence number:** Increment `sequence` byte in `ArtDmxHeader` per-packet for nodes that require it.
- **Per-universe send toggle:** Ability to mute/unmute individual universes for debugging.

---

## Part 4: Installation Reference Data

Pre-calculated parameters for the 12-fixture RGBW installation:

| Parameter | Value |
|---|---|
| Fixture type | Line, all vertical (36 pixels each) |
| Channels per pixel | 4 (RGBW) |
| Bytes per fixture | 144 |
| Total fixtures | 12 |
| Total DMX bytes | 1,728 |
| Total universes | 4 (last universe ~192 bytes used) |
| Fixtures per universe | 3 (neatly 3 × 144 = 432 bytes per universe) |
| Recommended refresh rate | 40 Hz |
| Recommended VFB resolution | 128 (fast) or 256 (sharper gradients) |
| ArtNet protocol | ArtDmx, port 6454, unicast per device |

---

## Day-by-Day Roadmap

```
Day 1  │ P0: Save/Load (XML serialization)
       │ P0: Clean startup (remove hardcoded test data)
       │ P0: GLSL threading fix (move GL setup to main thread)
       │
Day 2  │ P0: GLSL PBO async readback (eliminate glReadPixels stall)
       │ P1: File-watch hot-reload + "Reload from Disk" button
       │ P1: Canvas click-to-select fixture
       │
Day 3  │ P1: Multi-select — Shift+click + selection set model
       │ P1: Group drag MVP
       │
Day 4  │ P1: Marquee rubber-band select
       │ P2: Effect preset library (Presets.h + UI combo)
       │
Day 5  │ P2: Fixture duplication (Duplicate button + DMX auto-pack)
       │     Full 12-fixture patch workflow test end-to-end
       │
Day 6  │ P2: Effect Sequencer MVP (cue list, manual trigger, timed advance)
       │ P3: Crossfade between cues
       │
Day 7  │ P3: Polish items
       │     ArtNet hardware stress test with real nodes
       │     Buffer / bug fixes / final installation preparation
```

---

## Architectural Notes for Future Development

### Beyond the installation (future roadmap ideas)

- **3D fixture positioning:** The `Fixture::PixelData` already uses `glm::vec3`. A 3D canvas view (orbit camera, OpenGL scene) would unlock true volumetric mapping.
- **Effect sequencer (full):** Full crossfade blend, beat-sync via OSC/MIDI tap-tempo, per-cue parameter uniforms exposed in UI.
- **OSC / MIDI control:** Bind Lua/GLSL uniforms to external control signals (sliders, beat clock, etc.) for live performance.
- **Timeline / cue list with timeline editor:** Record snapshots of scripts/parameters on a drag-and-drop timeline and play them back in sequence.
- **Network patch distribution:** Multiple computers each running a subset of universes, synced via a shared patch file.
- **Per-fixture color calibration:** A calibration matrix to compensate for LED brightness/color variance between strips.
