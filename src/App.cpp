#include "App.h"
#include "Patch.h"
#include "Fixture.h"
#include "Artnet.h"
#include "Shape.h"
#include "ArtnetSender.h"
#include "CueList.h"
#include "EffectBank.h"
#include "FileWatcher.h"
#include "utils/FlecsUtils.h"


#include <thread>
#include <mutex>
#include <chrono>
#include <iostream>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace PixelMapper {
namespace App {

    GLFWwindow* sharedContextWindow = nullptr;
    PatchProgram* currentPatchProgram = nullptr;
    std::mutex patchProgramLock;
    bool b_rtPatchRunner = false;

    std::atomic<float> rtFps{0.0f};
    std::atomic<float> rtBitrateMbps{0.0f};
    float pendingCrossfadeDuration = 0.0f;

    flecs::entity get(const flecs::world& w){
        return w.target<Is>();
    }

    const Queries& getQueries(const flecs::world& w){
        return get(w).get<Queries>();
    }

    std::thread rtPatchRunner;

    void pushNewProgram(PatchProgram* newProg){
        patchProgramLock.lock();
        PatchProgram* oldProgram = currentPatchProgram;

        // ── Crossfade handoff ──
        float fade = pendingCrossfadeDuration;
        pendingCrossfadeDuration = 0.0f;
        if (newProg) {
            if (fade > 0.0f && oldProgram && oldProgram->vfbPixels && newProg->vfbPixelsOld) {
                // Copy the last rendered frame of the outgoing cue as the fade-from snapshot
                int copyPixels = std::min(oldProgram->vfbWidth  * oldProgram->vfbHeight,
                                          newProg->vfbWidth     * newProg->vfbHeight);
                std::memcpy(newProg->vfbPixelsOld, oldProgram->vfbPixels,
                            copyPixels * sizeof(ColorRGBW));
                newProg->crossfadeDuration = fade;
                newProg->crossfadeProgress = 0.0f;
            } else {
                newProg->crossfadeProgress = 1.0f; // no crossfade
            }
        }
        if (newProg && oldProgram) {
            newProg->editingCueIndex = oldProgram->editingCueIndex;
            newProg->editingBankIndex = oldProgram->editingBankIndex;
        }

        currentPatchProgram = newProg;
        patchProgramLock.unlock();
        delete oldProgram;
    }

    void runPatch(){
        b_rtPatchRunner = true;
        if (sharedContextWindow) {
            glfwMakeContextCurrent(sharedContextWindow);
        }
        ArtnetSender sender;
        auto lastSendTime = std::chrono::steady_clock::now();

        // ── RT stats ──
        int   rtFrameCount   = 0;
        int   rtBytesThisSec = 0;
        auto  rtStatsTimer   = std::chrono::steady_clock::now();

        while(b_rtPatchRunner){
            auto start = std::chrono::steady_clock::now();
            patchProgramLock.lock();
            if(currentPatchProgram){
                float intervalMs = 1000.0f / currentPatchProgram->refreshRate;
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(start - lastSendTime).count();
                if (elapsedMs >= intervalMs) {
                    lastSendTime = start;

                    render(App::currentPatchProgram);
                    encode(App::currentPatchProgram);

                    // Increment crossfade progress
                    if (currentPatchProgram->crossfadeDuration > 0.0f &&
                        currentPatchProgram->crossfadeProgress < 1.0f)
                    {
                        float dt = intervalMs / 1000.0f;
                        currentPatchProgram->crossfadeProgress = std::min(1.0f,
                            currentPatchProgram->crossfadeProgress + dt / currentPatchProgram->crossfadeDuration);
                    }

                    if (currentPatchProgram->networkEnabled) {
                        sender.send(currentPatchProgram);
                        // Approximate ArtDmx packet size: 18 header + 512 data = 530 bytes/universe
                        rtBytesThisSec += currentPatchProgram->universeCount * 530;
                    } else {
                        sender.closeSocket();
                    }

                    rtFrameCount++;
                }
            }
            patchProgramLock.unlock();

            // Update RT stats once per second (outside lock)
            {
                auto now = std::chrono::steady_clock::now();
                float elapsed = std::chrono::duration<float>(now - rtStatsTimer).count();
                if (elapsed >= 1.0f) {
                    rtFps.store(rtFrameCount / elapsed);
                    rtBitrateMbps.store((rtBytesThisSec * 8.0f) / (elapsed * 1e6f));
                    rtFrameCount   = 0;
                    rtBytesThisSec = 0;
                    rtStatsTimer   = now;
                }
            }

            std::this_thread::sleep_until(start + std::chrono::milliseconds(3));
        }

        if (sharedContextWindow) {
            glfwMakeContextCurrent(nullptr);
        }
    }

