# Sprint Plan: ECS & Render Pipeline Refactoring

This sprint document outlines the goals, architectural task breakdown, and success metrics for refactoring the ECS, thread synchronization, and OpenGL resource allocation systems in the PixelMapper codebase.

---

## 🎯 Sprint Goal
Optimize performance, eliminate thread-blocking GUI lag, and improve codebase modularity by decoupling OpenGL asset lifetimes from the ECS topology, implementing lock-free data sharing, and modeling UI/Cue structures as first-class ECS entities.

---

## 📋 Sprint Backlog & Task Breakdown

### Epic 1: Decoupling OpenGL Assets & Fine-Grained Compilation
Currently, changing any topology attribute (e.g. moving a fixture, modifying DMX maps) deletes the old [PatchProgram](file:///Users/leobecker/Dev/PixelMapper/src/Patch.h#L68) and recreates all OpenGL programs, FBOs, and textures.

*   **Task 1.1: Persist Shader Programs & Framebuffers**
    *   Introduce a persistent rendering manager or store OpenGL resource handles (like `glslProgram`, `glslFbo`, etc.) as components directly on the ECS entities (e.g. a `GPUProgram` component on the Patch or Cue entity).
    *   Implement resource recycle logic so compilation only deletes/recreates resources when the GLSL source changes.
*   **Task 1.2: Split Compilation into Structural vs. Shader Stages**
    *   Rewrite [PatchProgram::compile](file:///Users/leobecker/Dev/PixelMapper/src/Patch.cpp#L122) to support partial compilations:
        *   **Topology Update**: Quickly rebuild mapping instructions, pixel array indices, and universe buffers when coordinates or addresses change.
        *   **Shader Recompile**: Re-link GL program binaries only when GLSL source code is modified.

---

### Epic 2: Lock-Free Thread Communication
Currently, [App::runPatch](file:///Users/leobecker/Dev/PixelMapper/src/App.cpp#L77) holds `patchProgramLock` for the duration of the rendering and encoding frame, which blocks the GUI thread.

*   **Task 2.1: Implement Lock-Free Double-Buffering**
    *   Refactor [currentPatchProgram](file:///Users/leobecker/Dev/PixelMapper/src/App.h#L20) to use `std::atomic<PatchProgram*>` or a shared pointer wrapper.
    *   Update [pushNewProgram](file:///Users/leobecker/Dev/PixelMapper/src/App.cpp#L47) to execute an atomic pointer swap.
*   **Task 2.2: Implement Reference Counting on rendering program**
    *   Enable the RT thread to acquire a thread-safe read reference/pointer of the active `PatchProgram` at the start of its render loop and release it at the end of the frame. 
    *   This ensures the main thread can delete and replace programs in the background without blocking render runs or GUI draws.

---

### Epic 3: ECS-Centric Cues & Effect Banks
Refactor the cue list and effect bank data models to be fully ECS-driven, referencing shared effects instead of copying source code directly.

*   **Task 3.1: Model Cues & Effects as Individual ECS Entities**
    *   **Effects (Effect Bank)**: Represent each GLSL script as an `Effect` child entity under the `EffectBank` parent. An `Effect` entity will have `Name` and `GlslSource` components, which can be edited in the shader editor and previewed offline.
    *   **Cues (Cue List)**: Represent each cue in the list as a `Cue` child entity under the `CueList` parent. A `Cue` entity will hold:
        *   A relationship referencing the target `Effect` entity from the Effect Bank.
        *   `HoldDuration` (Hold time) and `FadeDuration` (Fade time).
        *   `IndexOrder` (for list ordering and sequence execution).
*   **Task 3.2: Implement Cue Reordering**
    *   Add drag-and-drop reordering logic to the Cue List table in [PixelMapperGui.cpp](file:///Users/leobecker/Dev/PixelMapper/src/gui/PixelMapperGui.cpp), similar to fixture reordering.
    *   Update index sorting query filters to display and run cues in their updated user-defined order.
*   **Task 3.3: Modernize Serialization & UI Queries**
    *   Update [PatchSerializer.cpp](file:///Users/leobecker/Dev/PixelMapper/src/PatchSerializer.cpp) to save/load individual cue and effect entities using the new ECS structure.
    *   Refactor [PixelMapperGui.cpp](file:///Users/leobecker/Dev/PixelMapper/src/gui/PixelMapperGui.cpp) to fetch and mutate cues and effects dynamically using pre-compiled ECS queries.

---

### Epic 4: Clean ECS UI State Management
Currently, GUI state like panel view states and active edit indices are stored as static global variables.

*   **Task 4.1: Create UI Session Config Entity**
    *   Define a new component `UIConfig` on a singleton/application entity (e.g. `UIConfig` on [PixelMapperApp](file:///Users/leobecker/Dev/PixelMapper/src/App.cpp#L186)).
    *   Store view selections, locks, opacities, and current shader edit indices in `UIConfig`.
*   **Task 4.2: Enable Layout & Editor Serialization**
    *   Include `UIConfig` attributes in the XML serializer to allow users to restore panel layouts and open editor targets on project load.

---

## 📈 Success Metrics & Verification Plan

### Success Metrics
1.  **Zero GUI Stuttering**: GUI framerate remains at a stable 60 FPS on the main thread during real-time shader compiles and edits.
2.  **Fast Hot-Reloading**: Recompiling on fixture movements or DMX mapping changes takes less than 2ms (previously ~30-50ms due to GL context changes).
3.  **Clean Thread Decoupling**: Thread analysis tools verify no mutex contention between the GUI thread and the DMX transmission thread.

### Verification Plan
*   **Performance Benchmarking**: Add telemetry around `pushNewProgram` to log and track compile times.
*   **Thread Safety Testing**: Run threads under TSAN (ThreadSanitizer) to ensure pointer exchanges are race-free.
*   **Manual Verification**: Verify that dragging fixtures on the canvas does not disrupt GLSL previews or cause stuttering.
