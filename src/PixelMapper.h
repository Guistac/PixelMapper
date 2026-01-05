#pragma once

#include <stdint.h>
#include <glm/glm.hpp>
#include <flecs.h>


namespace PixelMapper{


void import(flecs::world& w);

void menubar(flecs::world& w);
void gui(flecs::world& w);


struct Is{};
struct PatchFolder{};
struct SelectedPatch{};

flecs::entity get(const flecs::world& w);
flecs::entity createPatch(flecs::entity pixelMapper);
void iteratePatches(flecs::entity pixelMapper, std::function<void(flecs::entity patch)> fn);
flecs::entity getSelectedPatch(flecs::entity pixelMapper);
void selectPatch(flecs::entity pixelMapper, flecs::entity patch);


namespace Patch{
    void iterateFixtures(flecs::entity patch, std::function<void(flecs::entity fixture)> fn);
    void iterateFixturesInUniverse(flecs::entity patch, flecs::entity universe, std::function<void(flecs::entity fixture)> fn);
    void iterateDmxUniverses(flecs::entity patch, std::function<void(flecs::entity dmxUniverse)> fn);
    void iteratePixels(flecs::entity patch, std::function<void(flecs::entity pixel)> fn);

    flecs::entity spawnLineFixture(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels = 16, int channelsPerPixel = 4);
    flecs::entity spawnCircleFixture(flecs::entity patch, glm::vec2 center, float radius, int numPixels = 16, int channelsPerPixel = 4);

    flecs::entity getSelectedFixture(flecs::entity patch);
    void selectFixture(flecs::entity patch, flecs::entity fixture);
    void removeFixtureSelection(flecs::entity patch);

    flecs::entity getSelectedUniverse(flecs::entity patch);
    void selectUniverse(flecs::entity patch, flecs::entity universe);

    struct Is{};

    struct FixtureFolder{};
    struct DmxUniverseFolder{};

    struct SelectedFixture{};
    struct SelectedDmxUniverse{};

    struct DmxMapDirty{};
    struct RenderAreaDirty{};

    struct Settings{
        float refreshRate;
    };
    struct RenderArea{
        glm::vec2 min;
        glm::vec2 max;
    };

    void import(flecs::world& w);
};



namespace Fixture{
    void iteratePixels(flecs::entity fixture, std::function<void(flecs::entity pixel)> fn);
    void setDmxProperties(flecs::entity fixture, uint16_t universe, uint16_t startAddress);

    struct Is{};

    struct Shape{};
    struct InUniverse{};

    struct LayoutDirty{};
    struct PixelPositionsDirty{};

    struct Layout{
        int pixelCount;
        int colorChannels;
        int byteCount;
    };
    struct DmxAddress{
        uint16_t universe;
        uint16_t address;
    };

    void import(flecs::world& w);
};



namespace Pixel{
    struct Is{};

    struct ColorRGBWA{
        uint8_t r, g, b, w, a;
    };
    struct Position{
        glm::vec2 position;
    };

    void import(flecs::world& w);
};



namespace Dmx::Universe{
    struct Is{};

    struct SendToController{};

    struct Properties{
        uint16_t universeId;
        uint16_t usedSize;
    };
    struct Channels{
        uint8_t channels[512];
    };

    void import(flecs::world& w);
};


namespace ArtNetController{
    struct Is{};

    struct HasUniverse{};

    struct IpAddress{
        uint32_t address;
    };
};


namespace Shape{
    struct Line {
        glm::vec2 start;
        glm::vec2 end;
    };
    struct Circle{
        glm::vec2 center;
        float radius;
    };

    void import(flecs::world& w);
};

}