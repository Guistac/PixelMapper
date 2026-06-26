#include "Patch.h"
#include "Fixture.h"
#include "App.h"
#include "Artnet.h"
#include "CanvasBinding.h"
#include "CueList.h"
#include "EffectBank.h"
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <string>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <cstring>
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace PixelMapper {

namespace Patch {

    flecs::entity create(flecs::entity pixelMapper){
        std::string patchName = "Patch " + std::to_string(getCount(pixelMapper) + 1);

        auto patchFolder = pixelMapper.target<App::PatchFolder>();
        if(!patchFolder.is_valid()) return flecs::entity::null();
        const auto& world = pixelMapper.world();

        static const std::string defaultSetupScript =
            "-- Fixture Setup Script\n"
            "-- Exposes operations on the patch:\n"
            "--   patch:clear_fixtures()\n"
            "--   patch:create_line(name, startX, startY, startZ, endX, endY, endZ, numPixels, channels)\n"
            "--   patch:get_fixtures() -> array of Fixtures\n"
            "--\n"
            "-- Exposes operations on a Fixture:\n"
            "--   f:id() -> number\n"
            "--   f:name() -> string\n"
            "--   f:set_name(name)\n"
            "--   f:get_shape_type() -> string (\"Line\" / \"Circle\" / \"None\")\n"
            "--   f:get_line_properties() -> startX, startY, startZ, endX, endY, endZ\n"
            "--   f:set_line_properties(startX, startY, startZ, endX, endY, endZ)\n"
            "--   f:get_layout() -> pixelCount, channels\n"
            "--   f:set_layout(pixelCount, channels)\n"
            "--   f:get_dmx() -> universe, startAddress\n"
            "--   f:set_dmx(universe, startAddress)\n"
            "--   f:remove()\n"
            "\n"
            "-- Example: clear patch and create 4 parallel line fixtures\n"
            "patch:clear_fixtures()\n"
            "\n"
            "for i = 1, 4 do\n"
            "    local y = (i - 1) * 30\n"
            "    local name = \"Line \" .. i\n"
            "    local f = patch:create_line(name, 0, y, 0, 100, y, 0, 16, 4)\n"
            "    f:set_dmx(0, (i - 1) * 64)\n"
            "end\n";

        auto newPatch = world.entity()
            .add<Patch::Is>()
            .set<Patch::Settings>({})
            .set<Patch::ScriptData>({})
            .set<Patch::FixtureSetupScript>({defaultSetupScript, ""})
            .add<Patch::RenderArea>()
            .set<Patch::GPUResources>({})
            .set<Patch::GPUProgram>({})
            .child_of(patchFolder);

        Patch::safe_set_name(newPatch, patchName, patchFolder);

        auto fixtureFolder = world.entity("FixtureFolder").child_of(newPatch);
        auto dmxOutputFolder = world.entity("DmxOutputFolder").child_of(newPatch);
        auto artnetDeviceFolder = world.entity("ArtnetDeviceFolder").child_of(newPatch);

        newPatch.add<Patch::FixtureFolder>(fixtureFolder);
        newPatch.add<Patch::DmxUniverseFolder>(dmxOutputFolder);
        newPatch.add<Patch::ArtnetDeviceFolder>(artnetDeviceFolder);

        auto cueListFolder = world.entity("CueListFolder").child_of(newPatch);
        cueListFolder.add<CueList::Is>();
        cueListFolder.set<CueList::SessionState>({});

        auto effectBankFolder = world.entity("EffectBankFolder").child_of(newPatch);
        effectBankFolder.add<EffectBank::Is>();
        effectBankFolder.set<EffectBank::SessionState>({});

        auto paletteFolder = world.entity("PaletteFolder").child_of(newPatch);
        paletteFolder.add<Generative::PaletteFolder>();

        auto motiveFolder = world.entity("MotiveFolder").child_of(newPatch);
        motiveFolder.add<Generative::MotiveFolder>();

        newPatch.add<CueList::CueFolder>(cueListFolder);
        newPatch.add<EffectBank::EffectFolder>(effectBankFolder);
        newPatch.add<Generative::PaletteFolder>(paletteFolder);
        newPatch.add<Generative::MotiveFolder>(motiveFolder);
        newPatch.set<Generative::Settings>({});

        // ── Seed Default Palettes ──
        {
            // 1. Rainbow Wave (Continuous wrap looping)
            std::vector<Generative::ColorStop> stops1 = {
                { {1.0f, 0.0f, 0.0f, 1.0f}, 0.00f, 0.5f, {0.0f, 0.0f} },
                { {1.0f, 1.0f, 0.0f, 1.0f}, 0.17f, 0.5f, {0.0f, 0.0f} },
                { {0.0f, 1.0f, 0.0f, 1.0f}, 0.33f, 0.5f, {0.0f, 0.0f} },
                { {0.0f, 1.0f, 1.0f, 1.0f}, 0.50f, 0.5f, {0.0f, 0.0f} },
                { {0.0f, 0.0f, 1.0f, 1.0f}, 0.67f, 0.5f, {0.0f, 0.0f} },
                { {1.0f, 0.0f, 1.0f, 1.0f}, 0.83f, 0.5f, {0.0f, 0.0f} },
                { {1.0f, 0.0f, 0.0f, 1.0f}, 1.00f, 0.5f, {0.0f, 0.0f} }
            };
            auto pal1 = world.entity().child_of(paletteFolder)
                .add<Generative::Palette::Is>();
            Patch::safe_set_name(pal1, "Rainbow", paletteFolder);
            pal1.set<Generative::Palette::Stops>({stops1})
                .set<Generative::Palette::IsModeB>({false});

            // 2. Sunset
            std::vector<Generative::ColorStop> stops2 = {
                { {0.95f, 0.20f, 0.08f, 1.0f}, 0.00f, 0.5f, {0.0f, 0.0f} },
                { {0.85f, 0.05f, 0.40f, 1.0f}, 0.35f, 0.5f, {0.0f, 0.0f} },
                { {0.35f, 0.02f, 0.55f, 1.0f}, 0.70f, 0.5f, {0.0f, 0.0f} },
                { {0.95f, 0.20f, 0.08f, 1.0f}, 1.00f, 0.5f, {0.0f, 0.0f} }
            };
            auto pal2 = world.entity().child_of(paletteFolder)
                .add<Generative::Palette::Is>();
            Patch::safe_set_name(pal2, "Sunset", paletteFolder);
            pal2.set<Generative::Palette::Stops>({stops2})
                .set<Generative::Palette::IsModeB>({false});

            // 3. Cyberpunk Neon
            std::vector<Generative::ColorStop> stops3 = {
                { {0.0f, 0.95f, 0.95f, 1.0f}, 0.00f, 0.5f, {0.0f, 0.0f} },
                { {0.95f, 0.0f, 0.85f, 1.0f}, 0.50f, 0.5f, {0.0f, 0.0f} },
                { {0.0f, 0.95f, 0.95f, 1.0f}, 1.00f, 0.5f, {0.0f, 0.0f} }
            };
            auto pal3 = world.entity().child_of(paletteFolder)
                .add<Generative::Palette::Is>();
            Patch::safe_set_name(pal3, "Cyberpunk", paletteFolder);
            pal3.set<Generative::Palette::Stops>({stops3})
                .set<Generative::Palette::IsModeB>({false});
        }

        // ── Seed Default Motives ──
        {
            // 1. Calm Ambient
            Generative::Motive::Params calm;
            calm.velocity = 0.15f; calm.complexity = 0.20f; calm.scale = 0.35f;
            calm.distortion = 0.10f; calm.asymmetry = 0.05f; calm.intensity = 0.40f;
            for (int k = 0; k < 6; ++k) { calm.wanderAmp[k] = 0.08f; calm.wanderFreq[k] = 0.25f; }
            auto mot1 = world.entity().child_of(motiveFolder)
                .add<Generative::Motive::Is>();
            Patch::safe_set_name(mot1, "Calm Ambient", motiveFolder);
            mot1.set<Generative::Motive::Params>(calm);

            // 2. Organic Wandering
            Generative::Motive::Params wander;
            wander.velocity = 0.40f; wander.complexity = 0.50f; wander.scale = 0.55f;
            wander.distortion = 0.35f; wander.asymmetry = 0.25f; wander.intensity = 0.65f;
            for (int k = 0; k < 6; ++k) { wander.wanderAmp[k] = 0.15f; wander.wanderFreq[k] = 0.50f; }
            auto mot2 = world.entity().child_of(motiveFolder)
                .add<Generative::Motive::Is>();
            Patch::safe_set_name(mot2, "Organic Wandering", motiveFolder);
            mot2.set<Generative::Motive::Params>(wander);

            // 3. Kinetic Storm
            Generative::Motive::Params storm;
            storm.velocity = 0.85f; storm.complexity = 0.80f; storm.scale = 0.75f;
            storm.distortion = 0.65f; storm.asymmetry = 0.50f; storm.intensity = 0.90f;
            for (int k = 0; k < 6; ++k) { storm.wanderAmp[k] = 0.18f; storm.wanderFreq[k] = 1.20f; }
            auto mot3 = world.entity().child_of(motiveFolder)
                .add<Generative::Motive::Is>();
            Patch::safe_set_name(mot3, "Kinetic Storm", motiveFolder);
            mot3.set<Generative::Motive::Params>(storm);
        }

        select(pixelMapper, newPatch);
        
        return newPatch;
    }

    flecs::entity getSelected(flecs::entity pixelMapper){
        return pixelMapper.target<App::SelectedPatch>();
    }
    void select(flecs::entity pixelMapper, flecs::entity patch){
        pixelMapper.add<App::SelectedPatch>(patch);
    }

    int getCount(flecs::entity pixelMapper){
        auto patchFolder = pixelMapper.target<App::PatchFolder>();
        if(!patchFolder.is_valid()) return 0;
        const auto& queries = pixelMapper.get<App::Queries>();
        return queries.patch.set_var("parent", patchFolder).count();
    }

    void iterate(flecs::entity pixelMapper, std::function<void(flecs::entity patch)> fn){
        auto patchFolder = pixelMapper.target<App::PatchFolder>();
        if(!patchFolder.is_valid()) return;
        App::getQueries(pixelMapper.world()).patch.set_var("parent", patchFolder)
        .each([fn](flecs::entity patch, Patch::Is){
            fn(patch);
        });
    }

    void import(flecs::world& w){
        w.component<Is>();
        w.component<FixtureFolder>();
        w.component<DmxUniverseFolder>();
        w.component<SelectedFixture>();
        w.component<SelectedDmxUniverse>();
        w.component<DmxMapDirty>();
        w.component<RenderAreaDirty>();
        w.component<Settings>();
        w.component<ScriptData>();
        w.component<FixtureSetupScript>();
        w.component<RenderArea>();
        w.component<MultiSelection>();

        w.component<GPUProgram>();
        w.component<GPUResources>();

        w.observer<GPUResources>("CleanGPUResources").event(flecs::OnRemove)
        .each([](flecs::entity e, GPUResources& res) {
            if (res.glslProgram) glDeleteProgram(res.glslProgram);
            if (res.glslBlendProgram) glDeleteProgram(res.glslBlendProgram);
            if (res.glslFbo) glDeleteFramebuffers(1, &res.glslFbo);
            if (res.glslFboTex) glDeleteTextures(1, &res.glslFboTex);
            if (res.glslFboOld) glDeleteFramebuffers(1, &res.glslFboOld);
            if (res.glslFboTexOld) glDeleteTextures(1, &res.glslFboTexOld);
            if (res.glslFboBlend) glDeleteFramebuffers(1, &res.glslFboBlend);
            if (res.glslFboTexBlend) glDeleteTextures(1, &res.glslFboTexBlend);
            if (res.glslEditorFbo) glDeleteFramebuffers(1, &res.glslEditorFbo);
            if (res.glslEditorFboTex) glDeleteTextures(1, &res.glslEditorFboTex);
            if (res.glslPlaybackPreviewFbo) glDeleteFramebuffers(1, &res.glslPlaybackPreviewFbo);
            if (res.glslPlaybackPreviewFboTex) glDeleteTextures(1, &res.glslPlaybackPreviewFboTex);
            if (res.glslPlaybackPreviewFboOld) glDeleteFramebuffers(1, &res.glslPlaybackPreviewFboOld);
            if (res.glslPlaybackPreviewFboTexOld) glDeleteTextures(1, &res.glslPlaybackPreviewFboTexOld);
            if (res.glslPlaybackPreviewFboBlend) glDeleteFramebuffers(1, &res.glslPlaybackPreviewFboBlend);
            if (res.glslPlaybackPreviewFboTexBlend) glDeleteTextures(1, &res.glslPlaybackPreviewFboTexBlend);
            for (int i = 0; i < 2; i++) {
                if (res.glslPlaybackPreviewDisplayFbo[i]) glDeleteFramebuffers(1, &res.glslPlaybackPreviewDisplayFbo[i]);
                if (res.glslPlaybackPreviewDisplayTex[i]) glDeleteTextures(1, &res.glslPlaybackPreviewDisplayTex[i]);
            }

            if (res.glslVao) glDeleteVertexArrays(1, &res.glslVao);
            if (res.glslVbo) glDeleteBuffers(1, &res.glslVbo);
            if (res.glslPointVao) glDeleteVertexArrays(1, &res.glslPointVao);
            if (res.glslPointVbo) glDeleteBuffers(1, &res.glslPointVbo);
            if (res.glslQuadVao) glDeleteVertexArrays(1, &res.glslQuadVao);
            if (res.glslQuadVbo) glDeleteBuffers(1, &res.glslQuadVbo);
            if (res.glslPbo[0]) glDeleteBuffers(2, res.glslPbo);
            if (res.glslNoiseTex) glDeleteTextures(1, &res.glslNoiseTex);
            if (res.glslPositionTex) glDeleteTextures(1, &res.glslPositionTex);
        });

        w.observer<GPUProgram>("CleanGPUProgram").event(flecs::OnRemove)
        .each([](flecs::entity e, GPUProgram& gp) {
            if (gp.program) glDeleteProgram(gp.program);
            if (gp.previewProgram) glDeleteProgram(gp.previewProgram);
        });
    }

    const std::string defaultGLSL = R"(/*
--- Inputs & Outputs ---
in vec3 vPixelPos3D;        // Raw 3D position of the pixel in space
in vec2 vPixelPos2D;        // Normalized 2D canvas coordinates [0.0 - 1.0]
out vec4 fragColor;         // Output color of the fragment

--- Standard Uniforms ---
uniform float time;         // Running time of the pattern (seconds)
uniform float vTime;        // Integrated velocity-scaled time
uniform vec2 resolution;    // Viewport resolution (usually 256 x 256)
uniform float pixelCount;   // Total number of mapped pixels
uniform vec3 pixelPosMin;   // Min coordinate of the RenderArea bounding box
uniform vec3 pixelPosMax;   // Max coordinate of the RenderArea bounding box
uniform float zSlice;       // Active Z-Slice cross section preview [0.0 - 1.0]
uniform sampler2D iChannel0;// Optional 2D noise / texture channels
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;

--- Generative Engine Palettes ---
struct ColorStop {
   vec4 color;
    float position;
    float smoothness;
};
uniform int activeStops;       // Number of stops in the active palette
uniform ColorStop palette[16]; // Active color palette stops
vec4 samplePalette(float pos);        // Clamped gradient lookup [0.0 - 1.0]
vec4 samplePaletteWrapped(float pos); // Repeating (fract) gradient lookup

--- Generative Engine Motive uniforms ---
uniform float velocity;        // Physics parameter from LFO wandering
uniform float complexity;      // Physics parameter from LFO wandering
uniform float scale;           // Physics parameter from LFO wandering
uniform float distortion;      // Physics parameter from LFO wandering
uniform float asymmetry;       // Physics parameter from LFO wandering
uniform float intensity;       // Physics parameter from LFO wandering
*/

//@velocity_range -2.0 2.0     // Maps velocity slider [0.0 - 1.0] to speed multiplier [0.0 - 2.0]

void main() {
    // 1. Warp coordinates with 2D noise based on 'distortion'
    // We zoom into the noise using 'scale' so scale and distortion are independent.
    // 'vTime' is integrated by velocity on CPU, so texture drift updates smoothly.
    vec2 noiseUV = iPixelPos2D * (1.0 + scale * 3.0) + vec2(vTime * 0.1);
    vec2 noiseOffset = texture(iChannel0, noiseUV).rg - 0.5;
    vec2 warpedUV = iPixelPos2D + noiseOffset * distortion * 0.25;

    // 2. Skew the coordinates diagonally using the 'asymmetry' parameter
    warpedUV.x += (warpedUV.y - 0.5) * asymmetry * 0.6;

    // 3. Calculate a sweeping bar along the Y axis
    // 'vTime' is integrated by velocity on CPU, so sweep speed updates smoothly without time jumps.
    float sweep = mod(vTime, 1.0);
    
    // We adjust the bar width slightly based on complexity
    float barWidth = 0.04 + complexity * 0.04;
    float bar = smoothstep(sweep - barWidth, sweep, warpedUV.y)
              - smoothstep(sweep, sweep + barWidth, warpedUV.y);

    // 4. Generate complex secondary patterns (ripples) using 'complexity'
    float rippleFreq = 5.0 + complexity * 15.0;
    float ripple = sin(warpedUV.x * rippleFreq + vTime * 3.0) * 0.5 + 0.5;

    // 5. Sample the dynamic color palette gradient
    // Scale directly repeats the gradient mapping across the canvas
    float paletteCoord = warpedUV.x * (1.0 + scale * 3.0) + bar * 0.3 + ripple * complexity * 0.4;
    vec4 baseColor = samplePaletteWrapped(paletteCoord);

    // 6. Calculate base brightness signal (ambient background + sweeping bar + ripples)
    float ambient = 0.15;
    float signal = ambient + bar * 0.85 + (ripple * complexity * 0.3);
    
    // 7. Additive intensity glow (creates a bright white hot core on the bar)
    float glow = pow(bar, 3.0) * intensity * 2.5;

    fragColor = vec4((baseColor * signal).rgb + vec3(glow), 1.0);
}
)";

} // namespace Patch


