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

        auto newPatch = world.entity()
            .add<Patch::Is>()
            .set<Patch::Settings>({})
            .set<Patch::ScriptData>({})
            .add<Patch::RenderArea>()
            .set<Patch::GPUResources>({})
            .set<Patch::GPUProgram>({})
            .child_of(patchFolder);

        newPatch.set_name(patchName.c_str());

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

        newPatch.add<CueList::CueFolder>(cueListFolder);
        newPatch.add<EffectBank::EffectFolder>(effectBankFolder);

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
            if (res.glslVao) glDeleteVertexArrays(1, &res.glslVao);
            if (res.glslVbo) glDeleteBuffers(1, &res.glslVbo);
            if (res.glslPointVao) glDeleteVertexArrays(1, &res.glslPointVao);
            if (res.glslPointVbo) glDeleteBuffers(1, &res.glslPointVbo);
            if (res.glslQuadVao) glDeleteVertexArrays(1, &res.glslQuadVao);
            if (res.glslQuadVbo) glDeleteBuffers(1, &res.glslQuadVbo);
            if (res.glslPbo[0]) glDeleteBuffers(2, res.glslPbo);
            if (res.glslNoiseTex) glDeleteTextures(1, &res.glslNoiseTex);
        });

        w.observer<GPUProgram>("CleanGPUProgram").event(flecs::OnRemove)
        .each([](flecs::entity e, GPUProgram& gp) {
            if (gp.program) glDeleteProgram(gp.program);
            if (gp.previewProgram) glDeleteProgram(gp.previewProgram);
        });
    }

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

    bool compileShaderIfNeeded(const std::string& fsSourceStr, Patch::GPUProgram& gp) {
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

        // ─────────────────────────────────────────────────────────────────
        // 1. COMPILE MAIN (POINT-RENDERING) PROGRAM
        // ─────────────────────────────────────────────────────────────────
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

        std::string pointFsSource;
        if (isShadertoy) {
            pointFsSource =
                "#version 150\n"
                "in vec3 vPixelPos3D;\n"
                "in vec2 vPixelPos2D;\n"
                "out vec4 FragColor;\n"
                "#define iPixelPos3D vPixelPos3D\n"
                "#define iPixelPos2D vPixelPos2D\n"
                "uniform vec3      iResolution;\n"
                "uniform float     iTime;\n"
                "uniform float     iTimeDelta;\n"
                "uniform float     iFrameRate;\n"
                "uniform int       iFrame;\n"
                "uniform vec4      iMouse;\n"
                "uniform vec4      iDate;\n"
                "uniform sampler2D iChannel0;\n"
                "uniform sampler2D iChannel1;\n"
                "uniform sampler2D iChannel2;\n"
                "uniform sampler2D iChannel3;\n"
                "vec4 texture(sampler2D sampler, vec3 coord) { return texture(sampler, coord.xy); }\n"
                "vec4 textureLod(sampler2D sampler, vec3 coord, float lod) { return textureLod(sampler, coord.xy, lod); }\n" +
                fsSourceStr +
                "\n"
                "void main() {\n"
                "    vec2 fakeFragCoord = iPixelPos2D * iResolution.xy;\n"
                "    mainImage(FragColor, fakeFragCoord);\n"
                "}\n";
        } else {
            // Check if #version is already in user code
            if (fsSourceStr.find("#version") != std::string::npos) {
                pointFsSource = fsSourceStr;
            } else {
                pointFsSource =
                    "#version 150\n"
                    "in vec3 vPixelPos3D;\n"
                    "in vec2 vPixelPos2D;\n"
                    "out vec4 fragColor;\n"
                    "#define iPixelPos3D vPixelPos3D\n"
                    "#define iPixelPos2D vPixelPos2D\n"
                    "uniform float time;\n"
                    "uniform vec2 resolution;\n" +
                    fsSourceStr;
            }
        }

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

        // ─────────────────────────────────────────────────────────────────
        // 2. COMPILE PREVIEW (2D QUAD-RENDERING) PROGRAM
        // ─────────────────────────────────────────────────────────────────
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

        std::string quadFsSource;
        if (isShadertoy) {
            quadFsSource =
                "#version 150\n"
                "in vec3 vPixelPos3D;\n"
                "in vec2 vPixelPos2D;\n"
                "out vec4 FragColor;\n"
                "#define iPixelPos3D vPixelPos3D\n"
                "#define iPixelPos2D vPixelPos2D\n"
                "uniform vec3      iResolution;\n"
                "uniform float     iTime;\n"
                "uniform float     iTimeDelta;\n"
                "uniform float     iFrameRate;\n"
                "uniform int       iFrame;\n"
                "uniform vec4      iMouse;\n"
                "uniform vec4      iDate;\n"
                "uniform float     zSlice;\n"
                "uniform sampler2D iChannel0;\n"
                "uniform sampler2D iChannel1;\n"
                "uniform sampler2D iChannel2;\n"
                "uniform sampler2D iChannel3;\n"
                "vec4 texture(sampler2D sampler, vec3 coord) { return texture(sampler, coord.xy); }\n"
                "vec4 textureLod(sampler2D sampler, vec3 coord, float lod) { return textureLod(sampler, coord.xy, lod); }\n" +
                fsSourceStr +
                "\n"
                "void main() {\n"
                "    mainImage(FragColor, gl_FragCoord.xy);\n"
                "}\n";
        } else {
            if (fsSourceStr.find("#version") != std::string::npos) {
                quadFsSource = fsSourceStr;
            } else {
                quadFsSource =
                    "#version 150\n"
                    "in vec3 vPixelPos3D;\n"
                    "in vec2 vPixelPos2D;\n"
                    "out vec4 fragColor;\n"
                    "#define iPixelPos3D vPixelPos3D\n"
                    "#define iPixelPos2D vPixelPos2D\n"
                    "uniform float time;\n"
                    "uniform vec2 resolution;\n"
                    "uniform float zSlice;\n" +
                    fsSourceStr;
            }
        }

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

    static const std::string defaultGLSL = 
        "#version 150\n"
        "in vec2 uv;\n"
        "out vec4 fragColor;\n"
        "\n"
        "uniform float time;\n"
        "uniform vec2 resolution;\n"
        "\n"
        "void main() {\n"
        "    float x = uv.x * 10.0;\n"
        "    float y = uv.y * 10.0;\n"
        "    \n"
        "    float v1 = sin(x + time);\n"
        "    float v2 = sin(10.0 * (x * sin(time / 2.0) + y * cos(time / 3.0)) + time);\n"
        "    \n"
        "    float cx = x + 5.0 * sin(time / 5.0);\n"
        "    float cy = y + 5.0 * cos(time / 3.0);\n"
        "    float v3 = sin(sqrt(cx*cx + cy*cy + 1.0) - time);\n"
        "    \n"
        "    float v = (v1 + v2 + v3) / 3.0;\n"
        "    \n"
        "    float r = sin(v * 3.1415) * 0.5 + 0.5;\n"
        "    float g = sin(v * 3.1415 + 2.094) * 0.5 + 0.5;\n"
        "    float b = sin(v * 3.1415 + 4.188) * 0.5 + 0.5;\n"
        "    \n"
        "    fragColor = vec4(r, g, b, 1.0);\n"
        "}\n";

    // Populate memory sources from files if empty
    auto* sData = patch.try_get_mut<Patch::ScriptData>();
    if (sData) {
        std::string luaPath = "scripts/default_patch.lua";
        std::string glslPath = "shaders/default_patch.frag";
        if (const auto* settings = patch.try_get<Patch::Settings>()) {
            luaPath = settings->luaScriptPath;
            glslPath = settings->shaderPath;
        }
        if (sData->luaSource.empty()) {
            sData->luaSource = get_file_content(luaPath, defaultLua);
        }
        if (sData->glslSource.empty()) {
            sData->glslSource = get_file_content(glslPath, defaultGLSL);
        }
    }

    //prepare pixel buffers
    program->pixelCount = 0;
    program->fixtureCount = 0;
    Fixture::iterateWithDmx(patch, [&](flecs::entity fixture, const Fixture::Layout& layout, const Fixture::DmxAddress& addr){
        program->pixelCount += layout.pixelCount;
        program->fixtureCount++;
    });
    program->pixelColors = (ColorRGBW*)malloc(program->pixelCount * sizeof(ColorRGBW));
    program->pixelColorsTemp = (ColorRGBW*)malloc(program->pixelCount * sizeof(ColorRGBW));
    program->pixelPositions = (glm::vec3*)malloc(program->pixelCount * sizeof(glm::vec3));

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
            memcpy(program->pixelPositions + pixelIndex, pixelData->positions.data(), pixelData->positions.size() * sizeof(glm::vec3));
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

            // Initialize noise texture if needed
            if (res->glslNoiseTex == 0) {
                glGenTextures(1, &res->glslNoiseTex);
                glBindTexture(GL_TEXTURE_2D, res->glslNoiseTex);
                std::vector<uint8_t> noiseData(256 * 256 * 4);
                for (size_t i = 0; i < noiseData.size(); ++i) {
                    noiseData[i] = (uint8_t)(rand() % 256);
                }
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, noiseData.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
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
                    "uniform float mixFactor;\n"
                    "void main() {\n"
                    "    vec4 cActive = texture(texActive, uv);\n"
                    "    vec4 cOld = texture(texOld, uv);\n"
                    "    fragColor = mix(cOld, cActive, mixFactor);\n"
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
            compileShaderIfNeeded(scriptData->glslSource, *patchGp);
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
            program->glslFboOld = res->glslFboOld;
            program->glslFboTexOld = res->glslFboTexOld;
            program->glslFboBlend = res->glslFboBlend;
            program->glslFboTexBlend = res->glslFboTexBlend;
            program->glslBlendProgram = res->glslBlendProgram;
            program->glslNoiseTex = res->glslNoiseTex;

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
                            compileShaderIfNeeded(glsl->value, *fxGp);
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

                for (const auto& fx : fxList) {
                    PatchProgram::CompiledCue cc;
                    cc.program = 0;
                    cc.previewProgram = 0;

                    if (const auto* glsl = fx.try_get<EffectBank::Effect::GlslSource>()) {
                        if (!fx.has<Patch::GPUProgram>()) fx.set<Patch::GPUProgram>({});
                        auto* fxGp = &fx.get_mut<Patch::GPUProgram>();
                        compileShaderIfNeeded(glsl->value, *fxGp);
                        cc.program = fxGp->program;
                        cc.previewProgram = fxGp->previewProgram;
                        cc.compilerLog = fxGp->compilerLog;
                    }
                    program->compiledBankEffects.push_back(cc);
                }
            }
        }
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
}

