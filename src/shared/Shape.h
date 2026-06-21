#pragma once
#include <glm/glm.hpp>

namespace PixelMapper::Shape {

struct Line {
    glm::vec3 start;
    glm::vec3 end;
};

struct Circle {
    glm::vec3 center;
    float radius;
};

} // namespace PixelMapper::Shape
