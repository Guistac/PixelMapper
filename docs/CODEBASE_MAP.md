# PixelMapper Codebase Migration Manifest

This document maps all files from the monolithic `src/` directory to their target locations under the new separated CMake three-library architecture, as specified by the boundary rules in [ARCHITECTURE.md](file:///Users/leobecker/Dev/PixelMapper/docs/ARCHITECTURE.md).

## Library Boundary Reminders
1. **PixelMapper_CommonLib (`src/shared/`):** Contains data structures, network protocol opcodes, utilities, and Flecs component definitions. Must have **NO business logic**, NO ImGui references, and NO OpenGL rendering context calls.
2. **PixelMapper_ServerLib (`src/server/`):** Contains the `rtPatchRunner` real-time render thread, headless OpenGL contexts, DMX/Art-Net output layers, file serialization, and authoritative Flecs systems. Cannot reference client-only components.
3. **PixelMapper_ClientLib (`src/client/`):** Contains ImGui panels, canvas viewport confidence reviews, user event callbacks, and client-side UDP telemetry caches. Cannot reference server-only modules.
4. **Main Executable (`src/` root):** The main entry point `main.cpp` manages CLI parameter parsing and instantiates the respective server/client loops.

---

## 1. Shared Library (PixelMapper_CommonLib)
Target directory: `src/shared/`

- [ ] `src/Common.h` -> `src/shared/Common.h`
  * **Justification:** Defines the fundamental `ColorRGBW` structure which must be visible to both the server's render loops and the client's telemetry caching buffers.
- [ ] `src/Shape.h` -> `src/shared/Shape.h`
  * **Justification:** Declares the parametric geometric shape definitions (`Shape::Line` and `Shape::Circle`) used across both server rendering systems and client layout editors.
- [ ] `src/PixelMapper.h` -> `src/shared/PixelMapper.h`
  * **Justification:** Functions as an aggregator header for core definitions that must be shared across both the client and server compilation units.
- [ ] `src/Presets.h` -> `src/shared/Presets.h`
  * **Justification:** Declares static constexpr GLSL shader source strings shared for local preview selection on the client and headless program compilation on the server.
- [ ] `src/network/CommandProtocol.h` -> `src/shared/network/CommandProtocol.h`
  * **Justification:** Contains shared network RPC opcodes (`CommandType`) and TCP packet packet-header definitions utilized by both connection endpoints.
- [ ] `src/network/TelemetryProtocol.h` -> `src/shared/network/TelemetryProtocol.h`
  * **Justification:** Defines the UDP packet slice headers and magic constants needed by both the server's stream transmitter and the client's stream reassembly buffers.
- [ ] `src/network/SimpleJson.h` -> `src/shared/network/SimpleJson.h`
  * **Justification:** A header-only JSON serialization utility used by client-side commands and server-side RPC handlers to format payload transfers.
- [ ] `src/utils/FlecsUtils.h` -> `src/shared/utils/FlecsUtils.h`
  * **Justification:** Declares basic ECS helpers such as `EntityHasher` used within standard library containers across both execution environments.
- [ ] `src/utils/Profiling.h` -> `src/shared/utils/Profiling.h`
  * **Justification:** A profiling `Timer` utility used to audit execution and network latency across both client GUI updates and server real-time loops.
- [ ] `src/EffectBank.h` -> `src/shared/EffectBank.h`
  * **Justification:** Defines components for the experimental effect bank that must be accessible by both authoritative worlds and client presentation editors.
- [ ] `src/EffectBank.cpp` -> `src/shared/EffectBank.cpp`
  * **Justification:** Performs the component registration for the effect bank module in the Flecs world to ensure matching database IDs.
- [ ] `src/Fixture.h` -> `src/shared/Fixture.h`
  * **Justification:** Declares the components representing fixture layout, order, DMX addresses, and pixel data shared between the server rendering and client representation.
- [ ] `src/Fixture.cpp` -> `src/shared/Fixture.cpp`
  * **Justification:** Imports the fixture components into the Flecs world to synchronize component registries between server and client worlds.
- [ ] `src/Artnet.h` -> `src/shared/Artnet.h`
  * **Justification:** Declares universe properties and device configurations shared between client settings editors and server broadcast generators.
- [ ] `src/Artnet.cpp` -> `src/shared/Artnet.cpp`
  * **Justification:** Imports the Art-Net configuration component tags into Flecs to allow cross-network state synchronization.
- [ ] `src/Network.h` -> `src/shared/Network.h`
  * **Justification:** Declares ASIO UDP socket wrapping classes and helper methods utilized for communications by both server and client threads.
- [ ] `src/Network.cpp` -> `src/shared/Network.cpp`
  * **Justification:** Implements basic asynchronous socket wrappers for network interface setups shared across the program.
- [ ] `src/network/AsioNetworkManager.h` -> `src/shared/network/AsioNetworkManager.h`
  * **Justification:** Declares the unified network connection manager coordinating TCP sockets and UDP streams for client and server.
- [ ] `src/network/AsioNetworkManager.cpp` -> `src/shared/network/AsioNetworkManager.cpp`
  * **Justification:** Implements common network connection setups, TCP reads/writes, and client telemetry reassembly routines.
- [ ] `src/network/CommandHandlers.h` -> `src/shared/network/CommandHandlers.h`
  * **Justification:** Declares command callbacks, compiler error markers, and mutation triggers required by client issuers and server receivers.
- [ ] `src/network/CommandHandlers.cpp` -> `src/shared/network/CommandHandlers.cpp`
  * **Justification:** Maps RPC json packets to concrete world mutations, utilized on both client-side senders and server-side command receivers.

---

## 2. Server Library (PixelMapper_ServerLib)
Target directory: `src/server/`

- [ ] `src/App.h` -> `src/server/App.h`
  * **Justification:** Declares global real-time thread statistics, thread references, and authoritative world handlers.
- [ ] `src/App.cpp` -> `src/server/App.cpp`
  * **Justification:** Spawns the authoritative systems and runs `runPatch`, the real-time background rendering loop context of the server.
- [ ] `src/ArtnetSender.h` -> `src/server/ArtnetSender.h`
  * **Justification:** Declares the DMX packet broadcaster interface, a component used exclusively by the server's backend output threads.
- [ ] `src/ArtnetSender.cpp` -> `src/server/ArtnetSender.cpp`
  * **Justification:** Handles the transmission of compiled DMX channel frames to physical universe destinations, which is a server-only I/O task.
- [ ] `src/CanvasBinding.h` -> `src/server/CanvasBinding.h`
  * **Justification:** Declares canvas manipulation wrappers utilized directly inside server shader generation and procedural script routines.
- [ ] `src/CanvasBinding.cpp` -> `src/server/CanvasBinding.cpp`
  * **Justification:** Implements low-level grid drawing and noise routines called on the server during pattern calculations.
- [ ] `src/CueList.h` -> `src/server/CueList.h`
  * **Justification:** Declares the hold/fade states and properties of the cue sequence managed authoritatively by the server.
- [ ] `src/CueList.cpp` -> `src/server/CueList.cpp`
  * **Justification:** Registers the `CueAdvancer` system, which authoritatively checks timing and advances active cues on the server world.
- [ ] `src/FileWatcher.h` -> `src/server/FileWatcher.h`
  * **Justification:** Implements standard write-time detectors so the server can hot-reload modified scripting assets from the disk.
- [ ] `src/GenerativeEngine.h` -> `src/server/GenerativeEngine.h`
  * **Justification:** Declares motive transition settings and real-time generation thread models executed on the server.
- [ ] `src/GenerativeEngine.cpp` -> `src/server/GenerativeEngine.cpp`
  * **Justification:** Implements `GenerativeEngineRuntime` which updates generative state values and UBO variables on the server's real-time thread.
- [ ] `src/Patch.h` -> `src/server/Patch.h`
  * **Justification:** Declares offscreen GL settings, virtual framebuffers, and double-buffered GPU readback configurations used in real-time rendering.
- [ ] `src/Patch.cpp` -> `src/server/Patch.cpp`
  * **Justification:** Implements offscreen OpenGL framebuffer drawing, shader compilations, and PBO transfers that run in the headless backend.
- [ ] `src/PatchSerializer.h` -> `src/server/PatchSerializer.h`
  * **Justification:** Declares configuration loading and saving methods handled exclusively by the authoritative server.
- [ ] `src/PatchSerializer.cpp` -> `src/server/PatchSerializer.cpp`
  * **Justification:** Implements the server-side XML saving and loading engine to write state updates directly to disk.

---

## 3. Client Library (PixelMapper_ClientLib)
Target directory: `src/client/`

- [ ] `src/gui/ImGuiCanvas.h` -> `src/client/gui/ImGuiCanvas.h`
  * **Justification:** Manages the ImGui 2D/3D editor viewport drawing state, drag bounds, and canvas controls for client-side rendering.
- [ ] `src/gui/ImGuiHexView.h` -> `src/client/gui/ImGuiHexView.h`
  * **Justification:** Renders a read-only graphical DMX channel hex table used for visual confidence checks in the GUI.
- [ ] `src/gui/PixelMapperGui.cpp` -> `src/client/gui/PixelMapperGui.cpp`
  * **Justification:** Houses the GUI control panel draw logic, docking slots, sliders, palettes, and client-side view managers.
- [ ] `src/utils/imguiconfig.h` -> `src/client/utils/imguiconfig.h`
  * **Justification:** Defines custom vector conversion extensions for ImGui types, which is a GUI-only configuration.

---

## 4. Main Executable Entry
Target directory: `src/` (remains in the root of `src/`)

- [ ] `src/main.cpp` -> `src/main.cpp`
  * **Justification:** The primary program entry point that parses execution modes and starts standalone/headless/client loop setups.
