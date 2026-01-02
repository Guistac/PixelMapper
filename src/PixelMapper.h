#pragma once

#include <stdint.h>
#include <glm/glm.hpp>
#include <flecs.h>


namespace PixelMapper{

void init(flecs::world& w);
void gui(flecs::world& w);

struct DmxUniverse;
struct FixtureLayout;
struct IsPixel;


//Patch Components
struct IsPatch{};
struct PatchSettings{
    float refreshRate;
};
struct FixtureList{
    flecs::query<FixtureLayout> fixtureLayoutQuery;
    flecs::query<IsPixel> pixelQuery;
};
struct SelectedFixture{};
struct DmxOutput{
    flecs::query<DmxUniverse> sortedQuery;
};
struct DmxMapDirty{};
struct SelectedDmxUniverse{};
struct RenderArea{
    glm::vec2 min;
    glm::vec2 max;
};
struct RenderAreaDirty{};
//we only have one patch for now
flecs::entity getPatch(flecs::world& w);
flecs::entity getSelectedFixture(flecs::entity patch);
void selectFixture(flecs::entity patch, flecs::entity fixture);



//Fixture Components
struct IsFixture{};
struct LayoutDirty{};
struct PixelPositionsDirty{};
struct FixtureLayout{
    int pixelCount;
    int colorChannels;
    int byteCount;
    flecs::query<IsPixel> pixelQuery;
};
struct DmxAddress {
    uint16_t universe;
    uint16_t address;
};
struct InUniverse{};

//Fixture Shape Components
struct FixtureShape{};
struct Line {
    glm::vec2 start;
    glm::vec2 end;
};
struct Circle{
    glm::vec2 center;
    float radius;
};

//Pixel Components
struct IsPixel{};
struct ColorRGBWA{
    uint8_t r, g, b, w, a;
};
struct Position{
    glm::vec2 position;
};

flecs::entity createLineFixture(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels = 16, int channelsPerPixel = 4);
flecs::entity createCircleFixture(flecs::entity patch, glm::vec2 center, float radius, int numPixels = 16, int channelsPerPixel = 4);


struct IsDmxUniverse{};
struct DmxUniverse{
    uint16_t universeId;
    uint16_t lastAddress;
    uint8_t channels[512];
};

void selectUniverse(flecs::entity patch, flecs::entity universe);
flecs::entity getSelectedUniverse(flecs::entity patch);




struct DmxController{
    uint32_t ipAddress;
    std::vector<uint16_t> universes;
};

}