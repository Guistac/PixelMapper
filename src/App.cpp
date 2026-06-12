#include "App.h"
#include "Patch.h"
#include "Fixture.h"
#include "Artnet.h"
#include "Shape.h"
#include "utils/FlecsUtils.h"

#include <thread>
#include <mutex>
#include <chrono>
#include <iostream>
#include <cstring>
#include <algorithm>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace PixelMapper {
namespace App {

    flecs::entity get(const flecs::world& w){
        return w.target<Is>();
    }

    const Queries& getQueries(const flecs::world& w){
        return get(w).get<Queries>();
    }

    std::thread rtPatchRunner;
    PatchProgram* currentPatchProgram;
    std::mutex patchProgramLock;
    bool b_rtPatchRunner = false;

    void pushNewProgram(PatchProgram* newProg){
        patchProgramLock.lock();
        PatchProgram* oldProgram = currentPatchProgram;
        currentPatchProgram = newProg;
        patchProgramLock.unlock();
        delete oldProgram;
    }

    void runPatch(){
        b_rtPatchRunner = true;
        int socketFd = -1;
        uint16_t activeSourcePort = 0;
        auto lastSendTime = std::chrono::steady_clock::now();

        while(b_rtPatchRunner){
            auto start = std::chrono::steady_clock::now();
            patchProgramLock.lock();
            if(currentPatchProgram){
                render(App::currentPatchProgram);
                encode(App::currentPatchProgram);

                if (currentPatchProgram->networkEnabled) {
                    if (socketFd == -1 || activeSourcePort != currentPatchProgram->sourcePort) {
                        if (socketFd != -1) {
                            close(socketFd);
                            socketFd = -1;
                        }

                        socketFd = socket(AF_INET, SOCK_DGRAM, 0);
                        if (socketFd >= 0) {
                            int broadcastEnable = 1;
                            setsockopt(socketFd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

                            sockaddr_in localAddr{};
                            localAddr.sin_family = AF_INET;
                            localAddr.sin_port = htons(currentPatchProgram->sourcePort);
                            localAddr.sin_addr.s_addr = INADDR_ANY;

                            if (bind(socketFd, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
                                // Bind failed
                            }
                            activeSourcePort = currentPatchProgram->sourcePort;
                        }
                    }

                    float intervalMs = 1000.0f / currentPatchProgram->refreshRate;
                    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(start - lastSendTime).count();
                    if (elapsedMs >= intervalMs && socketFd >= 0) {
                        lastSendTime = start;

#pragma pack(push, 1)
                        struct ArtDmxHeader {
                            char id[8] = {'A', 'r', 't', '-', 'N', 'e', 't', '\0'};
                            uint16_t opCode = 0x5000;
                            uint16_t protVer = htons(14);
                            uint8_t sequence = 0x00;
                            uint8_t physical = 0x00;
                            uint16_t universe = 0;
                            uint16_t length = htons(512);
                        };
#pragma pack(pop)

                        for (uint32_t d = 0; d < currentPatchProgram->deviceCount; d++) {
                            const auto& device = currentPatchProgram->devices[d];
                            
                            sockaddr_in destAddr{};
                            destAddr.sin_family = AF_INET;
                            destAddr.sin_port = htons(6454);
                            destAddr.sin_addr.s_addr = device.ipAddress;

                            for (uint32_t u = 0; u < currentPatchProgram->universeCount; u++) {
                                const auto& universe = currentPatchProgram->universes[u];
                                if (universe.id >= device.startUniverse && 
                                    universe.id < device.startUniverse + device.universeCount) {
                                    
                                    ArtDmxHeader header;
                                    header.universe = universe.id;

                                    uint8_t packet[sizeof(ArtDmxHeader) + 512];
                                    std::memcpy(packet, &header, sizeof(header));
                                    std::memcpy(packet + sizeof(header), universe.buffer, 512);

                                    sendto(socketFd, packet, sizeof(packet), 0, (struct sockaddr*)&destAddr, sizeof(destAddr));
                                }
                            }
                        }
                    }
                } else {
                    if (socketFd != -1) {
                        close(socketFd);
                        socketFd = -1;
                        activeSourcePort = 0;
                    }
                }
            }
            patchProgramLock.unlock();
            std::this_thread::sleep_until(start + std::chrono::milliseconds(3));
        }

        if (socketFd != -1) {
            close(socketFd);
        }
    }

    void terminate(){
        b_rtPatchRunner = false;
        if(rtPatchRunner.joinable()) rtPatchRunner.join();
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
            .fixtureWithDmxInPatch = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress>()
                .term().first(flecs::ChildOf).second("$parent")
                .build(),
            .fixtureInDmxUniverse = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress>()
                .term().first(flecs::ChildOf).second("$parent")
                .with<Fixture::InUniverse>().second("$universe")
                .build(),
            .fixtureWithPixelDataInPatch = w.query_builder<Fixture::Is, Fixture::PixelData>()
                .term().first(flecs::ChildOf).second("$parent")
                .build(),
            .dmxUniverseInPatch = w.query_builder<Artnet::Universe::Is, Artnet::Universe::Properties>()
                .term().first(flecs::ChildOf).second("$parent")
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
                [&](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress dmxAddress){
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
            std::cout << "Compiling " << patch.name() << std::endl;
            patch.remove<Patch::ProgramDirty>();
            App::pushNewProgram(PatchProgram::compile(patch));
        });

        App::rtPatchRunner = std::thread([](){
            App::runPatch();
        });
    }

} // namespace App
} // namespace PixelMapper
