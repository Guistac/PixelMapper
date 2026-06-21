PixelMapper Project Architecture & Developer Guide

Welcome to PixelMapper (internal working name: Nique Madrix). This comprehensive document provides an overview of the project's goals, structural design, physical network boundary separation, entity relationships, execution pipelines, and runtime execution thread models.

1. Project Goal & Core Tech Stack

PixelMapper is a cross-platform, real-time DMX pixel mapping and pattern generation tool. It enables users to:

Design Spatial Layouts (Patches): Place and arrange multi-pixel lighting fixtures (e.g., lines, circles) on a 2D canvas.

Assign DMX Patches: Map pixels to specific DMX universes and start addresses.

Generate Real-Time Visual Effects: Animate pixel colors using C++ procedural functions, Lua scripts, or custom GLSL fragment shaders.

Transmit DMX/ArtNet Data: Stream raw channel data over UDP using the ArtNet protocol to multiple hardware controllers or visualizers.

Core Tech Stack

Language: C++20

Build System: CMake (configured with CMakeLists.txt)

Entity Component System (ECS): Flecs (used for managing scene graph state, layout observers, systems, and UI configuration)

GUI Frame & Context: GLFW, OpenGL 3.x, and Dear ImGui (configured with Docking and Viewports)

Asynchronous Networking: Asio (header-only network library handling low-latency UDP/TCP operations)

Scripting Integration: Sol2 (for running Lua scripting contexts)

Text Editor: goossens/ImGuiColorTextEdit (embedded in the shader and script editors)

Math Library: GLM (OpenGL Mathematics)

Data/Serialization: TinyXML2 (UI state) and nlohmann::json (network RPCs)

2. Physical Design & Build System (CMake Separation)

To enforce an absolute "Air Gap" compile-time boundary between the client GUI and the authoritative server, PixelMapper's source code is partitioned into three distinct CMake Object Libraries. Cross-inclusion of headers across disallowed boundaries is prohibited.

graph LR
    subgraph Common Library [PixelMapper_CommonLib]
        Shared[Namespace: PixelMapper::Shared<br/>Components, RPC Structs, CoreModule]
    end
    subgraph Server Library [PixelMapper_ServerLib]
        Server[Namespace: PixelMapper::Server<br/>rtPatchRunner, Authoritative Systems,<br/>Asio TCP/UDP Server Acceptor]
    end
    subgraph Client Library [PixelMapper_ClientLib]
        Client[Namespace: PixelMapper::Client<br/>ImGui views, Canvas previews,<br/>Asio TCP/UDP Client Sessions]
    end

    Server --> Shared
    Client --> Shared
    Server -.->|Cross-Inclusion Forbidden| Client
    Client -.->|Cross-Inclusion Forbidden| Server


A. PixelMapper_CommonLib (The Vocabulary)

Namespace: PixelMapper::Shared

Contents: Packed network structs (e.g., ColorRGBW, EngineStateUBO), network RPC opcodes, the PixelMapperCoreModule for component registration, and pure data structs.

Component Dictionary: All pure Flecs component definitions and tags have been extracted from logic headers and reside safely in src/shared/components/PatchComponents.h and AppComponents.h.

Constraints: Contains NO business logic, NO ImGui headers, and NO OpenGL rendering context calls. Linked by both Client and Server.

B. PixelMapper_ServerLib (The Source of Truth)

Namespace: PixelMapper::Server

Contents: rtPatchRunner rendering engine, authoritative Flecs logical systems, headless OpenGL setup, and server command handlers.

Constraints: Links to CommonLib. Forbidden from referencing anything in ClientLib.

C. PixelMapper_ClientLib (The Presentation Mirror)

Namespace: PixelMapper::Client

Contents: ImGui GUI panels, local 2D playback preview, read-only local Flecs presentation systems, and client command handlers.

Constraints: Links to CommonLib. Forbidden from referencing anything in ServerLib.

