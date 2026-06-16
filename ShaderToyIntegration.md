# Technical Specification: Shadertoy Local Runner[cite: 1]

## 1. System Objective[cite: 1]
Develop a local graphics execution environment capable of compiling and rendering fragment shaders copied directly from Shadertoy.com without user modification.[cite: 1] The runner must dynamically generate the necessary GLSL wrapper, bind required standard uniforms, and manage the render loop.[cite: 1]

## 2. Architectural Context[cite: 1]
* **Target Graphics API:** OpenGL 3.3+ (Core Profile) or equivalent (compatible with GLSL `#version 330 core`).[cite: 1]
* **Backend Runtime:** Designed for a high-performance execution environment (e.g., modern C++23).[cite: 1]
* **State Management:** Uniforms and input states (mouse, time) must be strictly synchronized per-frame before draw calls.[cite: 1] If integrating into a larger system architecture, mapping uniform state updates and render passes to an Entity Component System (ECS) pattern will ensure clean data flow and predictable synchronization.[cite: 1]

## 3. The Shader Assembly Pipeline[cite: 1]
Shadertoy scripts are not standalone shaders; they are individual functions (`mainImage`).[cite: 1] The application must concatenate three distinct string blocks before submitting to the GLSL compiler:[cite: 1]

### 3.1. Header & Uniform Declarations (Block A)[cite: 1]
```glsl
#version 330 core

out vec4 FragColor;

uniform vec3      iResolution;           // Viewport resolution (pixels)
uniform float     iTime;                 // Shader playback time (s)
uniform float     iTimeDelta;            // Render time (s)
uniform float     iFrameRate;            // Shader frame rate
uniform int       iFrame;                // Shader playback frame
uniform float     iChannelTime[4];       // Channel playback time (in seconds)
uniform vec3      iChannelResolution[4]; // Channel resolution (in pixels)
uniform vec4      iMouse;                // Mouse coords. xy: current (if MLB down), zw: click
uniform sampler2D iChannel0;             // Input channel 0
uniform sampler2D iChannel1;             // Input channel 1
uniform sampler2D iChannel2;             // Input channel 2
uniform sampler2D iChannel3;             // Input channel 3
uniform vec4      iDate;                 // (year, month, day, time in seconds)
uniform float     iSampleRate;           // Sound sample rate (i.e., 44100)
```[cite: 1]

### 3.2. User Payload (Block B)[cite: 1]
This block is the raw, unmodified string copied from the Shadertoy editor.[cite: 1] It must contain the signature:[cite: 1]

```glsl
void mainImage( out vec4 fragColor, in vec2 fragCoord ) { ... }
```[cite: 1]

### 3.3. Execution Footer (Block C)[cite: 1]
```glsl
void main() {
    mainImage(FragColor, gl_FragCoord.xy);
}
```[cite: 1]

## 4. Uniform State Machine Specifications[cite: 1]
The runner must update the following uniform locations every frame loop:[cite: 1]

| Uniform | Type | CPU-Side Calculation / Notes |
| :--- | :--- | :--- |
| `iResolution` | `vec3` | `(width, height, 1.0)`. Update on window resize events. |[cite: 1]
| `iTime` | `float` | Elapsed time in seconds since the shader was initialized or reset. |[cite: 1]
| `iTimeDelta` | `float` | `currentTime - previousFrameTime`. |[cite: 1]
| `iFrame` | `int` | Integer counter starting at 0, incremented at the end of every render loop. |[cite: 1]
| `iDate` | `vec4` | (Year, Month (1-12), Day (1-31), Seconds since midnight). |[cite: 1]

### 4.1. Input Handling: The `iMouse` Specification[cite: 1]
The `iMouse` vector (`vec4`) requires specific state tracking logic in the application's event loop:[cite: 1]

**State 1: Mouse Left Button is DOWN and dragging:**[cite: 1]
* `iMouse.x` = Current X pixel coordinate.[cite: 1]
* `iMouse.y` = Current Y pixel coordinate.[cite: 1]
* `iMouse.z` = Initial click X pixel coordinate (positive).[cite: 1]
* `iMouse.w` = Initial click Y pixel coordinate (positive).[cite: 1]

**State 2: Mouse Left Button is RELEASED:**[cite: 1]
* `iMouse.x` = Retains the last known X coordinate before release.[cite: 1]
* `iMouse.y` = Retains the last known Y coordinate before release.[cite: 1]
* `iMouse.z` = `-abs(Initial click X)` (Sign inverted to indicate button is up).[cite: 1]
* `iMouse.w` = `-abs(Initial click Y)` (Sign inverted to indicate button is up).[cite: 1]

*(Note: Coordinate systems for `iMouse` and `gl_FragCoord` assume `(0,0)` is the bottom-left of the viewport.[cite: 1] If your windowing API uses top-left as `(0,0)`, the Y-axis must be inverted on the CPU side: `y = windowHeight - y`).*[cite: 1]

## 5. Advanced Implementation: Multipass (Buffers)[cite: 1]
If the runner is expanded to support Shadertoy's Buffer A, Buffer B, etc., the architecture must implement a Directed Acyclic Graph (DAG) or sequential pipeline for pass resolution:[cite: 1]

* **Framebuffers:** Each Buffer requires an Offscreen Framebuffer Object (FBO) with two color attachments to facilitate ping-pong rendering (reading from the previous frame's texture state while writing to the current frame's FBO attachment).[cite: 1]
* **Execution Order:** Passes must be executed sequentially.[cite: 1] Buffer A compiles to its own shader, drawing a full-screen quad to `FBO_A`.[cite: 1]
* **Binding:** If the main Image or Buffer B uses Buffer A as `iChannel0`, `FBO_A`'s read texture must be bound to `GL_TEXTURE0` before dispatching the subsequent draw call.[cite: 1]