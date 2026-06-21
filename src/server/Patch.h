#pragma once
#include "shared/components/PatchComponents.h"

struct GLFWwindow;

namespace PixelMapper {

namespace Patch {
    void import(flecs::world& w);
}

void randomizeTransitionDirections(PatchProgram* program);
void render(PatchProgram* rtData);
void encode(PatchProgram* rtData);

} // namespace PixelMapper
