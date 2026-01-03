#pragma once

#include <stdint.h>
#include <glm/glm.hpp>
#include <flecs.h>


namespace PixelMapper{

namespace Fixture{
    struct Layout;
};
namespace Dmx::Universe{
    struct Is;
    struct Properties;
}
namespace Pixel{
    struct Is;
};


void init(flecs::world& w);
void gui(flecs::world& w);

struct Queries{
    flecs::query<Fixture::Layout> patchFixtureLayoutQuery;
    flecs::query<Dmx::Universe::Properties> patchDmxUniverseQuery;
    flecs::query<Pixel::Is> fixturePixelQuery;
    flecs::query<Pixel::Is> patchPixelQuery;
};

//we only have one patch for now
flecs::entity getPatch(flecs::world& w);

namespace Patch{
    void iterateFixtures(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::Layout& layout)> fn);
    void iterateDmxUniverses(flecs::entity patch, std::function<void(flecs::entity dmxUniverse, Dmx::Universe::Properties& up)> fn);
    void iteratePixels(flecs::entity patch, std::function<void(flecs::entity pixel)> fn);

    flecs::entity spawnLineFixture(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels = 16, int channelsPerPixel = 4);
    flecs::entity spawnCircleFixture(flecs::entity patch, glm::vec2 center, float radius, int numPixels = 16, int channelsPerPixel = 4);

    flecs::entity getSelectedFixture(flecs::entity patch);
    void selectFixture(flecs::entity patch, flecs::entity fixture);

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
};

namespace Fixture{
    void iteratePixels(flecs::entity fixture, std::function<void(flecs::entity pixel)> fn);

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

};

namespace Pixel{
    struct Is{};

    struct ColorRGBWA{
        uint8_t r, g, b, w, a;
    };
    struct Position{
        glm::vec2 position;
    };
};

namespace Dmx::Universe{
    struct Is{};
    struct Properties{
        uint16_t universeId;
        uint16_t lastAddress;
    };
    struct Channels{
        uint8_t channels[512];
    };
};



struct Line {
    glm::vec2 start;
    glm::vec2 end;
};
struct Circle{
    glm::vec2 center;
    float radius;
};

}