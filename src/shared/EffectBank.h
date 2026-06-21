#pragma once
#include <flecs.h>
#include <string>

namespace PixelMapper {
namespace EffectBank {

    struct Is {};
    struct EffectFolder {};

    namespace Effect {
        struct Is {};
        struct GlslSource { std::string value; };
    }

    struct SessionState {
        int activeIndex = -1;
    };

    void import(flecs::world& w);

} // namespace EffectBank
} // namespace PixelMapper
