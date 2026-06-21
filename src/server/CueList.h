#pragma once
#include <flecs.h>
#include "shared/components/CueListComponents.h"

namespace PixelMapper {
namespace CueList {

    /// Register components and the CueAdvancer system.
    void import(flecs::world& w);

} // namespace CueList
} // namespace PixelMapper