namespace {
    std::string get_file_content(const std::string& path, const std::string& defaultContent = "") {
        std::filesystem::path p(path);
        try {
            if (!std::filesystem::exists(p)) {
                if (p.has_parent_path()) {
                    std::filesystem::create_directories(p.parent_path());
                }
                std::ofstream out(p);
                if (out.is_open()) {
                    out << defaultContent;
                    out.close();
                }
                return defaultContent;
            }
            std::ifstream in(p);
            if (!in.is_open()) return defaultContent;
            std::stringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        } catch (...) {
            return defaultContent;
        }
    }

    bool compileStage(GLuint shader, const std::string& source, std::string& errorLog) {
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            errorLog = infoLog;
            return false;
        }
        return true;
    }

    bool linkProgram(GLuint program, GLuint vs, GLuint fs, std::string& errorLog) {
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        GLint success = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(program, 512, nullptr, infoLog);
            errorLog = infoLog;
            return false;
        }
        return true;
    }

    static bool hasShaderDeclaration(const std::string& source, const std::string& type, const std::string& name) {
        size_t pos = 0;
        while ((pos = source.find(name, pos)) != std::string::npos) {
            bool leftWordBound = (pos == 0 || (!std::isalnum(source[pos - 1]) && source[pos - 1] != '_'));
            bool rightWordBound = (pos + name.length() >= source.length() || (!std::isalnum(source[pos + name.length()]) && source[pos + name.length()] != '_'));
            if (leftWordBound && rightWordBound) {
                if (pos >= type.length()) {
                    size_t typePos = source.rfind(type, pos);
                    if (typePos != std::string::npos) {
                        bool typeLeftBound = (typePos == 0 || (!std::isalnum(source[typePos - 1]) && source[typePos - 1] != '_'));
                        bool typeRightBound = (typePos + type.length() >= source.length() || (!std::isalnum(source[typePos + type.length()]) && source[typePos + type.length()] != '_'));
                        if (typeLeftBound && typeRightBound) {
                            bool allSpaces = true;
                            for (size_t i = typePos + type.length(); i < pos; i++) {
                                if (!std::isspace((unsigned char)source[i])) {
                                    allSpaces = false;
                                    break;
                                }
                            }
                            if (allSpaces) {
                                return true;
                            }
                        }
                    }
                }
            }
            pos += name.length();
        }
        return false;
    }

    std::string stripComments(const std::string& source) {
        std::string result;
        result.reserve(source.size());
        bool inLineComment = false;
        bool inBlockComment = false;
        for (size_t i = 0; i < source.size(); ++i) {
            if (inLineComment) {
                if (source[i] == '\n') {
                    inLineComment = false;
                    result += '\n';
                }
            } else if (inBlockComment) {
                if (i + 1 < source.size() && source[i] == '*' && source[i+1] == '/') {
                    inBlockComment = false;
                    i++;
                }
            } else {
                if (i + 1 < source.size() && source[i] == '/' && source[i+1] == '/') {
                    inLineComment = true;
                    i++;
                } else if (i + 1 < source.size() && source[i] == '/' && source[i+1] == '*') {
                    inBlockComment = true;
                    i++;
                } else {
                    result += source[i];
                }
            }
        }
        return result;
    }

    void parseVelocityMetadata(const std::string& glslSource, float& minSpeed, float& maxSpeed) {
        minSpeed = 0.0f;
        maxSpeed = 1.0f; // Standard default fallback (0.0 is stopped, 1.0 is normal speed)
        size_t pos = glslSource.find("@velocity_range");
        if (pos != std::string::npos) {
            std::stringstream ss(glslSource.substr(pos + 15));
            float tempMin, tempMax;
            if (ss >> tempMin >> tempMax) {
                minSpeed = tempMin;
                maxSpeed = tempMax;
            }
        }
    }

    std::string buildFsSource(const std::string& userSource, bool isShadertoy) {
        std::string commentStripped = stripComments(userSource);
        std::string stripped = commentStripped;
        size_t pos = 0;
        while ((pos = stripped.find("#version")) != std::string::npos) {
            size_t endLine = stripped.find("\n", pos);
            if (endLine != std::string::npos) {
                stripped.erase(pos, endLine - pos + 1);
            } else {
                stripped.erase(pos);
            }
        }

        std::string header = "#version 150\n";
        
        if (!hasShaderDeclaration(stripped, "vec3", "vPixelPos3D")) {
            header += "in vec3 vPixelPos3D;\n";
        }
        if (!hasShaderDeclaration(stripped, "vec2", "vPixelPos2D")) {
            header += "in vec2 vPixelPos2D;\n";
        }
        if (!hasShaderDeclaration(stripped, "vec4", "fragColor")) {
            header += "out vec4 fragColor;\n";
        }
        if (stripped.find("#define iPixelPos3D") == std::string::npos) {
            header += "#define iPixelPos3D vPixelPos3D\n";
        }
        if (stripped.find("#define iPixelPos2D") == std::string::npos) {
            header += "#define iPixelPos2D vPixelPos2D\n";
        }
        if (!hasShaderDeclaration(stripped, "float", "time")) {
            header += "uniform float time;\n";
        }
        if (!hasShaderDeclaration(stripped, "float", "vTime")) {
            header += "uniform float vTime;\n";
        }
        if (!hasShaderDeclaration(stripped, "vec2", "resolution")) {
            header += "uniform vec2 resolution;\n";
        }
        if (!hasShaderDeclaration(stripped, "float", "pixelCount")) {
            header += "uniform float pixelCount;\n";
        }
        if (!hasShaderDeclaration(stripped, "vec3", "pixelPosMin")) {
            header += "uniform vec3 pixelPosMin;\n";
        }
        if (!hasShaderDeclaration(stripped, "vec3", "pixelPosMax")) {
            header += "uniform vec3 pixelPosMax;\n";
        }
        if (!hasShaderDeclaration(stripped, "float", "zSlice")) {
            header += "uniform float zSlice;\n";
        }
        if (!hasShaderDeclaration(stripped, "sampler2D", "iChannel0")) {
            header += "uniform sampler2D iChannel0;\n";
        }
        if (!hasShaderDeclaration(stripped, "sampler2D", "iChannel1")) {
            header += "uniform sampler2D iChannel1;\n";
        }
        if (!hasShaderDeclaration(stripped, "sampler2D", "iChannel2")) {
            header += "uniform sampler2D iChannel2;\n";
        }
        if (!hasShaderDeclaration(stripped, "sampler2D", "iChannel3")) {
            header += "uniform sampler2D iChannel3;\n";
        }

        if (stripped.find("struct ColorStop") == std::string::npos) {
            header += 
                "struct ColorStop {\n"
                "    vec4 color;\n"
                "    float position;\n"
                "    float smoothness;\n"
                "};\n";
        }
        if (stripped.find("uniform EngineState") == std::string::npos) {
            header += 
                "layout(std140) uniform EngineState {\n"
                "    int activeStops;\n"
                "    ColorStop palette[16];\n"
                "    float velocity;\n"
                "    float complexity;\n"
                "    float scale;\n"
                "    float distortion;\n"
                "    float asymmetry;\n"
                "    float intensity;\n"
                "};\n";
        }

        if (stripped.find("vec4 _samplePaletteInternal") == std::string::npos) {
            header +=
                "vec4 _samplePaletteInternal(float p) {\n"
                "    for (int i = 0; i < 15; i++) {\n"
                "        if (i >= activeStops - 1) break;\n"
                "        float p0 = palette[i].position;\n"
                "        float p1 = palette[i+1].position;\n"
                "        if (p >= p0 && p <= p1) {\n"
                "            float t = (p - p0) / max(p1 - p0, 0.0001);\n"
                "            float smoothness = mix(palette[i].smoothness, palette[i+1].smoothness, t);\n"
                "            float width = smoothness;\n"
                "            float mixFactor;\n"
                "            if (width > 0.001) {\n"
                "                float edge0 = 0.5 - width * 0.5;\n"
                "                float f = clamp((t - edge0) / width, 0.0, 1.0);\n"
                "                mixFactor = f * f * (3.0 - 2.0 * f);\n"
                "            } else {\n"
                "                mixFactor = (t < 0.5) ? 0.0 : 1.0;\n"
                "            }\n"
                "            return mix(palette[i].color, palette[i+1].color, mixFactor);\n"
                "        }\n"
                "    }\n"
                "    return palette[activeStops - 1].color;\n"
                "}\n";
        }

        if (stripped.find("vec4 samplePalette(") == std::string::npos) {
            header +=
                "vec4 samplePalette(float pos) {\n"
                "    if (activeStops <= 0) return vec4(0.0);\n"
                "    if (activeStops == 1) return palette[0].color;\n"
                "    return _samplePaletteInternal(clamp(pos, 0.0, 1.0));\n"
                "}\n";
        }

        if (stripped.find("vec4 samplePaletteWrapped(") == std::string::npos) {
            header +=
                "vec4 samplePaletteWrapped(float pos) {\n"
                "    if (activeStops <= 0) return vec4(0.0);\n"
                "    if (activeStops == 1) return palette[0].color;\n"
                "    return _samplePaletteInternal(fract(pos));\n"
                "}\n";
        }

        if (isShadertoy) {
            header += 
                "#define iResolution vec3(resolution.x, resolution.y, 1.0)\n"
                "#define iTime time\n"
                "vec4 texture(sampler2D sampler, vec3 coord) { return texture(sampler, coord.xy); }\n"
                "vec4 textureLod(sampler2D sampler, vec3 coord, float lod) { return textureLod(sampler, coord.xy, lod); }\n"
                "out vec4 FragColor;\n"
                + stripped +
                "\n"
                "void main() {\n"
                "    vec2 fakeFragCoord = iPixelPos2D * iResolution.xy;\n"
                "    vec4 outColor;\n"
                "    mainImage(outColor, fakeFragCoord);\n"
                "    fragColor = outColor;\n"
                "}\n";
        } else {
            header += stripped;
        }
        return header;
    }

    bool compileShaderIfNeeded(const std::string& fsSourceStr, Patch::GPUProgram& gp, float& outMinSpeed, float& outMaxSpeed) {
        parseVelocityMetadata(fsSourceStr, outMinSpeed, outMaxSpeed);
        if (gp.program != 0 && gp.previewProgram != 0 && gp.glslSource == fsSourceStr) {
            return true;
        }

        if (gp.program != 0) {
            glDeleteProgram(gp.program);
            gp.program = 0;
        }
        if (gp.previewProgram != 0) {
            glDeleteProgram(gp.previewProgram);
            gp.previewProgram = 0;
        }

        gp.glslSource = fsSourceStr;

        bool isShadertoy = (fsSourceStr.find("mainImage") != std::string::npos);
        std::string errLog;

        // 1. COMPILE MAIN (POINT-RENDERING) PROGRAM
        std::string pointVsSource =
            "#version 150\n"
            "in vec3 position3D;\n"
            "in vec2 texCoord2D;\n"
            "in float pixelIndex;\n"
            "uniform float pixelCount;\n"
            "out vec3 vPixelPos3D;\n"
            "out vec2 vPixelPos2D;\n"
            "void main() {\n"
            "    vPixelPos3D = position3D;\n"
            "    vPixelPos2D = texCoord2D;\n"
            "    float xNDC = ((pixelIndex + 0.5) / pixelCount) * 2.0 - 1.0;\n"
            "    gl_Position = vec4(xNDC, 0.0, 0.0, 1.0);\n"
            "}\n";

        std::string pointFsSource = buildFsSource(fsSourceStr, isShadertoy);

        GLuint pointVs = glCreateShader(GL_VERTEX_SHADER);
        if (!compileStage(pointVs, pointVsSource, errLog)) {
            gp.compilerLog = "Point Vertex Shader error: " + errLog;
            glDeleteShader(pointVs);
            return false;
        }

        GLuint pointFs = glCreateShader(GL_FRAGMENT_SHADER);
        if (!compileStage(pointFs, pointFsSource, errLog)) {
            gp.compilerLog = "Point Fragment Shader error: " + errLog;
            glDeleteShader(pointFs);
            glDeleteShader(pointVs);
            return false;
        }

        gp.program = glCreateProgram();
        glBindAttribLocation(gp.program, 0, "position3D");
        glBindAttribLocation(gp.program, 1, "texCoord2D");
        glBindAttribLocation(gp.program, 2, "pixelIndex");

        if (!linkProgram(gp.program, pointVs, pointFs, errLog)) {
            gp.compilerLog = "Point Shader link error: " + errLog;
            glDeleteShader(pointFs);
            glDeleteShader(pointVs);
            glDeleteProgram(gp.program);
            gp.program = 0;
            return false;
        }
        glDeleteShader(pointFs);
        glDeleteShader(pointVs);

        // 2. COMPILE PREVIEW (2D QUAD-RENDERING) PROGRAM
        std::string quadVsSource =
            "#version 150\n"
            "in vec2 position;\n"
            "out vec3 vPixelPos3D;\n"
            "out vec2 vPixelPos2D;\n"
            "uniform float zSlice;\n"
            "uniform vec3 pixelPosMin;\n"
            "uniform vec3 pixelPosMax;\n"
            "void main() {\n"
            "    vPixelPos2D = position * 0.5 + 0.5;\n"
            "    vPixelPos3D = mix(pixelPosMin, pixelPosMax, vec3(vPixelPos2D.x, vPixelPos2D.y, zSlice));\n"
            "    gl_Position = vec4(position, 0.0, 1.0);\n"
            "}\n";

        std::string quadFsSource = pointFsSource; // reuse exactly the same unified shader source

        GLuint quadVs = glCreateShader(GL_VERTEX_SHADER);
        if (!compileStage(quadVs, quadVsSource, errLog)) {
            gp.compilerLog = "Preview Vertex Shader error: " + errLog;
            glDeleteShader(quadVs);
            return false;
        }

        GLuint quadFs = glCreateShader(GL_FRAGMENT_SHADER);
        if (!compileStage(quadFs, quadFsSource, errLog)) {
            gp.compilerLog = "Preview Fragment Shader error: " + errLog;
            glDeleteShader(quadFs);
            glDeleteShader(quadVs);
            return false;
        }

        gp.previewProgram = glCreateProgram();
        glBindAttribLocation(gp.previewProgram, 0, "position");

        if (!linkProgram(gp.previewProgram, quadVs, quadFs, errLog)) {
            gp.compilerLog = "Preview Shader link error: " + errLog;
            glDeleteShader(quadFs);
            glDeleteShader(quadVs);
            glDeleteProgram(gp.previewProgram);
            gp.previewProgram = 0;
            return false;
        }
        glDeleteShader(quadFs);
        glDeleteShader(quadVs);

        gp.compilerLog = "Compile successful!";
        return true;
    }
}

