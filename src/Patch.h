#pragma once
#include <flecs.h>
#include <glm/glm.hpp>
#include <functional>
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

    struct Settings {
        float refreshRate = 40.0f;
        bool networkEnabled = false;
        uint16_t sourcePort = 6454;
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
};

void render(PatchProgram* rtData);
void encode(PatchProgram* rtData);

} // namespace PixelMapper