    void terminate(){
        b_rtPatchRunner = false;
        if(rtPatchRunner.joinable()) rtPatchRunner.join();
    }

    static int compareOrder(flecs::entity_t e1, const Fixture::Order* o1,
                           flecs::entity_t e2, const Fixture::Order* o2) {
        if (!o1 && !o2) return 0;
        if (!o1) return 1;
        if (!o2) return -1;
        return (o1->index > o2->index) - (o1->index < o2->index);
    }

    static int compareUniverse(flecs::entity_t e1, const Artnet::Universe::Properties* p1,
                              flecs::entity_t e2, const Artnet::Universe::Properties* p2) {
        if (!p1 && !p2) return 0;
        if (!p1) return 1;
        if (!p2) return -1;
        return (p1->universeId > p2->universeId) - (p1->universeId < p2->universeId);
    }

    void import(flecs::world& w){
        //——————————————————— COMPONENTS ——————————————————————
        w.component<Is>();
        w.component<PatchFolder>();
        w.component<SelectedPatch>();
        Patch::import(w);
        Fixture::import(w);
        Artnet::Universe::import(w);
        Artnet::Device::import(w); // Explicit registration!
        CueList::import(w);        // Cue list + CueAdvancer system
        EffectBank::import(w);     // Experimental effect bank

        //————————————————— PAIR PROPERTIES ———————————————————
        w.component<Fixture::WithShape>().add(flecs::Exclusive);
        w.component<Patch::SelectedFixture>().add(flecs::Exclusive);
        w.component<Patch::SelectedDmxUniverse>().add(flecs::Exclusive);
        w.component<Patch::SelectedArtnetDevice>().add(flecs::Exclusive);
        w.component<SelectedPatch>().add(flecs::Exclusive);

        //———————————————————— TREE ROOT ——————————————————————
        auto pixelMapper = w.entity("PixelMapperApp");
        w.add<Is>(pixelMapper);
        auto patchFolder = w.entity("Patches").child_of(pixelMapper);
        pixelMapper.add<PatchFolder>(patchFolder);

        //————————————————————— QUERIES ———————————————————————
        Queries queries{
            .patch = w.query_builder<Patch::Is>()
                .term().first(flecs::ChildOf).second("$parent")
                .build(),
            .fixtureWithDmxInPatch = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress, Fixture::Order>()
                .term().first(flecs::ChildOf).second("$parent")
                .order_by<Fixture::Order>(compareOrder)
                .build(),
            .fixtureInDmxUniverse = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress, Fixture::Order>()
                .term().first(flecs::ChildOf).second("$parent")
                .with<Fixture::InUniverse>().second("$universe")
                .order_by<Fixture::Order>(compareOrder)
                .build(),
            .fixtureWithPixelDataInPatch = w.query_builder<Fixture::Is, Fixture::PixelData, Fixture::Order>()
                .term().first(flecs::ChildOf).second("$parent")
                .order_by<Fixture::Order>(compareOrder)
                .build(),
            .dmxUniverseInPatch = w.query_builder<Artnet::Universe::Is, Artnet::Universe::Properties>()
                .term().first(flecs::ChildOf).second("$parent")
                .order_by<Artnet::Universe::Properties>(compareUniverse)
                .build(),
            .artnetDeviceInPatch = w.query_builder<Artnet::Device::Is, Artnet::Device::Settings>()
                .term().first(flecs::ChildOf).second("$parent")
                .build()
        };
        pixelMapper.set<Queries>(queries);

        //———————————————————— OBSERVERS ——————————————————————
        w.observer<Fixture::Layout>("ObserveFixtureLayout").event(flecs::OnSet)
        .with<Fixture::Is>()
        .each([](flecs::entity e, Fixture::Layout& l) {
            l.pixelCount = std::clamp<int>(l.pixelCount, 1, INT_MAX);
            l.channelsPerPixel = std::clamp<int>(l.channelsPerPixel, 1, 4);
            e.add<Fixture::LayoutDirty>();
        });
        
        w.observer<Fixture::DmxAddress>("ObserveFixtureDmxAddress").event(flecs::OnSet)
        .with<Fixture::Is>()
        .each([](flecs::entity fixture, Fixture::DmxAddress& dmx){
            dmx.address = std::clamp<uint16_t>(dmx.address, 0, 511);
            dmx.universe = std::clamp<uint16_t>(dmx.universe, 0, 32767);
            Fixture::getPatch(fixture).add<Patch::DmxMapDirty>();
        });

        //————————————————————— SYSTEMS ———————————————————————
        w.system<Fixture::Layout, Fixture::PixelData>("UpdateFixtureLayout").with<Fixture::LayoutDirty>()
        .kind(flecs::OnLoad)
        .with<Fixture::Is>()
        .immediate()
        .each([](flecs::entity fixture, Fixture::Layout& l, Fixture::PixelData& pd){
            pd.positions.resize(l.pixelCount);
            pd.colors.resize(l.pixelCount);
            fixture.remove<Fixture::LayoutDirty>();
            fixture.add<Fixture::PixelPositionsDirty>();
            Fixture::getPatch(fixture).add<Patch::DmxMapDirty>();
        });

        w.system<Fixture::PixelData>("UpdateLinePixelPositions").with<Fixture::WithShape, Shape::Line>()
        .kind(flecs::PostLoad)
        .with<Fixture::PixelPositionsDirty>()
        .with<Fixture::Is>()
        .immediate()
        .each([](flecs::entity fixture, Fixture::PixelData& pd) {
            const Shape::Line& line = fixture.get<Fixture::WithShape, Shape::Line>();
            Fixture::setPixelPositions(pd,
                [&](float range, int index, size_t count) -> glm::vec3{
                    glm::vec2 out = line.start + range * (line.end - line.start);
                    return glm::vec3(out.x, out.y, 0.0);
            });
            fixture.remove<Fixture::PixelPositionsDirty>();
            flecs::entity patch = Fixture::getPatch(fixture);
            patch.add<Patch::RenderAreaDirty>();
        });

        w.system<Fixture::PixelData>("UpdateCirclePixelPositions").with<Fixture::WithShape, Shape::Circle>()
        .kind(flecs::PostLoad)
        .with<Fixture::PixelPositionsDirty>()
        .with<Fixture::Is>()
        .immediate()
        .each([](flecs::entity fixture, Fixture::PixelData& pd) {
            const Shape::Circle& circle = fixture.get<Fixture::WithShape, Shape::Circle>();
            Fixture::setPixelPositions(pd,
                [&](float range, int index, size_t count) -> glm::vec3{
                    float angle = float(index) / float(count) * M_PI * 2.0;
                    glm::vec2 out{
                        circle.center.x + cosf(angle) * circle.radius,
                        circle.center.y + sinf(angle) * circle.radius
                    };
                    return glm::vec3(out.x, out.y, 0.0);
            });
            fixture.remove<Fixture::PixelPositionsDirty>();
            Fixture::getPatch(fixture).add<Patch::RenderAreaDirty>();
        });

        w.system<Patch::RenderArea>("UpdateRenderArea").with<Patch::RenderAreaDirty>()
        .kind(flecs::PreUpdate)
        .with<Patch::Is>()
        .immediate()
        .each([](flecs::entity patch, Patch::RenderArea& ra){
            glm::vec3 min(FLT_MAX);
            glm::vec3 max(-FLT_MAX);
            bool b_hasPixels = false;
            Fixture::iterateWithPixelData(patch,
                [&](flecs::entity fixture, Fixture::PixelData& pd){
                    for(auto& p : pd.positions){
                        min = glm::min(min, p);
                        max = glm::max(max, p);
                        b_hasPixels = true;
                    }
            });
            if(b_hasPixels){
                ra.min = min;
                ra.max = max;
            }
            patch.remove<Patch::RenderAreaDirty>();
            patch.add<Patch::ProgramDirty>();
        });

        w.system<Patch::Is>("UpdateDmxOutputMap").with<Patch::DmxMapDirty>()
        .kind(flecs::PreUpdate)
        .immediate()
        .each([](flecs::entity patch, Patch::Is){
            std::unordered_map<uint16_t, flecs::entity> univsByNumber;
            std::unordered_map<flecs::entity, std::vector<uint16_t>, EntityHasher> univsByFixture;

            Fixture::iterateWithDmx(patch,
                [&](flecs::entity fixture, const Fixture::Layout& layout, const Fixture::DmxAddress& dmxAddress){
                    int fixtureUniverseCount = 1;
                    int channels = dmxAddress.address + layout.pixelCount * layout.channelsPerPixel;
                    while(channels > 512){ fixtureUniverseCount++; channels -= 512; }
                    
                    auto& fixtureUnivNumbers = univsByFixture[fixture];
                    for(int i = 0; i < fixtureUniverseCount; i++){
                        univsByNumber[dmxAddress.universe + i] = flecs::entity::null();
                        fixtureUnivNumbers.push_back(dmxAddress.universe + i);
                    }
            });

            std::vector<flecs::entity> toDelete;
            Artnet::Universe::iterate(patch,
                [&](flecs::entity universe, Artnet::Universe::Properties& properties){
                    if(univsByNumber.count(properties.universeId) == 0)
                        toDelete.push_back(universe);
                    else univsByNumber[properties.universeId] = universe;
            });
            
            for (auto e : toDelete) e.destruct();
            for(auto& [id, universe] : univsByNumber){
                if(!universe.is_valid()){
                    flecs::entity newUniverse = Artnet::Universe::create(patch, id);
                    if(newUniverse.is_valid()) universe = newUniverse;
                }
            }

            for(const auto& [fixture, univList] : univsByFixture){
                fixture.remove<Fixture::InUniverse>(flecs::Wildcard);
                for(auto id : univList){
                    fixture.add<Fixture::InUniverse>(univsByNumber[id]);
                }
            }

            patch.remove<Patch::DmxMapDirty>();
            patch.add<Patch::ProgramDirty>();
        });

        w.system<Patch::Is>("PatchProgramCompile").with<Patch::ProgramDirty>()
        .kind(flecs::OnUpdate)
        .immediate()
        .each([](flecs::entity patch, Patch::Is){
            // ── Debounce: skip if compiled very recently (breaks runaway compile loops) ──
            static std::unordered_map<flecs::id_t, std::chrono::steady_clock::time_point> lastCompile;
            auto now = std::chrono::steady_clock::now();
            auto& last = lastCompile[patch.id()];
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() < 50) {
                patch.remove<Patch::ProgramDirty>(); // drop — too soon
                return;
            }
            last = now;

            std::cout << "Compiling " << patch.name() << std::endl;
            patch.remove<Patch::ProgramDirty>();
            App::pushNewProgram(PatchProgram::compile(patch));
        });

