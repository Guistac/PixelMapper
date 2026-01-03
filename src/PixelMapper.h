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



struct PixelMapperQueries{
    flecs::query<FixtureLayout> patchFixtureLayoutQuery;
    flecs::query<DmxUniverse> patchDmxUniverseQuery;
    flecs::query<IsPixel> fixturePixelQuery;
    flecs::query<IsPixel> patchPixelQuery;
};


//Patch Components
struct IsPatch{};
struct PatchSettings{
    float refreshRate;
};
struct FixtureList{};
struct SelectedFixture{};
struct DmxOutput{};
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

void fixtureIteratePixels(flecs::entity fixture, std::function<void(flecs::entity pixel)> fn);
void patchIterateFixtures(flecs::entity patch, std::function<void(flecs::entity fixture, FixtureLayout& layout)> fn);
void patchIterateDmxUniverses(flecs::entity patch, std::function<void(flecs::entity dmxUniverse, DmxUniverse& u)> fn);
void patchIteratePixels(flecs::entity patch, std::function<void(flecs::entity pixel)> fn);

//Fixture Components
struct IsFixture{};
struct LayoutDirty{};
struct PixelPositionsDirty{};
struct FixtureLayout{
    int pixelCount;
    int colorChannels;
    int byteCount;
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