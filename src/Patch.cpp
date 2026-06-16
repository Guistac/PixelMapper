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
            if (res.glslVao) glDeleteVertexArrays(1, &res.glslVao);
            if (res.glslVbo) glDeleteBuffers(1, &res.glslVbo);
            if (res.glslPbo[0]) glDeleteBuffers(2, res.glslPbo);
        });

        w.observer<GPUProgram>("CleanGPUProgram").event(flecs::OnRemove)
        .each([](flecs::entity e, GPUProgram& gp) {
            if (gp.program) glDeleteProgram(gp.program);
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

    bool compileShaderIfNeeded(const std::string& fsSourceStr, Patch::GPUProgram& gp) {
        if (gp.program != 0 && gp.glslSource == fsSourceStr) {
            return true;
        }

        if (gp.program != 0) {
            glDeleteProgram(gp.program);
            gp.program = 0;
        }

        gp.glslSource = fsSourceStr;

        const char* vsSource =
            "#version 150\n"
            "in vec2 position;\n"
            "out vec2 uv;\n"
            "void main() {\n"
            "    uv = vec2(position.x * 0.5 + 0.5, 1.0 - (position.y * 0.5 + 0.5));\n"
            "    gl_Position = vec4(position, 0.0, 1.0);\n"
            "}\n";

        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vsSource, nullptr);
        glCompileShader(vs);

        GLint success = 0;
        glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(vs, 512, nullptr, infoLog);
            gp.compilerLog = std::string("Vertex Shader compile error: ") + infoLog;
            glDeleteShader(vs);
            return false;
        }

        const char* fsSource = fsSourceStr.c_str();
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fsSource, nullptr);
        glCompileShader(fs);

        glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(fs, 512, nullptr, infoLog);
            gp.compilerLog = std::string("Shader Compile Error:\n") + infoLog;
            glDeleteShader(fs);
            glDeleteShader(vs);
            return false;
        }

        gp.program = glCreateProgram();
        glAttachShader(gp.program, vs);
        glAttachShader(gp.program, fs);
        glBindAttribLocation(gp.program, 0, "position");
        glLinkProgram(gp.program);
        glDeleteShader(fs);
        glDeleteShader(vs);

        glGetProgramiv(gp.program, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(gp.program, 512, nullptr, infoLog);
            gp.compilerLog = std::string("Shader Link Error:\n") + infoLog;
            glDeleteProgram(gp.program);
            gp.program = 0;
            return false;
        }

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
    Fixture::iterateWithDmx(patch, [&](flecs::entity fixture, const Fixture::Layout& layout, const Fixture::DmxAddress& addr){ program->pixelCount += layout.pixelCount; });
    program->pixelColors = (ColorRGBW*)malloc(program->pixelCount * sizeof(ColorRGBW));
    program->pixelPositions = (glm::vec3*)malloc(program->pixelCount * sizeof(glm::vec3));

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
    std::vector<PatchProgram::Pix2UniCopyInstr> p2us;
    Fixture::iterateWithDmx(patch, [&](flecs::entity fixture, const Fixture::Layout& layout, const Fixture::DmxAddress& addr){
        int fixtureByteCount = layout.pixelCount * layout.channelsPerPixel;
        int universeSpanSize = (addr.address + fixtureByteCount + 511) / 512;

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

    int R = 256;
    if(const auto* settings = patch.try_get<Patch::Settings>()){
        program->networkEnabled = settings->networkEnabled;
        program->sourcePort = settings->sourcePort;
        program->refreshRate = settings->refreshRate;
        program->renderMode = settings->renderMode;
        R = settings->vfbResolution;
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

    // Calculate VFB resolution aspect ratio:
    float dx = program->pixelPosMax.x - program->pixelPosMin.x;
    float dy = program->pixelPosMax.y - program->pixelPosMin.y;
    int vfbWidth = R;
    int vfbHeight = R;
    if (dx <= 0.001f && dy <= 0.001f) {
        vfbWidth = R;
        vfbHeight = R;
    } else if (dx >= dy) {
        vfbWidth = R;
        vfbHeight = std::max(1, (int)std::round(R * dy / dx));
    } else {
        vfbHeight = R;
        vfbWidth = std::max(1, (int)std::round(R * dx / dy));
    }
    program->vfbWidth = vfbWidth;
    program->vfbHeight = vfbHeight;

    // Allocate VFB CPU pixels
    int vfbSize = program->vfbWidth * program->vfbHeight;
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

            // Resize FBO if resolution changed
            if (res->glslFbo == 0 || res->vfbWidth != vfbWidth || res->vfbHeight != vfbHeight) {
                if (res->glslFbo) {
                    glDeleteFramebuffers(1, &res->glslFbo);
                    glDeleteTextures(1, &res->glslFboTex);
                    glDeleteFramebuffers(1, &res->glslEditorFbo);
                    glDeleteTextures(1, &res->glslEditorFboTex);
                    glDeleteFramebuffers(1, &res->glslFboOld);
                    glDeleteTextures(1, &res->glslFboTexOld);
                    glDeleteFramebuffers(1, &res->glslFboBlend);
                    glDeleteTextures(1, &res->glslFboTexBlend);
                    glDeleteBuffers(2, res->glslPbo);
                    res->glslFbo = 0;
                }

                res->vfbWidth = vfbWidth;
                res->vfbHeight = vfbHeight;

                glGenFramebuffers(1, &res->glslFbo);
                glGenTextures(1, &res->glslFboTex);
                glBindTexture(GL_TEXTURE_2D, res->glslFboTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vfbWidth, vfbHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, res->glslFbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslFboTex, 0);

                glGenFramebuffers(1, &res->glslEditorFbo);
                glGenTextures(1, &res->glslEditorFboTex);
                glBindTexture(GL_TEXTURE_2D, res->glslEditorFboTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vfbWidth, vfbHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, res->glslEditorFbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslEditorFboTex, 0);

                glGenFramebuffers(1, &res->glslFboOld);
                glGenTextures(1, &res->glslFboTexOld);
                glBindTexture(GL_TEXTURE_2D, res->glslFboTexOld);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vfbWidth, vfbHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, res->glslFboOld);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslFboTexOld, 0);

                glGenFramebuffers(1, &res->glslFboBlend);
                glGenTextures(1, &res->glslFboTexBlend);
                glBindTexture(GL_TEXTURE_2D, res->glslFboTexBlend);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vfbWidth, vfbHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, res->glslFboBlend);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res->glslFboTexBlend, 0);

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glBindTexture(GL_TEXTURE_2D, 0);

                int pboSize = vfbWidth * vfbHeight * 4;
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

            // Compile default patch shader
            compileShaderIfNeeded(scriptData->glslSource, *patchGp);
            program->compilerLog = patchGp->compilerLog;
            program->glslProgram = patchGp->program;
            program->defaultCompilerLog = patchGp->compilerLog;

            // Copy Handles to PatchProgram
            program->glslFbo = res->glslFbo;
            program->glslFboTex = res->glslFboTex;
            program->glslVao = res->glslVao;
            program->glslVbo = res->glslVbo;
            program->glslPbo[0] = res->glslPbo[0];
            program->glslPbo[1] = res->glslPbo[1];
            program->pboFrameIndex = res->pboFrameIndex;
            program->shaderCompiled = (program->glslProgram != 0);
            program->vaoReady = res->vaoReady;
            program->shaderSource = patchGp->glslSource;
            program->glslEditorFbo = res->glslEditorFbo;
            program->glslEditorFboTex = res->glslEditorFboTex;
            program->glslFboOld = res->glslFboOld;
            program->glslFboTexOld = res->glslFboTexOld;
            program->glslFboBlend = res->glslFboBlend;
            program->glslFboTexBlend = res->glslFboTexBlend;
            program->glslBlendProgram = res->glslBlendProgram;

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

                    flecs::entity targetEffect = entry.entity.target<CueList::Cue::TargetEffect>();
                    if (targetEffect.is_valid()) {
                        if (const auto* glsl = targetEffect.try_get<EffectBank::Effect::GlslSource>()) {
                            if (!targetEffect.has<Patch::GPUProgram>()) targetEffect.set<Patch::GPUProgram>({});
                            auto* fxGp = &targetEffect.get_mut<Patch::GPUProgram>();
                            compileShaderIfNeeded(glsl->value, *fxGp);
                            cc.program = fxGp->program;
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

                    if (const auto* glsl = fx.try_get<EffectBank::Effect::GlslSource>()) {
                        if (!fx.has<Patch::GPUProgram>()) fx.set<Patch::GPUProgram>({});
                        auto* fxGp = &fx.get_mut<Patch::GPUProgram>();
                        compileShaderIfNeeded(glsl->value, *fxGp);
                        cc.program = fxGp->program;
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
    free(pixelPositions);
    free(universes);
    free(vfbPixels);
    free(vfbPixelsOld);
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
                if (program->renderMode != Patch::RenderMode::GLSL) {
                    std::memcpy(program->vfbPixelsOld, program->vfbPixels,
                                program->vfbWidth * program->vfbHeight * sizeof(ColorRGBW));
                }
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

    // 1. Populate Virtual Framebuffer
    if (program->renderMode == Patch::RenderMode::CPP) {
        float cx = program->vfbWidth * 0.5f;
        float cy = program->vfbHeight * 0.5f;
        for (int y = 0; y < program->vfbHeight; y++) {
            for (int x = 0; x < program->vfbWidth; x++) {
                float dx = x - cx;
                float dy = y - cy;
                float dist = std::sqrt(dx*dx + dy*dy);
                float br = std::sin((dist - program->timeElapsed * 20.0f) / 10.0f);
                uint8_t out = br > 0 ? (uint8_t)(br * 255.0f) : 0;
                program->vfbPixels[y * program->vfbWidth + x] = {out, out, out, out};
            }
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
        // Shader compiled on main thread; VAO created here on first call (RT-thread context).
        if (program->glslFbo && program->shaderCompiled) {

            unsigned int activeProg = program->glslProgram;
            int actIdx = program->activeCueIndex.load();
            if (actIdx >= 0 && actIdx < (int)program->compiledCues.size()) {
                if (program->compiledCues[actIdx].program != 0) {
                    activeProg = program->compiledCues[actIdx].program;
                }
            }

            // ── Create VAO lazily on the RT thread (VAOs are NOT shared between GL contexts) ──
            if (!program->vaoReady) {
                float quadVertices[] = {
                    -1.0f,-1.0f,  1.0f,-1.0f,  -1.0f, 1.0f,
                    -1.0f, 1.0f,  1.0f,-1.0f,   1.0f, 1.0f,
                };
                glGenVertexArrays(1, &program->glslVao);
                glGenBuffers(1, &program->glslVbo);
                glBindVertexArray(program->glslVao);
                glBindBuffer(GL_ARRAY_BUFFER, program->glslVbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
                glEnableVertexAttribArray(0); // Position is always location 0
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindVertexArray(0);
                program->vaoReady = true;
            }

            // ── Draw Outgoing Program into glslFboOld (during crossfade) ──
            float progress = program->crossfadeProgress.load();
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
                    glViewport(0, 0, program->vfbWidth, program->vfbHeight);
                    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);

                    glUseProgram(oldProg);
                    GLint timeLoc = glGetUniformLocation(oldProg, "time");
                    if (timeLoc >= 0) glUniform1f(timeLoc, program->timeElapsed);
                    GLint resLoc = glGetUniformLocation(oldProg, "resolution");
                    if (resLoc >= 0) glUniform2f(resLoc, (float)program->vfbWidth, (float)program->vfbHeight);

                    glBindVertexArray(program->glslVao);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glBindVertexArray(0);
                    glUseProgram(0);
                }
            }

            // ── Draw Active Program into glslFbo ──
            glBindFramebuffer(GL_FRAMEBUFFER, program->glslFbo);
            glViewport(0, 0, program->vfbWidth, program->vfbHeight);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            
            if (activeProg != 0) {
                glUseProgram(activeProg);

                GLint timeLoc = glGetUniformLocation(activeProg, "time");
                if (timeLoc >= 0) glUniform1f(timeLoc, program->timeElapsed);
                GLint resLoc = glGetUniformLocation(activeProg, "resolution");
                if (resLoc >= 0) glUniform2f(resLoc, (float)program->vfbWidth, (float)program->vfbHeight);

                glBindVertexArray(program->glslVao);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);
                glUseProgram(0);
            }

            // ── Draw Blended Program into glslFboBlend (during crossfade) ──
            if (progress < 1.0f && program->glslFboBlend && program->glslBlendProgram) {
                glBindFramebuffer(GL_FRAMEBUFFER, program->glslFboBlend);
                glViewport(0, 0, program->vfbWidth, program->vfbHeight);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                glUseProgram(program->glslBlendProgram);

                // Bind texture attachments
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

                glBindVertexArray(program->glslVao);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);
                glUseProgram(0);

                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            // ── PBO async readback ──
            GLuint readFbo = program->glslFbo;
            if (progress < 1.0f && program->glslFboBlend) {
                readFbo = program->glslFboBlend;
            }

            int writeIdx = program->pboFrameIndex & 1;
            int readIdx  = 1 - writeIdx;

            glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, program->glslPbo[writeIdx]);
            glReadPixels(0, 0, program->vfbWidth, program->vfbHeight,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

            glBindBuffer(GL_PIXEL_PACK_BUFFER, program->glslPbo[readIdx]);
            void* ptr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            if (ptr) {
                std::memcpy(program->vfbPixels, ptr,
                    program->vfbWidth * program->vfbHeight * 4);
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            program->pboFrameIndex++;

            // ── Draw Offline Editor Preview into glslEditorFbo ──
            if (program->glslEditorFbo) {
                unsigned int editProg = program->glslProgram;
                int editIdx = program->editingCueIndex.load();
                int editBankIdx = program->editingBankIndex.load();

                if (editIdx >= 0 && editIdx < (int)program->compiledCues.size()) {
                    if (program->compiledCues[editIdx].program != 0) {
                        editProg = program->compiledCues[editIdx].program;
                    }
                } else if (editIdx == -2 && editBankIdx >= 0 && editBankIdx < (int)program->compiledBankEffects.size()) {
                    if (program->compiledBankEffects[editBankIdx].program != 0) {
                        editProg = program->compiledBankEffects[editBankIdx].program;
                    }
                }

                glBindFramebuffer(GL_FRAMEBUFFER, program->glslEditorFbo);
                glViewport(0, 0, program->vfbWidth, program->vfbHeight);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                if (editProg != 0) {
                    glUseProgram(editProg);
                    GLint timeLoc = glGetUniformLocation(editProg, "time");
                    if (timeLoc >= 0) glUniform1f(timeLoc, program->timeElapsed);
                    GLint resLoc = glGetUniformLocation(editProg, "resolution");
                    if (resLoc >= 0) glUniform2f(resLoc, (float)program->vfbWidth, (float)program->vfbHeight);

                    glBindVertexArray(program->glslVao);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    glBindVertexArray(0);
                    glUseProgram(0);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
        }
    }

    // ── Crossfade blend (runs for all render modes) ──────────────────────────
    // vfbPixels contains the NEW cue; vfbPixelsOld contains the OUTGOING cue.
    // crossfadeProgress goes from 0 → 1 over crossfadeDuration seconds.
    float progressBlend = program->crossfadeProgress.load();
    if (program->renderMode != Patch::RenderMode::GLSL && progressBlend < 1.0f && program->vfbPixelsOld) {
        float t = progressBlend;
        float it = 1.0f - t;
        int n = program->vfbWidth * program->vfbHeight;
        for (int i = 0; i < n; i++) {
            ColorRGBW& n_ = program->vfbPixels[i];
            const ColorRGBW& o = program->vfbPixelsOld[i];
            n_.r = (uint8_t)(it * o.r + t * n_.r + 0.5f);
            n_.g = (uint8_t)(it * o.g + t * n_.g + 0.5f);
            n_.b = (uint8_t)(it * o.b + t * n_.b + 0.5f);
            n_.w = (uint8_t)(it * o.w + t * n_.w + 0.5f);
        }
    }

    // 2. Perform Bilinear Spatial Sampling to populate physical pixelColors
    float dx = program->pixelPosMax.x - program->pixelPosMin.x;
    float dy = program->pixelPosMax.y - program->pixelPosMin.y;
    
    auto interp = [](float tx, float ty, uint8_t a00, uint8_t a10, uint8_t a01, uint8_t a11) -> uint8_t {
        float r1 = (1.0f - tx) * a00 + tx * a10;
        float r2 = (1.0f - tx) * a01 + tx * a11;
        float val = (1.0f - ty) * r1 + ty * r2;
        return (uint8_t)std::clamp(val, 0.0f, 255.0f);
    };

    for(int i = 0; i < program->pixelCount; i++){
        const auto& pos = program->pixelPositions[i];
        
        float u = 0.5f;
        float v = 0.5f;
        if (dx > 0.001f) {
            u = (pos.x - program->pixelPosMin.x) / dx;
        }
        if (dy > 0.001f) {
            v = (pos.y - program->pixelPosMin.y) / dy;
        }
        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);

        float x = u * (program->vfbWidth - 1);
        float y = v * (program->vfbHeight - 1);
        
        int x0 = (int)std::floor(x);
        int y0 = (int)std::floor(y);
        int x1 = std::min(x0 + 1, program->vfbWidth - 1);
        int y1 = std::min(y0 + 1, program->vfbHeight - 1);
        
        float tx = x - x0;
        float ty = y - y0;
        
        ColorRGBW c00 = program->vfbPixels[y0 * program->vfbWidth + x0];
        ColorRGBW c10 = program->vfbPixels[y0 * program->vfbWidth + x1];
        ColorRGBW c01 = program->vfbPixels[y1 * program->vfbWidth + x0];
        ColorRGBW c11 = program->vfbPixels[y1 * program->vfbWidth + x1];
        
        ColorRGBW finalColor;
        finalColor.r = interp(tx, ty, c00.r, c10.r, c01.r, c11.r);
        finalColor.g = interp(tx, ty, c00.g, c10.g, c01.g, c11.g);
        finalColor.b = interp(tx, ty, c00.b, c10.b, c01.b, c11.b);
        finalColor.w = interp(tx, ty, c00.w, c10.w, c01.w, c11.w);
        
        program->pixelColors[i] = finalColor;
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
