#pragma once

#include <stdint.h>
#include <glm/glm.hpp>
#include <flecs.h>


namespace PixelMapper{

namespace App{
    struct Is{};
    struct PatchFolder{};
    struct SelectedPatch{};
    void import(flecs::world& w);
    flecs::entity get(const flecs::world& w);
}

namespace Gui{
    void import(flecs::world& w);
};

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
        glm::vec3 min;
        glm::vec3 max;
    };

    flecs::entity create(flecs::entity pixelMapper);
    void select(flecs::entity pixelMapper, flecs::entity patch);
    flecs::entity getSelected(flecs::entity pixelMapper);

    int getCount(flecs::entity pixelMapper);
    void iterate(flecs::entity pixelMapper, std::function<void(flecs::entity patch)> fn);
};

struct ColorRGBW{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t w = 0;
};

namespace Fixture{
    struct Is{};

    struct WithShape{};
    struct InUniverse{};

    struct LayoutDirty{};
    struct PixelPositionsDirty{};

    struct Layout{
        int pixelCount;
        int channelsPerPixel;
    };
    struct DmxAddress{
        uint16_t universe;
        uint16_t address;
    };
    struct PixelData{
        std::vector<glm::vec3> positions;
        std::vector<ColorRGBW> colors;
    };

    void select(flecs::entity patch, flecs::entity fixture);
    flecs::entity getSelected(flecs::entity patch);
    void clearSelection(flecs::entity patch);

    flecs::entity createLine(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels = 16, int channelsPerPixel = 4);
    flecs::entity createCircle(flecs::entity patch, glm::vec2 center, float radius, int numPixels = 16, int channelsPerPixel = 4);
    
    void setDmxProperties(flecs::entity fixture, uint16_t universe, uint16_t startAddress);

    int getCountWithDmx(flecs::entity patch);
    void iterateWithDmx(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn);
    void iterateInDmxUniverse(flecs::entity patch, flecs::entity universe, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn);
    void iterateWithPixelData(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::PixelData&)> fn);
};


namespace Artnet::Universe{
    struct Is{};

    struct SendTo{};

    struct Properties{
        uint16_t universeId;
        uint16_t usedSize;
    };
    struct Channels{
        uint8_t channels[512];
    };

    flecs::entity getSelected(flecs::entity patch);
    void select(flecs::entity patch, flecs::entity universe);

    void iterate(flecs::entity patch, std::function<void(flecs::entity dmxUniverse, Artnet::Universe::Properties&)> fn);
};

namespace Artnet::Device{
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


}//namespace PixelMapper