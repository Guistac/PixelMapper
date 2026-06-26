#pragma once
#include <flecs.h>
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <memory>
#include <unordered_set>
#include <atomic>
#include <mutex>
#include <sol/sol.hpp>
#include "Common.h"
#include "Artnet.h"
#include "GenerativeEngine.h"

namespace PixelMapper {

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
        float masterBrightness = 1.0f;
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

    inline std::string makeUniqueChildName(flecs::entity parent, const std::string& baseName, flecs::entity entityToRename = flecs::entity::null()) {
        if (!parent.is_valid()) return baseName;
        std::string candidate = baseName;
        int counter = 1;
        while (true) {
            flecs::entity existing = parent.lookup(candidate.c_str());
            if (!existing.is_valid() || (entityToRename.is_valid() && existing == entityToRename)) {
                break;
            }
            counter++;
            candidate = baseName + " (" + std::to_string(counter) + ")";
        }
        return candidate;
    }

    inline std::string safe_set_name(flecs::entity entity, const std::string& baseName, flecs::entity parentOverride = flecs::entity::null()) {
        if (!entity.is_valid()) return "";
        if (baseName.empty()) {
            entity.set_name(nullptr);
            return "";
        } else {
            flecs::entity parent = parentOverride.is_valid() ? parentOverride : entity.parent();
            if (parent.is_valid()) {
                std::string uniqueName = makeUniqueChildName(parent, baseName, entity);
                entity.set_name(uniqueName.c_str());
                return uniqueName;
            } else {
                std::string candidate = baseName;
                int counter = 1;
                while (true) {
                    flecs::entity existing = entity.world().lookup(candidate.c_str());
                    if (!existing.is_valid() || existing == entity) {
                        break;
                    }
                    counter++;
                    candidate = baseName + " (" + std::to_string(counter) + ")";
                }
                entity.set_name(candidate.c_str());
                return candidate;
            }
        }
    }
    extern const std::string defaultGLSL;
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
    std::atomic<float> masterBrightness{1.0f};

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
    float lastFrameTime = 0.0f;
    float defaultVTime = 0.0f;
    float defaultMinSpeed = 0.0f;
    float defaultMaxSpeed = 1.0f;

    // ── Crossfade ──
    ColorRGBW* vfbPixelsOld = nullptr; ///< Snapshot of outgoing cue's last VFB frame
    std::atomic<float> crossfadeProgress{1.0f};    ///< 0 = full old, 1 = full new; RT thread increments this
    std::atomic<float> crossfadeDuration{0.0f};    ///< Seconds; 0 = no crossfade

    // ── Ahead-Of-Time Cue Compilation ──
    struct CompiledCue {
        unsigned int program = 0;
        unsigned int previewProgram = 0;
        std::string compilerLog;
        float vTime = 0.0f;
        float minSpeed = 0.0f;
        float maxSpeed = 1.0f;
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
    float& getVTimeRef(unsigned int prog);
    void getSpeedRange(unsigned int prog, float& minS, float& maxS);
};

void randomizeTransitionDirections(PatchProgram* program);
void render(PatchProgram* rtData);
void encode(PatchProgram* rtData);

} // namespace PixelMapper
