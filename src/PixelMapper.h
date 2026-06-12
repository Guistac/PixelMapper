#pragma once

#include <shared_mutex>
#include <stdint.h>
#include <glm/glm.hpp>
#include <flecs.h>

class ImGuiCanvas;

namespace PixelMapper{

namespace App{
    struct Is{};
    struct PatchFolder{};
    struct SelectedPatch{};
    void import(flecs::world& w);
    void terminate();
    flecs::entity get(const flecs::world& w);
}

namespace Gui{
    void import(flecs::world& w);
};

namespace Patch{
    struct Is{};

    struct FixtureFolder{};
    struct DmxUniverseFolder{};
    struct ArtnetDeviceFolder{};

    struct SelectedFixture{};
    struct SelectedDmxUniverse{};
    struct SelectedArtnetDevice{};

    struct DmxMapDirty{};
    struct RenderAreaDirty{};

    struct ProgramDirty{};

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

struct PatchProgram{

    static PatchProgram* compile(flecs::entity patch);
    ~PatchProgram();

    struct Universe{
        uint16_t id;
        uint8_t buffer[512];
    };
    struct Pix2UniCopyInstr{
        uint32_t pixelIndex;
        uint8_t pixelStartByte;
        uint8_t bytesPerPixel;
        uint32_t byteCount;
        uint16_t universeIndex;
        uint16_t universeOffset;
    };

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

    flecs::entity createLine(flecs::entity patch, glm::vec3 start, glm::vec3 end, int numPixels = 16, int channelsPerPixel = 4);
    flecs::entity createCircle(flecs::entity patch, glm::vec3 center, float radius, int numPixels = 16, int channelsPerPixel = 4);
    
    void setDmxProperties(flecs::entity fixture, uint16_t universe, uint16_t startAddress);

    int getCountWithDmx(flecs::entity patch);
    void iterateWithDmx(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn);
    void iterateInDmxUniverse(flecs::entity patch, flecs::entity universe, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn);
    void iterateWithPixelData(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::PixelData&)> fn);


    struct ShapeLogic{
        void (*setPixelPositions)(flecs::entity fixture);
        void (*guiShapeDisplay)(flecs::entity fixture);
        bool (*guiShapeEdit)(flecs::entity fixture);
        bool (*guiProps)(flecs::entity fixture);
    };
    void Line_setPixelPositions(flecs::entity fixture);
    void Line_guiShapeDisplay(const void* shapeProps, const ImGuiCanvas* canvas);
    bool Line_guiShapeEdit(const void* shapeProps, const ImGuiCanvas* canvas);
    bool Line_guiProps(const void* shapeProps, const ImGuiCanvas* canvas);
    void Circle_setPixelPositions(const void* shapeProps, std::vector<glm::vec3>& positions);
    void Circle_guiShapeDisplay(const void* shapeProps, const ImGuiCanvas* canvas);
    bool Circle_guiShapeEdit(const void* shapeProps);
    bool Circle_guiProps(const void* shapeProps);
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

    struct SendsUniverse{};

    struct Settings{
        uint32_t ipAddress;
        uint16_t startUniverse;
        uint16_t universeCount;
    };

    void iterateInPatch(flecs::entity patch, std::function<void(flecs::entity device, Settings&)> fn);
    void select(flecs::entity patch, flecs::entity device);
    flecs::entity getSelected(flecs::entity patch);
};

namespace Shape{
    struct Line {
        glm::vec3 start;
        glm::vec3 end;
    };
    struct Circle{
        glm::vec3 center;
        float radius;
    };
};


}//namespace PixelMapper