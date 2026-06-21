# PixelMapper Refactoring: Phased Roadmap & Execution Guide

This document defines the strict, phase-by-phase execution plan for transitioning PixelMapper from a monolithic architecture to a Server-Authoritative Distributed ECS. 

**Core Directive for AI Agents:** Do not attempt to execute multiple phases simultaneously. Complete the active phase, verify the build, commit the changes, and clear the context window before proceeding to the next phase.

---

## Phase 1: Physical Isolation & The Air Gap (✅ COMPLETED)
**Objective:** Enforce strict compile-time boundaries between the GUI and the Real-Time Engine.
* **Tasks:**
  * [x] Configure `CMakeLists.txt` for three Object Libraries: `CommonLib`, `ServerLib`, and `ClientLib`.
  * [x] Migrate source code into `src/shared/`, `src/server/`, and `src/client/`.
  * [x] Extract Flecs component data structures into `PatchComponents.h` and `AppComponents.h` to prevent inclusion traps.
  * [x] Split the monolithic `CommandHandlers` into isolated Server and Client variants.
  * [x] Verify that the application compiles cleanly and `--standalone` mode runs successfully.

---

## Phase 2: GUI Decoupling & TCP Command Plane (✅ COMPLETED)
**Objective:** Strip the Client GUI of its ability to directly mutate the local Flecs world, routing all user interactions through the asynchronous TCP network plane.
* **Tasks:**
  * [x] **Intercept ImGui Edits:** Scan `PixelMapperGui.cpp` for all direct `entity.set<T>()` and `entity.add<T>()` calls triggered by user interaction (e.g., sliders, layout drags).
  * [x] **Wrap with Asio TCP:** Replace direct mutations with `AsioNetworkManager::getInstance().sendEntityMutation<T>()`.
  * [x] **Server-Side Handling:** Ensure `ServerCommandHandlers.cpp` correctly parses incoming `EntityMutation` RPCs, applies them to the authoritative world, and triggers the necessary ECS systems (e.g., `UpdateFixtureLayout`).
  * [x] **Verification:** Dragging a slider in the GUI should send a TCP packet, mutate the Server world, and trigger a Server-side state recalculation without crashing.

---

## Phase 3: Remote Compilation & Error Routing (📍 ACTIVE)
**Objective:** Move all GLSL and Lua compilation to the headless Server, and route diagnostic feedback back to the Client GUI.
* **Tasks:**
  * [ ] **Intercept Script Triggers:** Modify the "Compile" button in the Client's `ImGuiColorTextEdit` to send an `ApplyScript` TCP RPC containing the raw source string.
  * [ ] **Headless Compilation:** Ensure the Server receives the script, attempts to compile it against the headless OpenGL context or Lua `sol::state`, and captures the logs.
  * [ ] **The `CompilationResult` RPC:** Program the Server to construct a JSON response containing the success state, error line numbers, and full log strings.
  * [ ] **GUI Error Binding:** Update `ClientCommandHandlers.cpp` to catch the `CompilationResult` RPC and map the line errors to red squiggly markers in the local ImGui text editor.

---

## Phase 4: State Synchronization & Pointer Safety
**Objective:** Ensure the Client safely rebuilds its UI state when the Server broadcasts massive world changes (e.g., loading an XML patch).
* **Tasks:**
  * [ ] **Pre-Sync Snapshot:** In `ClientCommandHandlers.cpp`, before invoking `world.from_json()` on a `SyncWorldState` command, extract the hierarchical string paths (`entity.path()`) of any entities currently selected in the GUI.
  * [ ] **World Deserialization:** Allow the Client to overwrite its local Flecs world.
  * [ ] **Post-Sync Restoration:** Immediately after deserialization, use `world.lookup()` with the saved string paths to re-acquire safe `flecs::entity` handles.
  * [ ] **Verification:** Clicking "Load Patch" should seamlessly update the UI without causing dangling pointer segfaults.

---

## Phase 5: The Telemetry Plane & Visual Sync (UDP)
**Objective:** Connect the high-frequency Server rendering engine to the Client's 2D canvas for live confidence monitoring.
* **Tasks:**
  * [ ] **Server UDP Broadcaster:** Ensure `rtPatchRunner` packs the `EngineStateUBO` and `vfbPixels` array into MTU-safe segments and transmits them at 60Hz.
  * [ ] **Client UDP Assembler:** Verify the ASIO network thread safely reassembles incoming packets into complete frames and pushes them to the `clientTelemetryMutex` cache.
  * [ ] **GUI Canvas Update:** Hook the Client's `ImGui` 2D canvas strictly to the `clientTelemetryPixels` cache, ensuring it never reads from the Server's memory pointers.
  * [ ] **Verification:** The Client GUI should smoothly render the live pixel output at 60 FPS entirely via local loopback UDP data.