3. Runtime Modes & Threading Architecture

The unified executable supports three runtime modes determined by CLI arguments (--server, --client, or --standalone). To maintain high visual responsiveness, the application isolates user interaction from the high-frequency rendering and networking engine.

Runtime Modes

Headless Server Mode (--server): Runs the authoritative rendering engine and DMX broadcaster. Hidden GLFW window. TCP acceptor (7777) and UDP telemetry sender.

Client GUI Mode (--client): Provides the visible editor interface. Visible GLFW window. TCP client socket and UDP receiver (7778). Output engine inactive.

Standalone Mode (--standalone, Default): Launches both Server and Client concurrently in a single process, communicating over a local loopback (127.0.0.1) socket.

Underlying Thread Models

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
        N[ASIO io_context loop] -->|async_send_to| O[UDP/TCP Network Plane]
    end

    G -.->|Atomic Swap| I
    M -->|asio::post / send| N


4. Entity Component System (ECS) & Replication

PixelMapper models its scene graphs, cues, effect templates, and generative states through components, relationship pairs, and systems using Flecs. The ECS is split across the network boundary to synchronize states.

4.1 Replicated ECS Architecture

sequenceDiagram
    participant C as Client (flecs::world)
    participant S as Server (flecs::world)
    Note over C,S: Startup: Both import PixelMapperCoreModule to match IDs
    C->>S: TCP: EntityMutation RPC (e.g. Request new Fixture)
    Note over S: Server spawns entity locally in authoritative world
    S->>C: TCP: SyncWorldState RPC (Broadcasts serialized JSON state)
    Note over C: Client clears local representation & rebuilds via from_json


Component Registration: Both worlds call world.import<PixelMapperCoreModule>() at startup to ensure component IDs match exactly for binary/JSON serialization.

Entity Authority: Only the Server can spawn/delete entities. The client dispatches mutations via TCP RPCs.

Network Addressing: Entities are targeted by their hierarchical paths (e.g., "Patches/Patch1/FixtureFolder/Fixture1").

4.2 Folder Hierarchy Layout

PixelMapperApp (Root)

Patches (Folder)

Patch (Entity):

FixtureFolder -> holds Fixture entities.

DmxOutputFolder -> holds Artnet::Universe entities.

ArtnetDeviceFolder -> holds Artnet::Device entities.

CueFolder -> holds CueList::Cue entities.

EffectFolder -> holds EffectBank::Effect entities.

PaletteFolder -> holds Generative::Palette::Is entities.

MotiveFolder -> holds Generative::Motive::Is entities.

4.3 Core Components Map

Component

Target Entity

Purpose

Fixture::Layout

Fixture

Defines number of pixels and bytes per pixel (e.g., 3 for RGB, 4 for RGBW).

Fixture::DmxAddress

Fixture

Defines start universe and start address (offset 0–511).

Fixture::PixelData

Fixture

Hosts dynamic runtime vectors of positions (glm::vec3) and colors.

Shape::Line / Circle

Fixture

Shape configurations containing dimensional parametric properties.

Artnet::Universe::...

Universe

Defines universe IDs and holds the raw uint8_t buffer[512].

Patch::RenderArea

Patch

Spatial bounding box of all active pixels.

Patch::GPUResources

Patch

Persistent OpenGL handles for FBOs, textures, PBOs, blend programs.

Patch::GPUProgram

Patch & Effect

Caches the compiled GLSL shader handle, source string, and compiler log.

CueList::Cue::...

Cue

Tag components, hold/fade sequencer durations, and ordering index.

EffectBank::Effect...

Effect

Effect tags and GLSL source code.

Patch::Settings

Patch

Network settings, white mode, highlight settings, file paths.

App::UIConfig

App

Stores visible windows, grid settings, editors, preview flags.

Generative::Settings

Patch

Generative timeline timings, parameters, and override states.

Generative::Palette...

Palette

Palette tag, color stops, and custom interpolation modes.

