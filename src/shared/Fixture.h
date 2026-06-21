#pragma once
#include <flecs.h>
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include "shared/Common.h"

class ImGuiCanvas;

namespace PixelMapper {

namespace Fixture {
    struct Is {};
    struct WithShape {};
    struct InUniverse {};
    struct LayoutDirty {};
    struct PixelPositionsDirty {};

    struct Order {
        int index;
    };

    struct Layout {
        int pixelCount;
        int channelsPerPixel;
    };
    struct DmxAddress {
        uint16_t universe;
        uint16_t address;
    };
    struct PixelData {
        std::vector<glm::vec3> positions;
        std::vector<ColorRGBW> colors;
    };

    void reorder(flecs::entity patch, flecs::entity dragFixture, flecs::entity dropFixture);

    void select(flecs::entity patch, flecs::entity fixture);
    flecs::entity getSelected(flecs::entity patch);
    void clearSelection(flecs::entity patch);

    flecs::entity createLine(flecs::entity patch, glm::vec3 start, glm::vec3 end, int numPixels = 16, int channelsPerPixel = 4);
    flecs::entity createCircle(flecs::entity patch, glm::vec3 center, float radius, int numPixels = 16, int channelsPerPixel = 4);

    /// Clone a fixture with a positional offset and return the new entity.
    flecs::entity duplicate(flecs::entity patch, flecs::entity fixture, glm::vec3 offset = {20.0f, 20.0f, 0.0f});

    /// Repack all fixtures' DMX addresses sequentially starting from universe 0 / address 0,
    /// in the order returned by iterateWithDmx.
    void autoPackDmx(flecs::entity patch);

    void updateFixtureNameCounter(flecs::world w);

    void setDmxProperties(flecs::entity fixture, uint16_t universe, uint16_t startAddress);

    int getCountWithDmx(flecs::entity patch);
    void iterateWithDmx(flecs::entity patch, std::function<void(flecs::entity fixture, const Layout&, const DmxAddress&)> fn);
    void iterateInDmxUniverse(flecs::entity patch, flecs::entity universe, std::function<void(flecs::entity fixture, const Layout&, const DmxAddress&)> fn);
    void iterateWithPixelData(flecs::entity patch, std::function<void(flecs::entity fixture, PixelData&)> fn);
    void iterateWithPixelData(flecs::entity patch, std::function<void(flecs::entity fixture, const PixelData&)> fn);

    struct ShapeLogic {
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

    void import(flecs::world& w);
    flecs::entity getPatch(flecs::entity fixture);
    void setPixelPositions(PixelData& pd, std::function<glm::vec3(float range, int index, size_t count)> pos_func);
    void writeColorsToUniverse(
        const std::vector<ColorRGBW>& colors,
        uint8_t* dmxChannels,
        int universeId,
        int fixtureStartUniverse,
        int fixtureStartAddress,
        int channelsPerColor);
}

} // namespace PixelMapper