PatchProgram* PatchProgram::compile(flecs::entity patch){
    PatchProgram* program = new PatchProgram();

    static const std::string defaultLua = 
        "-- Default PixelMapper Lua Script\n"
        "-- update(canvas, time) is called once per frame\n"
        "\n"
        "function update(canvas, time)\n"
        "    -- clear screen with black\n"
        "    canvas:clear(0, 0, 0, 0)\n"
        "    \n"
        "    local w = canvas:width()\n"
        "    local h = canvas:height()\n"
        "    \n"
        "    -- Draw a moving circle sweep\n"
        "    local cx = w * 0.5 + math.cos(time * 2.0) * (w * 0.3)\n"
        "    local cy = h * 0.5 + math.sin(time * 3.0) * (h * 0.3)\n"
        "    local radius = 15.0 + math.sin(time * 5.0) * 5.0\n"
        "    \n"
        "    -- Draw filled circle with color shifting over time\n"
        "    local r = math.floor((math.sin(time) * 0.5 + 0.5) * 255)\n"
        "    local g = math.floor((math.sin(time + 2.0) * 0.5 + 0.5) * 255)\n"
        "    local b = math.floor((math.sin(time + 4.0) * 0.5 + 0.5) * 255)\n"
        "    \n"
        "    canvas:draw_circle(math.floor(cx), math.floor(cy), math.floor(radius), r, g, b, 255, true)\n"
        "    \n"
        "    -- Draw some noise lines / points\n"
        "    for i = 0, 10 do\n"
        "        local nx = math.floor(w * 0.5 + (canvas:noise(i * 10.0, time * 0.5, 0.0)) * (w * 0.4))\n"
        "        local ny = math.floor(h * 0.5 + (canvas:noise(0.0, i * 10.0, time * 0.5)) * (h * 0.4))\n"
        "        canvas:set_pixel(nx, ny, 255, 255, 255, 255)\n"
        "    end\n"
        "end\n";
    // Populate memory sources from files if empty
    auto* sData = patch.try_get_mut<Patch::ScriptData>();
    if (sData) {
        std::string luaPath = "scripts/default_patch.lua";
        if (const auto* settings = patch.try_get<Patch::Settings>()) {
            luaPath = settings->luaScriptPath;
        }
        if (sData->luaSource.empty()) {
            sData->luaSource = get_file_content(luaPath, defaultLua);
        }
        if (sData->glslSource.empty()) {
            sData->glslSource = Patch::defaultGLSL;
        }
    }

    //prepare pixel buffers
    program->pixelCount = 0;
    program->fixtureCount = 0;
    Fixture::iterateWithDmx(patch, [&](flecs::entity fixture, const Fixture::Layout& layout, const Fixture::DmxAddress& addr){
        program->pixelCount += layout.pixelCount;
        program->fixtureCount++;
    });
    if (program->pixelCount > 0) {
        program->pixelColors = (ColorRGBW*)malloc(program->pixelCount * sizeof(ColorRGBW));
        program->pixelColorsTemp = (ColorRGBW*)malloc(program->pixelCount * sizeof(ColorRGBW));
        program->pixelPositions = (glm::vec3*)malloc(program->pixelCount * sizeof(glm::vec3));
    } else {
        program->pixelColors = nullptr;
        program->pixelColorsTemp = nullptr;
        program->pixelPositions = nullptr;
    }

    if (program->fixtureCount > 0) {
        program->fixtures = (PatchProgram::CompiledFixture*)malloc(program->fixtureCount * sizeof(PatchProgram::CompiledFixture));
    } else {
        program->fixtures = nullptr;
    }

    //prepare universe buffers
    program->universeCount = Artnet::Universe::getCount(patch);
    program->universes = (PatchProgram::Universe*)malloc(program->universeCount * sizeof(PatchProgram::Universe));
    std::unordered_map<uint16_t, int> universeIndexByID; //store the id of each universe for later retrieval
    int universeIndex = 0;
    Artnet::Universe::iterate(patch, [&](flecs::entity universe, Artnet::Universe::Properties& props){
        universeIndexByID[props.universeId] = universeIndex;
        program->universes[universeIndex].id = props.universeId;
        universeIndex++;
    });

    
    int pixelIndex = 0;
    int fixtureIdx = 0;
    std::vector<PatchProgram::Pix2UniCopyInstr> p2us;
    Fixture::iterateWithDmx(patch, [&](flecs::entity fixture, const Fixture::Layout& layout, const Fixture::DmxAddress& addr){
        int fixtureByteCount = layout.pixelCount * layout.channelsPerPixel;
        int universeSpanSize = (addr.address + fixtureByteCount + 511) / 512;

        if (program->fixtures) {
            program->fixtures[fixtureIdx] = {
                fixture.id(),
                (uint32_t)pixelIndex,
                (uint32_t)layout.pixelCount
            };
        }
        fixtureIdx++;

        if(const auto* pixelData = fixture.try_get<Fixture::PixelData>()){
            if (layout.pixelCount > 0 && !pixelData->positions.empty()) {
                memcpy(program->pixelPositions + pixelIndex, pixelData->positions.data(), pixelData->positions.size() * sizeof(glm::vec3));
            }
        }

        int universeId = addr.universe;
        int universeStartByte = addr.address;
        int pixelStartByte = 0;
        int remainingByteCount = fixtureByteCount;
        int fixturePixelIndex = 0;
        int fixturePixelByteIndex = 0;
        while(remainingByteCount > 0){
            int bytesInUniverse = std::min(512 - universeStartByte, remainingByteCount);

            if(universeIndexByID.count(universeId)){
                PatchProgram::Pix2UniCopyInstr p2u;
                p2u.universeIndex = universeIndexByID[universeId];
                p2u.universeOffset = universeStartByte;
                p2u.byteCount = bytesInUniverse;
                p2u.bytesPerPixel = layout.channelsPerPixel;
                p2u.pixelIndex = pixelIndex + fixturePixelIndex;
                p2u.pixelStartByte = fixturePixelByteIndex;
                p2us.push_back(p2u);
            }

            universeId++;
            universeStartByte = 0; //following universes always start at 0
            remainingByteCount -= bytesInUniverse;
            fixturePixelIndex += (fixturePixelByteIndex + bytesInUniverse) / layout.channelsPerPixel;
            fixturePixelByteIndex = (fixturePixelByteIndex + bytesInUniverse) % layout.channelsPerPixel;
        }

        pixelIndex += layout.pixelCount;
    });

    program->p2uCount = p2us.size();
    program->p2us = (PatchProgram::Pix2UniCopyInstr*)malloc(program->p2uCount * sizeof(PatchProgram::Pix2UniCopyInstr));
    memcpy(program->p2us, p2us.data(), program->p2uCount * sizeof(PatchProgram::Pix2UniCopyInstr));

    int previewRes = 256;
    if(const auto* settings = patch.try_get<Patch::Settings>()){
        program->networkEnabled = settings->networkEnabled;
        program->sourcePort = settings->sourcePort;
        program->refreshRate = settings->refreshRate;
        program->renderMode = settings->renderMode;
        program->whiteMode = settings->whiteMode;
        program->highlightFrequency = settings->highlightFrequency;
        previewRes = settings->vfbResolution;
    }

    int previewW = previewRes;
    int previewH = previewRes;
    if (patch.has<Patch::RenderArea>()) {
        const auto& ra = patch.get<Patch::RenderArea>();
        float dx = ra.max.x - ra.min.x;
        float dy = ra.max.y - ra.min.y;
        if (dx > 0.001f || dy > 0.001f) {
            if (dx >= dy) {
                previewH = std::max(1, (int)std::round(previewRes * dy / dx));
            } else {
                previewW = std::max(1, (int)std::round(previewRes * dx / dy));
            }
        }
    }
    program->previewWidth = previewW;
    program->previewHeight = previewH;

    if (program->pixelCount > 0) {
        program->pixelSelected = new std::atomic<bool>[program->pixelCount];
        for (uint32_t i = 0; i < program->pixelCount; ++i) {
            program->pixelSelected[i].store(false);
        }
    } else {
        program->pixelSelected = nullptr;
    }

    std::vector<Artnet::Device::Settings> devSettings;
    Artnet::Device::iterateInPatch(patch, [&](flecs::entity device, Artnet::Device::Settings& settings){
        devSettings.push_back(settings);
    });
    program->deviceCount = devSettings.size();
    if (program->deviceCount > 0) {
        program->devices = (Artnet::Device::Settings*)malloc(program->deviceCount * sizeof(Artnet::Device::Settings));
        std::memcpy(program->devices, devSettings.data(), program->deviceCount * sizeof(Artnet::Device::Settings));
    } else {
        program->devices = nullptr;
    }

    const auto& renderArea = patch.get<Patch::RenderArea>();
    program->pixelPosMin = renderArea.min;
    program->pixelPosMax = renderArea.max;

    // Calculate projected 2D coordinates on CPU for Point-Rendering
    std::vector<glm::vec2> projectedPositions(program->pixelCount);
    glm::vec2 pMin(FLT_MAX);
    glm::vec2 pMax(-FLT_MAX);

    Patch::ProjectionMode projMode = Patch::ProjectionMode::TOP_DOWN_XY;
    program->projectionMode = projMode;

    for (uint32_t i = 0; i < program->pixelCount; ++i) {
        glm::vec3 pos3d = program->pixelPositions[i];
        glm::vec2 projected(0.5f);
        if (projMode == Patch::ProjectionMode::TOP_DOWN_XY) {
            projected = glm::vec2(pos3d.x, pos3d.y);
        } else if (projMode == Patch::ProjectionMode::FRONT_XZ) {
            projected = glm::vec2(pos3d.x, pos3d.z);
        } else if (projMode == Patch::ProjectionMode::SIDE_YZ) {
            projected = glm::vec2(pos3d.y, pos3d.z);
        }
        projectedPositions[i] = projected;
        pMin = glm::min(pMin, projected);
        pMax = glm::max(pMax, projected);
    }

    float pdx = pMax.x - pMin.x;
    float pdy = pMax.y - pMin.y;

    std::vector<glm::vec2> normalizedUVs(program->pixelCount, glm::vec2(0.5f));
    for (uint32_t i = 0; i < program->pixelCount; ++i) {
        glm::vec2 p = projectedPositions[i];
        float u = 0.5f;
        float v = 0.5f;
        if (pdx > 0.001f) u = (p.x - pMin.x) / pdx;
        if (pdy > 0.001f) v = (p.y - pMin.y) / pdy;
        normalizedUVs[i] = glm::clamp(glm::vec2(u, v), 0.0f, 1.0f);
    }

    int mainVfbWidth = program->pixelCount > 0 ? program->pixelCount : 1;
    int mainVfbHeight = 1;
    program->vfbWidth = mainVfbWidth;
    program->vfbHeight = mainVfbHeight;

    // Allocate VFB CPU pixels
    int vfbSize = mainVfbWidth;
    program->vfbPixels    = (ColorRGBW*)malloc(vfbSize * sizeof(ColorRGBW));
    program->vfbPixelsOld = (ColorRGBW*)malloc(vfbSize * sizeof(ColorRGBW));
    std::memset(program->vfbPixels,    0, vfbSize * sizeof(ColorRGBW));
    std::memset(program->vfbPixelsOld, 0, vfbSize * sizeof(ColorRGBW));

    // Lua Setup
    if (program->renderMode == Patch::RenderMode::LUA) {
        if (const auto* scriptData = patch.try_get<Patch::ScriptData>()) {
            program->luaState = std::make_unique<sol::state>();
            program->luaState->open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
            
            program->luaState->new_usertype<Canvas>("Canvas",
                "width", &Canvas::width,
                "height", &Canvas::height,
                "clear", &Canvas::clear,
                "set_pixel", &Canvas::set_pixel,
                "draw_line", &Canvas::draw_line,
                "draw_rect", &Canvas::draw_rect,
                "draw_circle", &Canvas::draw_circle,
                "noise", &Canvas::noise
            );

            sol::load_result loadRes = program->luaState->load(scriptData->luaSource);
            if (!loadRes.valid()) {
                sol::error err = loadRes;
                program->compilerLog = err.what();
            } else {
                sol::protected_function_result runRes = loadRes();
                if (!runRes.valid()) {
                    sol::error err = runRes;
                    program->compilerLog = err.what();
                } else {
                    program->luaUpdateFn = (*program->luaState)["update"];
                    if (!program->luaUpdateFn.valid()) {
                        program->compilerLog = "Error: 'update(canvas, time)' function not found in Lua script.";
                    }
                }
            }
        }
    } else if (program->renderMode == Patch::RenderMode::GLSL) {
        if (const auto* scriptData = patch.try_get<Patch::ScriptData>()) {
            // Retrieve or create persistent GPU resources and programs
            if (!patch.has<Patch::GPUResources>()) patch.set<Patch::GPUResources>({});
            if (!patch.has<Patch::GPUProgram>()) patch.set<Patch::GPUProgram>({});
            auto* res = &patch.get_mut<Patch::GPUResources>();
            auto* patchGp = &patch.get_mut<Patch::GPUProgram>();

            // Initialize noise texture if needed (smooth fractal noise)
            if (res->glslNoiseTex == 0) {
                glGenTextures(1, &res->glslNoiseTex);
                glBindTexture(GL_TEXTURE_2D, res->glslNoiseTex);
                
                std::vector<float> grid16(16 * 16);
                std::vector<float> grid32(32 * 32);
                std::vector<float> grid64(64 * 64);
                for (int i = 0; i < 16 * 16; ++i) grid16[i] = (float)rand() / RAND_MAX;
                for (int i = 0; i < 32 * 32; ++i) grid32[i] = (float)rand() / RAND_MAX;
                for (int i = 0; i < 64 * 64; ++i) grid64[i] = (float)rand() / RAND_MAX;

                auto sampleGrid = [](const std::vector<float>& grid, int size, float x, float y) {
                    float fx = x * size;
                    float fy = y * size;
                    int ix0 = ((int)fx) % size;
                    int iy0 = ((int)fy) % size;
                    int ix1 = (ix0 + 1) % size;
                    int iy1 = (iy0 + 1) % size;
                    float tx = fx - std::floor(fx);
                    float ty = fy - std::floor(fy);
                    float sx = tx * tx * (3.0f - 2.0f * tx);
                    float sy = ty * ty * (3.0f - 2.0f * ty);
                    float v00 = grid[iy0 * size + ix0];
                    float v10 = grid[iy0 * size + ix1];
                    float v01 = grid[iy1 * size + ix0];
                    float v11 = grid[iy1 * size + ix1];
                    return glm::mix(glm::mix(v00, v10, sx), glm::mix(v01, v11, sx), sy);
                };

                std::vector<uint8_t> noiseData(256 * 256 * 4);
                for (int y = 0; y < 256; ++y) {
                    for (int x = 0; x < 256; ++x) {
                        float u = (float)x / 256.0f;
                        float v = (float)y / 256.0f;
                        float n = 0.5f * sampleGrid(grid16, 16, u, v) +
                                  0.3f * sampleGrid(grid32, 32, u, v) +
                                  0.2f * sampleGrid(grid64, 64, u, v);
                        uint8_t val = (uint8_t)(n * 255.0f);
                        int idx = (y * 256 + x) * 4;
                        noiseData[idx + 0] = val;
                        noiseData[idx + 1] = val;
                        noiseData[idx + 2] = val;
                        noiseData[idx + 3] = 255;
                    }
                }
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, noiseData.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            // Initialize position texture containing 3D physical coordinates
            if (res->glslPositionTex == 0 || res->vfbWidth != mainVfbWidth) {
                if (res->glslPositionTex == 0) {
                    glGenTextures(1, &res->glslPositionTex);
                }
                glBindTexture(GL_TEXTURE_2D, res->glslPositionTex);
                
                std::vector<float> posData(mainVfbWidth * 4, 0.0f);
                if (program->pixelPositions) {
                    for (int i = 0; i < (int)program->pixelCount && i < mainVfbWidth; i++) {
                        posData[i * 4 + 0] = program->pixelPositions[i].x;
                        posData[i * 4 + 1] = program->pixelPositions[i].y;
                        posData[i * 4 + 2] = program->pixelPositions[i].z;
                        posData[i * 4 + 3] = 1.0f;
                    }
                }
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, mainVfbWidth, 1, 0, GL_RGBA, GL_FLOAT, posData.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            // Resize FBO if resolution changed
            if (res->glslFbo == 0 || res->vfbWidth != mainVfbWidth || res->previewWidth != program->previewWidth || res->previewHeight != program->previewHeight) {
                if (res->glslFbo) {
                    glDeleteFramebuffers(1, &res->glslFbo);
                    glDeleteTextures(1, &res->glslFboTex);
                    glDeleteFramebuffers(1, &res->glslEditorFbo);
                    glDeleteTextures(1, &res->glslEditorFboTex);
                    glDeleteFramebuffers(1, &res->glslPlaybackPreviewFbo);
                    glDeleteTextures(1, &res->glslPlaybackPreviewFboTex);
                    glDeleteFramebuffers(1, &res->glslPlaybackPreviewFboOld);
                    glDeleteTextures(1, &res->glslPlaybackPreviewFboTexOld);
                    glDeleteFramebuffers(1, &res->glslPlaybackPreviewFboBlend);
                    glDeleteTextures(1, &res->glslPlaybackPreviewFboTexBlend);
                    for (int i = 0; i < 2; i++) {
                        if (res->glslPlaybackPreviewDisplayFbo[i]) {
                            glDeleteFramebuffers(1, &res->glslPlaybackPreviewDisplayFbo[i]);
                            res->glslPlaybackPreviewDisplayFbo[i] = 0;
                        }
                        if (res->glslPlaybackPreviewDisplayTex[i]) {
                            glDeleteTextures(1, &res->glslPlaybackPreviewDisplayTex[i]);
                            res->glslPlaybackPreviewDisplayTex[i] = 0;
                        }
                    }

                    glDeleteFramebuffers(1, &res->glslFboOld);
                    glDeleteTextures(1, &res->glslFboTexOld);
                    glDeleteFramebuffers(1, &res->glslFboBlend);
                    glDeleteTextures(1, &res->glslFboTexBlend);
                    glDeleteBuffers(2, res->glslPbo);
                    res->glslFbo = 0;
                }

                res->vfbWidth = mainVfbWidth;
                res->vfbHeight = 1;
                res->previewWidth = program->previewWidth;
                res->previewHeight = program->previewHeight;

                glGenFramebuffers(1, &res->glslFbo);
                glGenTextures(1, &res->glslFboTex);
                glBindTexture(GL_TEXTURE_2D, res->glslFboTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mainVfbWidth, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, res->glslFbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslFboTex, 0);

                // Editor preview
                glGenFramebuffers(1, &res->glslEditorFbo);
                glGenTextures(1, &res->glslEditorFboTex);
                glBindTexture(GL_TEXTURE_2D, res->glslEditorFboTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, res->previewWidth, res->previewHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, res->glslEditorFbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslEditorFboTex, 0);

                // Playback 2D preview
                glGenFramebuffers(1, &res->glslPlaybackPreviewFbo);
                glGenTextures(1, &res->glslPlaybackPreviewFboTex);
                glBindTexture(GL_TEXTURE_2D, res->glslPlaybackPreviewFboTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, res->previewWidth, res->previewHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, res->glslPlaybackPreviewFbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslPlaybackPreviewFboTex, 0);

                glGenFramebuffers(1, &res->glslPlaybackPreviewFboOld);
                glGenTextures(1, &res->glslPlaybackPreviewFboTexOld);
                glBindTexture(GL_TEXTURE_2D, res->glslPlaybackPreviewFboTexOld);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, res->previewWidth, res->previewHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, res->glslPlaybackPreviewFboOld);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslPlaybackPreviewFboTexOld, 0);

                glGenFramebuffers(1, &res->glslPlaybackPreviewFboBlend);
                glGenTextures(1, &res->glslPlaybackPreviewFboTexBlend);
                glBindTexture(GL_TEXTURE_2D, res->glslPlaybackPreviewFboTexBlend);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, res->previewWidth, res->previewHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, res->glslPlaybackPreviewFboBlend);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslPlaybackPreviewFboTexBlend, 0);

                for (int i = 0; i < 2; i++) {
                    glGenFramebuffers(1, &res->glslPlaybackPreviewDisplayFbo[i]);
                    glGenTextures(1, &res->glslPlaybackPreviewDisplayTex[i]);
                    glBindTexture(GL_TEXTURE_2D, res->glslPlaybackPreviewDisplayTex[i]);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, res->previewWidth, res->previewHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glBindFramebuffer(GL_FRAMEBUFFER, res->glslPlaybackPreviewDisplayFbo[i]);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslPlaybackPreviewDisplayTex[i], 0);
                }


                glGenFramebuffers(1, &res->glslFboOld);
                glGenTextures(1, &res->glslFboTexOld);
                glBindTexture(GL_TEXTURE_2D, res->glslFboTexOld);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mainVfbWidth, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glBindFramebuffer(GL_FRAMEBUFFER, res->glslFboOld);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslFboTexOld, 0);

                glGenFramebuffers(1, &res->glslFboBlend);
                glGenTextures(1, &res->glslFboTexBlend);
                glBindTexture(GL_TEXTURE_2D, res->glslFboTexBlend);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mainVfbWidth, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glBindFramebuffer(GL_FRAMEBUFFER, res->glslFboBlend);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslFboTexBlend, 0);

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glBindTexture(GL_TEXTURE_2D, 0);

                int pboSize = mainVfbWidth * 4;
                glGenBuffers(2, res->glslPbo);
                for (int pi = 0; pi < 2; pi++) {
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, res->glslPbo[pi]);
                    glBufferData(GL_PIXEL_PACK_BUFFER, pboSize, nullptr, GL_STREAM_READ);
                }
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                res->pboFrameIndex = 0;
                res->vaoReady = false;
            }

            // Compile blend shader if not done
            if (res->glslBlendProgram == 0) {
                const char* blendVsSource =
                    "#version 150\n"
                    "in vec2 position;\n"
                    "out vec2 uv;\n"
                    "void main() {\n"
                    "    uv = position * 0.5 + 0.5;\n"
                    "    gl_Position = vec4(position, 0.0, 1.0);\n"
                    "}\n";

                const char* blendFsSource =
                    "#version 150\n"
                    "in vec2 uv;\n"
                    "out vec4 fragColor;\n"
                    "uniform sampler2D texActive;\n"
                    "uniform sampler2D texOld;\n"
                    "uniform sampler2D texNoise;\n"
                    "uniform sampler2D texPosition;\n"
                    "uniform float mixFactor;\n"
                    "uniform int transitionType;\n"
                    "uniform int transitionVolumetric;\n"
                    "uniform int isPreview2D;\n"
                    "uniform vec3 pixelPosMin;\n"
                    "uniform vec3 pixelPosMax;\n"
                    "uniform float zSlice;\n"
                    "uniform vec2 sweepDirection;\n"
                    "uniform vec3 sweepDirection3D;\n"
                    "uniform vec3 sphereCenter3D;\n"
                    "uniform int circleWipeInward;\n"
                    "\n"
                    "float hash(vec3 p) {\n"
                    "    p = fract(p * vec3(443.897, 441.423, 437.195));\n"
                    "    p += dot(p, p.yzx + 19.19);\n"
                    "    return fract((p.x + p.y) * p.z);\n"
                    "}\n"
                    "float noise3D(vec3 p) {\n"
                    "    vec3 i = floor(p);\n"
                    "    vec3 f = fract(p);\n"
                    "    vec3 u = f * f * (3.0 - 2.0 * f);\n"
                    "    return mix(mix(mix(hash(i + vec3(0.0,0.0,0.0)), hash(i + vec3(1.0,0.0,0.0)), u.x),\n"
                    "                   mix(hash(i + vec3(0.0,1.0,0.0)), hash(i + vec3(1.0,1.0,0.0)), u.x), u.y),\n"
                    "               mix(mix(hash(i + vec3(0.0,0.0,1.0)), hash(i + vec3(1.0,0.0,1.0)), u.x),\n"
                    "                   mix(hash(i + vec3(0.0,1.0,1.0)), hash(i + vec3(1.0,1.0,1.0)), u.x), u.y), u.z);\n"
                    "}\n"
                    "float fbm3D(vec3 p) {\n"
                    "    float val = 0.5 * noise3D(p);\n"
                    "    val += 0.3 * noise3D(p * 2.0);\n"
                    "    val += 0.2 * noise3D(p * 4.0);\n"
                    "    return val;\n"
                    "}\n"
                    "\n"
                    "void main() {\n"
                    "    vec4 cActive = texture(texActive, uv);\n"
                    "    vec4 cOld = texture(texOld, uv);\n"
                    "    vec3 pos3D = vec3(0.0);\n"
                    "    if (transitionVolumetric == 1) {\n"
                    "        if (isPreview2D == 1) {\n"
                    "            pos3D = mix(pixelPosMin, pixelPosMax, vec3(uv.x, uv.y, zSlice));\n"
                    "        } else {\n"
                    "            pos3D = texture(texPosition, uv).xyz;\n"
                    "        }\n"
                    "    }\n"
                    "\n"
                    "    float factor = mixFactor;\n"
                    "\n"
                    "    if (transitionType == 1) {\n"
                    "        if (transitionVolumetric == 1) {\n"
                    "            vec3 normPos = (pos3D - pixelPosMin) / max(vec3(0.001), pixelPosMax - pixelPosMin);\n"
                    "            float noiseVal = fbm3D(normPos * 6.0);\n"
                    "            float threshold = mixFactor * 1.1 - 0.05;\n"
                    "            factor = 1.0 - smoothstep(threshold - 0.05, threshold + 0.05, noiseVal);\n"
                    "        } else {\n"
                    "            float noiseVal = texture(texNoise, uv * 4.0).r;\n"
                    "            float threshold = mixFactor * 1.1 - 0.05;\n"
                    "            factor = 1.0 - smoothstep(threshold - 0.05, threshold + 0.05, noiseVal);\n"
                    "        }\n"
                    "    } else if (transitionType == 2) {\n"
                    "        if (transitionVolumetric == 1) {\n"
                    "            float d = dot(pos3D, sweepDirection3D);\n"
                    "            float dMin = min(dot(pixelPosMin, sweepDirection3D), dot(pixelPosMax, sweepDirection3D));\n"
                    "            float dMax = max(dot(pixelPosMin, sweepDirection3D), dot(pixelPosMax, sweepDirection3D));\n"
                    "            float span = max(0.001, dMax - dMin);\n"
                    "            float edge = mixFactor * (span * 1.2) + dMin - span * 0.1;\n"
                    "            factor = 1.0 - smoothstep(edge - span * 0.05, edge + span * 0.05, d);\n"
                    "        } else {\n"
                    "            float d = dot(uv - vec2(0.5), sweepDirection) + 0.5;\n"
                    "            float edge = mixFactor * 1.2 - 0.1;\n"
                    "            factor = 1.0 - smoothstep(edge - 0.05, edge + 0.05, d);\n"
                    "        }\n"
                    "    } else if (transitionType == 3) {\n"
                    "        if (circleWipeInward == 1) {\n"
                    "            if (transitionVolumetric == 1) {\n"
                    "                vec3 center = sphereCenter3D;\n"
                    "                float dist = distance(pos3D, center);\n"
                    "                float maxRadius = distance(pixelPosMin, pixelPosMax) * 0.5;\n"
                    "                float radius = (1.0 - mixFactor) * maxRadius * 1.1;\n"
                    "                factor = smoothstep(radius - maxRadius * 0.05, radius + maxRadius * 0.05, dist);\n"
                    "            } else {\n"
                    "                float dist = distance(uv, vec2(0.5));\n"
                    "                float maxDist = 0.75;\n"
                    "                float radius = (1.0 - mixFactor) * maxDist * 1.1;\n"
                    "                factor = smoothstep(radius - 0.05, radius + 0.05, dist);\n"
                    "            }\n"
                    "        } else {\n"
                    "            if (transitionVolumetric == 1) {\n"
                    "                vec3 center = sphereCenter3D;\n"
                    "                float dist = distance(pos3D, center);\n"
                    "                float maxRadius = distance(pixelPosMin, pixelPosMax) * 0.5;\n"
                    "                float radius = mixFactor * maxRadius * 1.1;\n"
                    "                factor = 1.0 - smoothstep(radius - maxRadius * 0.05, radius + maxRadius * 0.05, dist);\n"
                    "            } else {\n"
                    "                float dist = distance(uv, vec2(0.5));\n"
                    "                float maxDist = 0.75;\n"
                    "                float radius = mixFactor * maxDist * 1.1;\n"
                    "                factor = 1.0 - smoothstep(radius - 0.05, radius + 0.05, dist);\n"
                    "            }\n"
                    "        }\n"
                    "    } else if (transitionType == 4) {\n"
                    "        float lum = dot(cActive.rgb, vec3(0.299, 0.587, 0.114));\n"
                    "        factor = smoothstep(1.0 - mixFactor - 0.05, 1.0 - mixFactor + 0.05, lum);\n"
                    "    }\n"
                    "\n"
                    "    fragColor = mix(cOld, cActive, factor);\n"
                    "}\n";

                GLuint blendVs = glCreateShader(GL_VERTEX_SHADER);
                glShaderSource(blendVs, 1, &blendVsSource, nullptr);
                glCompileShader(blendVs);

                GLuint blendFs = glCreateShader(GL_FRAGMENT_SHADER);
                glShaderSource(blendFs, 1, &blendFsSource, nullptr);
                glCompileShader(blendFs);

                res->glslBlendProgram = glCreateProgram();
                glAttachShader(res->glslBlendProgram, blendVs);
                glAttachShader(res->glslBlendProgram, blendFs);
                glBindAttribLocation(res->glslBlendProgram, 0, "position");
                glLinkProgram(res->glslBlendProgram);

                glDeleteShader(blendVs);
                glDeleteShader(blendFs);
            }

            // Create and upload buffers for Point-Rendering (on compile thread)
            if (res->glslPointVbo == 0) {
                glGenBuffers(1, &res->glslPointVbo);
            }
            if (res->glslQuadVbo == 0) {
                glGenBuffers(1, &res->glslQuadVbo);
                float quadVertices[] = {
                    -1.0f,-1.0f,  1.0f,-1.0f,  -1.0f, 1.0f,
                    -1.0f, 1.0f,  1.0f,-1.0f,   1.0f, 1.0f,
                };
                glBindBuffer(GL_ARRAY_BUFFER, res->glslQuadVbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
            }

            if (program->pixelCount > 0) {
                struct PointVertex {
                    glm::vec3 position3D;
                    glm::vec2 texCoord2D;
                    float pixelIndex;
                };
                std::vector<PointVertex> vertices(program->pixelCount);
                for (uint32_t i = 0; i < program->pixelCount; ++i) {
                    vertices[i].position3D = program->pixelPositions[i];
                    vertices[i].texCoord2D = normalizedUVs[i];
                    vertices[i].pixelIndex = (float)i;
                }
                glBindBuffer(GL_ARRAY_BUFFER, res->glslPointVbo);
                glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(PointVertex), vertices.data(), GL_STATIC_DRAW);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
            }
            res->vaoReady = false; // Trigger lazy VAO creation/setup on RT thread

            // Compile default patch shader
            compileShaderIfNeeded(scriptData->glslSource, *patchGp, program->defaultMinSpeed, program->defaultMaxSpeed);
            program->compilerLog = patchGp->compilerLog;
            program->glslProgram = patchGp->program;
            program->glslPreviewProgram = patchGp->previewProgram;
            program->defaultCompilerLog = patchGp->compilerLog;

            // Copy Handles to PatchProgram
            program->glslFbo = res->glslFbo;
            program->glslFboTex = res->glslFboTex;
            program->glslPointVao = res->glslPointVao;
            program->glslPointVbo = res->glslPointVbo;
            program->glslQuadVao = res->glslQuadVao;
            program->glslQuadVbo = res->glslQuadVbo;
            program->glslPbo[0] = res->glslPbo[0];
            program->glslPbo[1] = res->glslPbo[1];
            program->pboFrameIndex = res->pboFrameIndex;
            program->shaderCompiled = (program->glslProgram != 0);
            program->vaoReady = res->vaoReady;
            program->shaderSource = patchGp->glslSource;
            program->glslEditorFbo = res->glslEditorFbo;
            program->glslEditorFboTex = res->glslEditorFboTex;
            program->glslPlaybackPreviewFbo = res->glslPlaybackPreviewFbo;
            program->glslPlaybackPreviewFboTex = res->glslPlaybackPreviewFboTex;
            program->glslPlaybackPreviewFboOld = res->glslPlaybackPreviewFboOld;
            program->glslPlaybackPreviewFboTexOld = res->glslPlaybackPreviewFboTexOld;
            program->glslPlaybackPreviewFboBlend = res->glslPlaybackPreviewFboBlend;
            program->glslPlaybackPreviewFboTexBlend = res->glslPlaybackPreviewFboTexBlend;
            for (int i = 0; i < 2; i++) {
                program->glslPlaybackPreviewDisplayFbo[i] = res->glslPlaybackPreviewDisplayFbo[i];
                program->glslPlaybackPreviewDisplayTex[i] = res->glslPlaybackPreviewDisplayTex[i];
            }
            program->glslPlaybackPreviewReadIdx.store(0);
            program->glslCurrentPlaybackPreviewTexID.store(res->glslPlaybackPreviewDisplayTex[0]);


            program->glslFboOld = res->glslFboOld;
            program->glslFboTexOld = res->glslFboTexOld;
            program->glslFboBlend = res->glslFboBlend;
            program->glslFboTexBlend = res->glslFboTexBlend;
            program->glslBlendProgram = res->glslBlendProgram;
            program->glslNoiseTex = res->glslNoiseTex;
            program->glslPositionTex = res->glslPositionTex;

            // Initialize GLSL UBO for Generative Engine UBO state
            glGenBuffers(1, &program->glslUboId);
            glBindBuffer(GL_UNIFORM_BUFFER, program->glslUboId);
            glBufferData(GL_UNIFORM_BUFFER, sizeof(Generative::EngineStateUBO), nullptr, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);

            // Compile Cue Shaders
            flecs::entity cueFolder = patch.target<CueList::CueFolder>();
            if (cueFolder.is_valid()) {
                if (const auto* session = cueFolder.try_get<CueList::SessionState>()) {
                    program->activeCueIndex.store(session->activeIndex);
                }

                struct SortEntry {
                    flecs::entity entity;
                    int order = 0;
                };
                std::vector<SortEntry> sortedCues;
                cueFolder.children([&](flecs::entity child) {
                    if (child.has<CueList::Cue::Is>()) {
                        SortEntry entry;
                        entry.entity = child;
                        if (const auto* ord = child.try_get<CueList::Cue::IndexOrder>()) entry.order = ord->value;
                        sortedCues.push_back(entry);
                    }
                });
                std::sort(sortedCues.begin(), sortedCues.end(), [](const SortEntry& a, const SortEntry& b) {
                    return a.order < b.order;
                });

                for (const auto& entry : sortedCues) {
                    PatchProgram::CompiledCue cc;
                    cc.program = 0;
                    cc.previewProgram = 0;

                    flecs::entity targetEffect = entry.entity.target<CueList::Cue::TargetEffect>();
                    if (targetEffect.is_valid()) {
                        if (const auto* glsl = targetEffect.try_get<EffectBank::Effect::GlslSource>()) {
                            if (!targetEffect.has<Patch::GPUProgram>()) targetEffect.set<Patch::GPUProgram>({});
                            auto* fxGp = &targetEffect.get_mut<Patch::GPUProgram>();
                            compileShaderIfNeeded(glsl->value, *fxGp, cc.minSpeed, cc.maxSpeed);
                            cc.program = fxGp->program;
                            cc.previewProgram = fxGp->previewProgram;
                            cc.compilerLog = fxGp->compilerLog;
                        }
                    }
                    program->compiledCues.push_back(cc);
                }
            }

            // Compile Bank Effects
            flecs::entity bankFolder = patch.target<EffectBank::EffectFolder>();
            if (bankFolder.is_valid()) {
                if (const auto* session = bankFolder.try_get<EffectBank::SessionState>()) {
                    program->editingBankIndex.store(session->activeIndex);
                }

                std::vector<flecs::entity> fxList;
                bankFolder.children([&](flecs::entity child) {
                    if (child.has<EffectBank::Effect::Is>()) {
                        fxList.push_back(child);
                    }
                });

                std::sort(fxList.begin(), fxList.end(), [](const flecs::entity& a, const flecs::entity& b) {
                    return a.id() < b.id();
                });

                for (const auto& fx : fxList) {
                    PatchProgram::CompiledCue cc;
                    cc.program = 0;
                    cc.previewProgram = 0;

                    if (const auto* glsl = fx.try_get<EffectBank::Effect::GlslSource>()) {
                        if (!fx.has<Patch::GPUProgram>()) fx.set<Patch::GPUProgram>({});
                        auto* fxGp = &fx.get_mut<Patch::GPUProgram>();
                        compileShaderIfNeeded(glsl->value, *fxGp, cc.minSpeed, cc.maxSpeed);
                        cc.program = fxGp->program;
                        cc.previewProgram = fxGp->previewProgram;
                        cc.compilerLog = fxGp->compilerLog;
                    }
                    program->compiledBankEffects.push_back(cc);
                }
            }
        }
        glFlush();
    }
    // ── Compile Generative Engine Presets & Settings ──
    if (const auto* settings = patch.try_get<Generative::Settings>()) {
        program->generativeSettings = *settings;
    } else {
        program->generativeSettings = Generative::Settings();
    }

    flecs::entity paletteFolder = patch.target<Generative::PaletteFolder>();
    if (paletteFolder.is_valid()) {
        paletteFolder.children([&](flecs::entity child) {
            if (child.has<Generative::Palette::Is>()) {
                Generative::CompiledPalette cp;
                cp.name = child.name();
                if (const auto* stops = child.try_get<Generative::Palette::Stops>()) {
                    cp.stops = stops->value;
                }
                if (const auto* isModeB = child.try_get<Generative::Palette::IsModeB>()) {
                    cp.isModeB = isModeB->value;
                }
                program->palettePool.push_back(cp);
            }
        });
    }

    flecs::entity motiveFolder = patch.target<Generative::MotiveFolder>();
    if (motiveFolder.is_valid()) {
        motiveFolder.children([&](flecs::entity child) {
            if (child.has<Generative::Motive::Is>()) {
                Generative::CompiledMotive cm;
                cm.name = child.name();
                if (const auto* params = child.try_get<Generative::Motive::Params>()) {
                    cm.velocity = params->velocity;
                    cm.complexity = params->complexity;
                    cm.scale = params->scale;
                    cm.distortion = params->distortion;
                    cm.asymmetry = params->asymmetry;
                    cm.intensity = params->intensity;
                    std::memcpy(cm.wanderAmp, params->wanderAmp, sizeof(cm.wanderAmp));
                    std::memcpy(cm.wanderFreq, params->wanderFreq, sizeof(cm.wanderFreq));
                } else {
                    cm.velocity = 0.5f; cm.complexity = 0.5f; cm.scale = 0.5f;
                    cm.distortion = 0.5f; cm.asymmetry = 0.5f; cm.intensity = 0.5f;
                    std::fill(std::begin(cm.wanderAmp), std::end(cm.wanderAmp), 0.15f);
                    std::fill(std::begin(cm.wanderFreq), std::end(cm.wanderFreq), 0.5f);
                }
                program->motivePool.push_back(cm);
            }
        });
    }

    if (auto* scriptData = patch.try_get_mut<Patch::ScriptData>()) {
        scriptData->compilerLog = program->compilerLog;
    }

    return program;
}

