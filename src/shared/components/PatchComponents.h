#pragma once
#include <flecs.h>
#include <glm/glm.hpp>
#include <unordered_set>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>
#include <random>
#include <sol/sol.hpp>
#include "shared/Common.h"
#include "shared/Artnet.h"
#include "shared/network/TelemetryProtocol.h" // For ColorStop and EngineStateUBO

namespace PixelMapper {

namespace Generative {
    // ECS Component Tags and Folders
    struct Is {};
    struct PaletteFolder {};
    struct MotiveFolder {};

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

struct PatchProgram;

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

namespace Patch {
    struct Is {};
    struct FixtureFolder {};
    struct DmxUniverseFolder {};
    struct ArtnetDeviceFolder {};

    struct SelectedFixture {};
    struct SelectedDmxUniverse {};
    struct SelectedArtnetDevice {};

    struct DmxMapDirty {};
    struct RenderAreaDirty {};
    struct ProgramDirty {};

    /// Set of fixture IDs that are in the multi-selection.
    struct MultiSelection {
        std::unordered_set<flecs::id_t> ids;
    };

    enum class RenderMode {
        CPP = 0,
        LUA,
        GLSL
    };

    enum class WhiteMode {
        AUTO = 0,  // W = min(R,G,B), then subtract from RGB
        OFF,       // W = 0 always
        PASSTHROUGH // No conversion, raw shader output
    };

    enum class ProjectionMode {
        TOP_DOWN_XY = 0,
        FRONT_XZ,
        SIDE_YZ
    };

    struct Settings {
        float refreshRate = 40.0f;
        bool networkEnabled = false;
        uint16_t sourcePort = 6454;
        RenderMode renderMode = RenderMode::CPP;
        WhiteMode whiteMode = WhiteMode::AUTO;
        ProjectionMode projectionMode = ProjectionMode::TOP_DOWN_XY;
        int vfbResolution = 256;
        char luaScriptPath[256] = "scripts/default_patch.lua";
        char shaderPath[256] = "shaders/default_patch.frag";
        bool highlightSelected = true;
        float highlightFrequency = 1.0f;
    };

    struct ScriptData {
        std::string luaSource;
        std::string glslSource;
        std::string compilerLog;
    };

    struct FixtureSetupScript {
        std::string source;
        std::string compilerLog;
    };

    struct RenderArea {
        glm::vec3 min;
        glm::vec3 max;
    };

    struct GPUProgram {
        unsigned int program = 0;
        unsigned int previewProgram = 0;
        std::string glslSource;
        std::string compilerLog;
    };

    struct GPUResources {
        unsigned int glslProgram = 0;
        unsigned int glslFbo = 0;
        unsigned int glslFboTex = 0;
        unsigned int glslVao = 0; // Unused but kept for structure
        unsigned int glslVbo = 0; // Unused but kept for structure
        unsigned int glslPointVao = 0;
        unsigned int glslPointVbo = 0;
        unsigned int glslQuadVao = 0;
        unsigned int glslQuadVbo = 0;
        unsigned int glslPbo[2] = {0, 0};
        int pboFrameIndex = 0;
        bool vaoReady = false;

        // Preview editor FBOs
        unsigned int glslEditorFbo = 0;
        unsigned int glslEditorFboTex = 0;
        unsigned int glslPlaybackPreviewFbo = 0;
        unsigned int glslPlaybackPreviewFboTex = 0;
        unsigned int glslPlaybackPreviewFboOld = 0;
        unsigned int glslPlaybackPreviewFboTexOld = 0;
        unsigned int glslPlaybackPreviewFboBlend = 0;
        unsigned int glslPlaybackPreviewFboTexBlend = 0;
        unsigned int glslPlaybackPreviewDisplayFbo[2] = {0, 0};
        unsigned int glslPlaybackPreviewDisplayTex[2] = {0, 0};

        // Crossfading FBOs
        unsigned int glslFboOld = 0;
        unsigned int glslFboTexOld = 0;
        unsigned int glslFboBlend = 0;
        unsigned int glslFboTexBlend = 0;
        unsigned int glslBlendProgram = 0;
        unsigned int glslNoiseTex = 0;
        unsigned int glslPositionTex = 0;

        // Framebuffer size cache
        int vfbWidth = 0;
        int vfbHeight = 0;
        int previewWidth = 0;
        int previewHeight = 0;
    };

    flecs::entity create(flecs::entity pixelMapper);
    void select(flecs::entity pixelMapper, flecs::entity patch);
    flecs::entity getSelected(flecs::entity pixelMapper);
    int getCount(flecs::entity pixelMapper);
    void iterate(flecs::entity pixelMapper, std::function<void(flecs::entity patch)> fn);
    void import(flecs::world& w);
}

struct PatchProgram {
    static PatchProgram* compile(flecs::entity patch);
    ~PatchProgram();

    struct Universe {
        uint16_t id;
        uint8_t buffer[512];
    };
    struct Pix2UniCopyInstr {
        uint32_t pixelIndex;
        uint8_t pixelStartByte;
        uint8_t bytesPerPixel;
        uint32_t byteCount;
        uint16_t universeIndex;
        uint16_t universeOffset;
    };
    struct CompiledFixture {
        uint64_t entityId;
        uint32_t pixelStart;
        uint32_t pixelCount;
    };

    CompiledFixture* fixtures = nullptr;
    uint32_t fixtureCount = 0;
    std::atomic<bool>* pixelSelected = nullptr;
    float highlightFrequency = 1.0f;

