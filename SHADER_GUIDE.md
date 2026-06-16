# PixelMapper GLSL Shader Guide

This guide explains the architecture of PixelMapper's real-time GLSL rendering pipeline, the inputs/uniforms available to your fragment shaders, how physical 3D and projected 2D coordinates work, and Shadertoy compatibility.

---

## 1. Core Rendering Concept

PixelMapper utilizes a high-performance **1D Framebuffer & Point-Mapping** GPU rendering architecture:
1. **1D Viewport Mapping**: When rendering GLSL shaders, the engine sets the viewport resolution to `(pixelCount, 1)`.
2. **Point List Drawing**: It draws a list of points (using `GL_POINTS`), where each point maps to exactly 1 fragment in the 1D color texture.
3. **Double-Buffered PBO Readback**: The resulting pixel colors are copied asynchronously back to the CPU array using double-buffered Pixel Buffer Objects (PBOs) for low-latency encoding into DMX universe buffers.
4. **2D Quad Preview**: For editor previews and canvas backgrounds, the engine renders a full 2D quad of size `(256, 256)` using a separate viewport mapping.

---

## 2. Vertex Inputs (Coordinates)

Each point has two spatial variables exported from the vertex shader to the fragment shader:

### `vPixelPos2D` (or `#define iPixelPos2D`)
- **Type**: `vec2`
- **Range**: Normalized `[0.0, 1.0]` across both dimensions.
- **Meaning**: Re-projected spatial coordinates within the XY canvas projection boundaries of all active fixtures.
- **How it works**: During patch compilation, the engine projects the physical 3D positions onto a 2D plane (using the active projection mode, e.g., `TOP_DOWN_XY`). It computes the overall projected bounding box width and height, then normalizes each pixel:
  `u = (projected_x - min_x) / width`
  `v = (projected_y - min_y) / height`
- **Usage**: Used for traditional 2D sweep/wave patterns (like horizontal lines, gradients, and 2D canvas visuals). Since it is pre-normalized to `[0.0, 1.0]`, 2D shaders automatically scale to perfectly fill your layout regardless of its physical size.

### `vPixelPos3D` (or `#define iPixelPos3D`)
- **Type**: `vec3`
- **Range**: Raw physical coordinates (typically in millimeters, e.g., `[-500.0, 500.0]` or Z-depth coordinates).
- **Meaning**: The actual physical 3D coordinates calculated from the line/circle segment properties in space.
- **How it works**: This variable contains the **unscaled, physical coordinates** of the pixels in space. In the offline 2D depth slice preview, the engine automatically reconstructs the physical slice coordinates using the layout's minimum and maximum bounding box coordinates:
  `vPixelPos3D = mix(pixelPosMin, pixelPosMax, vec3(uv.x, uv.y, zSlice))`
- **Usage**: Ideal for volumetric shader effects that depend on 3D spatial relationships (like spheres, helix wraps, and depth-based waves). Sizing and distances in your math are specified in real-world physical units (e.g. millimeters).

---

## 3. Available Uniforms

Your shaders can declare and reference the following standard uniforms:

| Uniform | Type | Description |
| :--- | :--- | :--- |
| `time` / `iTime` | `float` | Elapsed playback time in seconds since the shader compilation. |
| `resolution` / `iResolution` | `vec2` / `vec3` | Dimensions of the render target. In point-rendering mode, this is `(pixelCount, 1.0)`. In 2D preview mode, this is `(256.0, 256.0)`. |
| `pixelCount` | `float` | The total number of output physical pixels in the active patch. |
| `pixelPosMin` | `vec3` | The minimum X, Y, and Z physical bounds of all mapped fixtures in the patch. |
| `pixelPosMax` | `vec3` | The maximum X, Y, and Z physical bounds of all mapped fixtures in the patch. |
| `zSlice` | `float` | Depth slider position `[0.0, 1.0]` (only bound during 2D offline preview rendering). |

