#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <random>
#include <atomic>
#include <memory>
#include <algorithm>
#include <flecs.h>

namespace PixelMapper {

// Forward declare PatchProgram
struct PatchProgram;

namespace Generative {

    // ECS Component Tags and Folders
    struct Is {};
    struct PaletteFolder {};
    struct MotiveFolder {};

    // C++ memory layout matching GLSL std140 for color stops
    struct alignas(16) ColorStop {
        glm::vec4 color;       // 16 bytes (RGBA)
        float position;        // 4 bytes [0.0 - 1.0]
        float smoothness;      // 4 bytes [0.0 - 1.0]
        glm::vec2 padding;     // 8 bytes padding
    };

    // C++ memory layout matching GLSL std140 for engine state
    struct alignas(16) EngineStateUBO {
        int activeStops;       // 4 bytes
        glm::ivec3 _pad0;      // 12 bytes padding
        ColorStop palette[16]; // 512 bytes (16 * 32 bytes)
        
        // Motive parameters
        float velocity;        // 4 bytes
        float complexity;      // 4 bytes
        float scale;           // 4 bytes
        float distortion;      // 4 bytes
        float asymmetry;       // 4 bytes
        float intensity;       // 4 bytes
        glm::vec2 _pad1;       // 8 bytes padding
    };

    namespace Palette {
        struct Is {};
        struct Stops { std::vector<ColorStop> value; };
        struct IsModeB { bool value; };
    }

    namespace Motive {
        struct Is {};
        struct Params {
            float velocity = 0.5f;
            float complexity = 0.5f;
            float scale = 0.5f;
            float distortion = 0.5f;
            float asymmetry = 0.5f;
            float intensity = 0.5f;
            
            float wanderAmp[6] = {0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f};
            float wanderFreq[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
        };
    }

    struct Settings {
        bool masterEnabled = false;
        bool paletteEnabled = true;
        bool motiveEnabled = true;
        bool shaderEnabled = true;
        
        // Queue timers (seconds)
        float paletteInterval = 15.0f;
        float paletteJitter = 5.0f;
        float paletteCrossfade = 4.0f;

        float motiveInterval = 20.0f;
        float motiveJitter = 5.0f;
        float motiveCrossfade = 6.0f;

        float shaderInterval = 30.0f;
        float shaderJitter = 10.0f;
        float shaderCrossfade = 5.0f;

        // Transition flags
        bool enableLinearDissolve = true;
        bool enableLumaWipe = true;
        bool enableSweep = true;
        bool enableCircleWipe = true;
        bool enableLuminosityWipe = true;
        bool transitionVolumetric = false;
        
        // Overrides
        bool manualPaletteOverride = false;
        int manualPaletteIndex = -1;
        bool manualMotiveOverride = false;
        int manualMotiveIndex = -1;
        bool manualShaderOverride = false;
        int manualShaderIndex = -1;
        int manualTransitionType = -1; // -1 = random, 0 = Linear, 1 = Luma, 2 = Sweep
    };

    void import(flecs::world& w);

    // Flat compiled representation of presets for the RT thread
    struct CompiledPalette {
        std::string name;
        std::vector<ColorStop> stops;
        bool isModeB = false;
    };

    struct CompiledMotive {
        std::string name;
        float velocity;
        float complexity;
        float scale;
        float distortion;
        float asymmetry;
        float intensity;
        float wanderAmp[6];
        float wanderFreq[6];
    };
}

// 1D Perlin Noise for smooth wandering
struct Noise1D {
    static float noise(float x) {
        int ix = (int)floorf(x);
        float fx = x - (float)ix;
        float ux = fx * fx * (3.0f - 2.0f * fx); // smoothstep
        
        auto hash = [](int i) -> float {
            int n = i;
            n = (n << 13) ^ n;
            return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
        };
        
        float g0 = hash(ix);
        float g1 = hash(ix + 1);
        return g0 * (1.0f - ux) + g1 * ux;
    }
};

// Thread-safe runtime that operates on the RT thread
class GenerativeEngineRuntime {
public:
    GenerativeEngineRuntime();
    ~GenerativeEngineRuntime() = default;