Generative::Motive...

Motive

Base physics parameters (velocity, complexity, scale, distortion, etc).

4.4 Relationship Pairs & Exclusive Tags

(WithShape, Shape::Line) or (WithShape, Shape::Circle) on fixtures.

(SelectedPatch, patch_entity) on the App root (exclusive).

(SelectedFixture, fixture_entity) on a Patch (exclusive).

(InUniverse, universe_entity) on a Fixture (can be multiple if spanning universes).

(TargetEffect, effect_entity) on a sequencer Cue (pairs the Cue with its active shader effect).

4.5 ECS Pipeline & Compilation Sequence

Observers and systems recalculate spatial layouts dynamically. To keep compilation times under ~2ms, PatchProgram::compile utilizes a fine-grained strategy (FBO Recycling, Shader Link Recycling):

[Layout Changed] -> UpdateFixtureLayout System (Resizes vectors, adds dirty tags)
                          |
                          v
[Shape Dragged]  -> UpdateLine/Circle PixelPositions (Computes local coords)
                          |
                          v
                    UpdateRenderArea (Computes absolute bounding box)
                          |
                          v
[DMX Re-patched] -> UpdateDmxOutputMap (Allocates/deletes Universe entities)
                          |
                          v
[Program Dirty]  -> PatchProgramCompile (Builds flat array, pushes atomic pointer to RT)


5. Client-Server Network Protocol Specification

PixelMapper separates UI interactions and real-time outputs via a Dual-Plane Network Protocol managed asynchronously using the Asio framework.

5.1 Dual-Plane Network Protocol Overview

All cross-boundary communication is split into two physical socket types to isolate reliability from throughput.

+─────────────────────────────────────────────────────────────+
|                         PixelMapper                         |
|   Client Session                                            |
+──────────────────────────────┬──────────────────────────────+
                               │ (Dual-Plane Protocol)
             Command Plane     │      Telemetry Plane
             (TCP, Port 7777)  │      (UDP, Port 7778)
                               │
+──────────────────────────────▼──────────────────────────────+
|   Server Session                                            |
+─────────────────────────────────────────────────────────────+


The Command Plane (TCP): Reliable, ordered transmission of state changes, file I/O operations, compile commands, and transport commands. Every TCP payload is framed with a header: [Length (uint32_t)][Type/RPC Code (uint16_t)][JSON Payload (Variable string)]

The Telemetry Plane (UDP): High-frequency, fire-and-forget streaming of real-time engine telemetry (pixel arrays and motive UBO variables) at 60Hz. Serialized buffers are sliced into payloads of maximum 1200 bytes to stay safely below the standard 1500-byte Ethernet MTU boundary.

5.2 Command Plane RPC Reference (TCP)

The following RPC codes are passed as uint16_t in the frame headers:

RPC Code

Name

Direction

Payload Structure & Description

1

SyncWorldState

Server $\rightarrow$ Client

Full JSON string serialized representation of the Server's authoritative Flecs world. On receipt, the client clears its world and invokes world.from_json().

2

EntityMutation

Client $\rightarrow$ Server

Serialized JSON containing: { "entityPath": string, "componentName": string, "componentData": string }. Triggers dynamic component deserialization and marks systems dirty.

3

ApplyScript

Client $\rightarrow$ Server

Raw source code payload containing GLSL or Lua script edits. Requests Server validation compilation.

4

CompilationResult

Server $\rightarrow$ Client

Structured JSON payload containing error codes, error line markers, and raw compiler log output strings.

5

FileIORequest

Client $\rightarrow$ Server

XML persistence requests to Save/Load patches on disk.

6

TransportControl

Client $\rightarrow$ Server

Playback controls (e.g. Play, Stop, next Cue Index, transition speed).

5.3 Component Mutation Deserialization Pipeline

When the Client alters a GUI element, it serializes the corresponding component and dispatches it:

