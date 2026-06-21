#pragma once
#include <glm/vec2.hpp>

#define IM_VEC2_CLASS_EXTRA \
    ImVec2(const glm::vec2& f) { x = f.x; y = f.y; } \
    operator glm::vec2() const { return glm::vec2(x, y); }