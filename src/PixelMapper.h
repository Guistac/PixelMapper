#pragma once

#include <stdint.h>
#include <cstdio>
#include <glm/glm.hpp>

#include <flecs.h>


namespace PixelMapper{

struct Color { uint8_t r, g, b, w, a; };
struct Position { glm::vec2 value; };

struct FixtureLine {
    glm::vec2 start;
    glm::vec2 end;
    size_t count;
};

struct DmxAddress {
    uint16_t universe;
    uint16_t address;
};

struct IpAddress {
    uint32_t value;
};

struct IsFixture {};
struct IsPixel {};
struct IsController {};

extern flecs::world world;

void init();
flecs::entity CreateFixture(glm::vec2 start, glm::vec2 end, int numPixels);
void gui();

}