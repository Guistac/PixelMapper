#pragma once
#include <array>

namespace PixelMapper {

struct Preset {
    const char* name;
    const char* glsl;
};

// ─────────────── Preset GLSL Shaders ────────────────────────────
// All shaders expect:
//   uniform float time;
//   uniform vec2  resolution;
//   in vec3 vPixelPos3D;
//   in vec2 vPixelPos2D;
//   out vec4 fragColor;

static constexpr const char* GLSL_HEADER =
    "#version 150\n"
    "in vec3 vPixelPos3D;\n"
    "in vec2 vPixelPos2D;\n"
    "out vec4 fragColor;\n"
    "#define iPixelPos3D vPixelPos3D\n"
    "#define iPixelPos2D vPixelPos2D\n"
    "uniform float time;\n"
    "uniform vec2 resolution;\n\n";

static constexpr const char* GLSL_PLASMA =
    "#version 150\n"
    "in vec3 vPixelPos3D;\n"
    "in vec2 vPixelPos2D;\n"
    "out vec4 fragColor;\n"
    "#define iPixelPos3D vPixelPos3D\n"
    "#define iPixelPos2D vPixelPos2D\n"
    "uniform float time;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "void main() {\n"
    "    float x = iPixelPos2D.x * 10.0;\n"
    "    float y = iPixelPos2D.y * 10.0;\n"
    "    float v1 = sin(x + time);\n"
    "    float v2 = sin(10.0 * (x * sin(time / 2.0) + y * cos(time / 3.0)) + time);\n"
    "    float cx = x + 5.0 * sin(time / 5.0);\n"
    "    float cy = y + 5.0 * cos(time / 3.0);\n"
    "    float v3 = sin(sqrt(cx*cx + cy*cy + 1.0) - time);\n"
    "    float v = (v1 + v2 + v3) / 3.0;\n"
    "    float r = sin(v * 3.1415) * 0.5 + 0.5;\n"
    "    float g = sin(v * 3.1415 + 2.094) * 0.5 + 0.5;\n"
    "    float b = sin(v * 3.1415 + 4.188) * 0.5 + 0.5;\n"
    "    fragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

static constexpr const char* GLSL_HORIZONTAL_SWEEP =
    "#version 150\n"
    "in vec3 vPixelPos3D;\n"
    "in vec2 vPixelPos2D;\n"
    "out vec4 fragColor;\n"
    "#define iPixelPos3D vPixelPos3D\n"
    "#define iPixelPos2D vPixelPos2D\n"
    "uniform float time;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "void main() {\n"
    "    float speed = 0.5;\n"
    "    float sweep = mod(time * speed, 1.0);\n"
    "    float bar   = smoothstep(sweep - 0.04, sweep, iPixelPos2D.x)\n"
    "                - smoothstep(sweep, sweep + 0.04, iPixelPos2D.x);\n"
    "    float bg = 0.05;\n"
    "    float brightness = bg + bar * (1.0 - bg);\n"
    "    fragColor = vec4(brightness, brightness, brightness * 0.8, 1.0);\n"
    "}\n";

static constexpr const char* GLSL_VERTICAL_CHASE =
    "#version 150\n"
    "in vec3 vPixelPos3D;\n"
    "in vec2 vPixelPos2D;\n"
    "out vec4 fragColor;\n"
    "#define iPixelPos3D vPixelPos3D\n"
    "#define iPixelPos2D vPixelPos2D\n"
    "uniform float time;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "void main() {\n"
    "    float speed = 0.5;\n"
    "    float sweep = mod(time * speed, 1.0);\n"
    "    float bar   = smoothstep(sweep - 0.04, sweep, iPixelPos2D.y)\n"
    "                - smoothstep(sweep, sweep + 0.04, iPixelPos2D.y);\n"
    "    float bg = 0.05;\n"
    "    float brightness = bg + bar * (1.0 - bg);\n"
    "    fragColor = vec4(brightness * 0.8, brightness, brightness, 1.0);\n"
    "}\n";

static constexpr const char* GLSL_RAINBOW_WAVE =
    "#version 150\n"
    "in vec3 vPixelPos3D;\n"
    "in vec2 vPixelPos2D;\n"
    "out vec4 fragColor;\n"
    "#define iPixelPos3D vPixelPos3D\n"
    "#define iPixelPos2D vPixelPos2D\n"
    "uniform float time;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "vec3 hsv2rgb(vec3 c) {\n"
    "    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);\n"
    "    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
    "    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
    "}\n"
    "void main() {\n"
    "    float hue = iPixelPos2D.x + time * 0.1;\n"
    "    vec3 col = hsv2rgb(vec3(mod(hue, 1.0), 1.0, 1.0));\n"
    "    fragColor = vec4(col, 1.0);\n"
    "}\n";

static constexpr const char* GLSL_FIRE =
    "#version 150\n"
    "in vec3 vPixelPos3D;\n"
    "in vec2 vPixelPos2D;\n"
    "out vec4 fragColor;\n"
    "#define iPixelPos3D vPixelPos3D\n"
    "#define iPixelPos2D vPixelPos2D\n"
    "uniform float time;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "float noise(vec2 p) {\n"
    "    vec2 i = floor(p); vec2 f = fract(p);\n"
    "    vec2 u = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(hash(i), hash(i+vec2(1,0)), u.x),\n"
    "               mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), u.x), u.y);\n"
    "}\n"
    "void main() {\n"
    "    vec2 q = iPixelPos2D;\n"
    "    q.y = 1.0 - q.y;  // flame rises upward\n"
    "    float t = time * 1.5;\n"
    "    float f = noise(q * vec2(3.0, 5.0) + vec2(0.0, -t));\n"
    "    f += 0.5 * noise(q * vec2(6.0, 10.0) + vec2(0.0, -t * 1.3));\n"
    "    f = clamp(f - q.y * 1.1, 0.0, 1.0);\n"
    "    vec3 fire = mix(vec3(1.0, 0.0, 0.0), vec3(1.0, 0.9, 0.0), f);\n"
    "    fire = mix(vec3(0.0), fire, pow(f, 0.4));\n"
    "    fragColor = vec4(fire, 1.0);\n"
    "}\n";

static constexpr const char* GLSL_SPARKLE =
    "#version 150\n"
    "in vec3 vPixelPos3D;\n"
    "in vec2 vPixelPos2D;\n"
    "out vec4 fragColor;\n"
    "#define iPixelPos3D vPixelPos3D\n"
    "#define iPixelPos2D vPixelPos2D\n"
    "uniform float time;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "void main() {\n"
    "    vec2 cell = floor(iPixelPos2D * 20.0);\n"
    "    float rnd  = hash(cell + floor(time * 12.0));\n"
    "    float spark = step(0.94, rnd);\n"
    "    fragColor = vec4(vec3(spark), 1.0);\n"
    "}\n";

static constexpr const char* GLSL_COLOR_SOLID =
    "#version 150\n"
    "in vec3 vPixelPos3D;\n"
    "in vec2 vPixelPos2D;\n"
    "out vec4 fragColor;\n"
    "#define iPixelPos3D vPixelPos3D\n"
    "#define iPixelPos2D vPixelPos2D\n"
    "uniform float time;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "vec3 hsv2rgb(vec3 c) {\n"
    "    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);\n"
    "    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
    "    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
    "}\n"
    "void main() {\n"
    "    float hue = mod(time * 0.05, 1.0);\n"
    "    vec3 col = hsv2rgb(vec3(hue, 0.9, 1.0));\n"
    "    fragColor = vec4(col, 1.0);\n"
    "}\n";

static constexpr const char* GLSL_BREATHING =
    "#version 150\n"
    "in vec3 vPixelPos3D;\n"
    "in vec2 vPixelPos2D;\n"
    "out vec4 fragColor;\n"
    "#define iPixelPos3D vPixelPos3D\n"
    "#define iPixelPos2D vPixelPos2D\n"
    "uniform float time;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "void main() {\n"
    "    float breathe = sin(time * 1.2) * 0.5 + 0.5;\n"
    "    breathe = breathe * breathe;  // ease\n"
    "    fragColor = vec4(breathe, breathe * 0.85, breathe * 1.0, 1.0);\n"
    "}\n";

static constexpr const char* GLSL_STROBE =
    "#version 150\n"
    "in vec3 vPixelPos3D;\n"
    "in vec2 vPixelPos2D;\n"
    "out vec4 fragColor;\n"
    "#define iPixelPos3D vPixelPos3D\n"
    "#define iPixelPos2D vPixelPos2D\n"
    "uniform float time;\n"
    "uniform vec2 resolution;\n"
    "\n"
    "void main() {\n"
    "    float hz = 4.0;  // strobe frequency\n"
    "    float on = step(0.5, fract(time * hz));\n"
    "    fragColor = vec4(on, on, on, 1.0);\n"
    "}\n";

// ────────────────── Preset Registry ─────────────────────────────

static constexpr Preset kPresets[] = {
    { "Plasma (default)",    GLSL_PLASMA           },
    { "Horizontal Sweep",    GLSL_HORIZONTAL_SWEEP },
    { "Vertical Chase",      GLSL_VERTICAL_CHASE   },
    { "Rainbow Wave",        GLSL_RAINBOW_WAVE     },
    { "Fire",                GLSL_FIRE             },
    { "Sparkle",             GLSL_SPARKLE          },
    { "Color Solid (cycle)", GLSL_COLOR_SOLID      },
    { "Breathing",           GLSL_BREATHING        },
    { "Strobe",              GLSL_STROBE           },
};

static constexpr int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

} // namespace PixelMapper