PatchProgram::~PatchProgram(){
    free(devices);
    free(p2us);
    free(pixelColors);
    free(pixelColorsTemp);
    free(pixelPositions);
    free(universes);
    free(vfbPixels);
    free(vfbPixelsOld);
    free(fixtures);
    delete[] pixelSelected;
    if (glslUboId != 0) {
        glDeleteBuffers(1, &glslUboId);
    }
}

float& PatchProgram::getVTimeRef(unsigned int prog) {
    if (prog == glslProgram || prog == glslPreviewProgram) {
        return defaultVTime;
    }
    for (auto& cc : compiledCues) {
        if (cc.program == prog || cc.previewProgram == prog) {
            return cc.vTime;
        }
    }
    for (auto& cc : compiledBankEffects) {
        if (cc.program == prog || cc.previewProgram == prog) {
            return cc.vTime;
        }
    }
    return defaultVTime;
}

void PatchProgram::getSpeedRange(unsigned int prog, float& minS, float& maxS) {
    if (prog == glslProgram || prog == glslPreviewProgram) {
        minS = defaultMinSpeed;
        maxS = defaultMaxSpeed;
        return;
    }
    for (const auto& cc : compiledCues) {
        if (cc.program == prog || cc.previewProgram == prog) {
            minS = cc.minSpeed;
            maxS = cc.maxSpeed;
            return;
        }
    }
    for (const auto& cc : compiledBankEffects) {
        if (cc.program == prog || cc.previewProgram == prog) {
            minS = cc.minSpeed;
            maxS = cc.maxSpeed;
            return;
        }
    }
    minS = 0.0f;
    maxS = 1.0f;
}


