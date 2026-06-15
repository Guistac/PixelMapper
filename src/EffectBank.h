#pragma once
#include <flecs.h>
#include <string>
#include <vector>

namespace PixelMapper {
namespace EffectBank {

struct Effect {
    std::string name;
    std::string glslSource;
};

struct Bank {
    std::vector<Effect> effects;
    int activeIndex = -1;
};

void import(flecs::world& w);

} // namespace EffectBank
} // namespace PixelMapper
