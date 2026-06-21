#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <random>
#include <atomic>
#include <memory>
#include <algorithm>
#include <flecs.h>
#include "shared/network/TelemetryProtocol.h"
#include "shared/components/PatchComponents.h"

namespace PixelMapper {

// Forward declare PatchProgram
struct PatchProgram;

namespace Generative {
    void import(flecs::world& w);
}

// 1D Perlin Noise for smooth wandering
struct Noise1D {
    static float noise(float x) {
        int ix = (int)floorf(x);
        float fx = x - (float)ix;
        float ux = fx * fx * (3.0f - 2.0f * fx); // smoothstep
        
        auto hash = [](int i) -> float {
            int n = i;
            n = (n << 13) ^ n;
            return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
        };
        
        float g0 = hash(ix);
        float g1 = hash(ix + 1);
        return g0 * (1.0f - ux) + g1 * ux;
    }
};

} // namespace PixelMapper