void randomizeTransitionDirections(PatchProgram* program) {
    if (!program) return;
    static thread_local std::mt19937 rng(std::random_device{}());
    
    float pi = 3.14159265358979323846f;
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * pi);
    float angle2D = angleDist(rng);
    program->sweepDirection = glm::vec2(std::cos(angle2D), std::sin(angle2D));
    
    std::uniform_real_distribution<float> zDist(-1.0f, 1.0f);
    float z = zDist(rng);
    float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    float angle3D = angleDist(rng);
    program->sweepDirection3D = glm::vec3(r * std::cos(angle3D), r * std::sin(angle3D), z);
    
    float xMin = program->pixelPosMin.x;
    float xMax = program->pixelPosMax.x;
    if (xMin > xMax) std::swap(xMin, xMax);
    if (xMax - xMin < 0.001f) { xMin -= 0.001f; xMax += 0.001f; }
    
    float yMin = program->pixelPosMin.y;
    float yMax = program->pixelPosMax.y;
    if (yMin > yMax) std::swap(yMin, yMax);
    if (yMax - yMin < 0.001f) { yMin -= 0.001f; yMax += 0.001f; }
    
    float zMin = program->pixelPosMin.z;
    float zMax = program->pixelPosMax.z;
    if (zMin > zMax) std::swap(zMin, zMax);
    if (zMax - zMin < 0.001f) { zMin -= 0.001f; zMax += 0.001f; }
    
    std::uniform_real_distribution<float> xDist(xMin, xMax);
    std::uniform_real_distribution<float> yDist(yMin, yMax);
    std::uniform_real_distribution<float> zPosDist(zMin, zMax);
    
    program->sphereCenter3D = glm::vec3(xDist(rng), yDist(rng), zPosDist(rng));
    std::uniform_int_distribution<int> coinDist(0, 1);
    program->circleWipeInward = (coinDist(rng) == 1);
}