    void init(PatchProgram* program);
    void update(float dt, PatchProgram* program);
    void triggerNextPalette(PatchProgram* program);
    void triggerNextMotive(PatchProgram* program);
    void triggerNextShader(PatchProgram* program);
    const Generative::EngineStateUBO& getUboState() const { return m_uboState; }

    // GUI Diagnostic states (lock-free reading via atomics/simple values)
    float getPaletteTimeLeft() const { return m_paletteTimer.load(); }
    float getMotiveTimeLeft() const { return m_motiveTimer.load(); }
    float getShaderTimeLeft() const { return m_shaderTimer.load(); }

    int getActivePaletteIndex() const { return m_activePaletteIdx.load(); }
    int getTargetPaletteIndex() const { return m_targetPaletteIdx.load(); }
    float getPaletteFadeProgress() const { return m_paletteFadeProgress.load(); }

    int getActiveMotiveIndex() const { return m_activeMotiveIdx.load(); }
    int getTargetMotiveIndex() const { return m_targetMotiveIdx.load(); }
    float getMotiveFadeProgress() const { return m_motiveFadeProgress.load(); }

    int getActiveShaderIndex() const { return m_activeShaderIdx.load(); }
    int getTargetShaderIndex() const { return m_targetShaderIdx.load(); }
    float getShaderFadeProgress() const { return m_shaderFadeProgress.load(); }
    int getShaderTransitionType() const { return m_shaderTransitionType.load(); }

    // Diagnostic readouts for live values
    float getLiveVelocity() const { return m_uboState.velocity; }
    float getLiveComplexity() const { return m_uboState.complexity; }
    float getLiveScale() const { return m_uboState.scale; }
    float getLiveDistortion() const { return m_uboState.distortion; }
    float getLiveAsymmetry() const { return m_uboState.asymmetry; }
    float getLiveIntensity() const { return m_uboState.intensity; }

private:
    void updateShuffleBag(std::vector<int>& bag, size_t poolSize);
    int drawFromShuffleBag(std::vector<int>& bag, size_t poolSize);
    void startPaletteTransition(int targetIdx, PatchProgram* program);

    // Timers
    std::atomic<float> m_paletteTimer{0.0f};
    std::atomic<float> m_motiveTimer{0.0f};
    std::atomic<float> m_shaderTimer{0.0f};

    // Crossfading progress
    std::atomic<float> m_paletteFadeProgress{1.0f};
    std::atomic<float> m_motiveFadeProgress{1.0f};
    std::atomic<float> m_shaderFadeProgress{1.0f};

    std::atomic<float> m_paletteFadeDuration{0.0f};
    std::atomic<float> m_motiveFadeDuration{0.0f};
    std::atomic<float> m_shaderFadeDuration{0.0f};

    // Current/Target states
    std::atomic<int> m_activePaletteIdx{-1};
    std::atomic<int> m_targetPaletteIdx{-1};

    std::atomic<int> m_activeMotiveIdx{-1};
    std::atomic<int> m_targetMotiveIdx{-1};

    std::atomic<int> m_activeShaderIdx{-1};
    std::atomic<int> m_targetShaderIdx{-1};
    std::atomic<int> m_shaderTransitionType{0}; // 0 = linear, 1 = luma wipe, 2 = sweep

    // Motive physics state
    float m_currentAnchors[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float m_targetAnchors[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float m_wanderTime[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // Shuffle Bags
    std::vector<int> m_paletteShuffleBag;
    std::vector<int> m_motiveShuffleBag;
    std::vector<int> m_shaderShuffleBag;

    // RNG
    std::mt19937 m_rng;

    // Packed State UBO
    Generative::EngineStateUBO m_uboState;

    std::vector<Generative::ColorStop> m_paddedA;
    std::vector<Generative::ColorStop> m_paddedB;
    int m_blendSize = 0;

    bool m_initialized = false;
};

} // namespace PixelMapper
