#pragma once

#include <stdint.h>
#include <glm/glm.hpp>
#include <flecs.h>


namespace PixelMapper{

void init(flecs::world& w);



struct Dirty{};


struct Patch {
    float refreshRate;
};
struct SelectedFixture{
    flecs::entity fixture;
};
struct RenderArea{
    glm::vec2 min;
    glm::vec2 max;
};

flecs::entity createPatch(flecs::world& w);
flecs::entity getPatch(flecs::world& w);



struct Fixture{
    int pixelCount = false;
};
struct DmxAddress {
    uint16_t universe;
    uint16_t address;
};
struct Pixel{
    uint8_t r, g, b, w, a;
    glm::vec2 position;
};
struct FixtureShape{};

struct Line {
    glm::vec2 start;
    glm::vec2 end;
};
struct Circle{
    glm::vec2 center;
    float radius;
};


flecs::entity createLineFixture(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels = 16, int channelsPerPixel = 4);
flecs::entity createCircleFixture(flecs::entity patch, glm::vec2 center, float radius, int numPixels = 16, int channelsPerPixel = 4);
flecs::entity getSelectedFixture(flecs::entity patch);
void selectFixture(flecs::entity patch, flecs::entity fixture);






struct DmxOutput{};
struct DmxUniverse{
    int universeNumber;
    std::vector<uint8_t> data;
};

struct DmxController{
    uint32_t ipAddress;
    std::vector<uint16_t> universes;
};



void gui(flecs::world& w);

}