void render(PatchProgram* program){
    float curTime = (float)glfwGetTime();
    float dt = (program->lastFrameTime > 0.0f) ? (curTime - program->lastFrameTime) : (1.0f / program->refreshRate);
    dt = std::min(dt, 0.1f);
    program->lastFrameTime = curTime;

    program->timeElapsed += dt;

    float velocity = 1.0f;
    if (program->generativeRuntime && program->generativeSettings.masterEnabled) {
        std::lock_guard<std::mutex> lock(program->generativeMutex);
        if (program->editorPreviewOverrideActive) {
            velocity = program->editorPreviewOverrideUbo.velocity;
        } else {
            velocity = program->generativeRuntime->getUboState().velocity;
        }
    }

    // Accumulate velocity-scaled integrated timelines (vTime)
    // 1. Default patch shader program
    float defaultSpeed = program->defaultMinSpeed + velocity * (program->defaultMaxSpeed - program->defaultMinSpeed);
    program->defaultVTime += dt * defaultSpeed;

    // 2. Compiled cues
    for (auto& cc : program->compiledCues) {
        float speed = cc.minSpeed + velocity * (cc.maxSpeed - cc.minSpeed);
        cc.vTime += dt * speed;
    }

    // 3. Compiled bank effects
    for (auto& cc : program->compiledBankEffects) {
        float speed = cc.minSpeed + velocity * (cc.maxSpeed - cc.minSpeed);
        cc.vTime += dt * speed;
    }


    if (program->generativeRuntime) {
        program->generativeRuntime->update(dt, program);
    }


    if (program->generativeSettings.masterEnabled && program->glslUboId != 0 && program->generativeRuntime) {
        glBindBuffer(GL_UNIFORM_BUFFER, program->glslUboId);
        Generative::EngineStateUBO uboState;
        {
            std::lock_guard<std::mutex> lock(program->generativeMutex);
            uboState = program->generativeRuntime->getUboState();
        }
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Generative::EngineStateUBO), &uboState);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }


    // ── Cue Switch Detection & Crossfade Triggering ──
    if (program->vfbPixels && program->vfbPixelsOld) {
        int targetCue = program->activeCueIndex.load();
        if (program->currentRenderedCueIndex != targetCue) {
            float fade = program->pendingCrossfadeDuration.load();
            program->pendingCrossfadeDuration.store(0.0f); // Consume
            if (fade > 0.0f && program->currentRenderedCueIndex != -2) {
                std::memcpy(program->vfbPixelsOld, program->vfbPixels,
                            program->vfbWidth * sizeof(ColorRGBW));
                program->previousCueIndex = program->currentRenderedCueIndex;
                program->crossfadeDuration.store(fade);
                program->crossfadeProgress.store(0.0f);
                randomizeTransitionDirections(program);
            } else {
                program->previousCueIndex = -2;
                program->crossfadeProgress.store(1.0f);
            }
            program->currentRenderedCueIndex = targetCue;
        }
    }

    // 1. Populate Virtual Framebuffer (vfbPixels)
    if (program->renderMode == Patch::RenderMode::CPP) {
        for (uint32_t i = 0; i < program->pixelCount; i++) {
            const auto& pos = program->pixelPositions[i];
            float dist = std::sqrt(pos.x*pos.x + pos.y*pos.y + pos.z*pos.z);
            float br = std::sin((dist - program->timeElapsed * 20.0f) / 10.0f);
            uint8_t out = br > 0 ? (uint8_t)(br * 255.0f) : 0;
            program->vfbPixels[i] = {out, out, out, out};
        }
    } else if (program->renderMode == Patch::RenderMode::LUA) {
        if (program->luaUpdateFn.valid()) {
            Canvas canvas(program->vfbWidth, program->vfbHeight, program->vfbPixels);
            try {
                sol::protected_function_result result = program->luaUpdateFn(canvas, (double)program->timeElapsed);
                if (!result.valid()) {
                    sol::error err = result;
                    program->compilerLog = std::string("Lua Runtime Error:\n") + err.what();
                }
            } catch (const std::exception& e) {
                program->compilerLog = std::string("Lua Exception:\n") + e.what();
            }
        }
    } else if (program->renderMode == Patch::RenderMode::GLSL) {
        if (program->glslFbo && program->shaderCompiled) {
            unsigned int activeProg = program->glslProgram;
            int actIdx = program->activeCueIndex.load();
            if (actIdx >= 0 && actIdx < (int)program->compiledCues.size()) {
                if (program->compiledCues[actIdx].program != 0) {
                    activeProg = program->compiledCues[actIdx].program;
                }
            }

            auto setupProgramUniforms = [&](unsigned int prog) {
                if (prog == 0) return;
                glUseProgram(prog);

                // Bind EngineState UBO to binding point 0
                if (program->glslUboId != 0) {
                    GLuint blockIndex = glGetUniformBlockIndex(prog, "EngineState");
                    if (blockIndex != GL_INVALID_INDEX) {
                        glUniformBlockBinding(prog, blockIndex, 0);
                        glBindBufferBase(GL_UNIFORM_BUFFER, 0, program->glslUboId);
                    }
                }

                GLint timeLoc = glGetUniformLocation(prog, "iTime");
                if (timeLoc >= 0) glUniform1f(timeLoc, program->timeElapsed);
                GLint timeLocLegacy = glGetUniformLocation(prog, "time");
                if (timeLocLegacy >= 0) glUniform1f(timeLocLegacy, program->timeElapsed);
                GLint vTimeLoc = glGetUniformLocation(prog, "vTime");
                if (vTimeLoc >= 0) glUniform1f(vTimeLoc, program->getVTimeRef(prog));


                GLint resLoc = glGetUniformLocation(prog, "iResolution");
                if (resLoc >= 0) glUniform3f(resLoc, 256.0f, 256.0f, 1.0f);
                GLint resLocLegacy = glGetUniformLocation(prog, "resolution");
                if (resLocLegacy >= 0) glUniform2f(resLocLegacy, 256.0f, 256.0f);

                GLint pCountLoc = glGetUniformLocation(prog, "pixelCount");
                if (pCountLoc >= 0) glUniform1f(pCountLoc, (float)program->pixelCount);

                GLint pMinLoc = glGetUniformLocation(prog, "pixelPosMin");
                if (pMinLoc >= 0) glUniform3f(pMinLoc, program->pixelPosMin.x, program->pixelPosMin.y, program->pixelPosMin.z);
                GLint pMaxLoc = glGetUniformLocation(prog, "pixelPosMax");
                if (pMaxLoc >= 0) glUniform3f(pMaxLoc, program->pixelPosMax.x, program->pixelPosMax.y, program->pixelPosMax.z);

                GLint sliceLoc = glGetUniformLocation(prog, "zSlice");
                if (sliceLoc >= 0) glUniform1f(sliceLoc, program->zSlice.load());

                if (program->glslNoiseTex != 0) {
                    for (int chan = 0; chan < 4; ++chan) {
                        std::string chanName = "iChannel" + std::to_string(chan);
                        GLint chanLoc = glGetUniformLocation(prog, chanName.c_str());
                        if (chanLoc >= 0) {
                            glActiveTexture(GL_TEXTURE0 + chan);
                            glBindTexture(GL_TEXTURE_2D, program->glslNoiseTex);
                            glUniform1i(chanLoc, chan);
                        }
                    }
                }
            };

            auto cleanupProgramTextures = [&]() {
                if (program->glslNoiseTex != 0) {
                    for (int chan = 3; chan >= 0; --chan) {
                        glActiveTexture(GL_TEXTURE0 + chan);
                        glBindTexture(GL_TEXTURE_2D, 0);
                    }
                    glActiveTexture(GL_TEXTURE0);
                }
            };

            // ── Lazy create VAOs on RT thread (not shared between GL contexts) ──
            if (!program->vaoReady) {
                // Point-Rendering VAO
                glGenVertexArrays(1, &program->glslPointVao);
                glBindVertexArray(program->glslPointVao);
                glBindBuffer(GL_ARRAY_BUFFER, program->glslPointVbo);
                
                // Stride is 6 floats: position3D (3), texCoord2D (2), pixelIndex (1)
                GLsizei stride = 6 * sizeof(float);
                glEnableVertexAttribArray(0); // position3D
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
                
                glEnableVertexAttribArray(1); // texCoord2D
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
                
                glEnableVertexAttribArray(2); // pixelIndex
                glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
                
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindVertexArray(0);

                // Quad VAO (for editor previews and blend passes)
                glGenVertexArrays(1, &program->glslQuadVao);
                glBindVertexArray(program->glslQuadVao);
                glBindBuffer(GL_ARRAY_BUFFER, program->glslQuadVbo);
                glEnableVertexAttribArray(0); // position
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindVertexArray(0);

                program->vaoReady = true;
            }

            // Determine if Generative Engine is active
            bool isGen = program->generativeSettings.masterEnabled && program->generativeRuntime;
            int activeCue = program->activeCueIndex.load();
            bool isCueActive = (activeCue >= 0);

            unsigned int outgoingProg = 0;
            unsigned int incomingProg = activeProg;
            float progressFactor = 1.0f;
            int currentTransType = 0;

            unsigned int outgoingProgPreview = 0;
            unsigned int incomingProgPreview = program->glslPreviewProgram;

            if (isGen && !isCueActive) {
                float genProgress = 0.0f;
                int genActIdx = -1;
                int genTgtIdx = -1;
                {
                    std::lock_guard<std::mutex> lock(program->generativeMutex);
                    genProgress = program->generativeRuntime->getShaderFadeProgress();
                    genActIdx = program->generativeRuntime->getActiveShaderIndex();
                    genTgtIdx = program->generativeRuntime->getTargetShaderIndex();
                    currentTransType = program->generativeRuntime->getShaderTransitionType();
                }

                if (genProgress < 1.0f && genTgtIdx >= 0) {
                    if (genActIdx >= 0 && genActIdx < (int)program->compiledCues.size()) {
                        outgoingProg = program->compiledCues[genActIdx].program;
                        outgoingProgPreview = program->compiledCues[genActIdx].previewProgram;
                    } else {
                        outgoingProg = program->glslProgram;
                        outgoingProgPreview = program->glslPreviewProgram;
                    }
                    if (genTgtIdx >= 0 && genTgtIdx < (int)program->compiledCues.size()) {
                        incomingProg = program->compiledCues[genTgtIdx].program;
                        incomingProgPreview = program->compiledCues[genTgtIdx].previewProgram;
                    } else {
                        incomingProg = program->glslProgram;
                        incomingProgPreview = program->glslPreviewProgram;
                    }
                    progressFactor = genProgress;
                } else {

                    if (genActIdx >= 0 && genActIdx < (int)program->compiledCues.size()) {
                        incomingProg = program->compiledCues[genActIdx].program;
                        incomingProgPreview = program->compiledCues[genActIdx].previewProgram;
                    } else {
                        incomingProg = program->glslProgram;
                        incomingProgPreview = program->glslPreviewProgram;
                    }
                    progressFactor = 1.0f;
                }
            } else {
                float cueProgress = program->crossfadeProgress.load();
                if (cueProgress < 1.0f) {
                    unsigned int oldProg = program->glslProgram;
                    unsigned int oldProgPreview = program->glslPreviewProgram;
                    int oldIdx = program->previousCueIndex;
                    if (oldIdx >= 0 && oldIdx < (int)program->compiledCues.size()) {
                        if (program->compiledCues[oldIdx].program != 0) {
                            oldProg = program->compiledCues[oldIdx].program;
                            oldProgPreview = program->compiledCues[oldIdx].previewProgram;
                        }
                    }
                    outgoingProg = oldProg;
                    outgoingProgPreview = oldProgPreview;
                    incomingProg = activeProg;
                    
                    int cueActIdx = program->activeCueIndex.load();
                    if (cueActIdx >= 0 && cueActIdx < (int)program->compiledCues.size()) {
                        if (program->compiledCues[cueActIdx].previewProgram != 0) {
                            incomingProgPreview = program->compiledCues[cueActIdx].previewProgram;
                        }
                    }
                    progressFactor = cueProgress;
                } else {
                    incomingProg = activeProg;
                    int cueActIdx = program->activeCueIndex.load();
                    if (cueActIdx >= 0 && cueActIdx < (int)program->compiledCues.size()) {
                        if (program->compiledCues[cueActIdx].previewProgram != 0) {
                            incomingProgPreview = program->compiledCues[cueActIdx].previewProgram;
                        }
                    }
                    progressFactor = 1.0f;
                }
                currentTransType = 0;
            }

            // ── Render Outgoing to glslFboOld (during crossfade) ──
            if (progressFactor < 1.0f && program->glslFboOld && outgoingProg != 0) {
                glBindFramebuffer(GL_FRAMEBUFFER, program->glslFboOld);
                glViewport(0, 0, program->vfbWidth, 1);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                setupProgramUniforms(outgoingProg);
                glBindVertexArray(program->glslPointVao);
                glDrawArrays(GL_POINTS, 0, program->pixelCount);
                glBindVertexArray(0);
                cleanupProgramTextures();
                glUseProgram(0);
            }

            // ── Render Incoming to glslFbo ──
            glBindFramebuffer(GL_FRAMEBUFFER, program->glslFbo);
            glViewport(0, 0, program->vfbWidth, 1);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            if (incomingProg != 0) {
                setupProgramUniforms(incomingProg);
                glBindVertexArray(program->glslPointVao);
                glDrawArrays(GL_POINTS, 0, program->pixelCount);
                glBindVertexArray(0);
                cleanupProgramTextures();
                glUseProgram(0);
            }

            // ── Blend Outgoing and Incoming in glslFboBlend ──
            if (progressFactor < 1.0f && program->glslFboBlend && program->glslBlendProgram) {
                glBindFramebuffer(GL_FRAMEBUFFER, program->glslFboBlend);
                glViewport(0, 0, program->vfbWidth, 1);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                glUseProgram(program->glslBlendProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, program->glslFboTex);
                GLint activeTexLoc = glGetUniformLocation(program->glslBlendProgram, "texActive");
                if (activeTexLoc >= 0) glUniform1i(activeTexLoc, 0);

                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, program->glslFboTexOld);
                GLint oldTexLoc = glGetUniformLocation(program->glslBlendProgram, "texOld");
                if (oldTexLoc >= 0) glUniform1i(oldTexLoc, 1);

                if (currentTransType == 1 && program->glslNoiseTex != 0) {
                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, program->glslNoiseTex);
                    GLint noiseTexLoc = glGetUniformLocation(program->glslBlendProgram, "texNoise");
                    if (noiseTexLoc >= 0) glUniform1i(noiseTexLoc, 2);
                }

                GLint volLoc = glGetUniformLocation(program->glslBlendProgram, "transitionVolumetric");
                if (volLoc >= 0) glUniform1i(volLoc, program->generativeSettings.transitionVolumetric ? 1 : 0);

                GLint circleWipeInwardLoc = glGetUniformLocation(program->glslBlendProgram, "circleWipeInward");
                if (circleWipeInwardLoc >= 0) glUniform1i(circleWipeInwardLoc, program->circleWipeInward ? 1 : 0);

                GLint isPrevLoc = glGetUniformLocation(program->glslBlendProgram, "isPreview2D");
                if (isPrevLoc >= 0) glUniform1i(isPrevLoc, 0);

                GLint pMinLoc = glGetUniformLocation(program->glslBlendProgram, "pixelPosMin");
                if (pMinLoc >= 0) glUniform3f(pMinLoc, program->pixelPosMin.x, program->pixelPosMin.y, program->pixelPosMin.z);

                GLint pMaxLoc = glGetUniformLocation(program->glslBlendProgram, "pixelPosMax");
                if (pMaxLoc >= 0) glUniform3f(pMaxLoc, program->pixelPosMax.x, program->pixelPosMax.y, program->pixelPosMax.z);

                GLint zSliceLoc = glGetUniformLocation(program->glslBlendProgram, "zSlice");
                if (zSliceLoc >= 0) glUniform1f(zSliceLoc, program->zSlice.load());

                GLint sweepDirLoc = glGetUniformLocation(program->glslBlendProgram, "sweepDirection");
                if (sweepDirLoc >= 0) glUniform2f(sweepDirLoc, program->sweepDirection.x, program->sweepDirection.y);

                GLint sweepDir3DLoc = glGetUniformLocation(program->glslBlendProgram, "sweepDirection3D");
                if (sweepDir3DLoc >= 0) glUniform3f(sweepDir3DLoc, program->sweepDirection3D.x, program->sweepDirection3D.y, program->sweepDirection3D.z);

                GLint sphereCenter3DLoc = glGetUniformLocation(program->glslBlendProgram, "sphereCenter3D");
                if (sphereCenter3DLoc >= 0) glUniform3f(sphereCenter3DLoc, program->sphereCenter3D.x, program->sphereCenter3D.y, program->sphereCenter3D.z);

                if (program->glslPositionTex != 0) {
                    glActiveTexture(GL_TEXTURE3);
                    glBindTexture(GL_TEXTURE_2D, program->glslPositionTex);
                    GLint posTexLoc = glGetUniformLocation(program->glslBlendProgram, "texPosition");
                    if (posTexLoc >= 0) glUniform1i(posTexLoc, 3);
                }

                GLint mixLoc = glGetUniformLocation(program->glslBlendProgram, "mixFactor");
                if (mixLoc >= 0) glUniform1f(mixLoc, progressFactor);

                GLint typeLoc = glGetUniformLocation(program->glslBlendProgram, "transitionType");
                if (typeLoc >= 0) glUniform1i(typeLoc, currentTransType);

                glBindVertexArray(program->glslQuadVao);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);
                glUseProgram(0);

                if (program->glslPositionTex != 0) {
                    glActiveTexture(GL_TEXTURE3);
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
                if (currentTransType == 1 && program->glslNoiseTex != 0) {
                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            // ── Readback FBO to CPU vfbPixels (Async via PBO) ──
            GLuint readFbo = program->glslFbo;
            if (progressFactor < 1.0f && program->glslFboBlend) {
                readFbo = program->glslFboBlend;
            }

            int writeIdx = program->pboFrameIndex & 1;
            int readIdx  = 1 - writeIdx;

            glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, program->glslPbo[writeIdx]);
            glReadPixels(0, 0, program->vfbWidth, 1, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

            glBindBuffer(GL_PIXEL_PACK_BUFFER, program->glslPbo[readIdx]);
            void* ptr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            if (ptr) {
                std::memcpy(program->vfbPixels, ptr, program->vfbWidth * sizeof(ColorRGBW));
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            program->pboFrameIndex++;

            // ── Draw 2D Offline Preview into glslEditorFbo ──
            if (program->glslEditorFbo) {
                unsigned int editProgPreview = program->glslPreviewProgram;
                int editIdx = program->editingCueIndex.load();
                int editBankIdx = program->editingBankIndex.load();

                if (editIdx >= 0 && editIdx < (int)program->compiledCues.size()) {
                    if (program->compiledCues[editIdx].previewProgram != 0) {
                        editProgPreview = program->compiledCues[editIdx].previewProgram;
                    }
                } else if (editIdx == -2 && editBankIdx >= 0 && editBankIdx < (int)program->compiledBankEffects.size()) {
                    if (program->compiledBankEffects[editBankIdx].previewProgram != 0) {
                        editProgPreview = program->compiledBankEffects[editBankIdx].previewProgram;
                    }
                }

                glBindFramebuffer(GL_FRAMEBUFFER, program->glslEditorFbo);
                glViewport(0, 0, program->previewWidth, program->previewHeight);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                if (editProgPreview != 0) {
                    bool overrideActive = false;
                    {
                        std::lock_guard<std::mutex> lock(program->generativeMutex);
                        overrideActive = program->editorPreviewOverrideActive;
                    }
                    if (overrideActive && program->glslUboId != 0) {
                        glBindBuffer(GL_UNIFORM_BUFFER, program->glslUboId);
                        std::lock_guard<std::mutex> lock(program->generativeMutex);
                        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Generative::EngineStateUBO), &program->editorPreviewOverrideUbo);
                        glBindBuffer(GL_UNIFORM_BUFFER, 0);
                    }

                    setupProgramUniforms(editProgPreview);
                    glBindVertexArray(program->glslQuadVao);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glBindVertexArray(0);
                    cleanupProgramTextures();
                    glUseProgram(0);

                    // Restore normal UBO state
                    if (overrideActive && program->glslUboId != 0 && program->generativeRuntime) {
                        glBindBuffer(GL_UNIFORM_BUFFER, program->glslUboId);
                        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Generative::EngineStateUBO), &program->generativeRuntime->getUboState());
                        glBindBuffer(GL_UNIFORM_BUFFER, 0);
                    }
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            // ── Draw 2D Playback Preview into glslPlaybackPreviewFbo ──
            if (program->glslPlaybackPreviewFbo && program->showPlaybackPreview.load()) {
                // 1. Render Outgoing 2D Preview to glslPlaybackPreviewFboOld (if crossfading)
                if (progressFactor < 1.0f && program->glslPlaybackPreviewFboOld && outgoingProgPreview != 0) {
                    glBindFramebuffer(GL_FRAMEBUFFER, program->glslPlaybackPreviewFboOld);
                    glViewport(0, 0, program->previewWidth, program->previewHeight);
                    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);

                    setupProgramUniforms(outgoingProgPreview);
                    glBindVertexArray(program->glslQuadVao);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glBindVertexArray(0);
                    cleanupProgramTextures();
                    glUseProgram(0);
                }

                // 2. Render Incoming 2D Preview to glslPlaybackPreviewFbo
                glBindFramebuffer(GL_FRAMEBUFFER, program->glslPlaybackPreviewFbo);
                glViewport(0, 0, program->previewWidth, program->previewHeight);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                if (incomingProgPreview != 0) {
                    setupProgramUniforms(incomingProgPreview);
                    glBindVertexArray(program->glslQuadVao);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glBindVertexArray(0);
                    cleanupProgramTextures();
                    glUseProgram(0);
                }

                // 3. Blend Active and Outgoing Previews into glslPlaybackPreviewFboBlend (if crossfading)
                if (progressFactor < 1.0f && program->glslPlaybackPreviewFboBlend && program->glslBlendProgram) {
                    glBindFramebuffer(GL_FRAMEBUFFER, program->glslPlaybackPreviewFboBlend);
                    glViewport(0, 0, program->previewWidth, program->previewHeight);
                    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);

                    glUseProgram(program->glslBlendProgram);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, program->glslPlaybackPreviewFboTex);
                    GLint activeTexLoc = glGetUniformLocation(program->glslBlendProgram, "texActive");
                    if (activeTexLoc >= 0) glUniform1i(activeTexLoc, 0);

                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, program->glslPlaybackPreviewFboTexOld);
                    GLint oldTexLoc = glGetUniformLocation(program->glslBlendProgram, "texOld");
                    if (oldTexLoc >= 0) glUniform1i(oldTexLoc, 1);

                    if (currentTransType == 1 && program->glslNoiseTex != 0) {
                        glActiveTexture(GL_TEXTURE2);
                        glBindTexture(GL_TEXTURE_2D, program->glslNoiseTex);
                        GLint noiseTexLoc = glGetUniformLocation(program->glslBlendProgram, "texNoise");
                        if (noiseTexLoc >= 0) glUniform1i(noiseTexLoc, 2);
                    }

                    GLint volLoc = glGetUniformLocation(program->glslBlendProgram, "transitionVolumetric");
                    if (volLoc >= 0) glUniform1i(volLoc, program->generativeSettings.transitionVolumetric ? 1 : 0);

                    GLint circleWipeInwardLoc = glGetUniformLocation(program->glslBlendProgram, "circleWipeInward");
                    if (circleWipeInwardLoc >= 0) glUniform1i(circleWipeInwardLoc, program->circleWipeInward ? 1 : 0);

                    GLint isPrevLoc = glGetUniformLocation(program->glslBlendProgram, "isPreview2D");
                    if (isPrevLoc >= 0) glUniform1i(isPrevLoc, 1);

                    GLint pMinLoc = glGetUniformLocation(program->glslBlendProgram, "pixelPosMin");
                    if (pMinLoc >= 0) glUniform3f(pMinLoc, program->pixelPosMin.x, program->pixelPosMin.y, program->pixelPosMin.z);

                    GLint pMaxLoc = glGetUniformLocation(program->glslBlendProgram, "pixelPosMax");
                    if (pMaxLoc >= 0) glUniform3f(pMaxLoc, program->pixelPosMax.x, program->pixelPosMax.y, program->pixelPosMax.z);

                    GLint zSliceLoc = glGetUniformLocation(program->glslBlendProgram, "zSlice");
                    if (zSliceLoc >= 0) glUniform1f(zSliceLoc, program->zSlice.load());

                    GLint sweepDirLoc = glGetUniformLocation(program->glslBlendProgram, "sweepDirection");
                    if (sweepDirLoc >= 0) glUniform2f(sweepDirLoc, program->sweepDirection.x, program->sweepDirection.y);

                    GLint sweepDir3DLoc = glGetUniformLocation(program->glslBlendProgram, "sweepDirection3D");
                    if (sweepDir3DLoc >= 0) glUniform3f(sweepDir3DLoc, program->sweepDirection3D.x, program->sweepDirection3D.y, program->sweepDirection3D.z);

                    GLint sphereCenter3DLoc = glGetUniformLocation(program->glslBlendProgram, "sphereCenter3D");
                    if (sphereCenter3DLoc >= 0) glUniform3f(sphereCenter3DLoc, program->sphereCenter3D.x, program->sphereCenter3D.y, program->sphereCenter3D.z);

                    if (program->glslPositionTex != 0) {
                        glActiveTexture(GL_TEXTURE3);
                        glBindTexture(GL_TEXTURE_2D, program->glslPositionTex);
                        GLint posTexLoc = glGetUniformLocation(program->glslBlendProgram, "texPosition");
                        if (posTexLoc >= 0) glUniform1i(posTexLoc, 3);
                    }

                    GLint mixLoc = glGetUniformLocation(program->glslBlendProgram, "mixFactor");
                    if (mixLoc >= 0) glUniform1f(mixLoc, progressFactor);

                    GLint typeLoc = glGetUniformLocation(program->glslBlendProgram, "transitionType");
                    if (typeLoc >= 0) glUniform1i(typeLoc, currentTransType);

                    glBindVertexArray(program->glslQuadVao);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glBindVertexArray(0);
                    glUseProgram(0);

                    if (program->glslPositionTex != 0) {
                        glActiveTexture(GL_TEXTURE3);
                        glBindTexture(GL_TEXTURE_2D, 0);
                    }
                    if (currentTransType == 1 && program->glslNoiseTex != 0) {
                        glActiveTexture(GL_TEXTURE2);
                        glBindTexture(GL_TEXTURE_2D, 0);
                    }
                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
                GLuint sourceFbo = program->glslPlaybackPreviewFbo;
                if (progressFactor < 1.0f && program->glslPlaybackPreviewFboBlend) {
                    sourceFbo = program->glslPlaybackPreviewFboBlend;
                }

                int readIdx = program->glslPlaybackPreviewReadIdx.load();
                int writeIdx = 1 - readIdx;

                if (sourceFbo != 0 && program->glslPlaybackPreviewDisplayFbo[writeIdx] != 0) {
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, program->glslPlaybackPreviewDisplayFbo[writeIdx]);
                    glBlitFramebuffer(0, 0, program->previewWidth, program->previewHeight,
                                      0, 0, program->previewWidth, program->previewHeight,
                                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    
                    program->glslPlaybackPreviewReadIdx.store(writeIdx);
                    program->glslCurrentPlaybackPreviewTexID.store(program->glslPlaybackPreviewDisplayTex[writeIdx]);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);

            }
        }
    }


    // ── CPU Blended Crossfade (For CPP/LUA modes) ──
    float progressBlend = program->crossfadeProgress.load();
    if (program->renderMode != Patch::RenderMode::GLSL && progressBlend < 1.0f && program->vfbPixelsOld) {
        float t = progressBlend;
        float it = 1.0f - t;
        int n = program->vfbWidth;
        for (int i = 0; i < n; i++) {
            ColorRGBW& n_ = program->vfbPixels[i];
            const ColorRGBW& o = program->vfbPixelsOld[i];
            n_.r = (uint8_t)(it * o.r + t * n_.r + 0.5f);
            n_.g = (uint8_t)(it * o.g + t * n_.g + 0.5f);
            n_.b = (uint8_t)(it * o.b + t * n_.b + 0.5f);
            n_.w = (uint8_t)(it * o.w + t * n_.w + 0.5f);
        }
    }

    // 2. Direct mapping: copy vfbPixels to pixelColorsTemp (no interpolation needed!)
    if (program->pixelCount > 0 && program->vfbPixels && program->pixelColorsTemp) {
        std::memcpy(program->pixelColorsTemp, program->vfbPixels, program->pixelCount * sizeof(ColorRGBW));

        // 3. Highlight/Flash selected pixels for "Find" feature
        if (program->pixelSelected) {
            float period = 1.0f / (program->highlightFrequency > 0.0f ? program->highlightFrequency : 1.0f);
            float phase = fmodf(program->timeElapsed, period);
            bool flashOn = (phase < period * 0.5f);
            if (flashOn) {
                for (uint32_t i = 0; i < program->pixelCount; ++i) {
                    if (program->pixelSelected[i].load()) {
                        program->pixelColorsTemp[i] = {255, 255, 255, 255};
                    }
                }
            }
        }

        // 4. RGB→RGBW white channel conversion
        if (program->whiteMode == Patch::WhiteMode::AUTO) {
            for (uint32_t i = 0; i < program->pixelCount; ++i) {
                ColorRGBW& c = program->pixelColorsTemp[i];
                uint8_t w = std::min({c.r, c.g, c.b});
                c.r -= w;
                c.g -= w;
                c.b -= w;
                c.w = w;
            }
        } else if (program->whiteMode == Patch::WhiteMode::OFF) {
            for (uint32_t i = 0; i < program->pixelCount; ++i) {
                program->pixelColorsTemp[i].w = 0;
            }
        }
        
        // Final copy: publish processed colors to pixelColors atomically/safely
        std::memcpy(program->pixelColors, program->pixelColorsTemp, program->pixelCount * sizeof(ColorRGBW));
    }
}

