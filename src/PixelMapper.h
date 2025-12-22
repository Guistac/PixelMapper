#include <stdint.h>
#include <cstdio>
#include <glm/glm.hpp>

struct Pixel{
    uint8_t r,g,b,w,a;
    glm::vec2 pos;
};

class Fixture{
public:
    glm::vec2 startPos;
    glm::vec2 endPos;
    uint16_t dmxUniverseStart;
    uint16_t dmxAddressStart;
    size_t pixelCount;
    Pixel* startPixel;
    
    void calcPixelPositions();
};

struct Controller{
    uint32_t ip;
    uint8_t universeStart;
};

class Patch{
public:
    std::vector<Pixel> pixels;
    std::vector<Fixture> fixtures;
    std::vector<Controller> controllers;

    void buildPixels();
};