// 1. Serialize component on Client
std::string componentJson = world.to_json<T>(componentPtr);

// 2. Wrap in JSON RPC payload
nlohmann::json rpc;
rpc["entityPath"] = entity.path();
rpc["componentName"] = world.entity<T>().name();
rpc["componentData"] = componentJson;

sendCommandToServer(CommandType::EntityMutation, rpc.dump());


Upon receipt, the Server maps and updates the raw memory block:

// 3. Apply mutation on Server
flecs::entity entity = world.lookup(entityPath.c_str());
flecs::entity compType = world.lookup(componentName.c_str());

void* ptr = entity.get_mut(compType);
flecs::ptr_from_json(world.c_ptr(), compType.id(), ptr, componentData.c_str(), nullptr);

// 4. Notify observers to recalculate topologies
entity.modified(compType);


5.4 Remote Compilation Pipeline

To edit Lua scripts and GLSL shaders safely without risk of freezing or crashing the client application, a split-compilation pattern is enforced:

sequenceDiagram
    participant C as Client (ImGui Editor)
    participant S as Server (Headless Context)
    C->>S: TCP: ApplyScript RPC (raw string source code)
    Note over S: Compiles inside headless OpenGL or Lua VM
    alt Compilation Failed
        S->>C: TCP: CompilationResult (JSON line-errors)
        Note over C: Renders red error markers in Editor
    else Compilation Succeeded
        Note over S: Swaps running PatchProgram in rtPatchRunner
        S->>C: TCP: CompilationResult (Success tag)
        Note over C: Authorizes Client to compile local 2D preview
    end


5.5 Telemetry Plane Specification (UDP)

The Telemetry plane streams packed binary structures at 60Hz.

Telemetry Slice Header:

#pragma pack(push, 1)
struct TelemetrySliceHeader {
    uint32_t magic;             // TELEMETRY_SLICE_MAGIC (0x50584D53)
    uint32_t frameNumber;       // Monotonically increasing frame index
    uint32_t totalFrameSize;    // Reassembled size (EngineStateUBO + Pixels)
    uint32_t pixelCount;        // Number of pixels in the frame
    uint16_t sliceIndex;        // Slice index (0-based)
    uint16_t sliceCount;        // Total slices in the frame
    uint32_t payloadOffset;     // Target buffer offset
    uint16_t payloadSize;       // Payload size in bytes
};
#pragma pack(pop)


Slice Reassembly Pipeline:

Server Slicing: The server packs the EngineStateUBO struct followed immediately by the ColorRGBW array. If the total byte size exceeds 1200 bytes, it segments it into $N$ slices, filling in payloadOffset and sliceIndex in each header.

Client Assembly: The Client's ASIO thread receives slices. It compares incoming frameNumber values:

If a new frameNumber is received, it discards any incomplete frames and allocates a reassembly buffer.

It writes payload slices directly to reassembledBuffer + payloadOffset.

When sliceCount slices are gathered, it locks the telemetry cache mutex and writes the complete frame to memory.

5.6 Thread Safety & OpenGL Affinity

OpenGL driver resources are bound strictly to the thread that creates them. To avoid context collisions and thread conflicts:

GUI Context Bounds: The visible viewport context is bound exclusively to the Main GUI Thread.

RT Context Bounds: The headless shared rendering context is bound exclusively to the rtPatchRunner thread.

No OpenGL on ASIO: The ASIO network socket thread must never invoke OpenGL functions.

Shared Telemetry Cache: Slices reassembled by the ASIO thread are copied to a CPU cache protected by clientTelemetryMutex. At the start of the next GUI frame, the main thread safely copies this data to local presentation structures and uploads parameters to the GPU:

void applyTelemetryToClientProgram() {
    std::lock_guard<std::mutex> lock(PixelMapper::App::clientTelemetryMutex);
    if (PixelMapper::App::clientTelemetryDataNew) {
        auto program = std::atomic_load(&PixelMapper::App::currentPatchProgram);
        if (program) {
            // Copy pixels to client array for confidence visualization
            uint32_t toCopy = std::min(program->pixelCount, (uint32_t)PixelMapper::App::clientTelemetryPixels.size());
            if (toCopy > 0 && program->pixelColors) {
                std::memcpy(program->pixelColors, PixelMapper::App::clientTelemetryPixels.data(), toCopy * sizeof(PixelMapper::ColorRGBW));
            }
            // Upload engine state to the client's GPU UBO for previews
            if (program->glslUboId > 0) {
                glBindBuffer(GL_UNIFORM_BUFFER, program->glslUboId);
                glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PixelMapper::Generative::EngineStateUBO), &PixelMapper::App::clientTelemetryUbo);
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
            }
        }
        PixelMapper::App::clientTelemetryDataNew = false;
    }
}


This routine runs at the top of the GUI update loop, guaranteeing thread-safe frame rendering.

6. Real-Time Execution Engine

The Real-Time Thread (rtPatchRunner) runs a lock-free loop against a flat, compiled representation of the scene called PatchProgram.

6.1 GPU Point-Mapping Concept & Coordinates

To render shaders on custom non-grid fixtures, PixelMapper uses a 1D Framebuffer & Point-Mapping pipeline:

1D Viewport: Resolution is exactly (pixelCount, 1).

Point Drawing: Uses glDrawArrays(GL_POINTS, 0, pixelCount).

Readback: Double-buffered PBOs copy the texture asynchronously back to vfbPixels.

Available Coordinates in Shaders:

vPixelPos2D (vec2): Projected grid coords normalized [0.0, 1.0]. Ideal for 2D sweeps.

vPixelPos3D (vec3): Physical world coordinates (millimeters). Ideal for volumetric 3D graphics.

6.2 Uniform Buffer Object (UBO) Layout

The engine injects real-time generative parameters into shaders via a UBO:

layout (std140, binding = 0) uniform EngineState {
    float time;               // Playback time in seconds
    float pixelCount;         // Total output pixels
    float zSlice;             // Diagnostic 2D preview slice depth [0-1]
    int activeStops;          // Number of active stops in morphing palette
    vec3 pixelPosMin;         // Bounding box minimum bounds
    vec3 pixelPosMax;         // Bounding box maximum bounds
    
    // Motive physics parameters (animated via wandering Perlin noise LFOs)
    float velocity;
    float complexity;
    float scale;
    float distortion;
    float asymmetry;
    float intensity;

    // Palette gradient stops (up to 16)
    vec4 stopColors[16];      // Colors (RGBA)
    float stopPositions[16];  // Stop positions [0.0 - 1.0]
};


6.3 Post-Rendering & Encoding

RGB to RGBW White Extraction: Auto-extracts white (W = min(R,G,B)) based on whiteMode settings.

Crossfading: CPU-side memory interpolation (for Lua/C++) or GPU-side FBO blending using glslBlendProgram for GLSL transitions.

Encoding: Fast memory copy of pixels to pre-compiled DMX universe map offsets:

void encode(PatchProgram* program) {
    for (int i = 0; i < program->p2uCount; i++) {
        const PatchProgram::Pix2UniCopyInstr& map = program->p2us[i];
        uint8_t* dest = program->universes[map.universeIndex].buffer + map.universeOffset;
        std::memcpy(dest + bytesWritten, src + pByte, toCopy);
    }
}


7. Modular Rendering: Lua Scripting

To support runtime programmability, PixelMapper implements a modular Lua scripting engine against the 1D canvas.

Isolation: Each PatchProgram owns a dedicated sol::state (Lua VM).

Direct Mapping: Script execution bypasses PCI-e sampling by direct std::memcpy of the 1D virtual framebuffer back to the pixel arrays.

Example Lua Implementation:

function update(canvas, time)
    local count = canvas:getPixelCount()
    for i = 0, count - 1 do
        local pos3d = canvas:getPixelPos3D(i)
        local r = 127 + 127 * math.sin(pos3d.x * 0.01 + time)
        canvas:setPixel(i, r, 0, 0, 255)
    end
end


8. Generative Visual Engine & Show Control

The Generative Visual Engine orchestrates three decoupled queues: Palettes, Motives, and Shaders.

8.1 Zero-Width Palette Morphing

To transition between palettes with different stop counts smoothly:

Pre-Flight Padding: Pads the smaller palette to match the size of the larger by cloning stops using zero-distance boundaries.

Morphing: Linearly interpolates positions and colors using a smoothstep(0.0, 1.0, progress) easing function.

Clean-up: Restores activeStops to the true target size to save GPU cycles.

8.2 Engine Controls & Previews

Cue Priority: If an explicit Cue is triggered (activeCueIndex >= 0), its shader takes absolute rendering priority over the Generative Shader queue.

Editor Overrides: For offline programming, checking "Override Generative Data for Preview" swaps the live UBO parameters with custom sliders during the offline render pass, restoring the live state immediately afterward.

9. Fixture Setup Scripting Reference

The Fixture Setup Script allows programmatically constructing, clearing, modifying, and patching lighting fixtures using design-time Lua script execution inside the editor workspace.

9.1 Intent & Architectural Overview

Manually placing and patching hundreds of individual fixtures or complex 3D arrays (e.g. concentric circles, volumetric trees, spirals) is error-prone. The Fixture Setup Script solves this by exposing the internal Flecs entity mutations through a sandboxed C++/Lua binding.

graph TD
    subgraph Client App [Client GUI / Main Thread]
        A[ImGui Fixture Setup Window] -->|Debounced Edit Check| B[Validate Syntax]
        A -->|Click RUN| C[Instantiate sol::state VM]
        C -->|Register User Types| D[LuaPatch & LuaFixture]
        D -->|Execute Script| E[Flecs ECS mutations]
        E -->|Dirty Flags| F[DmxMapDirty, RenderAreaDirty, ProgramDirty]
    end
    subgraph Server App [Headless Server Thread]
        F -->|TCP command Sync| G[Authoritative Server World]
    end


Key Architectural Rules:

Main Thread Affinity: The setup script runs strictly on the Main GUI Thread (within the WindowFixtureSetupScript system). This ensures that fixture creation and memory allocation do not conflict with active OpenGL resources or background world queries.

Design-Time Sandbox: Execution happens inside an isolated Lua virtual machine (sol::state), separate from the runtime show VM.

Dirty-State Signals: Once the script completes execution, the patch entity is flagged with DmxMapDirty, RenderAreaDirty, and ProgramDirty. This tells the engine to recalculate the canvas bounding boxes, update the DMX encoding arrays, rebuild the 1D OpenGL pipeline, and synchronize the client/server state over TCP.

9.2 API Reference

The script is executed with a global object patch representing the active LuaPatch instance.

Patch Interface (patch)

Function

Return Type

Description

patch:clear_fixtures()

void

Deletes all fixture entities from the current patch.

patch:create_line(name, sx, sy, sz, ex, ey, ez, numPixels, channels)

Fixture

Spawns a new line fixture in the patch at start (sx,sy,sz) to end (ex,ey,ez) with the specified number of pixels and DMX channels per pixel. Returns a Fixture object wrapper.

patch:get_fixtures()

Fixture[]

Returns a standard Lua table array containing wrappers for all fixtures in the patch.

Fixture Interface

Wraps a numerical Flecs entity ID to perform safe mutations:

Function

Return Type

Description

f:id()

number

Returns the unique numeric Flecs entity ID.

f:name()

string

Returns the fixture's name.

f:set_name(name)

void

Sets a new name string.

f:remove()

void

Deletes the fixture from the patch.

f:get_shape_type()

string

Returns "Line", "Circle", or "None".

f:get_line_properties()