void encode(PatchProgram* program) {
    for (int i = 0; i < program->p2uCount; i++) {
        const PatchProgram::Pix2UniCopyInstr& map = program->p2us[i];
        uint8_t* dest = program->universes[map.universeIndex].buffer + map.universeOffset;
        int bytesWritten = 0;
        
        int p = map.pixelIndex;
        int pByte = map.pixelStartByte;
        
        while (bytesWritten < map.byteCount) {
            const uint8_t* src = reinterpret_cast<const uint8_t*>(&program->pixelColors[p]);
            int availableInPixel = map.bytesPerPixel - pByte;
            int remainingInMap = map.byteCount - bytesWritten;
            int toCopy = std::min(availableInPixel, remainingInMap);
            
            std::memcpy(dest + bytesWritten, src + pByte, toCopy);
            
            bytesWritten += toCopy;
            p++;
            pByte = 0;
        }
    }
}

float PatchProgram::getShaderCrossfadeProgress() const {
    bool isGen = generativeSettings.masterEnabled && generativeRuntime && (activeCueIndex.load() < 0);
    if (isGen) {
        std::lock_guard<std::mutex> lock(generativeMutex);
        int targetIdx = generativeRuntime->getTargetShaderIndex();
        if (targetIdx >= 0) {
            return generativeRuntime->getShaderFadeProgress();
        }
        return 1.0f;
    }
    return crossfadeProgress.load();
}

} // namespace PixelMapper
