#pragma once

#include <stdint.h>
#include <glm/glm.hpp>
#include <flecs.h>


namespace PixelMapper{

extern flecs::world world;
void init();





struct Patch {
    float refreshRate;
};
struct SelectedFixture{
    flecs::entity fixture;
};

flecs::entity createPatch();
flecs::entity getPatch();




struct Fixture{
    int pixelCount;
    int channelsPerPixel;
};

struct Line {
    glm::vec2 start;
    glm::vec2 end;
    float length;
    bool b_fixedLength = true;
};

struct Pixel{
    uint8_t r, g, b, w, a;
    glm::vec2 position;
};

struct DmxAddress {
    uint16_t universe;
    uint16_t address;
};

flecs::entity createFixture(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels = 16, int channelsPerPixel = 4);
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



void gui();

}