---

## 4. 2D vs Volumetric 3D Rendering

### Standard 2D Rendering
Shaders that only use `iPixelPos2D` map their calculations to the normalized grid viewport:
- Coordinates are normalized, meaning `(0,0)` is always the bottom-left edge and `(1,1)` is the top-right edge of the canvas bounding box.
- Changes in fixture coordinates do not distort or offset the pattern coordinates; they only sample a different part of the grid.

### Volumetric 3D Rendering
Shaders that use `iPixelPos3D` utilize physical 3D measurements:
- Calculations are done in world space (e.g. `distance(iPixelPos3D, center)` in millimeters).
- This allows patterns to flow across physical 3D layouts (like columns, stairs, and 3D rigs) naturally based on actual spacing.
- **2D Preview Sync**: To ensure the 2D depth scanner preview matches, the preview vertex shader scales the quad's XY plane and Z-depth slices using the `pixelPosMin` and `pixelPosMax` uniforms.

---

## 5. Shadertoy Compatibility

PixelMapper compiles Shadertoy shaders directly without modification by wrapping them:
- **Detection**: The compiler checks if `mainImage` is defined in your shader source.
- **Wrapper Entry**: It automatically defines Shadertoy uniforms (like `iTime`, `iResolution`, `iMouse`, etc.) and adds the execution entry point:
  ```glsl
  void main() {
      mainImage(FragColor, iPixelPos2D * iResolution.xy);
  }
  ```
- This means you can copy-paste any 2D Shadertoy fragment shader directly into the editor.

---

## 6. Code Examples

### A. Simple 2D Sweep
```glsl
#version 150
in vec3 vPixelPos3D;
in vec2 vPixelPos2D;
out vec4 fragColor;
#define iPixelPos3D vPixelPos3D
#define iPixelPos2D vPixelPos2D
uniform float time;

void main() {
    float speed = 1.0;
    // Normalized 2D sweep from left to right
    float sweep = mod(time * speed, 1.0);
    float edge = smoothstep(sweep - 0.05, sweep, iPixelPos2D.x) 
               - smoothstep(sweep, sweep + 0.05, iPixelPos2D.x);
    fragColor = vec4(edge * 0.2, edge * 1.0, edge * 0.5, 1.0);
}
```

### B. Volumetric 3D Sphere Wave
```glsl
#version 150
in vec3 vPixelPos3D;
in vec2 vPixelPos2D;
out vec4 fragColor;
#define iPixelPos3D vPixelPos3D
#define iPixelPos2D vPixelPos2D
uniform float time;
uniform vec3 pixelPosMin;
uniform vec3 pixelPosMax;

void main() {
    vec3 pMin = pixelPosMin;
    vec3 pMax = pixelPosMax;
    // Fallback if bounds are flat or not calculated yet
    if (distance(pMin, pMax) < 1.0) {
        pMin = vec3(-200.0, -100.0, -100.0);
        pMax = vec3(200.0, 100.0, 100.0);
    }
    // Center point moves dynamically in physical space
    vec3 center = mix(pMin, pMax, vec3(
        0.5 + 0.3 * sin(time * 1.5),
        0.5 + 0.3 * cos(time * 1.0),
        0.5 + 0.3 * sin(time * 2.0)
    ));
    float dist = distance(iPixelPos3D, center);
    
    // Wave shell propagation
    float maxRadius = 0.5 * distance(pMin, pMax);
    float radius = mod(time * 150.0, maxRadius);
    float thickness = 30.0;
    float wave = smoothstep(thickness, 0.0, abs(dist - radius));
    
    vec3 col = vec3(0.5 + 0.5 * sin(iPixelPos3D.x * 0.01),
                    0.5 + 0.5 * sin(iPixelPos3D.y * 0.01),
                    0.5 + 0.5 * sin(iPixelPos3D.z * 0.01));
    fragColor = vec4(col * wave, 1.0);
}
```
