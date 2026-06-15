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
            .child_of(patchFolder);

        newPatch.set_name(patchName.c_str());

        auto fixtureFolder = world.entity("FixtureFolder").child_of(newPatch);
        auto dmxOutputFolder = world.entity("DmxOutputFolder").child_of(newPatch);
        auto artnetDeviceFolder = world.entity("ArtnetDeviceFolder").child_of(newPatch);

        newPatch.add<Patch::FixtureFolder>(fixtureFolder);
        newPatch.add<Patch::DmxUniverseFolder>(dmxOutputFolder);
        newPatch.add<Patch::ArtnetDeviceFolder>(artnetDeviceFolder);

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
    if (dx <= 0.001f && dy <= 0.001f) {
        program->vfbWidth = R;
        program->vfbHeight = R;
    } else if (dx >= dy) {
        program->vfbWidth = R;
        program->vfbHeight = std::max(1, (int)std::round(R * dy / dx));
    } else {
        program->vfbHeight = R;
        program->vfbWidth = std::max(1, (int)std::round(R * dx / dy));
    }

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
            // ── Compile shaders on the main thread (safe, owns main GL context) ──
            const std::string& src = scriptData->glslSource;
            program->shaderSource = src;

            const char* vsSource =
                "#version 150\n"
                "in vec2 position;\n"
                "out vec2 uv;\n"
                "void main() {\n"
                "    uv = position * 0.5 + 0.5;\n"
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
                program->compilerLog = std::string("Vertex Shader compile error: ") + infoLog;
                glDeleteShader(vs);
                program->shaderCompiled = true;
            } else {
                // 1. Compile default fragment shader (if any)
                const char* fsSource = src.c_str();
                GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
                glShaderSource(fs, 1, &fsSource, nullptr);
                glCompileShader(fs);

                glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
                if (!success) {
                    char infoLog[512];
                    glGetShaderInfoLog(fs, 512, nullptr, infoLog);
                    program->compilerLog = std::string("Shader Compile Error:\n") + infoLog;
                    glDeleteShader(fs);
                    program->glslProgram = 0;
                } else {
                    program->glslProgram = glCreateProgram();
                    glAttachShader(program->glslProgram, vs);
                    glAttachShader(program->glslProgram, fs);
                    glBindAttribLocation(program->glslProgram, 0, "position"); // Force location 0
                    glLinkProgram(program->glslProgram);
                    glDeleteShader(fs);

                    glGetProgramiv(program->glslProgram, GL_LINK_STATUS, &success);
                    if (!success) {
                        char infoLog[512];
                        glGetProgramInfoLog(program->glslProgram, 512, nullptr, infoLog);
                        program->compilerLog = std::string("Shader Link Error:\n") + infoLog;
                        glDeleteProgram(program->glslProgram);
                        program->glslProgram = 0;
                    } else {
                        program->compilerLog = "Compile successful!";
                    }
                }
                program->defaultCompilerLog = program->compilerLog;

                // 2. Generate FBO and PBOs (always, so cue shaders can render even if default shader failed)
                glGenFramebuffers(1, &program->glslFbo);
                glGenTextures(1, &program->glslFboTex);
                glBindTexture(GL_TEXTURE_2D, program->glslFboTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                    program->vfbWidth, program->vfbHeight,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, program->glslFbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, program->glslFboTex, 0);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glBindTexture(GL_TEXTURE_2D, 0);

                // Generate editor offline preview FBO + texture
                glGenFramebuffers(1, &program->glslEditorFbo);
                glGenTextures(1, &program->glslEditorFboTex);
                glBindTexture(GL_TEXTURE_2D, program->glslEditorFboTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                    program->vfbWidth, program->vfbHeight,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, program->glslEditorFbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, program->glslEditorFboTex, 0);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glBindTexture(GL_TEXTURE_2D, 0);

                // Generate FBO for Old (outgoing) cue
                glGenFramebuffers(1, &program->glslFboOld);
                glGenTextures(1, &program->glslFboTexOld);
                glBindTexture(GL_TEXTURE_2D, program->glslFboTexOld);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                    program->vfbWidth, program->vfbHeight,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, program->glslFboOld);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, program->glslFboTexOld, 0);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glBindTexture(GL_TEXTURE_2D, 0);

                // Generate FBO for Blend result
                glGenFramebuffers(1, &program->glslFboBlend);
                glGenTextures(1, &program->glslFboTexBlend);
                glBindTexture(GL_TEXTURE_2D, program->glslFboTexBlend);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                    program->vfbWidth, program->vfbHeight,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, program->glslFboBlend);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D, program->glslFboTexBlend, 0);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glBindTexture(GL_TEXTURE_2D, 0);

                // Compile built-in mix/blend shader program
                {
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

                    program->glslBlendProgram = glCreateProgram();
                    glAttachShader(program->glslBlendProgram, blendVs);
                    glAttachShader(program->glslBlendProgram, blendFs);
                    glBindAttribLocation(program->glslBlendProgram, 0, "position"); // Force location 0
                    glLinkProgram(program->glslBlendProgram);

                    glDeleteShader(blendVs);
                    glDeleteShader(blendFs);
                }

                // PBOs for double-buffered async readback
                int pboSize = program->vfbWidth * program->vfbHeight * 4;
                glGenBuffers(2, program->glslPbo);
                for (int pi = 0; pi < 2; pi++) {
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, program->glslPbo[pi]);
                    glBufferData(GL_PIXEL_PACK_BUFFER, pboSize, nullptr, GL_STREAM_READ);
                }
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

                program->shaderCompiled = true;

                // 3. Compile cue shaders ahead of time
                if (const auto* cueList = patch.try_get<CueList::List>()) {
                    program->activeCueIndex = cueList->activeIndex;
                    for (const auto& cue : cueList->cues) {
                        PatchProgram::CompiledCue cc;
                        cc.program = 0;

                        const char* cueFsSource = cue.glslSource.c_str();
                        GLuint cueFs = glCreateShader(GL_FRAGMENT_SHADER);
                        glShaderSource(cueFs, 1, &cueFsSource, nullptr);
                        glCompileShader(cueFs);

                        glGetShaderiv(cueFs, GL_COMPILE_STATUS, &success);
                        if (!success) {
                            char infoLog[512];
                            glGetShaderInfoLog(cueFs, 512, nullptr, infoLog);
                            cc.compilerLog = std::string("Shader Compile Error:\n") + infoLog;
                            glDeleteShader(cueFs);
                        } else {
                            cc.program = glCreateProgram();
                            glAttachShader(cc.program, vs);
                            glAttachShader(cc.program, cueFs);
                            glBindAttribLocation(cc.program, 0, "position"); // Force location 0
                            glLinkProgram(cc.program);
                            glDeleteShader(cueFs);

                            glGetProgramiv(cc.program, GL_LINK_STATUS, &success);
                            if (!success) {
                                char infoLog[512];
                                glGetProgramInfoLog(cc.program, 512, nullptr, infoLog);
                                cc.compilerLog = std::string("Shader Link Error:\n") + infoLog;
                                glDeleteProgram(cc.program);
                                cc.program = 0;
                            } else {
                                cc.compilerLog = "Compile successful!";
                            }
                        }
                        program->compiledCues.push_back(cc);
                    }
                }

                // Compile bank effects ahead of time
                if (const auto* bank = patch.try_get<EffectBank::Bank>()) {
                    program->editingBankIndex = bank->activeIndex;
                    for (const auto& fx : bank->effects) {
                        PatchProgram::CompiledCue cc;
                        cc.program = 0;

                        const char* fxFsSource = fx.glslSource.c_str();
                        GLuint fxFs = glCreateShader(GL_FRAGMENT_SHADER);
                        glShaderSource(fxFs, 1, &fxFsSource, nullptr);
                        glCompileShader(fxFs);

                        glGetShaderiv(fxFs, GL_COMPILE_STATUS, &success);
                        if (!success) {
                            char infoLog[512];
                            glGetShaderInfoLog(fxFs, 512, nullptr, infoLog);
                            cc.compilerLog = std::string("Shader Compile Error:\n") + infoLog;
                            glDeleteShader(fxFs);
                        } else {
                            cc.program = glCreateProgram();
                            glAttachShader(cc.program, vs);
                            glAttachShader(cc.program, fxFs);
                            glBindAttribLocation(cc.program, 0, "position"); // Force location 0
                            glLinkProgram(cc.program);
                            glDeleteShader(fxFs);

                            glGetProgramiv(cc.program, GL_LINK_STATUS, &success);
                            if (!success) {
                                char infoLog[512];
                                glGetProgramInfoLog(cc.program, 512, nullptr, infoLog);
                                cc.compilerLog = std::string("Shader Link Error:\n") + infoLog;
                                glDeleteProgram(cc.program);
                                cc.program = 0;
                            } else {
                                cc.compilerLog = "Compile successful!";
                            }
                        }
                        program->compiledBankEffects.push_back(cc);
                    }
                }

                glDeleteShader(vs);
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

    if (glslProgram) glDeleteProgram(glslProgram);
    if (glslBlendProgram) glDeleteProgram(glslBlendProgram);
    for (auto& cc : compiledCues) {
        if (cc.program) glDeleteProgram(cc.program);
    }
    for (auto& cc : compiledBankEffects) {
        if (cc.program) glDeleteProgram(cc.program);
    }
    if (glslFbo) glDeleteFramebuffers(1, &glslFbo);
    if (glslFboTex) glDeleteTextures(1, &glslFboTex);
    if (glslFboOld) glDeleteFramebuffers(1, &glslFboOld);
    if (glslFboTexOld) glDeleteTextures(1, &glslFboTexOld);
    if (glslFboBlend) glDeleteFramebuffers(1, &glslFboBlend);
    if (glslFboTexBlend) glDeleteTextures(1, &glslFboTexBlend);
    if (glslEditorFbo) glDeleteFramebuffers(1, &glslEditorFbo);
    if (glslEditorFboTex) glDeleteTextures(1, &glslEditorFboTex);
    if (glslVao) glDeleteVertexArrays(1, &glslVao);
    if (glslVbo) glDeleteBuffers(1, &glslVbo);
    if (glslPbo[0]) glDeleteBuffers(2, glslPbo);
}