void render(PatchProgram* program){
    program->timeElapsed = (float)glfwGetTime();

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

                GLint timeLoc = glGetUniformLocation(prog, "iTime");
                if (timeLoc >= 0) glUniform1f(timeLoc, program->timeElapsed);
                GLint timeLocLegacy = glGetUniformLocation(prog, "time");
                if (timeLocLegacy >= 0) glUniform1f(timeLocLegacy, program->timeElapsed);

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

            float progress = program->crossfadeProgress.load();

            // ── Render Outgoing Cue to glslFboOld (during crossfade) ──
            if (progress < 1.0f && program->glslFboOld) {
                unsigned int oldProg = program->glslProgram;
                int oldIdx = program->previousCueIndex;
                if (oldIdx >= 0 && oldIdx < (int)program->compiledCues.size()) {
                    if (program->compiledCues[oldIdx].program != 0) {
                        oldProg = program->compiledCues[oldIdx].program;
                    }
                }
                if (oldIdx >= -1 && oldProg != 0) {
                    glBindFramebuffer(GL_FRAMEBUFFER, program->glslFboOld);
                    glViewport(0, 0, program->vfbWidth, 1);
                    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);

                    setupProgramUniforms(oldProg);
                    glBindVertexArray(program->glslPointVao);
                    glDrawArrays(GL_POINTS, 0, program->pixelCount);
                    glBindVertexArray(0);
                    cleanupProgramTextures();
                    glUseProgram(0);
                }
            }

            // ── Render Active Cue to glslFbo ──
            glBindFramebuffer(GL_FRAMEBUFFER, program->glslFbo);
            glViewport(0, 0, program->vfbWidth, 1);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            if (activeProg != 0) {
                setupProgramUniforms(activeProg);
                glBindVertexArray(program->glslPointVao);
                glDrawArrays(GL_POINTS, 0, program->pixelCount);
                glBindVertexArray(0);
                cleanupProgramTextures();
                glUseProgram(0);
            }

            // ── Blend Outgoing and Active in glslFboBlend ──
            if (progress < 1.0f && program->glslFboBlend && program->glslBlendProgram) {
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

                GLint mixLoc = glGetUniformLocation(program->glslBlendProgram, "mixFactor");
                if (mixLoc >= 0) glUniform1f(mixLoc, progress);

                glBindVertexArray(program->glslQuadVao);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);
                glUseProgram(0);

                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            // ── Readback FBO to CPU vfbPixels (Async via PBO) ──
            GLuint readFbo = program->glslFbo;
            if (progress < 1.0f && program->glslFboBlend) {
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
                    setupProgramUniforms(editProgPreview);
                    glBindVertexArray(program->glslQuadVao);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glBindVertexArray(0);
                    cleanupProgramTextures();
                    glUseProgram(0);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            // ── Draw 2D Playback Preview into glslPlaybackPreviewFbo ──
            if (program->glslPlaybackPreviewFbo && program->showPlaybackPreview.load()) {
                float progress = program->crossfadeProgress.load();

                // 1. Render Outgoing Cue's 2D Preview to glslPlaybackPreviewFboOld (if crossfading)
                if (progress < 1.0f && program->glslPlaybackPreviewFboOld) {
                    unsigned int oldProgPreview = program->glslPreviewProgram;
                    int oldIdx = program->previousCueIndex;
                    if (oldIdx >= 0 && oldIdx < (int)program->compiledCues.size()) {
                        if (program->compiledCues[oldIdx].previewProgram != 0) {
                            oldProgPreview = program->compiledCues[oldIdx].previewProgram;
                        }
                    }
                    if (oldIdx >= -1 && oldProgPreview != 0) {
                        glBindFramebuffer(GL_FRAMEBUFFER, program->glslPlaybackPreviewFboOld);
                        glViewport(0, 0, program->previewWidth, program->previewHeight);
                        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                        glClear(GL_COLOR_BUFFER_BIT);

                        setupProgramUniforms(oldProgPreview);
                        glBindVertexArray(program->glslQuadVao);
                        glDrawArrays(GL_TRIANGLES, 0, 6);
                        glBindVertexArray(0);
                        cleanupProgramTextures();
                        glUseProgram(0);
                    }
                }

                // 2. Render Active Cue's 2D Preview to glslPlaybackPreviewFbo
                unsigned int activeProgPreview = program->glslPreviewProgram;
                int actIdx = program->activeCueIndex.load();
                if (actIdx >= 0 && actIdx < (int)program->compiledCues.size()) {
                    if (program->compiledCues[actIdx].previewProgram != 0) {
                        activeProgPreview = program->compiledCues[actIdx].previewProgram;
                    }
                }

                glBindFramebuffer(GL_FRAMEBUFFER, program->glslPlaybackPreviewFbo);
                glViewport(0, 0, program->previewWidth, program->previewHeight);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                if (activeProgPreview != 0) {
                    setupProgramUniforms(activeProgPreview);
                    glBindVertexArray(program->glslQuadVao);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glBindVertexArray(0);
                    cleanupProgramTextures();
                    glUseProgram(0);
                }

                // 3. Blend Active and Outgoing Previews into glslPlaybackPreviewFboBlend (if crossfading)
                if (progress < 1.0f && program->glslPlaybackPreviewFboBlend && program->glslBlendProgram) {
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

                    GLint mixLoc = glGetUniformLocation(program->glslBlendProgram, "mixFactor");
                    if (mixLoc >= 0) glUniform1f(mixLoc, progress);

                    glBindVertexArray(program->glslQuadVao);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glBindVertexArray(0);
                    glUseProgram(0);

                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, 0);
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

} // namespace PixelMapper