        // ──────────────────────── FILE WATCH SYSTEM ──────────────────────────
        // Runs in PreStore, once per second. Checks Lua + GLSL file timestamps.
        // On change: reloads source into ScriptData, sets ProgramDirty.
        w.system<Patch::Is, const Patch::Settings, Patch::ScriptData>("FileWatchSystem")
        .kind(flecs::PreStore)
        .each([](flecs::iter& it, size_t i,
                 Patch::Is, const Patch::Settings& settings, Patch::ScriptData& sd)
        {
            static std::unordered_map<flecs::id_t, FileWatcher> luaWatchers;
            static std::unordered_map<flecs::id_t, FileWatcher> glslWatchers;
            static float accumulator = 0.0f;

            accumulator += it.delta_time();
            if (accumulator < 1.0f) return; // poll once per second
            // Reset only after checking all patches (done in the first iteration index)
            if (i == 0) accumulator = 0.0f;

            flecs::id_t id = it.entity(i).id();

            // Ensure watchers exist for this patch
            auto& luaW  = luaWatchers[id];
            auto& glslW = glslWatchers[id];
            if (luaW.path != settings.luaScriptPath)   luaW.setPath(settings.luaScriptPath);
            if (glslW.path != settings.shaderPath)      glslW.setPath(settings.shaderPath);

            bool dirty = false;

            if (luaW.check()) {
                // Reload Lua
                std::ifstream f(settings.luaScriptPath);
                if (f.is_open()) {
                    std::stringstream ss; ss << f.rdbuf();
                    sd.luaSource = ss.str();
                    dirty = true;
                    std::cout << "[FileWatch] Reloaded " << settings.luaScriptPath << "\n";
                }
            }
            if (glslW.check()) {
                // Reload GLSL
                std::ifstream f(settings.shaderPath);
                if (f.is_open()) {
                    std::stringstream ss; ss << f.rdbuf();
                    sd.glslSource = ss.str();
                    dirty = true;
                    std::cout << "[FileWatch] Reloaded " << settings.shaderPath << "\n";
                }
            }

            if (dirty) {
                it.entity(i).add<Patch::ProgramDirty>();
            }
        });

        App::rtPatchRunner = std::thread([](){
            App::runPatch();
        });
    }

} // namespace App
} // namespace PixelMapper