void render(PatchProgram* program){
    program->timeElapsed = (float)glfwGetTime();

    // ── Cue Switch Detection & Crossfade Triggering ──
    if (program->renderMode == Patch::RenderMode::GLSL && program->vfbPixels && program->vfbPixelsOld) {
        int targetCue = program->activeCueIndex;
        if (program->currentRenderedCueIndex != targetCue) {
            float fade = program->pendingCrossfadeDuration;
            program->pendingCrossfadeDuration = 0.0f; // Consume
            if (fade > 0.0f && program->currentRenderedCueIndex != -2) {
                program->previousCueIndex = program->currentRenderedCueIndex;
                program->crossfadeDuration = fade;
                program->crossfadeProgress = 0.0f;
            } else {
                program->previousCueIndex = -2;
                program->crossfadeProgress = 1.0f;
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
            int actIdx = program->activeCueIndex;
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
            if (program->crossfadeProgress < 1.0f && program->glslFboOld) {
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
            if (program->crossfadeProgress < 1.0f && program->glslFboBlend && program->glslBlendProgram) {
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
                if (mixLoc >= 0) glUniform1f(mixLoc, program->crossfadeProgress);

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
            if (program->crossfadeProgress < 1.0f && program->glslFboBlend) {
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
                int editIdx = program->editingCueIndex;
                int editBankIdx = program->editingBankIndex;

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
    if (program->renderMode != Patch::RenderMode::GLSL && program->crossfadeProgress < 1.0f && program->vfbPixelsOld) {
        float t = program->crossfadeProgress;
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