sx, sy, sz, ex, ey, ez

Returns start and end coordinates as 6 multiple values.

f:set_line_properties(sx, sy, sz, ex, ey, ez)

void

Sets new line coordinates.

f:get_layout()

pixelCount, channels

Returns pixel count and channels per pixel.

f:set_layout(pixelCount, channels)

void

Sets layout configuration (e.g. 3 channels for RGB, 4 for RGBW).

f:get_dmx()

universe, startAddress

Returns start universe and start address [0-511].

f:set_dmx(universe, startAddress)

void

Sets the start DMX patch target coordinates.

9.3 Code Examples

A. Constructing a 3x3 Fixture Grid
Clears the patch and populates 9 parallel line fixtures, sequentially offset in DMX:

-- Clear current patch
patch:clear_fixtures()

local count = 1
for row = 0, 2 do
    for col = 0, 2 do
        local x = col * 40
        local y = row * 40
        local name = "Grid_" .. row .. "_" .. col
        
        -- Create an 8-pixel RGBW line (channels = 4)
        local f = patch:create_line(name, x, y, 0, x + 30, y, 0, 8, 4)
        
        -- Patch DMX address offset sequentially
        f:set_dmx(0, (count - 1) * 32)
        count = count + 1
    end
end


B. Volumetric Circle Spiral (Hanging Fixtures)
Arranges fixtures vertically downwards in space in a circular spiral arrangement:

patch:clear_fixtures()

local numFixtures = 12
local radius = 300 -- mm
local heightRange = 1000 -- mm
local dmxUniverse = 0
local dmxAddress = 0

for i = 1, numFixtures do
    local angle = (i - 1) * (2 * math.pi / numFixtures)
    local x = radius * math.cos(angle)
    local z = radius * math.sin(angle)
    local y = -(i - 1) * (heightRange / numFixtures)
    
    -- Hanging vertical line (from Y downward)
    local f = patch:create_line("Spiral_"..i, x, y, z, x, y - 500, z, 16, 4)
    f:set_dmx(dmxUniverse, dmxAddress)
    
    -- Auto-advance DMX universes
    dmxAddress = dmxAddress + (16 * 4) -- 64 channels
    if dmxAddress >= 512 then
        dmxUniverse = dmxUniverse + 1
        dmxAddress = 0
    end
end


10. Development Rules & Implementation Roadmap

10.1 Strict Development Rules

Absolute Air Gap: The Client GUI thread and Server RT thread must never directly share pointers. Direct reads from CPU-side framebuffers by the Client canvas are strictly forbidden (use Telemetry cache only).

No Global State Sync: Shared atomic variables must only be updated via the network loopback.

No OpenGL from ASIO Threads: The ASIO network socket thread must never make driver calls.

Namespace Discipline: Classes/components must strictly reside under Shared, Server, or Client.

10.2 Configuration Parameters

TCP Command Port: 7777

UDP Telemetry Port (Client Receive): 7778

Max UDP Payload Size: 1200 bytes (MTU-safe)

Authoritative World Refresh Rate: 60 Hz (~16.6ms frame target)

Real-time Loop Target Interval: ~3 ms (330 Hz update capacity)

10.3 Client-Server Progress & Roadmap

Completed:

Unified CMake build targets & library separation.

Asynchronous TCP server/client structures and frame payloads.

Server UDP Telemetry broadcaster (MTU slicing) and Client assembler.

Dynamic reflection serializer (ptr_from_json).

Pending (Active Sprint):

Hook GUI Edits to TCP Serialization Triggers: Wrap GUI edit setters to dispatch via Network::AsioNetworkManager::getInstance().sendEntityMutation<T>().

Bind Remote Compiler Errors: Route CompilationResult RPC data to the ImGui text editor state on the Client.

SyncWorldState Binding: After invoking world.from_json(), resolve hierarchical paths to re-bind GUI selection pointers (e.g., active UI selections) to the newly deserialized entities.
