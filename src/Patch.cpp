#include "Patch.h"
#include "Fixture.h"
#include "App.h"
#include "Artnet.h"
#include <string>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <cstring>
#include <imgui.h>

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
        w.component<RenderArea>();
    }

} // namespace Patch


PatchProgram* PatchProgram::compile(flecs::entity patch){
    PatchProgram* program = new PatchProgram();

    //prepare pixel buffers
    program->pixelCount = 0;
    Fixture::iterateWithDmx(patch, [&](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress& addr){ program->pixelCount += layout.pixelCount; });
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
    Fixture::iterateWithDmx(patch, [&](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress& addr){
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

    if(const auto* settings = patch.try_get<Patch::Settings>()){
        program->networkEnabled = settings->networkEnabled;
        program->sourcePort = settings->sourcePort;
        program->refreshRate = settings->refreshRate;
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

    return program;
}

PatchProgram::~PatchProgram(){
    free(devices);
    free(p2us);
    free(pixelColors);
    free(pixelPositions);
    free(universes);
}

void render(PatchProgram* program){
    glm::vec3 center = (program->pixelPosMin + program->pixelPosMax) * 0.5f;
    float time = ImGui::GetTime();
    for(int i = 0; i < program->pixelCount; i++){
        const auto& pos = program->pixelPositions[i];
        auto& col = program->pixelColors[i];
        float dist = glm::distance(pos, center);
        float br = std::sin((dist - time * 100.0) / 30.0);
        uint8_t out = br > 0 ? br * 255.0 : 0;
        col.r = out;
        col.g = out;
        col.b = out;
        col.w = out;
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
