#pragma once
#include <flecs.h>
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <memory>
#include <unordered_set>
#include <sol/sol.hpp>
#include "Common.h"
#include "Artnet.h"

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

    struct Settings {
        float refreshRate = 40.0f;
        bool networkEnabled = false;
        uint16_t sourcePort = 6454;
        RenderMode renderMode = RenderMode::CPP;
        int vfbResolution = 256;
        char luaScriptPath[256] = "scripts/default_patch.lua";
        char shaderPath[256] = "shaders/default_patch.frag";
    };

    struct ScriptData {
        std::string luaSource;
        std::string glslSource;
        std::string compilerLog;
    };

    struct RenderArea {
        glm::vec3 min;
        glm::vec3 max;
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

    Artnet::Device::Settings* devices = nullptr;
    uint32_t deviceCount = 0;
    bool networkEnabled = false;
    uint16_t sourcePort = 6454;
    float refreshRate = 40.0f;

    glm::vec3 pixelPosMin, pixelPosMax;

    glm::vec3* pixelPositions;
    ColorRGBW* pixelColors;
    uint32_t pixelCount;

    Pix2UniCopyInstr* p2us;
    uint32_t p2uCount;

    Universe* universes;
    uint32_t universeCount;

    // Modular rendering fields
    Patch::RenderMode renderMode = Patch::RenderMode::CPP;
    int vfbWidth = 0;
    int vfbHeight = 0;
    ColorRGBW* vfbPixels = nullptr; // CPU-side Virtual Framebuffer

    std::unique_ptr<sol::state> luaState;
    sol::protected_function luaUpdateFn;

    unsigned int glslProgram = 0;
    unsigned int glslFbo = 0;
    unsigned int glslFboTex = 0;
    unsigned int glslVao = 0;
    unsigned int glslVbo = 0;
    unsigned int glslPbo[2] = {0, 0};  // double-buffered PBO for async readback
    int pboFrameIndex = 0;
    bool shaderCompiled = false;
    bool vaoReady = false;            // VAO must be created on the RT thread (not shared between GL contexts)
    std::string shaderSource;
    std::string compilerLog;
    float timeElapsed = 0.0f;

    // ── Crossfade ──
    ColorRGBW* vfbPixelsOld = nullptr; ///< Snapshot of outgoing cue's last VFB frame
    float crossfadeProgress = 1.0f;    ///< 0 = full old, 1 = full new; RT thread increments this
    float crossfadeDuration = 0.0f;    ///< Seconds; 0 = no crossfade

    // ── Ahead-Of-Time Cue Compilation ──
    struct CompiledCue {
        unsigned int program = 0;
        std::string compilerLog;
    };
    std::vector<CompiledCue> compiledCues;
    std::string defaultCompilerLog;

    int activeCueIndex = -1;
    float pendingCrossfadeDuration = 0.0f;
    int currentRenderedCueIndex = -2; // -1 = default patch shader, -2 = uninitialized
    int previousCueIndex = -2;        // Cue index currently fading out (-1 = default shader, -2 = none/invalid)
    int editingCueIndex = -1;         // Cue index currently open in shader editor (-1 = default shader)
    int editingBankIndex = -1;        // Bank effect index currently open in shader editor (-1 = none)

    // Editor offline preview FBO + texture
    unsigned int glslEditorFbo = 0;
    unsigned int glslEditorFboTex = 0;

    // GPU-side Crossfading FBOs and textures
    unsigned int glslFboOld = 0;
    unsigned int glslFboTexOld = 0;
    unsigned int glslFboBlend = 0;
    unsigned int glslFboTexBlend = 0;
    unsigned int glslBlendProgram = 0;

    std::vector<CompiledCue> compiledBankEffects;
};

void render(PatchProgram* rtData);
void encode(PatchProgram* rtData);

} // namespace PixelMapper