    Artnet::Device::Settings* devices = nullptr;
    uint32_t deviceCount = 0;
    bool networkEnabled = false;
    uint16_t sourcePort = 6454;
    float refreshRate = 40.0f;

    glm::vec3 pixelPosMin, pixelPosMax;

    glm::vec3* pixelPositions;
    ColorRGBW* pixelColors;
    ColorRGBW* pixelColorsTemp = nullptr;
    uint32_t pixelCount;

    Pix2UniCopyInstr* p2us;
    uint32_t p2uCount;

    Universe* universes;
    uint32_t universeCount;

    // Modular rendering fields
    Patch::RenderMode renderMode = Patch::RenderMode::CPP;
    Patch::WhiteMode whiteMode = Patch::WhiteMode::AUTO;
    Patch::ProjectionMode projectionMode = Patch::ProjectionMode::TOP_DOWN_XY;
    std::atomic<float> zSlice{0.5f};
    int vfbWidth = 0;
    int vfbHeight = 0;
    int previewWidth = 256;
    int previewHeight = 256;
    ColorRGBW* vfbPixels = nullptr; // CPU-side Virtual Framebuffer

    std::unique_ptr<sol::state> luaState;
    sol::protected_function luaUpdateFn;

    unsigned int glslProgram = 0;
    unsigned int glslPreviewProgram = 0;
    unsigned int glslFbo = 0;
    unsigned int glslFboTex = 0;
    unsigned int glslVao = 0; // Unused but kept for structure
    unsigned int glslVbo = 0; // Unused but kept for structure
    unsigned int glslPointVao = 0;
    unsigned int glslPointVbo = 0;
    unsigned int glslQuadVao = 0;
    unsigned int glslQuadVbo = 0;
    unsigned int glslPbo[2] = {0, 0};  // double-buffered PBO for async readback
    int pboFrameIndex = 0;
    bool shaderCompiled = false;
    bool vaoReady = false;            // VAO must be created on the RT thread (not shared between GL contexts)
    std::string shaderSource;
    std::string compilerLog;
    float timeElapsed = 0.0f;

    // ── Crossfade ──
    ColorRGBW* vfbPixelsOld = nullptr; ///< Snapshot of outgoing cue's last VFB frame
    std::atomic<float> crossfadeProgress{1.0f};    ///< 0 = full old, 1 = full new; RT thread increments this
    std::atomic<float> crossfadeDuration{0.0f};    ///< Seconds; 0 = no crossfade

    // ── Ahead-Of-Time Cue Compilation ──
    struct CompiledCue {
        unsigned int program = 0;
        unsigned int previewProgram = 0;
        std::string compilerLog;
    };
    std::vector<CompiledCue> compiledCues;
    std::string defaultCompilerLog;

    std::atomic<int> activeCueIndex{-1};
    std::atomic<float> pendingCrossfadeDuration{0.0f};
    int currentRenderedCueIndex = -2; // -1 = default patch shader, -2 = uninitialized
    int previousCueIndex = -2;        // Cue index currently fading out (-1 = default shader, -2 = none/invalid)
    std::atomic<int> editingCueIndex{-1};         // Cue index currently open in effect editor (-1 = default shader)
    std::atomic<int> editingBankIndex{-1};        // Bank effect index currently open in effect editor (-1 = none)

    // Editor offline preview FBO + texture
    unsigned int glslEditorFbo = 0;
    unsigned int glslEditorFboTex = 0;
    unsigned int glslPlaybackPreviewFbo = 0;
    unsigned int glslPlaybackPreviewFboTex = 0;
    unsigned int glslPlaybackPreviewFboOld = 0;
    unsigned int glslPlaybackPreviewFboTexOld = 0;
    unsigned int glslPlaybackPreviewFboBlend = 0;
    unsigned int glslPlaybackPreviewFboTexBlend = 0;
    std::atomic<unsigned int> glslCurrentPlaybackPreviewTexID{0};
    unsigned int glslPlaybackPreviewDisplayFbo[2] = {0, 0};
    unsigned int glslPlaybackPreviewDisplayTex[2] = {0, 0};
    std::atomic<int> glslPlaybackPreviewReadIdx{0};

    // GPU-side Crossfading FBOs and textures
    unsigned int glslFboOld = 0;
    unsigned int glslFboTexOld = 0;
    unsigned int glslFboBlend = 0;
    unsigned int glslFboTexBlend = 0;
    unsigned int glslBlendProgram = 0;
    unsigned int glslNoiseTex = 0;
    unsigned int glslPositionTex = 0;
    glm::vec2 sweepDirection = glm::vec2(1.0f, 0.0f);
    glm::vec3 sweepDirection3D = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 sphereCenter3D = glm::vec3(0.0f, 0.0f, 0.0f);
    bool circleWipeInward = false;

    std::vector<CompiledCue> compiledBankEffects;
    std::atomic<bool> showPlaybackPreview{false};

    // Generative Engine fields
    Generative::Settings generativeSettings;
    std::vector<Generative::CompiledPalette> palettePool;
    std::vector<Generative::CompiledMotive> motivePool;
    std::shared_ptr<GenerativeEngineRuntime> generativeRuntime;
    unsigned int glslUboId = 0;
    bool editorPreviewOverrideActive = false;
    Generative::EngineStateUBO editorPreviewOverrideUbo;
    mutable std::mutex generativeMutex;
    float getShaderCrossfadeProgress() const;
};

} // namespace PixelMapper
