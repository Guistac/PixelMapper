#pragma once

#include <stdint.h>
#include <glm/glm.hpp>
#include <flecs.h>


namespace PixelMapper{

void menubar(flecs::world& w);
void gui(flecs::world& w);


namespace Patch{
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
};


namespace Fixture{
    struct Is{};

    struct WithShape{};
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
};


namespace Pixel{
    struct Is{};

    struct ColorRGBW{
        uint8_t r, g, b, w;
    };
    struct Position{
        glm::vec2 position;
    };
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
};

namespace Application{
    struct Is{};
    struct PatchFolder{};
    struct SelectedPatch{};
    struct Queries{
        flecs::query<Patch::Is> patchQuery;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress> patchDmxFixtureQuery;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress> patchFixtureInUniverseQuery;
        flecs::query<Dmx::Universe::Is, Dmx::Universe::Properties> patchDmxUniverseQuery;
        flecs::query<Pixel::Is, Pixel::Position, Pixel::ColorRGBW> patchPixelQuery;
        flecs::query<Pixel::Is, Pixel::Position, Pixel::ColorRGBW> fixturePixelQuery;
    };
    void import(flecs::world& w);
}


namespace Application{
    flecs::entity get(const flecs::world& w);
    flecs::entity createPatch(flecs::entity pixelMapper);
    void iteratePatches(flecs::entity pixelMapper, std::function<void(flecs::entity patch)> fn);
    flecs::entity getSelectedPatch(flecs::entity pixelMapper);
    void selectPatch(flecs::entity pixelMapper, flecs::entity patch);
}

namespace Patch{
    void iterateDmxFixtures(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn);
    void iterateFixturesInUniverse(flecs::entity patch, flecs::entity universe, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn);
    void iterateDmxUniverses(flecs::entity patch, std::function<void(flecs::entity dmxUniverse, Dmx::Universe::Properties&)> fn);
    void iteratePixels(flecs::entity patch, std::function<void(flecs::entity pixel, Pixel::Position&, Pixel::ColorRGBW&)> fn);

    flecs::entity getSelectedFixture(flecs::entity patch);
    void selectFixture(flecs::entity patch, flecs::entity fixture);
    void removeFixtureSelection(flecs::entity patch);
    
    flecs::entity getSelectedUniverse(flecs::entity patch);
    void selectUniverse(flecs::entity patch, flecs::entity universe);
};

namespace Fixture{
    void iteratePixels(flecs::entity fixture, std::function<void(flecs::entity pixel, Pixel::Position&, Pixel::ColorRGBW&)> fn);
    void setDmxProperties(flecs::entity fixture, uint16_t universe, uint16_t startAddress);

    flecs::entity createLine(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels = 16, int channelsPerPixel = 4);
    flecs::entity createCircle(flecs::entity patch, glm::vec2 center, float radius, int numPixels = 16, int channelsPerPixel = 4);
};

}