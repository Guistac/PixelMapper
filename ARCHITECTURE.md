# PixelMapper Project Architecture & Developer Guide

Welcome to **PixelMapper** (internal working name: *Nique Madrix*). This document provides an overview of the project's goals, structural design, entity relationships, execution pipelines, and runtime execution thread models.

---

## 1. Project Goal

**PixelMapper** is a cross-platform, real-time DMX pixel mapping and pattern generation tool. It enables users to:
1. **Design Spatial Layouts (Patches):** Place and arrange multi-pixel lighting fixtures (e.g., lines, circles) on a 2D canvas.
2. **Assign DMX Patches:** Map pixels to specific DMX universes and start addresses.
3. **Generate Real-Time Visual Effects:** Animate pixel colors using C++ procedural functions, Lua scripts, or custom GLSL fragment shaders.
4. **Transmit DMX/ArtNet Data:** Stream raw channel data over UDP using the ArtNet protocol to multiple hardware controllers or visualizers.

---

## 2. Core Tech Stack

- **Language:** C++20
- **Build System:** CMake (configured with `CMakeLists.txt`)
- **Entity Component System (ECS):** [Flecs](https://github.com/SanderMertens/flecs) (used for managing scene graph state, layout observers, systems, and UI configuration)
- **GUI Frame & Context:** GLFW, OpenGL 3.x, and [Dear ImGui](https://github.com/ocornut/imgui) (configured with Docking and Viewports)
- **Asynchronous Networking:** [Asio](https://think-async.com/Asio/) (header-only network library handling low-latency UDP operations)
- **Scripting Integration:** Sol2 (for running Lua scripting contexts)
- **Text Editor:** [goossens/ImGuiColorTextEdit](https://github.com/goossens/ImGuiColorTextEdit) (embedded in the shader and script editors)
- **Math Library:** GLM (OpenGL Mathematics)
- **XML Library:** TinyXML2 (used for patch and UI state serialization)

---

## 3. Thread Architecture & Synchronization

To maintain high visual responsiveness without causing GUI micro-stutters, the application isolates user interaction from the high-frequency DMX rendering and networking engine. 

The main thread runs the UI loop and the ECS world update cycle, a detached real-time execution thread renders patterns, and an asynchronous network worker thread handles UDP packet transmission.

```mermaid
graph TD
    subgraph Main Thread [Main / GUI Thread]
        A[GLFW & OpenGL Context] --> B[Flecs World Progress]
        B --> C[ImGui Panels / Editors / Canvas]
        C --> D[User Edits Fixture / Shaders / Drag-Drop Cue]
        D -->|Triggers OnSet Observer / UI| E[ECS Dirty State Components]
        E -->|Flecs Update Systems| F[Fine-Grained Recompilation]
        F -->|pushNewProgram| G[Thread-Safe std::atomic Pointer Swap]
    end

    subgraph Real-Time Thread [rtPatchRunner Thread]
        H[rtPatchRunner Loop ~3ms] -->|std::atomic_load| I[Active PatchProgram Pointer]
        I --> J[render: Execute C++/Lua/GLSL on GPU]
        J --> K[Async GPU Readback via Double-Buffered PBOs]
        K --> L[encode: Map Pixel Colors to Universe Buffers]
        L --> M[ArtnetSender: send via Network::UdpSocket]
    end

    subgraph Network Thread [ASIO Network Thread]
        N[ASIO io_context loop] -->|async_send_to| O[UDP IP Broadcast / Unicast to Devices]
    end

    G -.->|Atomic Swap| I
    M -->|asio::post / send| N
```

### 3.1 The Main/GUI Thread
- Runs the windowing loop, processes keyboard/mouse events, and renders the ImGui UI.
- Advances the Flecs ECS world using `world.progress()` on each frame.
- Re-compiles patterns, updates DMX patching maps, and handles layout recalculations.
- Coordinates OpenGL resource allocation (such as generating framebuffers and compiling shader pipelines).

### 3.2 The Real-Time Thread (`rtPatchRunner`)
- Spawns as a detached worker thread executing `App::runPatch()`.
- Runs continuously with a target cycle time of ~3ms (~330 Hz output capability) to ensure low latency.
- **Lock-Free Execution:** Thread synchronization is achieved via `std::atomic<std::shared_ptr<PatchProgram>>`. The real-time thread retrieves the active program using `std::atomic_load` and processes it without acquiring global mutex locks.
- Performs pattern rendering (`render`), async PBO readbacks, channel mapping (`encode`), and delegates sending to the network socket.

### 3.3 The ASIO Network Thread
- Spawns at initialization (`Network::init()`) running `asio::io_context::run()`.
- Processes asynchronous UDP socket output queue entries to prevent networking I/O latency from blocking the time-critical real-time rendering loop.

---

## 4. Entity Component System (ECS) Design

PixelMapper represents its data hierarchies and logical states through **Flecs** components, pair relationships, observers, and systems.

### 4.1 Hierarchy & Entities
- **`PixelMapperApp` (Root):** Holds the main system queries, reference configurations, and the `UIConfig` component.
  - **`Patches` (Folder):** Houses patch entities.
    - **`Patch` (Entity):** Houses a single configuration, and contains folders:
      - `FixtureFolder` -> holds `Fixture` entities.
      - `DmxOutputFolder` -> holds `Artnet::Universe` entities.
      - `ArtnetDeviceFolder` -> holds `Artnet::Device` entities.
      - `CueFolder` -> holds `CueList::Cue` entities.
      - `EffectFolder` -> holds `EffectBank::Effect` entities.

### 4.2 Key Components

| Component | Target Entity | Purpose |
| :--- | :--- | :--- |
| `Fixture::Layout` | Fixture | Defines number of pixels (`pixelCount`) and bytes per pixel (`channelsPerPixel`, e.g., 3 for RGB, 4 for RGBW). |
| `Fixture::DmxAddress` | Fixture | Defines start `universe` and start `address` (offset 0–511). |
| `Fixture::PixelData` | Fixture | Hosts dynamic runtime vectors of `positions` (glm::vec3) and `colors` (ColorRGBW). |
| `Shape::Line` / `Shape::Circle` | Fixture | Shape configurations containing dimensional properties (e.g., start/end, center/radius). |
| `Artnet::Universe::Properties` | Universe | Defines the DMX universe identifier (`universeId`). |
| `Artnet::Universe::Channels` | Universe | Raw `uint8_t buffer[512]` containing current universe bytes. |
| `Patch::RenderArea` | Patch | Bounding box coordinates representing the spatial span of all active pixels. |
| `Patch::GPUResources` | Patch | Holds persistent OpenGL handles for FBOs, textures, PBOs, and the blend program. |
| `Patch::GPUProgram` | Patch & Effect | Caches the compiled GLSL shader handle, source string, and compiler log. |
| `CueList::Cue::Is` | Cue | Tag component specifying that the entity is a sequencer Cue. |
| `CueList::Cue::HoldDuration` / `FadeDuration` | Cue | Float values storing sequencer time parameters for the cue. |
| `CueList::Cue::IndexOrder` | Cue | Integer index used to sort and reorder cues in the sequencer. |
| `EffectBank::Effect::Is` | Effect | Tag component specifying that the entity is a bank effect shader. |
| `EffectBank::Effect::GlslSource` | Effect | String storing the GLSL fragment shader code for the effect. |
| `Patch::Settings` | Patch | Stores patch configurations like refresh rate, network settings, white mode, highlight settings, and file paths. |
| `Patch::MultiSelection` | Patch | Holds a list of entity IDs representing the currently multi-selected fixtures in the patch. |
| `App::UIConfig` | App | Stores visible windows, layout choices, grid settings, viewport toggles (show fixtures, pixels, bounds), auto-zoom configurations, pixel size, and active editor indexes. |

### 4.3 Relationship Pairs & Exclusive Tags
Flecs relationship pairs specify routing, connections, and selection states:
- `(WithShape, Shape::Line)` or `(WithShape, Shape::Circle)` on fixtures.
- `(SelectedPatch, patch_entity)` on the App root (exclusive).
- `(SelectedFixture, fixture_entity)` on a Patch (exclusive).
- `(SelectedDmxUniverse, universe_entity)` on a Patch (exclusive).
- `(SelectedArtnetDevice, device_entity)` on a Patch (exclusive).
- `(InUniverse, universe_entity)` on a Fixture (can be multiple if a fixture spans universes).
- `(TargetEffect, effect_entity)` on a sequencer Cue (pairs the Cue with its active shader effect).

---

## 5. ECS Pipeline & Logical Systems

Observers and systems recalculate spatial layouts and patch maps in response to changes in the editor.

### 5.1 Observers & GPU Lifetimes
1. **`ObserveFixtureLayout` (OnSet layout):** Normalizes inputs and adds `LayoutDirty` to the fixture.
2. **`ObserveFixtureDmxAddress` (OnSet dmxAddress):** Clamps universe/address bounds and flags the parent patch as `DmxMapDirty`.
3. **`CleanGPUResources` / `CleanGPUProgram` (OnRemove observers):** Registered in `Patch.cpp`, these observers automatically delete OpenGL framebuffers, textures, and linked programs on the main GUI thread (possessing the OpenGL context) when a patch or effect entity is destructed, preventing resource leaks.

### 5.2 Dynamic Selection Tracking
- **`UpdateRtSelectionFlags` System:** Runs during the `PreStore` phase on each frame. It reads the active `SelectedFixture` and the `MultiSelection` components on the patch. It maps these IDs against the `CompiledFixture` array in the active `PatchProgram` and updates the `pixelSelected` atomic array. This relays selection changes to the real-time runner thread on-the-fly, avoiding costly program recompilations.

### 5.3 Systems & Compilation Sequence

```
[Layout Changed] -> UpdateFixtureLayout System
                    • Resizes pixel position & color vectors
                    • Adds PixelPositionsDirty to Fixture
                    • Adds DmxMapDirty to Patch
                          |
                          v
[Shape Dragged]  -> UpdateLinePixelPositions / UpdateCirclePixelPositions
                    • Computes local coordinates based on Line/Circle specs
                    • Removes PixelPositionsDirty
                    • Adds RenderAreaDirty to Patch
                          |
                          v
                    UpdateRenderArea
                    • Computes absolute min/max bounding box of all pixels
                    • Adds ProgramDirty to Patch
                          |
                          v
[DMX Re-patched] -> UpdateDmxOutputMap
                    • Determines needed universes
                    • Allocates/deletes Universe entities
                    • Updates InUniverse relationship edges
                    • Adds ProgramDirty to Patch
                          |
                          v
[Program Dirty]  -> PatchProgramCompile
                    • Executes fine-grained OpenGL updates
                    • Flattens active patch layout and cue entities into static arrays
                    • Pushes flat PatchProgram pointer to rtPatchRunner via atomic swap
```

### 5.4 Fine-Grained OpenGL Compilation
To keep compilation times low (typically under 2ms for topology changes), `PatchProgram::compile` uses a fine-grained strategy:
1. **FBO Recycling:** Resizes the framebuffers (`GPUResources` component) only if the Virtual Framebuffer (VFB) resolution changes.
2. **Shader Link Recycling:** Checks if the GLSL source code stored in `GPUProgram` has actually changed before recompiling and relinking. If the code matches, it recycles the compiled OpenGL shader program.
3. **Cue List Sorting:** Cues are queried and compiled ahead-of-time, sorted by `IndexOrder`.

---

## 6. Real-Time Execution Structure (`PatchProgram`)

The compilation phase creates a flat, cache-friendly representation `PatchProgram` for the execution thread:

- **`pixelPositions` (Array of vec3):** Flat list of positions for visual algorithms.
- **`pixelColors` (Array of ColorRGBW):** Shared array where rendering outputs write.
- **`universes` (Array of Universe structs):** Contains final 512-byte buffers.
- **`p2us` (Array of Pix2UniCopyInstr):** Pre-compiled instructions mapping contiguous byte ranges from the pixel colors array directly to destination universe buffers.
- **`fixtures` (Array of CompiledFixture):** Pre-compiled fixture information containing `entityId`, `pixelStart`, and `pixelCount`.
- **`pixelSelected` (Array of std::atomic<bool>):** Indicators signaling which pixels are currently selected in the GUI.
- **`whiteMode` (WhiteMode enum):** Dictates how raw RGB colors are mapped to RGBW outputs (Auto, Off, or Pass-through).
- **`highlightFrequency` (float):** Flash frequency for identifying selected fixtures.

### 6.1 GPU Point-Rendering & Previews
When the active render mode is GLSL, the real-time thread executes the following operations:
1. **1D GPU Point-Rendering:** Binds the active 1D FBO (`glslFbo`) and configures the viewport to `(pixelCount, 1)`. It renders the pixels as a point list using `glDrawArrays(GL_POINTS, 0, pixelCount)`. The vertex shader assigns raw 3D positions (`vPixelPos3D`) and CPU-projected 2D coordinates (`vPixelPos2D`) to variables, mapping each point to exactly 1 fragment in the 1D viewport.
2. **Double-Buffered PBO Readback:** Employs double-buffered Pixel Buffer Objects (PBOs) to copy the rendered 1D pixel buffer back to the CPU array (`vfbPixels`) asynchronously. Since the buffer size is extremely small (e.g. `pixelCount * sizeof(ColorRGBW)`), readback overhead is negligible.
3. **2D Offline Preview FBO:** Renders a 2D quad of size `previewWidth x previewHeight` to the editor preview FBO (`glslEditorFbo`) using `glslPreviewProgram` and the current `zSlice` uniform value. This is displayed in the **Effect Preview** window, and its dimensions are configured via the "Preview Resolution" slider, matching the aspect ratio of the RenderArea.
4. **2D Playback Preview FBO:** Renders the active playback program into the `previewWidth x previewHeight` 2D playback preview FBO (`glslPlaybackPreviewFbo`) to display the live pattern as the canvas background. To optimize GPU performance, it is only rendered when the "Rendered" checkbox is enabled, the canvas is in 2D mode, and the Patch Editor window is open.

### 6.2 Post-Rendering Filters
After generating pixel colors via GPU or CPU (Lua/C++), the real-time thread runs post-processing steps:
1. **Selection Flashing (Find):** Pixels with their atomic select flags set to `true` are flashed white (RGBW: `255, 255, 255, 255`) at the speed defined by `highlightFrequency`.
2. **RGB to RGBW White Extraction:** Matches the `whiteMode` settings:
   - **`AUTO`:** Extracts white from the RGB channels: $W = \min(R,G,B)$, then subtracts $W$ from the RGB channels and assigns it to $W$.
   - **`OFF`:** Enforces $W = 0$ for all pixel outputs.
   - **`PASSTHROUGH`:** Bypasses conversion, using the raw $W$ channel outputted by shaders or canvas renderers.

### 6.3 CPU-Side Crossfading
During cue transitions in non-GLSL modes (Lua, C++), the real-time thread performs CPU-side crossfading:
- Prior to the transition, it captures the current state by copying `vfbPixels` into `vfbPixelsOld` using a fast `std::memcpy`.
- It interpolates the pixels using the transition's blend factors in memory before encoding them.

### 6.4 GPU-Side Crossfading
During sequence transitions, the real-time thread performs GPU-side crossfading for both primary outputs and 2D playback previews:
- **Primary 1D Outputs**: The outgoing and active cues are rendered to separate 1D textures (`glslFboTexOld` and `glslFboTex`), blended via `glslBlendProgram` into `glslFboTexBlend` using the active cue's transition `mixFactor`, and read back via PBOs.
- **2D Playback Previews**: The outgoing and active 2D previews are rendered to separate `previewWidth x previewHeight` textures (`glslPlaybackPreviewFboTexOld` and `glslPlaybackPreviewFboTex`), blended via `glslBlendProgram` into `glslPlaybackPreviewFboTexBlend` using the transition progress, and bound as the background image in the GUI.

### 6.5 Encoding
Copies pixel color channels to universe buffers using pre-compiled instructions:
```cpp
void encode(PatchProgram* program) {
    for (int i = 0; i < program->p2uCount; i++) {
        const PatchProgram::Pix2UniCopyInstr& map = program->p2us[i];
        uint8_t* dest = program->universes[map.universeIndex].buffer + map.universeOffset;
        ...
        std::memcpy(dest + bytesWritten, src + pByte, toCopy);
    }
}
```

---

## 7. Art-Net UDP Sender Implementation

Streaming DMX universes to external fixtures is handled by the **`ArtnetSender`** subsystem:

1. **Protocol Header Format:**
   `ArtnetSender` formats outgoing universe buffers into `ArtDmx` UDP packets conforming to the Art-Net protocol:
   - Header: `"Art-Net\0"` (8 bytes)
   - OpCode: `0x5000` (Little Endian, 2 bytes)
   - ProtVer: `14` (Big Endian, 2 bytes)
   - Sequence: `0x00` (disabled) or incrementing (1 byte)
   - Physical: `0x00` (1 byte)
   - Universe: Target universe ID (14-bit value, Big Endian, 2 bytes)
   - Length: Payload length (512 bytes, Big Endian, 2 bytes)
   - Data: Raw channel data (512 bytes)

2. **Socket Management & Asio Integration:**
   Instead of direct blocking POSIX sockets, `ArtnetSender` delegates operations to a `Network::UdpSocket` instance running on top of **Asio**. Sockets are opened and bound asynchronously inside Asio's I/O thread. Broadcast options (`SO_BROADCAST`) and address reuse (`SO_REUSEPORT` / `SO_REUSEADDR`) are managed natively by the Asio layer to prevent binding issues, especially on macOS.

3. **Multi-Device IP Routing:**
   Iterates over all devices registered under the `ArtnetDeviceFolder`. Matches the active universes to each device's start universe and universe count, then routes packets to the target device's IP address (translated from network byte order to host byte order for the Asio wrapper).

4. **Thread-Safe Status Reporting:**
   Asynchronous socket error callbacks update the network status string. This string is stored thread-safely in `App::rtNetworkStatus` under `App::rtNetworkStatusMutex`, enabling the GUI main thread to render connection health updates in the Settings window.

---

## 8. Modular Rendering: Lua Scripting & 1D Canvas

To support runtime programmability without recompiling the application, PixelMapper implements a modular Lua scripting engine.

### 8.1 Architectural Paradigm
To accommodate non-grid spatial configurations while preserving high performance, PixelMapper uses a **1D Canvas & Point-Mapping** architecture:

1. **1D Framebuffer**: The C++ and Lua rendering pipelines execute directly against a 1D virtual framebuffer (`vfbPixels`) of dimensions `(pixelCount, 1)`. The $i$-th pixel in the canvas corresponds directly to the physical fixture pixel index.
2. **Lua Drawing Context**: The Lua script interacts with a C++ class bound via `Sol2` (representing a `Canvas` API), drawing procedurally or reading pixel parameters.
3. **Direct Mapping**: During the render phase, CPU-side bilinear spatial sampling has been replaced by a direct memory copy (`std::memcpy`) of the 1D virtual framebuffer to the final `pixelColors` array, eliminating PCI-e bandwidth bottlenecks and sampling stutters.

### 8.2 Execution & Thread Safety
- **Isolate VMs:** To prevent concurrency race conditions, each `PatchProgram` owns a dedicated, self-contained `sol::state` (Lua VM).
- **Compile on GUI Thread:** Script loading and compilation happen on the Main/GUI thread.
- **Hybrid Script Storage:** The patch settings store a reference path to an external `.lua` script file. The source code is loaded into memory for inline editing, allowing simple file persistence, version control, and serialization.
- **Run on Real-Time Thread:** Once compiled successfully, the new program is pushed to `rtPatchRunner`. The real-time thread executes the Lua function `update(canvas, time)` once per frame, updates the grid, samples the colors to physical pixels, and encodes the DMX payload.

### 8.3 Text Editor & Compilation Feedback UI
A built-in script editor is embedded using the modern [goossens/ImGuiColorTextEdit](https://github.com/goossens/ImGuiColorTextEdit) fork, providing:
- Lua syntax highlighting, line numbers, and indentation.
- **Compilation Error Handling:** If compilation fails (e.g. syntax or runtime compile error), the script's compiler log is parsed:
  - Error markers are placed on the exact source line in the ImGui editor workspace.
  - A persistent "Log Console" window is shown at the bottom of the editor displaying full stack traces and debug output.
- Code folding and search/replace functionality.
