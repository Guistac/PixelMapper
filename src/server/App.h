#pragma once
#include "server/Patch.h"
#include "shared/Fixture.h"
#include "shared/Artnet.h"
#include <flecs.h>
#include <mutex>
#include <atomic>
#include <memory>
#include "shared/components/AppComponents.h"

struct GLFWwindow;
struct PatchProgram;

namespace PixelMapper {

namespace Gui {
    void import(flecs::world& w);
}

namespace App {
    extern GLFWwindow* sharedContextWindow;

    void import(flecs::world& w);
    void terminate();
}

} // namespace PixelMapper
